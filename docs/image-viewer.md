# 图片查看

右侧查看器在后台加载图片。成功后显示图片工具栏；其他文件、未安装解码器或不支持的编码显示“格式不支持”，保留文件基本信息。损坏图片、失效路径和超限图片显示具体原因。

## 使用

- 双击文件或按 Enter 打开；文件树中上下键选中文件后立即打开，选中目录时保留当前画面，Enter 展开／收起目录。鼠标单击仍只改变选择。
- “适应窗口”保持比例，不放大小图；“原始尺寸”按一个图片像素对应一个屏幕像素显示，包含高分屏缩放修正。
- `+` / `-` 或 Ctrl+滚轮缩放，拖动平移；普通滚轮滚动。查看区内 `0` 适应窗口、`1` 原始尺寸，PageUp / PageDown 切换图片。
- 切图时保留查看区焦点，可以连续使用 PageUp / PageDown；从文件树打开图片不会抢走树的焦点。跨不同缩放比例的屏幕时，原始尺寸和手动缩放保持物理像素比例，适应窗口模式重新适配。
- 上一张／下一张沿用文件树当前目录的排序，仅查找图片候选项，不递归扫描，也不循环播放。文件树键盘导航不受这些快捷键影响。
- “详细信息”显示尺寸、格式、文件大小，以及文件中已有的相机、镜头、拍摄时间、光圈、曝光时间、ISO、焦距、GPS 坐标、海拔和 EXIF/IPTC/XMP 字段。没有记录的字段不推测；不联网查询地址。
- 切换文件、清空目录或文件失效时，清除旧图片并关闭旧的详细信息窗口；后台旧任务不能覆盖当前图片。当前文件被覆盖保存后会重新加载。

## 格式范围

| 类别 | 解码方式与范围 |
| --- | --- |
| 常见位图 | Qt：JPEG/JPG、PNG、BMP、GIF、ICO、PBM/PGM/PPM、XBM/XPM |
| 扩展位图 | Qt Image Formats：WebP、TIFF/TIF、TGA、ICNS、WBMP；依赖对应插件部署 |
| 矢量图 | Qt SVG：SVG/SVGZ；按文档尺寸栅格化显示 |
| 手机／网页照片 | libheif + libde265 + libaom：HEIC/HEIF、AVIF 的主图 |
| 相机 RAW | LibRaw 0.22.2：CR2/CR3/CRW、NEF/NRW、ARW/SR2/SRF、RAF、ORF、RW2、PEF、DNG、KDC、X3F 等；具体型号和压缩变体取决于 LibRaw |

