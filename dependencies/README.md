# 运行依赖

`gallery.dll` 保持原版图库格式兼容；应用和 DLL 调用方必须为 x86。

`runtime/` 中三个文件取自旧仓库 `623429b0066505bc7667dfd25c0a832ca16f3e1a` 的 `runtime-copy/`，已与该提交逐个核对 Git blob，内容一致。它们用于首次释放基础图库和导出主题；没有包含旧版配置、个人图库或测试输出。`build.cmd` 从本目录复制这些依赖，单独克隆本仓库即可构建，无需旧工程目录。

| 文件 | 字节数 | SHA-256 |
| --- | ---: | --- |
| `gallery.dll` | 815104 | `AF9AAF5A5B2770632EAA7BAA8D65CB02C773EDB057BB694CF6E7960370F6BEF4` |
| `runtime/cabarc.exe` | 114688 | `3ED33E71641645367442E65DCA6DAB0D326B22B48EF9A4C2A2488E67383AA9A6` |
| `runtime/ForTheme.EDT` | 18880 | `4B3CE2B05ECC0805163085D5D789A62FEB03309EAD35BFD0F3F1C90F2356F685` |
| `runtime/GalleryBase.EDT` | 62590577 | `87FCC47D801EB70E8259CABF9D133A129ECA8A6B5A61DA9DA15307059B5DA58C` |

7-Zip 25.01 由 `tools/build_7za.cmd` 获取并校验固定修订 `5e96a8279489832924056b1fa82f29d5837c9469`，静态编译为 `7za.exe`，同时复制上游许可到产物目录。生成文件保存在 `build/`，不纳入 Git。

首次构建需要联网获取 7-Zip 源码，后续构建复用已校验的本地源码。当前主程序不加载旧版 `7z.dll` 或 `cabinet.dll`。
