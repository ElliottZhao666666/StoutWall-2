# 资源清单与原版字节核对

日期：2026-11-20
结论口径：资源身份与清单数据核对；**未验收所有资源的运行时解码、布局或屏幕显示。**

## 原图标

原始资源 `../../image/icon.bin` 是 Windows ICO，文件长度 **432,254 bytes**。ICO 目录含 9 个图像项，声明尺寸为 **256×256、128×128、96×96、72×72、64×64、48×48、32×32、24×24、16×16**。

| 项目 | 值 |
|---|---|
| 原始路径 | `image/icon.bin` |
| 原始大小 | 432,254 bytes |
| SHA-256 | `082def294e64cb9feba5e840619fd7e96ca2201cd543e40a27eea3b9dd258781` |
| 重建包路径 | `StoutWall_ReBuild_2/resources/app.ico` |
| 清单对应关系 | `appIcon.source=image/icon.bin`；`payload=app.ico`；`alsoIncludedAs=IMG/icon` |
| 字节核对 | 原始文件与 `app.ico` 长度相同，SHA-256 相同 |

尺寸来自 ICO 文件头中的目录项；不表示每种尺寸已在应用中实际显示验收。

## 683 项既有 manifest

`StoutWall_ReBuild_2/resources/manifest.json` 声明 `assetCount=683`，`assets` 数组也有 **683 项**，`totalPayloadBytes=33,592,319`。以下分类和数字均取自该清单，不是对屏幕显示结果的计数：

| 清单 `format` | 项数 |
|---|---:|
| `PNG` | 378 |
| `EXUI-extension` | 139 |
| `base64-property` | 149 |
| `ICO` | 5 |
| `BMP` | 1 |
| `JPEG` | 1 |
| `opaque` | 1 |
| `XML` | 9 |
| **合计** | **683** |

清单含 9 个原始窗口 XML 来源文件。`assets` 中有 681 项的 `source` 来自 `src/*.xml`（包括完整 XML 载荷行与窗口 XML 内嵌资源）；另两项来自 `image/icon.bin` 和 `image/nopic.bin`。清单中所有 683 个 `payload` 相对路径在当前资源目录均有对应文件。文件存在和哈希/清单对应关系不证明每个控件都能正确解码、绘制或呈现。

## XML 载荷路径关系

清单记录原始来源、资源名、载荷相对路径及 SHA-256。完整窗口 XML 使用独立载荷项，例如：

```text
source:  src/_启动窗口.xml
name:    XML/_启动窗口/@source/xml
payload: payload/526746c031fc5666b84508b7a7299767005f587a09a03b91a219b0618196069f.bin
```

窗口 XML 内嵌图像等二进制属性也逐项映射为内容哈希命名的载荷文件。例如：

```text
source:  src/库迁移窗口.xml#窗口EX1/@扩展属性数据
payload: payload/5f01e05aa2548f1adf0ac79d03c24ffaa94697ae9c6569aff51c5046d79451d8.bin
```

以上具体路径是一项 162-byte PNG 载荷的清单记录。实际资源路径以 `manifest.json` 每一项的 `payload` 字段为准；`source` 保留 XML 控件/属性出处，`payload` 指向重建资源目录中的二进制文件。

## 验收边界

- 本核对确认原图标的字节数、ICO 声明尺寸、SHA-256，以及重建 `app.ico` 的字节级一致性；同时记录既有 manifest 的 **683** 项及 XML 载荷映射。
- 不据此声称已逐一测试 378 个 PNG、其它图像、九个窗口的图片控件，或实际 UI 的资源显示。运行时资源验收仍属于 [ACCEPTANCE.md](ACCEPTANCE.md) 中未执行的项目。
