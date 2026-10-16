# WSL Ubuntu 编译与测试

WSL 2 的 Ubuntu 24.04 可以验证 Linux 编译、POSIX 文件操作、Qt 文件监听和后台解码。启用 WSLg 后也可以验证实际 Qt 窗口和 OpenGL 画面。它不能替代 Ubuntu Desktop 实机的显卡、音频设备和发行包验证，也不能验证 Windows 独占访问通知。

## 当前环境

2026-10-15 使用 Ubuntu 24.04.4、GCC 13.3、CMake 3.28.3、Ninja、Qt 6.11.2 和系统 libmpv 0.37.0。Qt 安装于 `$HOME/.cache/videx-wsl/Qt/6.11.2/gcc_64`，包含 Qt Base、SVG、Wayland、Image Formats 和 ICU。Ubuntu 24.04 自带的 Qt 6.4 不满足项目 Qt 6.5 最低要求；其 LibRaw、libheif、Exiv2 也低于当前项目要求，因此图片库使用 CMake 固定版本源码构建。

基础依赖可在 Ubuntu 中安装：

```sh
sudo apt update
sudo apt install build-essential cmake ninja-build git pkg-config python3-venv \
  libmpv-dev libgl1-mesa-dev libegl1-mesa-dev libxkbcommon-dev \
  libfontconfig1-dev libfreetype-dev libxcb-cursor0 libxcb-icccm4 \
  libxcb-image0 libxcb-keysyms1 libxcb-render-util0 libxcb-xinerama0 \
  libxcb-xkb1 libxkbcommon-x11-0 libwayland-client0 libwayland-cursor0 \
  libwayland-egl1 zlib1g-dev libexpat1-dev fonts-noto-cjk mesa-utils
```

另需安装满足要求的 Linux Qt SDK，且插件必须与该 SDK 匹配。`qtpaths --qt-version` 和 `ldd <Qt目录>/plugins/platforms/libqxcb.so` 可检查版本及窗口插件依赖。

## 编译与回归

源码可放在 Windows 挂载目录；构建目录建议放在 WSL 自身文件系统，避免大量编译中间文件经过挂载层。以下从源码目录执行，按实际安装位置设置 Qt 前缀：

```sh
export LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8
cmake -S . -B "$HOME/.cache/videx-wsl/build-release" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$HOME/.cache/videx-wsl/Qt/6.11.2/gcc_64" \
  -DBUILD_TESTING=ON -DVIDEX_BUILD_IMAGE_PLUGINS=OFF
cmake --build "$HOME/.cache/videx-wsl/build-release" --parallel 8
ctest --test-dir "$HOME/.cache/videx-wsl/build-release" --output-on-failure --parallel 2
```

`locale -a` 应列出上述语言环境；缺少时可执行 `sudo localedef -i en_US -f UTF-8 en_US.UTF-8`。测试使用有效的桌面语言环境，避免 Qt 在纯 C 语言环境下不提供数字自然排序。播放器在创建首个 mpv 实例前单独把 C 数字区域设为 `C`，满足 libmpv 的数字解析要求，不改变 Qt 的界面语言和文件排序语言环境。

Linux 的独立进程读取按 POSIX 共享语义验证，不把普通打开误当成 Windows 独占打开。`file_operations_checks` 同时覆盖文件身份、连续及大小写改名、删除、移动、修改、原路径替换、暂停 5 秒释放与恢复、过期结果隔离，以及 JPEG、PNG、SVG、RAW、HEIC/AVIF 和大于 32 MiB 的私有输入快照。

## WSLg 实际画面

以下命令在构建目录执行。测试视频应为包含彩色画面的可跳转视频；纯音频 MP4 用于检查类型识别和导航。媒体样本由测试者提供，不需要安装播放器命令行工具。

```sh
export VIDEX_TEST_VIDEO=/absolute/path/to/color-video.mp4
export VIDEX_TEST_AUDIO_CONTAINER=/absolute/path/to/audio-only.mp4
./videx_file_operations_checks -platform xcb
./videx_playback_checks -platform xcb
./videx_playback_lifecycle_checks -platform xcb
```

画面检查读取实际 framebuffer，检查彩色像素，包含暂停释放后的冻结画面、恢复播放，以及活动视频被删除后的关闭行为；窗口销毁检查同时开启 Qt 断言。截图保存在测试工作目录的 `artifacts`。常规 CTest 使用 offscreen，不能单独证明视频显示正常。

当前 WSLg 使用 Mesa llvmpipe 软件 OpenGL；通过画面检查不能证明 GPU 硬件加速、实际扬声器听感、原生 Wayland 或 Ubuntu Desktop 发行包可用。`glxinfo -B` 可确认本次实际渲染器。

