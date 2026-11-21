# 网络验收与接续记录

日期：2026-11-21（北京时间）。正式主工程：`StoutWall_ReBuild_2`。

## 本轮接续点和改动

上个会话停在未提交的 `Network.cpp` 和后端网络回归修改。本轮保留并完成该修复，移除临时标准错误调试输出。网络 DLL 在持久后台工作线程上使用异步 WinHTTP，统一等待完成通知、调用方取消事件和单次请求的总截止时间；元数据与图片共用该路径。超时返回 `ERROR_TIMEOUT`（1460），取消返回 `ERROR_CANCELLED`（1223）。一次刷新可以包含多次请求，不能把单次请求预算理解为整个刷新的总预算。

关闭请求后继续保留回调上下文和读缓冲区，直到最终 `HANDLE_CLOSING` 通知。回调关闭语义依据微软 [WinHttpCloseHandle](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpclosehandle) 和 [Concurrency in WinHTTP](https://learn.microsoft.com/en-us/windows/win32/winhttp/concurrency-in-winhttp)；没有设置忽略证书验证的选项。

进一步实测发现，产品的 `BeginClose` 先前只排队关闭图库，没有先取消进行中的请求。挂起回环代理时，确认退出超过 5 秒仍未结束。本轮补上退出时发送取消信号，图库关闭和后端释放继续留在原工作线程，避免为快速退出破坏图库线程约束。

## 已完成验证

| 检查 | 本轮结果 |
|---|---|
| 完整产品构建 | `cmd /c build.cmd` 成功；无新增编译警告。 |
| 代理拒绝与截止时间 | 回环代理返回 502 被拒绝；CONNECT 阶段 1200 ms 预算实测 1200 ms 返回 1460。完成 CONNECT 后收到 TLS ClientHello 的挂起场景同样受总截止时间约束。 |
| 在途取消 | 15 秒请求预算下，元数据请求实测约 5 ms 以 1223 结束。图片请求在 CONNECT/TLS 两种阶段交替取消，共 8 次。 |
| 下载文件保护 | 预取消、代理失败、超时和重复在途取消后，原目标内容不变，输出目录没有额外临时文件。 |
| 任务线程停止 | `TaskRunner::Stop` 实测约 4 ms 完成取消、线程汇合并清空结果消息。 |
| GUI 在途退出 | 同一挂起回环代理用例修复后约 546 ms 正常退出，进程返回 0；没有结束其他应用。 |
| TLS 证书拒绝 | 私有回环代理提供名称匹配但未受信任的自签名证书，只允许 TLS 1.2；WinHTTP 返回 12175、HTTP 状态 0，服务端没有收到 HTTP 请求正文或请求头。证书及密钥仅存于隔离测试目录，未导入信任库，未更改系统代理。 |
| 三源实网 | 全球 Bing、中国 Bing、自建来源均返回 HTTP 200、日期 `20261121`；三源共 15 张 JPEG 均实下载，五档尺寸、保存字节数和重新验证结果一致。当前出口下三源仍返回同一张图。 |
| 完整后端 | 收藏、图库/收藏轮播、空库/单图、配置重开、两种主题包、导出、删除、迁移冲突、锁文件回滚和字节一致的重试通过。 |
| 完整双启动 GUI | 五个选项卡、来源选择保存/取消、可用辅助窗口、两种主题包及同名保护、iOS 外部动作隔离记录、大图四种模式像素、收藏重开、清空收藏确认和托盘恢复回调通过。 |
| 发布依赖 | 产品 EXE/DLL、7za 和 cabarc 均为 x86，子系统不高于 6.01；未发现 MSVCRT/MSVCP/VCRUNTIME/UCRT 动态依赖。 |

已下载和证书测试使用的 `network.dll` 与最终发布目录文件 SHA-256 一致：`B0CCEE20F93906CCD2F6905D65921325D35D404207647ACE57BBD48E4A632147`。最终主程序 SHA-256：`6EF90B522A8DE1EE0C49CAF78A962FF3C675DB9C7F84518BC3AA3131FEDFD50F`。

## 测试入口与证据

在正式主工程目录执行：

```powershell
cmd /c build.cmd
cmd /c tools\run_backend_integration_test.cmd
cmd /c tools\run_network_smoke_test.cmd
python tools\verify_network_certificate.py
pwsh -NoProfile -File tools\verify_product.ps1 -GalleryFixture build\tests\isolated-current-20261121-01\Gallery
pwsh -NoProfile -File tools\verify_product.ps1 -GalleryFixture build\tests\isolated-current-20261121-01\Gallery -FocusedNetworkShutdown -TimeoutSeconds 5
```

普通后端回归的网络故障部分只使用回环代理。`run_network_smoke_test.cmd` 会访问三源和图片服务器，另写全新的 `build/tests/network-smoke-*`；也可以显式传 `--root NEW_DIRECTORY --proxy HOST:PORT`。证书脚本需要开发环境的 Python `cryptography`，复用已构建的 x86 网络探针，测试目录必须全新。上述依赖仅用于测试，不随产品发布。

本轮证据：

- 构建日志：`build/tests/build-resume.log`。
- 最终后端日志：`build/tests/backend-integration-resume.log`；数据目录 `build/tests/backend-integration-11700`。
- 三源日志：`build/tests/network-smoke-resume.log`；图片目录 `build/tests/network-smoke-9398`。
- 证书拒绝：`build/tests/network-certificate-ce323423ffb9/client.log` 和 `server.log`。
- 修复前 GUI 退出失败目录：`%TEMP%/stoutwall-verify-c049350a3efa436dbc3c3e5c4b279724`。
- 修复后 GUI 退出日志：`build/tests/gui-network-shutdown-resume.log`；隔离目录 `%TEMP%/stoutwall-verify-82c71d67b5914cfe9f664708ee1f2b40`。
- 最终双启动 GUI 日志：`build/tests/gui-resume.log`；隔离目录 `%TEMP%/stoutwall-verify-da162632e08645238e409c9da57718c3`。
- PE/导入检查：`build/tests/pe-audit-resume.json`；最终主程序仅增加取消事件的现有 Kernel32 调用，未增加发布依赖。

## 后续仍需完成

1. **正式构建在 Win7 SP1 上验收。** 本轮对既有目标的 WinRM TCP 5985 重新只读探测仍超时，没有调整虚拟机、防火墙或远程桌面。历史候选结果不能替代当前正式构建实测。
2. **原版逐控件视觉与 EX9 语义。** EX9 的旧版扩展属性映射仍缺运行时证据，继续保持未决，不据此猜测显示或可用状态。大图关闭按钮透明度时间变化、多分辨率/DPI尚需实测。
3. **主题包收藏范围 GUI 及其余选项组合。** 本轮完整 GUI 检查覆盖两种格式的全库导出，不能扩写为全部组合均通过。
4. **异常恢复矩阵和真实计时串联。** 正文传输中断/读取中取消、认证代理、归档工具失败、长周期自动更新与轮播串联尚需补证据。

所有测试继续使用隔离配置、图库和输出；不访问旧版数据，不读写系统启动项，不实际应用桌面壁纸或安装主题包。原版七日页、自选主题的未完成业务边界继续保留。
