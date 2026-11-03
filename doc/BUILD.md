# Windows 构建

从仓库根目录用 PowerShell 7.2 或以上运行：

```powershell
git submodule update --init --recursive
./Scripts/Build-Release.ps1 -CheckOnly
./Scripts/Build-Release.ps1
```

脚本检查依赖，编译编辑器目标供烘焙使用，然后编译 Win64 Shipping、烘焙、核对资产、打包并输出到 `dist/<产品>-<版本>-<提交>-<UTC时间>/`。从输出目录双击 `HonkaiRendering.exe`；保留旁边的 `Engine`、`HonkaiRendering` 和 `Licenses` 目录。失败时检查 `Saved/ReleaseBuilds` 和 UAT 日志，已有发行目录不会被覆盖。关闭正在占用同一项目二进制的 UE 编辑器，再执行构建。一次只构建同一工作区的一个版本。

## 依赖

- Windows x64、Git、PowerShell 7.2+、UE 5.8（已验证 5.8.4）。脚本按项目引擎关联查找自定义引擎注册表、Epic Launcher 安装清单及引擎安装注册表。也可以设置 `UE_ROOT` 或传 `-EngineRoot`。
- Visual Studio 或 Build Tools，安装 C++ 桌面开发、x64 MSVC 14.44 和完整 Windows SDK（UE 5.8.4 首选 10.0.22621.0）。脚本通过 `vswhere` 和 Windows Kits 注册表查找工具；无需手动填写本机绝对路径。
- Git 子模块须与宿主提交锁定的版本一致。VRM4U fork 提供 Assimp；按 [PHYSICS.md](../PHYSICS.md) 用 `Scripts/Prepare-PmxPhysics.ps1` 准备 Bullet 静态库。编译该库时另需 CMake、Ninja 和固定版本的 Bullet 源码。
- 按 [NativeTracking.md](../NativeTracking.md) 准备原生 MediaPipe 运行时及三份通用识别权重，通过 `Plugins/HonkaiMotionCapture/Scripts/Stage-LocalRuntime.ps1 -RuntimeSource <本地运行时目录>` 校验并放入插件。发行脚本不会自动下载或安装软件，依赖缺失时直接失败。
- DLSS 可选。按 [渲染插件说明](../Plugins/HonkaiToonRenderer/README.md) 使用官方 UE 5.8 SDK 和准备脚本；用 `-RequireDlss` 可要求本次构建必须含官方 DLSS。SDK 源码不随发行包分发。

## 版本与资产

`Config/DefaultGame.ini` 的 `ProjectVersion` 是发行基础版本。构建脚本把版本（含 Git 短 SHA）、UTC 构建时间和当前提交可达的 Git 作者显示名编译进程序。作者由 Git mailmap 归并，不读取或写入电子邮件。程序内按 Esc → 应用 → 关于查看；插件版本来自本次程序启用的描述文件。完整许可、构建清单和 SHA256 文件随发行目录提供。

只从已提交的代码做发行构建。用 `doc/CookedAssets.allowlist.tsv` 对照 Unreal 导出的完整烘焙资产清单；只有核实新增资源是可分发的通用资产后才能更新白名单。禁止提交或打包角色模型、贴图、增强贴图、参考截图和录像。通用 MediaPipe 权重例外。模型验收使用本机忽略目录。

## Epic 官方启动图

[Unreal Engine EULA §7(a)](https://www.unrealengine.com/eula/unreal) 要求有制作人员名单时提供相应引擎署名；它不授予 Unreal Engine Logo 的使用权。先通过 [Epic 官方品牌门户](https://www.unrealengine.com/en-US/branding) 确认适用于本项目启动画面的书面许可及指定素材。没有许可的构建仅在关于页显示引擎署名，不打包 Logo。Epic 提供并明确许可原样使用 BMP 时：

```powershell
./Scripts/Build-Release.ps1 -LicensedEpicSplashBmp <授权官方BMP> -EpicSplashApproval <本地许可记录>
```

脚本要求两个输入同时存在，检查 BMP 格式并原样复制到 UE 原生启动图路径；不会上传许可文件，也无法替代人工核对授权范围。请勿把 Epic 图像或许可文件提交到仓库。