## 2026-10-15 实机交接

Release 构建成功，文件操作、图片、界面及 offscreen 窗口销毁检查通过。播放检查有偶发相对跳转偏差；WSLg 原生窗口中音频切换视频后有时只输出接近黑色的画面，原始解码帧仍有正常颜色，软件解码也会复现。一次原生关闭崩溃在后续 32 轮普通／调试和 5 轮播放组件 ASan/UBSan 检查中未复现。尚不能确定这些问题是否与 WSL 有关，不能将 Ubuntu 全套测试标为通过。

按用户要求，后续优先验证 Windows，Ubuntu 转交实机测试。没有保留排查过程中无效的渲染器初始化或 OpenGL 配置改动，也没有关闭正常构建的硬件解码。

`tools/PackageUbuntu.py` 生成内部测试用 `.deb`、免安装压缩包和当前工作区源码快照：

```sh
sudo apt install patchelf
python3 tools/PackageUbuntu.py \
  --build "$HOME/.cache/videx-wsl/build-release" \
  --qt "$HOME/.cache/videx-wsl/Qt/6.11.2/gcc_64" \
  --output artifacts/ubuntu24.04
```

打包器使用现有 CMake 构建，携带匹配的 Qt、ICU、Exiv2、libheif 和窗口／图片插件；mpv、显卡驱动和系统库由 Ubuntu 软件包管理。库查找路径改为相对路径，并检查不依赖构建目录，另执行未打开媒体页的 offscreen 启动检查。该启动检查不证明视频显示正常。`.deb` 安装到 `/opt/videx`，添加 `videx` 启动命令和应用菜单入口；免安装包解压后运行 `./videx`。各包附有依赖、日志采集及人工验收说明。

## 2026-10-16 Hyper-V 原生桌面验证

经用户确认，先在 Ubuntu 26.04.1 LTS 桌面验证，Ubuntu Desktop 24.04 仍待补测。虚拟机为 x86_64，GCC 15.2、CMake 4.2.3、系统 Qt 6.10.2、系统 libmpv 0.41.0；另验证测试包携带的 Qt 6.11.2。图形后端为 Mesa 26.0.8 / llvmpipe 软件 OpenGL，不能据此声称 GPU 硬件解码或实际扬声器听感通过。

用户在自己的 tmux 会话中完成依赖安装。使用项目固定版本的 LibRaw 0.22.2，CMake 实际采用固定版本的 Exiv2/libheif 回退构建。Release 构建成功，5 组常规 offscreen 回归通过；GCC 15 提示的编译警告来自第三方依赖源码，未发现新增 Videx 源码警告。

系统图片库构建也可通过 `-DVIDEX_HEIF_TEST_FILES_DIR=/absolute/path/to/examples` 指定 libheif 的 `example.heic`、`example.avif` 样本。本次图片及文件操作测试包含这两种样本和项目自带的 DNG。

X11 原生窗口验证曾显示正常彩色视频，文件操作测试覆盖暂停释放、保留冻结画面、恢复和活动视频删除。Qt 6.11.2 测试包运行库的一轮完整播放及窗口销毁检查通过，但系统 Qt 的后续 X11 完整播放运行又复现黑屏，因此不能将 X11 原生视频标为稳定通过。早期系统 Qt 原生运行有一次焦点断言失败；测试现明确请求窗口激活并核实键盘导航前置焦点，后台启动仍受桌面窗口管理策略影响。

Wayland 下 Qt 6.10.2 和 6.11.2 均复现视频画面近黑，时间轴仍前进，因此不能将其认定为 WSL 专属问题。黑屏等待导致到达片尾后的部分键盘检查连带失败。帧缓冲重新绑定、Qt 原生绘制交接的诊断均未解决黑屏，没有纳入正式源码。一次 X11 冻结画面截图出现异常网格，随后同一流程保存的原始冻结帧与重绘画面正常；该现象尚未稳定复现或宣称修复。

调试器明确捕获另一个独立崩溃：到达片尾时取消长按加速，恢复倍速同步发出状态信号，导致 `cancelSeekHold()` 与 `syncPlayback()` 无限递归。修复先清除长按状态再恢复倍速，新增“长按加速自然结束”回归。此前 WSL 那次未留存调用栈的关闭崩溃不能据此认定为同一问题。

Windows Debug、Release 在该修复后均重新构建，5 组完整回归通过。Linux 原生视频显示尚未稳定通过，Ubuntu 24.04 原生桌面、真实 GPU 和人工音频检查仍未验证。
