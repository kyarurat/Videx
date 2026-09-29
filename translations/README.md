# 多语言扩展

中文源文案是当前默认语言。使用 Qt Linguist 的 lupdate 提取 src 中的 tr() 文案到 videx_<locale>.ts，经翻译和 lrelease 后将 videx_<locale>.qm 放到可执行程序旁的 translations 目录。

入口已持有 QTranslator 并从该目录加载 videx_zh_CN；后续增加语言选择时，将选中语言传入该加载入口即可。当前不承诺运行中切换语言；新增语言可在下一次启动应用时加载。LinguistTools 不作为 GUI 构建的强制依赖。
