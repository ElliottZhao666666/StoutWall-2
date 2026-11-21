# 本机界面实测与主题导出修复

日期：2026-11-21（北京时间）。按用户要求，本轮只在当前本机测试，Win7 实机验收留后续；没有连接或探测 Win7 环境。

## 发现与修复

1. **UHD 收藏主题导出失败。** 手工界面复现默认 4K 档导出收藏返回 `Win32 error 11`。逐档导出基础图库的 70 张 JPEG 后发现，`20230808` 的 UHD 原图为 4500×2531，`20230809` 为 4596×2583；此前平台层只接受 3840×2160。现在 UHD 档保留宽至少 3840、高至少 2160、两边不超过 12000 的原图，其他四档仍要求各自的准确尺寸，不缩放或重新编码。导出校验同时检查 JPEG 起始、结束标记，大小限制为 64 MiB；这不是完整图像解码检查。实网下载的严格尺寸规则未放宽。
2. **主题表单标签覆盖下拉框。** 收窄参数分组中的四个静态标签，文字不再遮挡选项。原窗口、控件分组和业务入口继续保留。
3. **字体和选项卡显示不完整。** 控件字体按原版 EXUI 的微软雅黑约定创建；启用原来已经设置宽度所需的固定宽度选项卡样式。深色模式复选框在原有矩形内换行显示。
4. **键盘 Tab 无法进入表单。** 增加原生对话框消息处理、容器遍历和缺失的 Tab 停靠点，表单按位置排列遍历顺序。全屏图片/关闭按钮的叠放顺序单独保留。Escape 先关闭展开的下拉框。
5. **主题名称和错误提示不够明确。** 界面与 DLL 共用文件名检查，拦截控制字符、禁用字符、尾随句点/空格和 Windows 保留设备名；名称限制为 100 个 UTF-16 代码单元。主题导出错误显示中文原因，并保留 Win32 和打包工具退出码。同名包继续拒绝覆盖。
6. **主题名称受系统代码页限制。** 新增中文 GUI 回归先发现检查器将 ANSI 当作 UTF-8 读取；随后以 `我的收藏-ĒЖ🌄` 做真实 DLL 往返，进一步复现名称丢字。现在只把临时解包的主题模板转换为带 BOM 的 UTF-16LE，再写入名称和参数。原 EDT 模板不变，两种格式的中文名称及混合 Unicode 名称均已读回验证。

## 实测与回归结果

| 检查 | 结果及覆盖范围 |
|---|---|
| Computer Use 手工实测 | 隔离实例中重现导出错误，修复后同组 UHD 收藏导出成功；检查主题下拉框、主选项卡、Tab 焦点、下拉框 Escape、图库、大图右键菜单和悬停关闭按钮，最后经退出确认正常结束进程。 |
| 真实五分钟计时 | 在界面选择图库模式、5 分钟、顺序且未暂停。17:51:01 记录当前 `20230801`；17:57:58 复核为 `20230802`，当前图片修改时间为 17:56:00。配置保持 `Mode=2`、`ChangeFreq=0`、`pause=0`，图片 SHA-256 由 `D2E33CF8…F9D6` 变为 `FE7886F0…A4A5`。期间图库和大图模态窗口保持打开，主计时仍推进；没有实际应用桌面壁纸。 |
| 完整构建 | `build.cmd` 六阶段成功，退出码 0，未见编译警告。 |
| 最终双启动 GUI | 五个选项卡、来源保存/取消、辅助窗口、iOS 动作隔离记录、两种主题格式的全库和 UHD 收藏、非法名修改后重试、同名保护、大图四种模式像素采样、收藏持久性、清空收藏确认链及空收藏禁用选项、模拟托盘恢复、正常退出均通过。 |
| 主题专项 | 两种格式 × 五档尺寸的三张收藏，共十组；解包后逐张核对图片字节和次序，并检查名称、间隔、乱序、五种应用样式、颜色及深色/透明/自动主题色选项。另验证全库 UHD、单收藏无轮播、Unicode 名称、坏模板打包失败后以相同参数重试、同名包哈希/修改时间不变、错误档位/缺 JPEG 结束标记/非法名称被拒绝、临时目录清理。全部通过。 |
| 最终后端集成 | 配置/图库重开、收藏、图库/收藏推进、暂停、空库/单图、失败应用保持状态、导出/删除、迁移冲突和锁文件回滚后重试均通过。网络故障回归包括代理拒绝、CONNECT/TLS 截止时间、元数据和下载取消、目标文件保护及重复回调清理。 |
| 在途网络任务退出 | 挂起回环代理下，GUI 确认退出后 826 ms 正常结束，进程返回 0；后端请求取消约 6 ms，任务线程停止约 5 ms。 |
| PE 与发布依赖 | 八个发布 EXE/DLL 均为 x86，子系统版本不高于 6.01；导入表未见 MSVCRT/MSVCP/VCRUNTIME/UCRT 动态依赖。未把测试执行器计入产品依赖。此静态检查不代替 Win7 运行验证。 |

