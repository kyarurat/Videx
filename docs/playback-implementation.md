# 音视频实现与验证记录

本轮基础播放实现已完成；操作与限制见 [音视频播放](playback.md)。播放进度恢复按本轮约定暂缓，设置项保持禁用。

## 验证环境

Windows 11 x64，Qt 6.11.2 / MinGW 13.1，Release 构建。libmpv SDK 为 shinchiro/mpv-winbuild-cmake 的 `mpv-dev-x86_64-20260928-git-e470f8986e.7z`；压缩包 SHA-256：`81795D759E01016F1550FD71651A1A5D59AB5C28EF31C0B6793224E9CFF39459`。SDK 位于忽略的 build 目录，通过 MPV_ROOT 配置，不写入源码中的本机路径。

## 实施结果

| 要求 | 实现和证据 |
| --- | --- |
| 共用基础音视频后端 | MpvPlayer + VideoView；WAV、带封面 MP3、纯音频 MP4 和 H.264/AAC 视频原生检查通过 |
| 播放控制 | 暂停、跳转、音量、静音、倍速、重播、全屏、同类导航和自动下一项均有检查 |
| 安全切换 | 快速音频互切、音频与图片互切、损坏文件恢复、切换目录停止旧会话检查通过 |
| 快捷键 | 查看区 Space / M / 方向键 / PageUp / PageDown；重复方向键处理与 Space 去重复已验证，文件树焦点保留检查通过 |
| 深浅主题 | 视频与音频的四张 Qt 截图已人工查看；视频 framebuffer 包含实际彩色帧，音频信息集中显示 |
| 代码检查 | 检查过事件生命周期、上下文释放、状态回填、错误处理和焦点；修复了结束重复触发、滑块键盘跳转、操作失败隐藏会话等问题 |
| 进度恢复 | 暂缓；没有新增历史读写，不给图片创建时间轴 |

最终构建成功，无新增编译警告。`playback_checks`、`image_checks`、`ui_checks` 全部通过（3/3）。Windows 原生播放检查使用三个可选样本环境变量：VIDEX_TEST_VIDEO、VIDEX_TEST_COVER_AUDIO、VIDEX_TEST_AUDIO_CONTAINER，结果为 Playback failures: 0。

本机验证日志保存在 artifacts/build-playback-final.log、artifacts/playback-native-final.log 和 artifacts/playback-regression-final.log；主题截图为 artifacts/playback-dark.png、playback-light.png、playback-audio-dark.png、playback-audio-light.png；视频帧为 artifacts/playback-frame.png。以上本机产物均不纳入版本控制。

## 实际限制

视频改用 Qt OpenGLWidgets 与 mpv render API，未使用 wid 原生窗口嵌入。无界面检查使用 null 视频输出，原生视频检查另行读取实际 OpenGL framebuffer；不使用桌面截图证明视频显示。

Ubuntu 编译、X11 / Wayland 实机显示、不同显卡、实际扬声器听感及独立发行包仍未验证。代码采用 Qt 的跨平台画布，避免平台窗口句柄，但不能将 Windows 验证当作 Linux 验证。分发前应按所选 mpv / FFmpeg 构建补齐第三方许可与源码材料。

## 按键体验优化

文件树选中文件时可操作音视频左右键；选中目录时仍用于展开／折叠。短按左右键跳转 5 秒，长按左键每次精确后退 1 秒；长按右键约 300 毫秒后临时使用 2×，松开或失去焦点恢复原倍速。切换媒体会取消加速，暂停状态保持不变。

新增检查已覆盖文件树短按／长按、系统重复按键释放、恢复 1.5× 原倍速、焦点移走，以及视频精确一秒后退和切换图片取消加速。Windows 构建无新增警告，三组回归检查和原生视频检查全部通过。

## Review + fix 复查记录

使用 review-agent 检查当前未提交的音视频实现及按键体验变更。每轮先只读审查，再按用户授权修复；覆盖后端、OpenGL 生命周期、媒体切换、控件、文件树、主窗口连接、构建配置和文档。共执行三轮，在第三轮没有新的可确认问题时停止，未达到十轮上限。

