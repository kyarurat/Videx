# RAW 解码性能验证

2026-09-30，Windows 11、Qt 6.11.2、MinGW 13.1，Intel i9-13900HX。
使用用户本地提供的 39 张 CR2（约 27 MB/张），完整输出均为 6024 × 4020。
照片仅在本地只读测试，不随仓库分发。

| 构建方式 | 平均解压 | 平均后处理 | 平均完整解码 |
| --- | ---: | ---: | ---: |
| 原 Debug，未优化、单线程 | 611 ms | 4677 ms | 5380 ms |
| Debug + LibRaw `-O2`，单线程 | 501 ms | 1942 ms | 2514 ms |
| Debug + LibRaw `-O2` + OpenMP，最多 8 线程 | 335 ms | 363 ms | 737 ms |

多核版本每张完整解码耗时为 719–757 ms，平均约为原版本的 7.3 倍速度。
编译优化版本和多核版本的全部 39 张输出像素 SHA-256 均与原版一致。
没有改动去马赛克算法、分辨率、白平衡和输出色彩参数。

主要瓶颈是后处理计算。启用 LibRaw 自带的 OpenMP CPU 路径，并将其线程数限制为最多 8，
避免预加载在多核机器上占满全部核心；`OMP_NUM_THREADS` 可以进一步限制线程数。
当前没有 GPU RAW 解码实现。参见 [LibRaw 维护者对 GPU 解码和后处理的说明](https://www.libraw.org/node/2831)。

计时工具为 `videx_raw_benchmark`（通过 `VIDEX_BUILD_BENCHMARKS=ON` 构建）。
计时覆盖文件打开、解压、完整 RAW 后处理和像素输出，不经过 Videx 图片缓存；
不包含元数据读取、窗口绘制和像素校验的耗时。
操作系统文件缓存未清空，每种配置各运行一轮；CPU 频率、温度和其他负载会影响结果，
这不是对其他相机或机器的速度保证。

原始本地记录：`build/raw-baseline.jsonl`、`build/raw-optimized.jsonl`、`build/raw-openmp8.jsonl`。
选项、关闭方式和 Windows OpenMP DLL 部署说明见 [图片查看](image-viewer.md)。
Ubuntu、MSVC 和无开发环境的完整安装包尚未在本轮实测。

主程序验证：两组 CTest 全部通过；Windows 原生图片测试使用同一批 39 张 CR2，完整图片加载（含 Qt 图片转换和元数据读取）平均 783.1 ms，全部解码和预加载检查通过。日志为 `build/raw-acceleration-native.log`。本轮最终构建无编译警告。

后续 Qt Creator Debug 目录核查：缓存中两个 RAW 加速选项为 ON，但源码构建脚本的对应配置块缺失，实际编译命令未包含 `-O2` / `-fopenmp`。补回配置后，以 8 路并行重建 Qt Creator 使用的 Debug 程序；39 张照片通过实际图片加载器复测，平均 1685.4 ms，范围 767–2153 ms。原独立基准同一时段单张复测为 744 ms；不同运行时段及执行路径存在差异，不应把首次报告的均值作为每次 GUI 运行的保证。日志：`build/qtcreator-debug-speed-check.log`。

## Release 配置与分阶段加载修复（2026-09-30）

Qt Creator 的 Release 目录仍保留旧 LibRaw 构建配置：有 `-O3 -DNDEBUG`，但没有
`LIBRAW_FORCE_OPENMP` / `-fopenmp`。重新配置 `VIDEX_RAW_OPENMP=ON` 并重编译后，
已核对实际 Release 编译命令包含上述优化与 OpenMP 参数。OpenMP 配置适用于所有构建类型；
配置过程现在明确打印启用状态，无法找到 OpenMP 时会警告。

同一轮顺序测试 5 张 CR2 的独立 RAW 解码基准：

| 构建 | 平均解码 | 范围 |
| --- | ---: | ---: |
| Debug，LibRaw -O2 + OpenMP | 750.4 ms | 743–757 ms |
| Release，LibRaw -O3 + OpenMP | 705.8 ms | 699–713 ms |

5 张照片的输出像素 SHA-256 全部一致。记录：`build/release-comparison.json`。
这仅测量 RAW 打开、解压、后处理及像素输出，不包含 EXIF 面板、Qt 转换和窗口绘制；
未清空操作系统文件缓存，不作为每次界面打开的时间保证。

图片加载流程改为先发布像素，再用独立单线程读取拍摄元数据。
一个前台解码位置和一个预加载位置分别调度；预加载文件变化时丢弃旧结果并重新解码。
Debug / Release 的 image_checks、ui_checks 均通过，新增确定性测试覆盖阻塞的预加载、
预加载中修改文件、延迟元数据、快速切换、清空后旧元数据丢弃，以及 EXIF 方向保持。
未在 Linux 环境执行本轮构建或测试。

现有 Release 的 `release` 部署子目录已同步本轮程序、第三方 DLL、OpenMP DLL 和图片插件，
并重新执行 windeployqt；部署目录与构建目录的 Videx.exe 哈希一致。
