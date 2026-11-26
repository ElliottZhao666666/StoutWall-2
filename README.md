# StoutWall ReBuild 2

当前内部测试版：**1.1.9.2611B**（PE 数值版本 `1.1.9.2611`）。4K 壁纸的界面预览使用 1920×1080 图像；桌面应用、保存与导出仍保留选定原尺寸。

本轮优化、性能测量与测试范围见 [4K 预览性能记录](docs/PERFORMANCE_20261126.md)；后续正式界面的三套布局与实施建议见 [界面设计方案](docs/UI_DESIGN_OPTIONS.md)。

当前仓库承接胖哥每日壁纸的第二次重制。2026-11-26 起，`main` 使用本工程作为开发主线，旧重构目录已从主线移除。

这是第二次重制的正式主工程。正式主线采用 **C++17、Win32、Direct2D/DirectWrite**，目标系统为 Windows 7 SP1 x86 及以上，联网必须使用 TLS 1.2。选择依据是已在 Win7 SP1 上运行的候选与较少的运行时部署依赖；目前没有合格的双栈资源采样，故不声称它的内存或 CPU 占用实测优于 WPF。WPF/.NET Framework 4.8 原型保留作界面和行为参考。

两套原型已在 Win7 SP1 完成首帧显示，并通过同一代理分别完成 Bing 中国源真实入库和独立进程重开。Demo 对照阶段按“核心链路验证可用”收口；完整界面交互和资源验收尚未完成，也没有性能排名。原型报告保留在本地同级 `StoutWall_ReBuild_2_Test`，未包含在本仓库中；当前主工程的验收记录见 `docs/`。

当前 src/ 与 dependencies/gallery.dll 是从通过 Win7 真实网络/重开验证的 C++ v6-04b-03 候选迁入的首个开发基线，来源版本和 DLL 哈希见 DEVELOPMENT_BASELINE.md。这不是最终产品版本；接下来会把原型夹具参数、测试专用输出及临时控制入口从产品启动流程中逐步剥离。

原始易语言目录只作行为参照，图库数据格式通过现有 x86 gallery.dll 兼容。构建需要的原版运行文件已归入 `dependencies/runtime/`，无需同级旧工程目录；资源提取脚本仅在重新提取原始易语言资源时需要原工程。

## 已确认边界

- 原易语言 1.1.0 是未完成功能的半成品，不应将七日壁纸和主题包自选视为已实现业务。
- gallery.dll 使用 x86 stdcall、ANSI 文本和串行会话；调用方必须是 32 位进程并显式管理生命周期。
- 新程序采用 Unicode 界面和 UTF-8 配置；图库格式继续由兼容层处理。
- 网络、配置、系统壁纸与图库写入需隔离测试，发布前建立可重复回归矩阵。
- 测试过程不读写旧软件的自动启动配置。

## 构建与运行

运行 `cmd /c build.cmd`，产物位于 `build/win32`。需要 Windows、Git、Visual Studio C++ x86 构建工具和 Windows SDK。默认工具链路径为 `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat`，MSVC 14.44、SDK 10.0.26100.0；其他安装可设置 `STOUTWALL_VSBUILD`、`STOUTWALL_MSVC_VERSION` 和 `STOUTWALL_SDK`。

首次构建会下载 7-Zip 25.01 的固定源码修订，静态编译独立的 7za.exe，并随产物保存许可文件。主程序和 C++ DLL 使用 /MT，assets.dll 只含资源。原版运行依赖及校验值见 [依赖说明](dependencies/README.md)。

无参数启动使用 `%LOCALAPPDATA%\StoutWallReBuild2` 下的专用配置和图库，并按设置更新桌面。开机自启只保留界面偏好。验收必须使用 `--test-mode` 或 `--ui-test`，该模式不会应用桌面壁纸，配置可用 `--data-root` 和 `--config` 指向独立测试目录。本机实测和 Win7 SP1 待测项见 [验收记录](docs/ACCEPTANCE.md)。

网络超时、取消、证书拒绝、三源下载及挂起请求下退出的复测入口和接续待办见 [网络验收与接续记录](docs/NETWORK_ACCEPTANCE.md)。

2026-11-21 进一步完成本机 Computer Use 实测和真实五分钟图库轮播，修复 UHD 收藏主题导出、Unicode 主题名称、参数遮挡和键盘导航。最终双启动 GUI、主题专项、后端及在途退出回归均通过，详细范围与日志见 [本机验收与修复记录](docs/LOCAL_ACCEPTANCE.md)。Win7 实机验收按用户要求留后续。

## 许可

延续原仓库的 [PolyForm Noncommercial License 1.0.0](LICENSE)。7-Zip 构建产物附带其上游许可。
