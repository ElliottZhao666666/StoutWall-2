# StoutWall ReBuild 2

第二次重制主工程。正式技术栈将在同一标准的 WPF/.NET Framework 4.8 与 C++/Direct2D 原型完成测试后确定；Windows 7 SP1 是主程序最低系统，TLS 1.2 是网络硬要求。原始易语言目录仅作行为参照，图库兼容层沿用现有 x86 gallery.dll 接口。

本仓库先记录产品边界与重制决策，测试原型和报告位于同级 StoutWall_ReBuild_2_Test。当前禁止实现或启用开机自动启动；旧版启动项污染风险必须由独立的后续设计处理。

## 已确认边界

- 原易语言 1.1.0 是未完成功能的半成品，不应将七日壁纸和主题包自选视为已实现业务。
- gallery.dll 使用 x86 stdcall、ANSI 文本和串行会话；调用方必须是 32 位进程并显式管理生命周期。
- 新程序采用 Unicode 界面和 UTF-8 配置；图库格式继续由兼容层处理。
- 网络、配置、系统壁纸与图库写入需隔离测试，发布前建立可重复回归矩阵。
- 测试过程不读写旧软件的自动启动配置。

## 当前阶段

技术栈原型对照。见 ../StoutWall_ReBuild_2_Test/TEST_STANDARD.md 和 FEATURE_MATRIX.md。Win7 SP1 必须由真实 Win7 SP1 环境验收；Server 2012 R2 和当前开发机只能提供补充证据。
