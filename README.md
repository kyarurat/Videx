# Videx

轻量的原生本地视频与图片查看器，使用 C++20、Qt 6 Widgets 和 CMake。
产品目标是支持多种视频与图片格式，在同一窗口中浏览和查看本地媒体。
当前版本完成 GUI 展示；libmpv、真实目录打开、视频播放、图片查看和设置持久化尚未接入，演示数据目前仅包含视频。

## 构建

需要 Qt 6.5 或更高版本（Core / Gui / Widgets）、CMake 3.21+ 与 C++20 编译器。

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=<Qt安装目录>
cmake --build build --parallel
```

多配置生成器使用 `cmake --build build --config Release`。Windows 运行时需让 Qt 与编译器运行库位于 PATH，或使用 Qt 的 windeployqt 部署。Ubuntu 可使用发行版提供的 Qt 开发包；最低 Qt 版本由构建配置检查。

## 界面

- 正常启动显示欢迎页。通过“视图 → 界面演示”体验临时示例媒体目录和五种播放器状态。
- 模拟播放不会推进时间或解码视频；进度、倍速与音量可手动调整。退出演示会释放模型并删除临时目录。
- “文件 → 设置”包含常规、播放、界面三类选项，应用后仅在本次会话保留。未来业务选项有明确提示。
- 主题默认自动跟随系统，可手动选择深色或浅色；自动模式在系统不报告配色时使用浅色。窗口原生标题栏由系统管理。
- Ctrl+O 打开目录说明，Ctrl+, 设置，Ctrl+B 文件树，Space 播放/暂停，M 静音，F 全屏，Esc 退出全屏。播放器获得焦点时方向键跳转或调整音量；文件树和控件保留原生方向键操作。

## 结构

- `src/app`：会话与媒体展示状态、独立演示控制器、主题管理器。
- `src/ui`：主窗口协调器；explorer、player、dialogs 按功能拆分；common 存放真正复用的组件。
- `resources/styles/common.qss`：共享布局样式及语义颜色占位符，由 ThemeManager 使用深浅调色板填充。
- UI 通过 signals 发出意图，通过 MediaUiState 更新显示；未来播放器可以替换演示控制器，不需要重写组件。

## 验证

```sh
ctest --test-dir build --output-on-failure
```

默认构建轻量集成检查程序，使用 Qt offscreen 平台验证演示状态、资源释放、主题和设置行为，并将界面图片写到运行目录的 `artifacts/ui`。该检查不代替真实操作系统主题切换或 Linux 桌面验证。可用 `-DBUILD_TESTING=OFF` 关闭检查程序构建。

## 国际化

中文为源文案，所有用户可见文字通过 Qt 翻译接口提供。`translations/README.md` 说明添加语言目录的方法；当前界面只提供简体中文。

## 应用图标

`images/logo.png` 是窗口图标源文件，通过 Qt 资源嵌入，Windows 和 Linux 共用。
`images/logo.ico` 包含 16、20、24、32、40、48、64、128、256 像素版本，用于 Windows 可执行文件图标。
更换 PNG 后，在 Windows 运行 `powershell -ExecutionPolicy Bypass -File tools/GenerateIcon.ps1` 更新 ICO，然后重新构建。
生成脚本使用系统自带的图像处理能力，不增加应用运行或正常构建依赖。
