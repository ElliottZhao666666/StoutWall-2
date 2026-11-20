# 开发基线

- 日期：2026-11-21
- 候选来源：../StoutWall_ReBuild_2_Test/2_cpp_d2d/v6/candidates/20261120-v6-04b/builds/20261120-v6-04b-03/source
- 候选构建：20261120-v6-04b-03，x86、C++17、/MT、Windows 7 子系统目标 6.01。
- 已迁入产品源：src/Core.cpp、src/Core.hpp、src/Integration.cpp、src/Presentation.cpp、src/Presentation.hpp、src/Ui.cpp。测试执行器、网络探针、UI 首帧测试探针和历史轮次文件未迁入。
- dependencies/gallery.dll SHA-256：AF9AAF5A5B2770632EAA7BAA8D65CB02C773EDB057BB694CF6E7960370F6BEF4，与既有 gallerydll 工程输出一致。
- 当前开发基线首次构建：build/win32/StoutWall.exe，443,904 B，SHA-256 955F8D04FC3A5DD123126EE794EF368EA61C72605AD11FF23AD8BD31561CBABF；PE32 x86，Windows GUI 子系统，OS/SubSystem 版本 6.01。编译使用 MSVC 14.44、C++17、/MT、Windows 7 SDK 目标。后续迭代产物摘要及验收结果以 docs/ACCEPTANCE.md 记录为准。
- 基线阶段完成两轮 GUI 验证和后端集成验证。2026-11-21 迭代另完成双启动 GUI 与后端回归：四种大图显示模式像素采样、清空收藏确认路径、定时状态调整、自动保存最新壁纸探针及主题包同名保护结果见 docs/ACCEPTANCE.md。
- 构建与静态依赖检查已通过：主程序、network.dll、platform.dll、library.dll 使用 /MT；未发现 MSVCP、VCRUNTIME 或 ucrtbase 动态 CRT 依赖；assets.dll 为资源型 DLL。测试使用隔离配置、图库和输出目录，未读写系统启动项，未在常规测试中应用真实桌面壁纸。
- Win7 SP1 上的 Bing 中国源真实入库和独立进程重开记录仍作为候选阶段补充证据；当前主工程的联网来源完整矩阵、失败/取消路径、原版全部界面细节和性能采样仍待后续轮次。

每次改造都应保留上述候选和测试证据原件。新功能优先在正式主工程实现，测试环境使用隔离图库和输出目录。任何构建、测试或启动不得访问旧版或新版开机启动位置，不得在测试中实际应用系统壁纸。
