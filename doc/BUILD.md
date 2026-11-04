# Windows 构建

本项目提供 PowerShell 发行脚本，自动定位已安装的引擎、C++ 工具链和 Windows SDK，缺少依赖时直接报错。**发行构建不下载 SDK、不安装环境，也不捆绑角色素材。** 请先完成依赖准备。

## 环境与源码

| 依赖 | 要求 |
| --- | --- |
| 操作系统 | Windows x64 |
| Shell / Git | PowerShell 7.2+，Git 可从命令行调用 |
| Unreal Engine | 项目关联 UE 5.8；已有验证使用 5.8.4 |
| C++ | Visual Studio / Build Tools 的桌面 C++、x64 MSVC 14.44 |
| Windows SDK | 完整 x64 SDK；脚本优先选择引擎声明的版本 |
| PMX 物理准备 | CMake、Ninja、匹配的 MSVC 和固定 Bullet 依赖 |
| 动捕运行时 | 与插件清单匹配的本地 MediaPipe DLL、三份通用识别权重 |
| DLSS（可选） | 与 UE 5.8 匹配的官方 NVIDIA 插件及其许可 |

先使用现有环境，不必重装工具。源码仓库及其子模块应处于所选择提交的固定版本：

```powershell
git submodule sync --recursive
git submodule update --init --recursive
```

不要通过 `git submodule update --remote` 取得未经验证的依赖。两仓内部均使用 `HonkaiRendering.uproject`、`HonkaiRenderingEditor` 和 `HonkaiRendering` target；产品名从 `Config/DefaultGame.ini` 读取。

## 准备本地依赖

1. **PMX 导入**：VRM4U fork 提供所需 Assimp 源码及接口。确认固定版本的 `assimp-vc141-mt.lib` / DLL 已按插件说明准备，不能使用不匹配的预编译文件。
2. **物理**：按[物理说明](PHYSICS.md)运行 `Scripts/Prepare-PmxPhysics.ps1`，构建并测试 `Tools/UMTNativePlugin/build-static-ue144/bundled/UMTNativePlugin.lib`。首次准备可能需要取得固定 Bullet 源码；这一步独立于发行脚本，不会自动安装 CMake 或编译器。
3. **动捕**：按[动捕说明](NATIVE_TRACKING.md)取得匹配的本地运行时，执行以下校验和复制。该脚本不下载或执行输入目录中的程序：

```powershell
./Plugins/HonkaiMotionCapture/Scripts/Stage-LocalRuntime.ps1 -RuntimeSource '<已有运行库目录>'
```

输入目录须包含 `libmediapipe.dll` 和 `Models` 下插件清单指定的三个 `.task` 文件。脚本验证固定 SHA256。GitHub 仓库已命名为 [MotionCapture](https://github.com/furinasdog/MotionCapture)，UE 模块名及本地路径仍是 `HonkaiMotionCapture`。

4. **DLSS（可选）**：先取得适用于项目的官方 SDK 并核对其许可，再用渲染插件的准备脚本导入本地包：

```powershell
./Plugins/HonkaiToonRenderer/Scripts/Prepare-NvidiaDlss.ps1 -ProjectRoot . -ArchivePath '<官方SDK压缩包>'
```

依赖安装到宿主 `Plugins/NVIDIA`，不进入共享渲染插件仓库。不要提交 SDK 源码或二进制。没有 SDK 仍可编译 TSR 路径，NVIDIA 选项会报告不可用；使用 `-RequireDlss` 可要求发行检查必须找到官方 SDK。

## 检查与构建

关闭占用**本项目**二进制的编辑器，确认希望发布的源码已提交。在仓库根目录执行：

```powershell
./Scripts/Build-Release.ps1 -CheckOnly
./Scripts/Build-Release.ps1
```

`-CheckOnly` 检查依赖与许可，不运行编译和烘焙。正式流程先编译编辑器目标，再构建 Win64 Shipping、烘焙、审核完整资产清单并打包。失败立即终止；检查 `Saved/ReleaseBuilds` 和 UAT 日志，已有发行目录不会被覆盖。同一工作区一次只运行一个构建。

| 参数 / 环境变量 | 用途 |
| --- | --- |
| `UE_ROOT`、`-EngineRoot` | 覆盖引擎位置；默认检查引擎关联、Epic 安装清单与注册表 |
| `-CheckOnly` | 仅检查，不生成发行包 |
| `-RequireDlss` | 缺少官方 NVIDIA SDK 时拒绝构建 |
| `-LicensedEpicSplashBmp` 与 `-EpicSplashApproval` | 同时提供已授权官方 BMP 和本地许可记录 |

编辑器开发可生成项目工程文件并编译 `HonkaiRenderingEditor`，不需要每次执行完整发行打包。资源制作及测试见[开发说明](DEVELOPMENT.md)。

## 输出与验收

输出位置为 `dist/<产品>-<版本>-<提交>-<UTC时间>/`。两个应用的启动文件均为 `HonkaiRendering.exe`；交付完整目录，保留 `Engine`、`HonkaiRendering`、`Licenses` 和其他依赖。

- `BuildManifest.json` 记录产品、源码、引擎、工具链、子模块、构建时间与功能标记。
- `SHA256SUMS.txt` 用于核验输出文件，新增或替换文件后旧校验值不再代表当前目录。
- 版本基于 `ProjectVersion` 与 Git SHA；UTC 构建时间和当前提交可达的作者显示名写入程序。“应用 → 关于”展示这些信息和启用的插件版本。
- 作者显示名由 Git mailmap 归并，不写入贡献者电子邮件；完整原始许可证放入 `Licenses`。

实际启动该目录的 EXE，检查窗口模式、首次许可、拖入合法模型包、选择 PMX、渲染及退出。需要发布动捕/输出功能时，还要验证真实摄像头的启动、识别、停止/重开与实际 Spout 接收端。只通过编辑器、NullRHI 或离屏测试不足以说明这些场景已通过。

`doc/CookedAssets.allowlist.tsv` 限定通用烘焙资产。清单不一致时先检查导出的完整资产表，确认每个新增资源的分发权；不要直接接受全部差异。**角色模型、模型压缩包、源贴图、增强贴图、参考截图、立绘与用户录像一律不得进入源码或发行包。** 通用 MediaPipe 识别权重按其许可提供，不属于此处的角色素材。

## 启动画面素材

当前有不依赖图片的项目加载页。使用 Epic 品牌素材前，须核对 [Epic 官方品牌入口](https://www.unrealengine.com/en-US/branding)和适用许可；不要把引擎署名等同于 Logo 授权。

构建脚本只在同时提供两个输入时复制原始 BMP：

```powershell
./Scripts/Build-Release.ps1 -LicensedEpicSplashBmp '<授权官方BMP>' -EpicSplashApproval '<本地许可记录>'
```

输入检查不能代替发行者核对授权范围。不提供时不打包 Logo；不将品牌图像或许可记录提交到公共仓库。其他第三方许可同样应保留，不以本项目 MIT 重新许可。