主题的 `.themepack` 与 `.deskthemepack` 均在本机生成和解包检查，没有安装主题。生成旧版格式不表示已进行 Win7 实测。

## 复测入口

在正式主工程根目录执行：

```powershell
cmd /c build.cmd
cmd /c tools\run_backend_integration_test.cmd
cmd /c tools\run_theme_integration_test.cmd
pwsh -NoProfile -File tools\verify_product.ps1 -GalleryFixture build\tests\isolated-current-20261121-01\Gallery
pwsh -NoProfile -File tools\verify_product.ps1 -GalleryFixture build\tests\isolated-current-20261121-01\Gallery -FocusedNetworkShutdown -TimeoutSeconds 5
```

完整 GUI 与退出专项应顺序运行，避免同时抢占焦点。两个原生集成执行器各自复制夹具到全新的目录。测试所需夹具与编译工具仍沿用仓库既有的本机环境。

## 证据

- 构建：`build/tests/build-local-review.log`。
- 手工界面与计时：`build/tests/local-cua-20261121-173344`，含隔离配置、图库、生成的主题包和 `timer-before.json` / `timer-after.json`。
- 尺寸排查：`build/tests/theme-audit-before/images`，70 张导出 JPEG。
- Unicode 丢字复现：`build/tests/themes-unicode-before.log` 和 `build/tests/theme-integration-26298`；失败记录保留，不计入通过结果。
- 最终主题专项：`build/tests/themes-local-review-final.log`，数据 `build/tests/theme-integration-26736`。
- 最终后端：`build/tests/backend-local-review-final.log`，数据 `build/tests/backend-integration-26736`。
- 最终 GUI：`build/tests/gui-local-review-final.log`，数据 `%TEMP%/stoutwall-verify-fd07d4a6eb7c4dc98d5c44361b2b0811`。
- 最终退出专项：`build/tests/gui-network-shutdown-local-review.log`，数据 `%TEMP%/stoutwall-verify-2be269638a4549a8a7630fe5cc32ae2d`。
- PE/导入及完整哈希：`build/tests/pe-audit-local-review.json`。

最终 `build/win32` 主要产物 SHA-256：

| 文件 | SHA-256 |
|---|---|
| `StoutWall.exe` | `584882D62EA11A8C091BF3F7CD9E00E54A417BB4253038E2988D70D3161DA87D` |
| `platform.dll` | `65F65A9FC20F63457C8B0D8A741D667D620892D7CB6B52BEAA124EFE7D7F7D05` |
| `network.dll` | `B0CCEE20F93906CCD2F6905D65921325D35D404207647ACE57BBD48E4A632147` |
| `library.dll` | `EF416F27A085F4194EF66495CB3F8E1B82DE23DAF849A341C4B1E6837A43F18C` |
| `assets.dll` | `90F3205C23AB866D30D5C92AC5016B9B1491FB513894AC2EFCAC0A6018CE4968` |

## 后续仍需完成

1. 原版逐控件视觉对照，以及 EX9“重置随机顺序”的原版运行时显示语义。保持未决，不猜测旧版扩展属性。
2. 多分辨率/DPI、物理托盘图标点击；目前大图四种模式像素回归覆盖 1920×1080，关闭按钮另做了本机悬停/静止实测。
3. 正文传输中断/读取阶段取消、认证代理、进程强制终止后恢复，以及跨日自动联网入库与轮播的长时串联。五分钟本地轮播通过不代表这些路径已经覆盖。
4. 当前正式构建的 Win7 SP1 实机验收，按用户要求留到后续。

原版七日页和自选主题的未完成业务边界继续保留。所有测试仅使用隔离配置、图库与输出，没有读写系统启动项，没有访问旧版用户配置或图库，没有实际应用壁纸或安装主题。
