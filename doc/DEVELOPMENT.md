# 开发说明

两个宿主使用相同的业务代码与固定版本插件。主项目侧重静态展示，VTuber 侧重动捕使用；仓库产品名、用户数据目录与发行标识独立。目前默认均隐藏菜单且不自动开启摄像头。版本或界面差异应以源码与实际发行构建为准。

## 模块边界

| 目录 | 职责 |
| --- | --- |
| `Source/HonkaiRendering/Mmd` | ZIP 导入、查看器与浮窗、协议、增强任务和部件显隐 |
| `Source/HonkaiRendering/Vtuber` | 识别快照转换、模型重定向、映射/标定和 Spout 输出 |
| `Source/HonkaiWindowStartup`、`Source/HonkaiLoadingScreen` | 窗口启动设置、引擎初始化加载页 |
| `Content/MmdViewer` | 应用关卡与通用背景资源 |
| `Plugins/HonkaiToonRenderer` | 通用材质、HLSL、运行时渲染及可选 NVIDIA 升频适配 |
| `Plugins/HonkaiMotionCapture` | 独立识别后端，GitHub 仓库名为 [MotionCapture](https://github.com/furinasdog/MotionCapture) |
| `Plugins/VRM4U`、`Tools/UMTNativePlugin` | PMX/Assimp 导入与 Bullet 刚体物理 |
| `Scripts` | 正式构建、资源生成、ZIP 与增强工具及检查入口 |

渲染插件只接收普通 UE 网格、相机和纹理对象，不依赖宿主 UI、PMX 或动捕。需要把卡通渲染用于自己的 UE 项目时，从[插件安装与接口说明](../Plugins/HonkaiToonRenderer/README.md)开始，不必复制整个查看器。

MotionCapture 仓库的名称不改变 UE 模块名与本地目录 `HonkaiMotionCapture`。插件输出原始识别数据；镜像、统一坐标变换、可见度、校准与角色映射由宿主处理，避免头部和身体重复或相反转换。

## 获取源码与依赖

从相应项目的 README 克隆仓库，然后初始化固定子模块：

```powershell
git submodule sync --recursive
git submodule update --init --recursive
```

不要使用 `--remote` 随意漂移依赖。修改共用插件时先提交并推送插件，再在两个宿主更新相同的 pin。PMX 改动通过现有 VRM4U/Assimp fork，物理约束通过已有 Bullet 实现，不另写解析器或按角色名称修补。

依赖位置及编辑器/Shipping 构建见[构建文档](BUILD.md)。识别实现与旧独立后端边界见[动捕说明](NATIVE_TRACKING.md)，PMX 刚体构建与回归见[物理说明](PHYSICS.md)。依赖准备与发行构建分开，发行脚本不自动下载 SDK 或安装工具。

## 编辑器工作流

编译 `HonkaiRenderingEditor`，打开项目关卡 `/Game/MmdViewer/L_Viewer`。两仓 `.uproject` 和 UE target 名仍为 `HonkaiRendering`，不能直接把 VTuber 的 target 改成仓库目录名。

共享着色器或图结构修改后，在启用 Python 的 Unreal Editor 环境运行：

```text
Plugins/HonkaiToonRenderer/Scripts/build_materials.py
Plugins/HonkaiToonRenderer/Scripts/verify_materials.py
```

应用关卡/背景初始化入口为 `Scripts/setup_viewer.py`。它是资源制作脚本，不是最终用户首次启动必须执行的步骤；资源生成须检查实际材质图、依赖和渲染输出。无界面制作材质时需要真实可渲染的环境及 `-AllowCommandletRendering`。

不要关闭用户正在使用的编辑器或热替换它加载的 DLL。UCLASS、模块或结构体布局变化后使用正常重启；在独立输出/独立用户目录验证，避免测试覆盖使用者的映射、协议和素材。

## 验证

基础 Windows 检查只需已安装的 Python 标准库与 PowerShell：

```powershell
python Scripts/test_archive.py
python Scripts/test_enhancement_service.py
```

这两项分别覆盖 ZIP 路径/体积边界、增强任务与缓存边界，不打开摄像头，也不验证 GPU 图像。涉及增强图算法时按工作器说明使用已安装且兼容的 Blender/NumPy 执行对应检查。增强协议见[工作器](ENHANCEMENT_WORKER.md)和[任务服务](ENHANCEMENT_SERVICE.md)。

UE 自动化按改动选择前缀：

| 前缀 | 覆盖范围 |
| --- | --- |
| `HonkaiRendering` | 导入状态、增强、协议与部分界面/配置 |
| `HonkaiVTuber` | 快照、重定向、眼口混合、映射、标定与输出配置 |
| `VRM4U.PMX` | PMX 材质、刚体、固定步时序、显示插值及 Mode2 等 |
| `HonkaiToon` | 通用渲染插件的绑定、材质与升频状态 |
| `HonkaiMotionCapture` | 动捕插件快照与生命周期约束 |

UE 控制台使用 `Automation RunTests <前缀>`。检查报告内的失败、未执行与警告，不能只看进程退出码。测试名称或覆盖会演进，以当前注册的测试为准。

渲染改动使用真实 GPU，同输入比较修改前后及参考图；物理按静止、运动、动停、重置和多帧率测试。分别报告 native 刚体轨迹、显示骨骼与最终网格的结果。NullRHI、合成数据、图片识别与真人动作不是同一种验收，不能相互替代。

源码中的 `WITH_DEV_AUTOMATION_TESTS` 参数用于隔离回归。离线识别回放须明确标注没有使用摄像头；测试跳过协议只能用于独立测试用户目录，不能替真实用户确认许可。测试素材、原始快照、截图与本地报告放入被忽略的 `Saved` 等目录，不进入 Git 或发行包。

## 渲染及数据约束

当前管线面向桌面延迟渲染，不支持把相同配置直接视为已兼容 Forward、Mobile、Substrate 或多主体组合。以源颜色/透明度、PMX Toon 与描边标志、球面贴图和通用回退为输入；自定义面部控制图是独立可选数据，不是模型名字的预设。

共享插件对 GBuffer 通道、曝光和相机有明确要求，修改宿主光照或其他后处理前阅读插件契约。半透明覆盖层没有完整 GBuffer，不能承诺与不透明表面相同的分段处理。源素材缺少原游戏美术数据时，不宣称自动恢复其 FaceMap 或 LightMap。

## 发布

按[构建文档](BUILD.md)从已提交源码生成全新输出目录，并真实运行该目录的 EXE。核对 `BuildManifest.json`、`SHA256SUMS.txt`、许可目录和烘焙资产白名单。新增资源必须先确认可以分发，不能为了让构建通过而盲目扩大白名单。

严禁提交或分发角色模型、原始模型包、贴图、增强贴图、立绘、参考游戏截图和私人录像。官方通用识别权重、Unreal Engine/NVIDIA 运行文件分别遵守自身许可；SDK 源码、品牌图像和本地授权记录不作为 MIT 源码提交。