扩展名只作为导航候选提示，实际打开会检查内容。无法承诺所有厂商、所有新机型或所有编码变体均可读取。LibRaw 的[相机列表](https://www.libraw.org/supported-cameras)是后端能力说明，不代表每个型号都经过 Videx 实测。

TIFF 容器在打开时先由 LibRaw 识别是否包含可解码的 RAW 数据，再选择解码器，避免改名或移除扩展名的 RAW 被当作普通 TIFF 或内嵌预览读取；普通 TIFF 仍使用 Qt。识别不会在浏览文件树时逐文件进行。回归覆盖合成 Bayer DNG 的无扩展名和 `.tif` 副本，以及使用 `.raw` 扩展名的普通 TIFF。

当前查看静态画面：GIF/WebP 动图和多页 TIFF 显示首帧／首页，HEIF 显示主图；动画播放、多页导航、HDR 专用显示和图片编辑未实现。SVG 放大使用已栅格化画面。图片中有效的 ICC 色彩描述转换到 sRGB；RAW 使用相机白平衡和 sRGB 输出。

单文件读取上限 512 MiB、图片上限 1 亿像素；各解码器还保留自身的分配与安全限制。超限文件会给出提示，不尝试无限分配内存。最多同时运行一个当前图片解码任务和一个预加载任务，连续打开只保留最新待处理请求。当前图片完成后，按文件树顺序预加载同目录下一张、上一张图片，覆盖 JPG、PNG、RAW 等全部支持格式，不递归扫描。已完成预加载的图片直接使用缓存，正在加载的目标可转为当前请求；无关预加载不会占用当前图片的专用解码位置。预加载期间文件变化时丢弃结果并重新加载。

图片像素准备好后先显示，再由独立的单线程任务读取 EXIF/IPTC/XMP。提前打开详细信息窗口会显示“正在读取…”，读取完成后自动更新。快速切换会合并待处理的元数据请求，旧文件的结果不能覆盖新文件；方向校正所需的解码器信息仍在像素解码时读取。此优化不省略 RAW 本身解码必需的相机参数解析。

## 构建与部署

需要 C++20、CMake、Qt 6.5+ 的 Core / Gui / Widgets / Svg / Concurrent。优先使用系统 Exiv2、LibRaw、libheif；缺少时，默认下载固定版本源码并构建。源码来源与许可见 [第三方依赖](third-party.md)。首次配置需要网络，后续可使用 CMake FetchContent 的离线源码覆盖选项。

Windows 默认启用 `VIDEX_BUILD_IMAGE_PLUGINS`，构建匹配当前 Qt 版本的 Qt Image Formats，要求 Qt BuildInternals 和 Ninja。插件使用独立 Ninja 构建目录，主项目可以继续使用原生成器；可通过 `VIDEX_NINJA_EXECUTABLE` 指定 Ninja 路径。libaom 构建使用 Perl；可在 PATH 中提供 Perl，或使用 Git for Windows 自带的 Perl。构建输出自动携带 Exiv2、libheif 和扩展图片插件；发布仍需用 `windeployqt` 部署 Qt（包括 SVG、JPEG、GIF 等插件）及编译器运行库。

Linux 可安装 Qt Image Formats 插件包，或启用 `-DVIDEX_BUILD_IMAGE_PLUGINS=ON` 从源码构建。使用系统 libheif 时还需安装 HEVC、AV1 解码插件。`-DVIDEX_FETCH_IMAGE_DEPENDENCIES=OFF` 关闭解码库自动下载并要求系统提供所需库；Qt 插件源码构建由 `VIDEX_BUILD_IMAGE_PLUGINS` 单独控制。配置错误会明确指出缺失依赖。

```sh
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

`image_checks` 使用临时生成的图片、拍摄参数和 GPS 数据验证解码、方向校正、错误处理、快速切换、详细信息和主窗口接入。合成 DNG 位于 `tests/fixtures/synthetic.dng`，可用 `tools/GenerateDngFixture.py` 重新生成。源码构建 libheif 时，还测试其上游提供的 HEIC/AVIF 样本。

可通过 `VIDEX_RAW_FIXTURES` 指定真实相机样本目录，测试其中所有文件。外部样本不随仓库分发。界面截图生成在测试工作目录的 `artifacts/images`；Windows 视觉检查使用 `videx_image_checks -platform windows`，避免离屏字体环境影响中文截图。

预加载缓存按解码后的图片和元数据大小计费，上限 256 MiB（不含当前显示资源和解码器工作内存）。超过缓存预算的单张图片仍可在原有尺寸限制内打开，但不会缓存。重新使用前核对文件大小、修改时间和可读性，当前图片发生修改会强制重新加载；切换根目录时释放缓存。首次打开以及尚未预加载完成时仍需解码，预加载不保证首次打开立即显示。

### RAW 计算加速

源码构建的 LibRaw 默认在 Debug、Release 等所有配置中启用可用的 OpenMP CPU 多核处理，每个解码任务最多使用 8 个线程，也尊重更小的 `OMP_NUM_THREADS`。缺少 OpenMP 时回退串行处理，并在配置时警告。此功能不是 GPU 解码，不改变 RAW 分辨率、去马赛克算法或输出色彩参数。GNU/Clang 的 Debug 构建默认只为 LibRaw 启用 `-O2`，保留调试符号和应用层的 Debug 配置。Release 沿用工具链的 Release 优化参数；旧构建目录需重新运行 CMake 并重编译，不能只复用旧的可执行文件。手动复制到部署目录的程序也需同步更新。

`VIDEX_RAW_OPENMP=OFF` 可关闭多核处理；`VIDEX_RAW_OPTIMIZE_DEBUG=OFF` 可关闭 LibRaw 的 Debug 编译优化，便于逐行调试第三方库。以上构建选项不重编译系统安装的 LibRaw。Windows MinGW 构建会部署所需的 OpenMP DLL。

启用 `VIDEX_BUILD_BENCHMARKS=ON` 可构建 `videx_raw_benchmark`，传入 RAW 文件或目录，输出打开、解压、后处理、像素输出的分阶段毫秒耗时及像素 SHA-256。目录模式递归测量 CR2 文件，不使用 Videx 图片缓存；操作系统文件缓存不清空，计时不包含元数据面板和界面绘制。
