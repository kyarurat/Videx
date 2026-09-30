# 图片依赖与许可

不调用命令行图片转换程序，不上传照片，不在应用运行时下载编解码器。第三方库用于原生图片解码和只读元数据解析。

| 组件 | 默认源码版本 | 用途 | 上游许可 |
| --- | --- | --- | --- |
| [Qt / Qt Image Formats](https://code.qt.io/cgit/qt/qtimageformats.git/) | 与安装的 Qt 完全匹配 | 界面、SVG、常见图片及 WebP/TIFF 插件 | Qt 对应许可；内含编解码器各自许可 |
| [LibRaw](https://github.com/LibRaw/LibRaw) | 0.22.2 | 相机传感器 RAW 解码 | LGPL-2.1 / CDDL 双许可，见上游文件 |
| [Exiv2](https://github.com/Exiv2/exiv2) | 0.28.9 | EXIF、IPTC、XMP、GPS、厂商元数据 | GPL-2.0-or-later |
| [zlib](https://github.com/madler/zlib) | 1.3.1 | PNG 元数据、DNG Deflate 解码 | zlib |
| [libheif](https://github.com/strukturag/libheif) | 1.21.2 | HEIF/AVIF 容器和像素输出 | LGPL，见上游 COPYING |
| [libde265](https://github.com/strukturag/libde265) | 1.0.19 | HEIC 的 HEVC 解码 | LGPL，见上游 COPYING |
| [libaom](https://aomedia.googlesource.com/aom/) | 3.13.3 | AVIF 的 AV1 解码 | BSD-2-Clause 及 AOM Patent License |

libaom 源码下载使用 Arthenica 的版本镜像，其他库使用各自上游；压缩包固定 SHA-256。Qt Image Formats 使用与 Qt SDK 一致的版本标签。LibRaw 构建启用 Deflate DNG、X3F 和 Raspberry Pi RAW；未包含可选 RawSpeed、DNG SDK、GPR SDK 和 JPEG DNG 后端，因此相关特殊变体可能不支持。Exiv2 源码构建启用内置 Adobe XMP SDK，使用 Expat 解析 XML，详细信息展示 EXIF/IPTC/XMP。系统 Exiv2 也应启用 XMP 支持；暂不读取独立的 .xmp 旁车文件。

本仓库已有的 Apache-2.0 许可文件未被改写。新增依赖中 Exiv2 为 GPL-2.0-or-later，不能把包含它的组合发行包标为仅 Apache-2.0。发行时应保留各依赖的版权、许可证及其要求的源码和构建信息。对应许可原文放在 `docs/licenses`，Qt 模块内第三方许可还应随 Qt 的部署材料保留。

构建适配仅涉及：隔离第三方编译参数及 DLL 导出宏；补全 Exiv2/libheif 的生成头文件路径；将 libde265 的 `dist` 辅助目标改名以避免与 libaom 冲突，并将其 C++ 专用警告选项限定在 C++ 文件；在 LibRaw 局部重新定义 `ZERO` 宏前显式取消旧定义。没有修改解码算法。

XMP 新增依赖：内置 Adobe XMP SDK（BSD，见 `licenses/Adobe-XMP.txt`）及 [Expat](https://github.com/libexpat/libexpat) 2.8.5 源码回退（MIT，见 `licenses/Expat.txt`）。系统 Expat 优先；回退版本静态链接。Windows GCC 的函数指针转换警告仅对 Exiv2 的 `basicio.cpp` 关闭，其余源码保持原有警告策略。

RAW 加速：源码构建的 LibRaw 可启用编译器 OpenMP 运行库。MinGW 使用 GCC libgomp，Windows 构建自动复制 `libgomp-1.dll`，许可为 GPL-3.0-or-later 加 GCC Runtime Library Exception（原文见 `licenses/GCC-GPL-3.0.txt`、`licenses/GCC-Runtime-Exception.txt`）。其他工具链遵循各自 OpenMP 运行库的部署要求。