| 轮次 | 发现与处理 |
| --- | --- |
| 1 | [P1] 同一 GUI／视频绘制线程使用同步 mpv 属性读取，违反 render API 的线程要求，可能等待绘制而导致卡顿。改为带类型的属性观察与异步媒体流读取，移除同步 getter。 |
| 1 | [P2] 左键长按第一次按下仍后退 5 秒。新增完整按下、重复、松开序列复现失败后修复：短按松开跳转 5 秒，长按从首步开始每次 1 秒，松开不追加跳转。 |
| 2 | [P2] 后台状态更新覆盖正在拖动的音量滑块位置。检查复现失败后修复：拖动期间保留用户位置，释放后恢复状态回填。 |
| 3 | No findings. 复查修复及其调用路径，未发现新的符合缺陷判定条件的问题。 |

最终 Windows Release 构建成功，编译日志无 warning／error；三组回归检查 3/3 通过。原生检查覆盖真实视频 framebuffer、带封面 MP3、纯音频 MP4、文件树及查看区短按／长按、恢复原倍速、焦点和媒体切换，结果为 Playback failures: 0。损坏文件样本按预期产生格式错误提示。

最终日志为 artifacts/build-review-final.log、artifacts/review-regression-final.log 和 artifacts/review-native-final.log。Ubuntu、不同显卡、实际听感及发行包验证仍未完成；本次审查不将这些未验证环境判定为已通过。

## 后续播放问题修复

后续独立审查确认三处问题，本轮已修复：

- VideoView 析构前断开 OpenGL 上下文销毁回调，避免 QOpenGLWidget 基类析构时再次进入已经销毁的子类。
- 导航不再用扩展名判断实际同类。新增 MediaNavigation，在导航时按顺序检查候选的媒体流；探测会话暂停、静音且使用 null 音频输出，选到同类文件后立即释放。纯音频 MP4 可连续导航，视频序列会跳过它。文件／目录切换和新导航请求取消旧检查，过期回调不能选中文件；损坏候选保留原有错误显示路径。
- MpvPlayer 保留用户请求的静音状态，新播放会话在初始化和加载前恢复；即使旧会话的属性通知尚未到达，也不会在切换后突然取消静音。

Windows Release 构建成功，未发现新增编译警告。playback_checks、image_checks、ui_checks 全部通过（3/3）。原生播放检查使用真实 H.264/AAC 视频、带封面 MP3 和纯音频 AAC MP4，验证同类手动／自动导航、混合 MP4 序列跳过音频、静音保持和视频 framebuffer，结果为 Playback failures: 0。

新增 videx_playback_lifecycle_checks 目标在 Release 下启用 Qt 断言，重复验证活动视频时先销毁画布，以及先销毁后端的安全退出顺序，结果通过。日志为 artifacts/build-playback-fixes.log、artifacts/regression-playback-fixes.log、artifacts/playback-native-fixes.log 和 artifacts/playback-lifecycle-fixes.log。以上为本机忽略产物。Ubuntu 和发行包验证仍未完成。

## 完整播放器界面析构修复

后续审查通过调试器确认：QWidget 基类删除子播放器时，PlayerWidget 的子类生命周期已经结束，但 MpvPlayer 析构中的 stop() 仍发出 stateChanged，回调 PlayerWidget::syncPlayback()。此前独立画布／后端析构检查未覆盖这一父界面路径。

新增 PlayerWidget 析构函数，先取消长按状态，断开后端到本界面的回调，再趁视频画布仍有效时停止播放和释放渲染资源。保留 Qt 的子对象所有权；之后子播放器析构不会再次进入已销毁的 PlayerWidget。

扩展 videx_playback_lifecycle_checks，在 Release 下重新编译播放组件并启用 Qt 断言，保留真实状态连接。修复前，空闲 PlayerWidget 析构即触发“class destructor may have already run”断言；修复后，空闲、音频播放、长按加速、关闭完整主窗口均通过。offscreen 检查纳入 CTest；原生检查另覆盖实际视频与加速时销毁完整界面，并保留独立画布／后端两种析构顺序。

Windows Release 构建成功，无新增编译警告。四组 CTest 检查全部通过（4/4）；使用实际 H.264/AAC 视频的原生析构检查通过，原生音视频播放检查为 Playback failures: 0。复现日志为 artifacts/widget-lifecycle-before.log；修复后日志为 artifacts/build-widget-lifecycle-fixed.log、artifacts/regression-widget-lifecycle-fixed.log、artifacts/widget-lifecycle-native-fixed.log 和 artifacts/playback-native-widget-fixed.log。Ubuntu 仍未验证。
