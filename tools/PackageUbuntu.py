#!/usr/bin/env python3
"""Bundle an existing Linux CMake build for Ubuntu 24.04 x86_64 testing."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile


def run(*args, **kwargs):
    return subprocess.run(args, check=True, text=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--qt', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    options = parser.parse_args()
    source = Path(__file__).resolve().parents[1]
    build, qt, output = (path.resolve() for path in (options.build, options.qt, options.output))
    output.mkdir(parents=True, exist_ok=True)
    version = '0.2.0~rc1+externalfiles.20261016'
    for command in ('cmake', 'patchelf', 'dpkg-deb', 'dpkg-shlibdeps'):
        if not shutil.which(command):
            raise RuntimeError(f'Missing packaging tool: {command}')
    run('cmake', '--build', str(build), '--target', 'Videx', '--parallel', '8')

    with tempfile.TemporaryDirectory(prefix='ubuntu-package-', dir=build) as directory:
        work = Path(directory)
        root = work / 'root'
        app = root / 'opt/videx'
        lib = app / 'lib'
        for folder in ('bin', 'lib', 'plugins', 'share/doc/videx'):
            (app / folder).mkdir(parents=True, exist_ok=True)
        executable = app / 'bin/Videx'
        shutil.copy2(build / 'Videx', executable)
        elf_files = [executable]
        libraries = {}
        search_env = os.environ | {'LD_LIBRARY_PATH': ':'.join(map(str, (
            qt / 'lib', build / 'lib', build / '_deps/libheif-build/libheif')))}

        def dependencies(original):
            listing = run('ldd', str(original), capture_output=True, env=search_env).stdout
            if 'not found' in listing:
                raise RuntimeError(f'Unresolved runtime library for {original}:\n{listing}')
            for name, location in re.findall(r'^\s*(\S+) => (/\S+) \(', listing, re.MULTILINE):
                path = Path(location)
                if path.is_relative_to(qt) or path.is_relative_to(build):
                    if name in libraries:
                        continue
                    destination = lib / name
                    shutil.copy2(path.resolve(), destination)
                    libraries[name] = destination
                    elf_files.append(destination)

        dependencies(build / 'Videx')
        categories = ('platforms', 'imageformats', 'iconengines', 'platforminputcontexts',
                      'xcbglintegrations', 'egldeviceintegrations', 'wayland-decoration-client',
                      'wayland-graphics-integration-client', 'wayland-shell-integration')
        for category in categories:
            for plugin in sorted((qt / 'plugins' / category).glob('*.so')):
                destination = app / 'plugins' / category / plugin.name
                destination.parent.mkdir(exist_ok=True)
                shutil.copy2(plugin, destination)
                elf_files.append(destination)
                dependencies(plugin)
        for elf in elf_files:
            relative = os.path.relpath(lib, elf.parent)
            run('patchelf', '--set-rpath', '$ORIGIN' if relative == '.' else f'$ORIGIN/{relative}', str(elf))
        (app / 'bin/qt.conf').write_text('[Paths]\nPrefix=..\nLibraries=lib\nPlugins=plugins\n', encoding='utf-8')
        launcher = app / 'videx'
        launcher.write_text('''#!/bin/sh
set -eu
app_dir=$(CDPATH= cd -- "$(dirname -- "$(readlink -f -- "$0")")" && pwd)
export LD_LIBRARY_PATH="$app_dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$app_dir/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$app_dir/plugins/platforms"
exec "$app_dir/bin/Videx" "$@"
''', encoding='utf-8')
        launcher.chmod(0o755)
        executable.chmod(0o755)
        docs = app / 'share/doc/videx'
        for name in ('third-party.md', 'playback.md', 'image-viewer.md', 'wsl-testing.md'):
            shutil.copy2(source / 'docs' / name, docs / name)
        shutil.copy2(source / 'LICENSE', docs / 'LICENSE-Videx')
        shutil.copytree(source / 'docs/licenses', docs / 'licenses')
        shutil.copytree(qt / 'sbom', docs / 'qt-sbom')
        shutil.copy2(source / 'images/logo.png', app / 'share/videx.png')
        info = run('git', '-C', str(source), 'rev-parse', 'HEAD', capture_output=True).stdout.strip()
        (docs / 'BUILD.txt').write_text(
            f'Videx {version}\nBase commit: {info}; includes current uncommitted changes.\n'
            'Ubuntu 24.04 x86_64; GCC 13.3; Qt 6.11.2; system libmpv 0.37.0.\n'
            'Qt, ICU, Exiv2 and libheif are private bundled libraries.\n'
            'System libraries and graphics drivers are resolved by Ubuntu packages.\n', encoding='utf-8')

        # Check relocation before deriving Debian dependencies. SDK/build paths
        # must not accidentally satisfy an incomplete private runtime bundle.
        validation_env = os.environ | {'LD_LIBRARY_PATH': str(lib)}
        for elf in elf_files:
            listing = run('ldd', str(elf), capture_output=True, env=validation_env).stdout
            if 'not found' in listing or str(qt) in listing or str(build / '_deps') in listing:
                raise RuntimeError(f'Runtime relocation failed for {elf}:\n{listing}')
        debian = work / 'debian'
        debian.mkdir()
        (debian / 'control').write_text('Source: videx\nSection: video\nPriority: optional\n'
            'Maintainer: Videx test build <videx@example.invalid>\n\n'
            'Package: videx\nArchitecture: amd64\nDescription: Videx local media browser test build\n', encoding='utf-8')
        local_shlibs = []
        for name in libraries:
            match = re.fullmatch(r'(.+)\.so\.(.+)', name)
            if match:
                local_shlibs.append(f'{match[1]} {match[2]} videx')
        (debian / 'shlibs.local').write_text('\n'.join(local_shlibs) + '\n', encoding='utf-8')
        args = ['dpkg-shlibdeps', '-O', '--ignore-missing-info', '-xvidex', f'-l{lib}']
        args += [f'-e{elf}' for elf in elf_files]
        result = subprocess.run(args, cwd=work, check=True, text=True, capture_output=True)
        (output / 'dependency-analysis.log').write_text(result.stderr, encoding='utf-8')
        depends = next(line.split('=', 1)[1] for line in result.stdout.splitlines()
                       if line.startswith('shlibs:Depends='))
        # These are supplied by the bundle; pulling Ubuntu's older Qt/ICU
        # packages into the application would mix incompatible SDK versions.
        for dependency in depends.split(', '):
            if dependency.startswith(('libqt6', 'libicu73', 'libexiv2', 'libheif')):
                raise RuntimeError(f'Bundled library unexpectedly resolved to system package: {dependency}')
        depends += ', fonts-noto-cjk'
        (app / 'SYSTEM-DEPENDENCIES.txt').write_text(depends + '\n', encoding='utf-8')
        readme = f'''Videx Ubuntu Desktop 24.04 LTS x86_64 实机测试包

版本：{version}（当前 external-file-operations 分支工作区）
此包包含 Linux Release 程序、Qt 6.11.2 和图片解码运行库；不需要安装 Qt SDK。
mpv、桌面窗口库和显卡驱动使用 Ubuntu 系统版本。

安装 .deb：在安装包所在目录执行 sudo apt install ./videx_{version}_amd64.deb
然后从应用菜单打开 Videx，或在终端执行 videx。
卸载：sudo apt remove videx

免安装压缩包：在 Ubuntu 原生目录解压后运行 ./videx。
系统依赖列在 SYSTEM-DEPENDENCIES.txt；建议优先用 .deb 自动安装依赖。

请优先检查：
1. 视频、音频、图片能否正常打开；从音频或纯音频 MP4 切换到视频是否有画面。
2. 暂停 5 秒释放文件后，继续播放和暂停跳转能否正确恢复进度与画面。
3. 播放或暂停时，在文件管理器中同目录改名，名称和定位更新且保持进度。
4. 删除、移出目录、修改或替换音视频后关闭媒体，不自动播放下一项。
5. 修改图片后自动刷新，快速换文件、改名及关闭窗口不出现旧画面或崩溃。

保存日志：QT_FORCE_STDERR_LOGGING=1 videx > ~/videx-test.log 2>&1
如 Wayland 会话有显示问题，可对照：QT_QPA_PLATFORM=xcb videx

验证范围：Ubuntu 26.04.1 Hyper-V 桌面已完成 Release 构建及 5 组常规回归。
X11 下曾显示正常视频，但重复测试仍可黑屏；Wayland 同样复现，尚未修复。
另已修复长按加速到片尾时的递归崩溃，并添加回归检查。
虚拟机使用 Mesa llvmpipe 软件 OpenGL，不代表真实 GPU 或扬声器验证。
Ubuntu Desktop 24.04 目标平台仍待补测；WSL 的结果不能替代该项验收。
该包为实机测试版本；Windows 的最终验证结果见交付说明。
'''
        (app / 'README-UBUNTU.txt').write_text(readme, encoding='utf-8')
        shutil.copy2(app / 'README-UBUNTU.txt', output / 'README-UBUNTU.txt')
        control = root / 'DEBIAN'
        control.mkdir()
        installed_size = sum(path.stat().st_size for path in app.rglob('*') if path.is_file()) // 1024
        (control / 'control').write_text(f'Package: videx\nVersion: {version}\nArchitecture: amd64\n'
            'Maintainer: Videx test build <videx@example.invalid>\nSection: video\nPriority: optional\n'
            f'Installed-Size: {installed_size}\nDepends: {depends}\n'
            'Description: Native local media browser and viewer (test build)\n'
            ' Qt Widgets application for local video, audio and image browsing.\n', encoding='utf-8')
        (root / 'usr/bin').mkdir(parents=True)
        (root / 'usr/bin/videx').symlink_to('/opt/videx/videx')
        desktop = root / 'usr/share/applications'
        desktop.mkdir(parents=True)
        (desktop / 'videx.desktop').write_text('[Desktop Entry]\nType=Application\nName=Videx\n'
            'Comment=Browse and view local media\nExec=/usr/bin/videx\n'
            'Icon=/opt/videx/share/videx.png\nTerminal=false\nCategories=AudioVideo;Graphics;\n', encoding='utf-8')
        # Empty-page startup only: this is not a claim that native video works.
        smoke_env = validation_env | {'QT_QPA_PLATFORM': 'offscreen', 'QT_FORCE_STDERR_LOGGING': '1',
            'XDG_CONFIG_HOME': str(work / 'config'), 'XDG_CACHE_HOME': str(work / 'cache')}
        with (output / 'package-startup.log').open('w') as log:
            process = subprocess.Popen([str(launcher)], env=smoke_env, stdout=log, stderr=log)
            try:
                status = process.wait(timeout=4)
                raise RuntimeError(f'Packaged application exited during startup: {status}')
            except subprocess.TimeoutExpired:
                process.terminate()
                process.wait(timeout=10)
        deb = output / f'videx_{version}_amd64.deb'
        run('dpkg-deb', '--root-owner-group', '--build', str(root), str(deb))
        portable = output / f'Videx-{version}-ubuntu24.04-x86_64.tar.gz'
        with tarfile.open(portable, 'w:gz') as archive:
            archive.add(app, arcname='Videx')
        tracked = subprocess.check_output(['git', '-C', str(source), 'ls-files', '-z',
            '--cached', '--others', '--exclude-standard']).split(b'\0')
        snapshot = output / f'Videx-{version}-source.tar.gz'
        with tarfile.open(snapshot, 'w:gz') as archive:
            for entry in sorted(set(tracked)):
                if entry:
                    path = source / os.fsdecode(entry)
                    if path.is_file():
                        archive.add(path, arcname='Videx-source/' + os.fsdecode(entry))
        checksums = []
        for path in (deb, portable, snapshot):
            checksums.append(f'{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}')
            print(f'Created {path} ({path.stat().st_size / 1048576:.1f} MiB)', flush=True)
        (output / 'SHA256SUMS').write_text('\n'.join(checksums) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
