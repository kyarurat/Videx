# 性能验证

启用 `VIDEX_BUILD_BENCHMARKS=ON` 构建 `videx_performance_checks`。默认生成 10000 个非媒体文件及两个有效 WAV，在临时目录验证文件树加载、同目录打开复用模型、跨非媒体文件导航，并测量 5 毫秒界面心跳的最大间隔。`VIDEX_PERF_FILES` 可改为 100000；临时目录仅由该程序创建并在退出时清理。

`VIDEX_PERF_MEDIA` 可指定已有大型音视频文件，测量加载及暂停后跳到 80% 的耗时；`VIDEX_PERF_IMAGE` 可指定图片，对比 1280 × 720 预览与原图解码耗时和像素字节数。Windows 原生验证使用 `-platform windows`；offscreen 仅验证逻辑与时间轴，不验证视频显示。Qt 插件、编译优化、设备、系统缓存和同时运行的程序均影响结果。

`tools/CreateSparseAudioFixture.py` 可生成有效 RF64 PCM 静音文件，用于检查超过 4 GiB 的大小、时长、跳转与按需读取。示例：`python tools/CreateSparseAudioFixture.py artifacts/large-silence.wav 20`。目标必须不存在；Windows 设置 NTFS 稀疏标志，Linux 通过稀疏区间表示真实静音采样。这个样本不能代表冷磁盘读取、复杂编码、长 GOP 或大型 MP4/MKV 索引的性能。

回归检查另外覆盖预览方向、原图升级保留缩放和平移、过期原图丢弃、媒体类型缓存、累计跳转、纯音频无需 OpenGL 以及快速切换和损坏文件恢复。实际编码、网络盘和 Ubuntu 原生图形环境需要各自验证。

`tools/CreateSparseVideoFixture.py` 从单帧 RGB24 MOV 生成有正确采样索引和 64 位偏移的稀疏黑色视频。示例：`python tools/CreateSparseVideoFixture.py seed.mov artifacts/large-black.mov 20`；种子生成方式见脚本说明。文件内容是有效原始帧，不是尾部填充；用于验证大文件寻址、容器加载和跳转，不代表 H.264/HEVC 解码或长 GOP 性能。

2026-09-30，Windows 11、Qt 6.11.2 / MinGW Release 的本机样本结果：原生窗口下 10000 个非媒体文件之间导航 184 ms，最大界面心跳间隔 8 ms；同目录重新打开复用原模型。20 GiB 原始 MOV 加载 208 ms，跳到 80% 为 20 ms。10000 × 10000 JPEG 的预览解码 40 ms、像素数据 2073600 字节，原图解码 259 ms、像素数据 400000000 字节。时间不包含冷磁盘成本，像素字节数不是进程峰值内存。

100000 文件的 offscreen 检查中，跨非媒体文件导航 1745 ms，最大界面心跳间隔 9 ms；20 GiB RF64 音频加载 181 ms，跳到 80% 为 3 ms。

大量文件首次加载仍包含 Qt 文件系统模型在 GUI 线程上的自然排序。本机 100000 文件首次填充及稳定耗时 8272 ms，最大心跳间隔 1376 ms；分块导航改善了加载完成后的响应，尚未消除首次排序停顿。图片仍受 100 MP / 512 MiB 文件限制；不支持直接打开多 GiB 图片。Ubuntu 原生运行尚未验证。
