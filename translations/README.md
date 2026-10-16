# 界面语言

设置支持简体中文和 English，点击“应用”或“确定”保存；重启软件生效。默认保持简体中文，未知语言配置回退至中文。取消不保存草稿，恢复默认需应用后保存。

英文使用标准 Qt TS/QM 语言包，嵌入应用资源，因此安装包不需要额外 translations 目录。中文使用源文案。已提交 TS 源文件和生成的 QM，普通构建不依赖 LinguistTools。

更新文案后，在安装了 Qt Linguist 工具的环境执行：

```sh
lupdate src -no-obsolete -ts translations/videx_en_US.ts
lrelease translations/videx_en_US.ts -qm translations/videx_en_US.qm
```

翻译新增项，保持占位符完整，再同时提交 TS 和 QM。`LanguageService` 在创建界面之前加载保存的语言；正在打开的窗口不会即时切换语言。
