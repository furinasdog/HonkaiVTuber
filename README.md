# HonkaiVTuber

从 HonkaiRendering `8ba15b0` 派生的 UE 5.8 原生 C++ VTuber 预览工程。当前包含面部、头部、上半身与手部通道、可编辑响应曲线和不透明 Spout 输出；真人动捕与完整第二阶段尚未验收。原静态查看器保留在独立仓库。

运行说明见 [PHASE2_PREVIEW.md](PHASE2_PREVIEW.md)，本机实际验证与限制见 [PHASE2_VALIDATION.md](PHASE2_VALIDATION.md)，摄像头及推理说明见 [NativeTracking.md](NativeTracking.md)。当前默认进程内动捕；旧 EXE 后端的黑帧、约 1 FPS 及退出延迟是历史记录，不代表当前插件表现。默认不启动摄像头，真人效果由用户验收。

以下保留上游静态查看器说明。派生工程的用户数据根目录改为 `%LOCALAPPDATA%/HonkaiVTuber`。

## HonkaiRendering / 崩坏渲染

基于 UE 5.8 的静态 MMD 角色查看器，研究米哈游角色渲染风格。MMD 读取由 VRM4U 完成；项目开发集中于卡通明暗、色彩、脸部受光、描边和角色展示。

An open-source UE 5.8 MMD character viewer and Honkai-inspired toon-rendering study. This is an independent project and is not affiliated with miHoYo/HoYoverse or Epic Games.

## 使用

默认只显示角色与 Blender 中性灰背景。将包含 PMX 和贴图的 ZIP 拖入窗口，在画面内选择 PMX。按 H 或 Esc 显示左上角可拖动、可收起的浮动导航，分为“模型、捕捉、映射、画面、场景、应用”六组；子页在同一视口内浮动，不创建独立窗口。H 隐藏或恢复导航与子页；Esc 关闭当前子页，再按 Esc 隐藏或恢复导航。普通子页的空白区域仍可操控视角，选择 PMX 与加载状态页会阻止误操作。文件导入失败会给出可关闭的提示，已加载模型会保留。

- 中键拖动：绕模型旋转，水平方向跟随拖动；Shift + 中键：平移；Ctrl + 中键或滚轮：缩放。
- 映射 → 选项：可调整当前角色的默认手臂展开角（0° 下垂，90° 平举），未跟踪手臂时使用并自动保存。
- WASD：水平移动角色；E：上移角色；Q：下移角色。按下和松开时平滑加减速，失去焦点时停止移动。
- 小键盘 1 / 3 / 7：正面 / 右侧 / 顶部；Ctrl 配合为反向视图。
- 小键盘 5：正交 / 透视；小键盘小数点或 Home：聚焦。

ZIP 中应保留 PMX 与贴图的相对目录结构。首版支持 Windows 上的普通 ZIP，解包使用系统自带 PowerShell/.NET；导入副本位于用户数据目录 `%LOCALAPPDATA%/HonkaiRendering/Imports`，源文件不被修改。

## 构建

Windows Shipping 一键打包和依赖准备见 [构建文档](doc/BUILD.md)。

需要已安装的 UE 5.8、兼容的 Visual Studio C++ 工具链和 Windows SDK。基础脚本检查仅使用 Python 标准库；Python 不是应用运行时依赖。

```powershell
git clone --recurse-submodules https://github.com/furinasdog/HonkaiRendering.git
cd HonkaiRendering
```

生成 `HonkaiRendering.uproject` 的工程文件并编译 `HonkaiRenderingEditor`，打开 `/Game/MmdViewer/L_Viewer`。VRM4U 以 submodule 固定到本项目 fork 的已提交版本；更新源码后使用 `git submodule update --init --recursive`。

`Content/MmdViewer` 保留应用关卡和背景。共享材质、HLSL 和运行时渲染由 [HonkaiToonRenderer](https://github.com/furinasdog/HonkaiToonRenderer) 子模块维护；其接口直接接受 UE 网格、相机和贴图，不依赖 PMX 或动捕。修改插件着色器后运行 `Plugins/HonkaiToonRenderer/Scripts/build_materials.py`；应用场景初始化仍使用 `Scripts/setup_viewer.py`。无界面模式加 `-AllowCommandletRendering`。

## 渲染范围

渲染对所有模型使用同一套灯光与参数，不按角色名称或模型高度调色。身体材质使用 UE Default Lit 取得方向光和投影，再在时间上采样前仅对角色光照做两段量化，保留原始贴图变化。通过 Custom Stencil 和深度限定角色范围；细窄亮边来自深度轮廓和光向。固定曝光，关闭电影色调曲线、局部曝光对比调整及屏幕空间 AO，避免眼窝和嘴角产生写实灰暗。背景球不参与投影。

共享材质以非金属、无物理镜面反射作为光照估算前提。PMX 作者配置的球面贴图按视空间法线采样，保留乘法/加法模式，为相应材质补充随视角变化的反射；不靠名字或颜色猜测材质类型。半透明 PMX 覆盖层保留原色和透明度；它们没有完整 GBuffer，不参与此光照分段。描边沿用模型作者提供的开关、颜色和宽度，统一缩放。

PMX 中明确禁用 Toon 的材质保留不分段的原色和球面效果，避免把眼睛和饰件一律压入共用阴影。禁用开关与缺失贴图是两个不同状态；仅后者使用通用明暗色回退。

PMX 自定义 Toon 贴图按材质保留，皮肤与头发／服装可以使用作者各自的阴影色。加载时将渐变中心列打包到小型 sRGB 图集，明暗两端颜色通过抗锯齿后的分段光照混合；面部使用同一源渐变并统一减少阴影中的蓝色分量。共用内置 Toon 编号、缺失文件或不能读取的渐变使用通用回退。最多容纳 255 种不同渐变，超出部分回退，不修改模型。此查看器关闭 AO、静态光照及间接光，将 Material AO 的 GBuffer 字节用于渐变索引；如扩展其他光照管线，须重新分配这一通道。

fork 中的 PMX 适配禁止 LOD0 自动减面，保留原始 Diffuse、Opacity、双面标记和材质描边属性，使隐藏表情、服装覆盖层和半透明材质维持其原始状态。Assimp 源码另以嵌套 submodule 关联，按需初始化，重建步骤见插件的 `HONKAI_RENDERING.md`。球面贴图目前支持 PMX 的乘法/加法模式，不支持使用额外 UV 的模式 3；找不到文件时保留基础材质。

面部可使用独立的线性空间阴影阈值贴图，绕开几何法线与投影，保留平滑、清楚的卡通色块。光向转换到模型局部坐标，左右各有一个通道，支持不对称 UV。面部材质角色由外部数据明确指定，运行时不猜名字或硬编码角色槽位。

面部亮面使用统一的暖色亮部曲线，暗色五官与阴影独立保留；水平光照角以统一的幂曲线放大侧光响应，让近正面光下也能读出侧脸暖色阴影。正面与背面的端点保持不变。身体的浅色提亮从更高的亮度起作用，避免把灰蓝色一起推成白色；共用颜色响应同时区分冷暖色、温暖中间色与高饱和颜色。所有模型使用相同曲线，不按角色名、材质名或槽位调色。浅色描边以基础色设定明度下限，减少纯黑线条。

通用 MMD 模型没有统一的 LightMap/FaceMap 标准。没有面部数据时使用共用身体材质回退。自动生成的数据是可编辑的风格化起点，不是恢复出的原游戏贴图；完整原游戏材质、动画、表情编辑与物理模拟不在首版范围内。

## 抗锯齿

抗锯齿使用 TSR，默认以 150% 内部分辨率渲染再输出，历史缓存为 200%。细发丝和描边在时间积累前参与处理。该质量设置增加 GPU 开销；需要降低负载时，可将 `Config/DefaultEngine.ini` 中的 `r.ScreenPercentage` 改为 100，仍保留 TSR。截图对比工具不额外模糊图像。

`r.EarlyZPassOnlyMaterialMasking=0` 保证颜色绘制阶段也检查透明度。PMX 的隐藏覆盖层与主体可能重合；只在深度预处理阶段裁剪透明度，会让部分零透明度描边在颜色阶段显示为黑块。

## 生成增强效果

按 Esc → 模型 → 生成增强效果。后台先核对缓存，然后查找兼容的 Blender 5.0 与 MMD Tools 4.5.3；找不到时使用 `Scripts/enhancement-tools.json` 固定的官方便携包，检查 SHA256 后准备独立副本。不会启用或修改用户 Blender 的插件与偏好。当前算法在 Blender 的独立进程中运行，使用自带 NumPy/bpy，不要求另装 Python 或 OpenCV。

浅色菜单保留模型、渲染、背景、设置及视角入口。生成中显示阶段和进度，可以取消、重试、强制重新生成或关闭菜单继续查看当前角色。只有结果通过源哈希、材质绑定和 PNG 检查后才更新显示。无法可靠确定面部时，通过右侧材质列表高亮预览并明确选择，不按角色名称指定槽位。

渲染页的“原效果／增强效果”只切换材质参数和描边材质，保持摄像机与灯光。“原效果”是生成增强数据之前的当前渲染，不表示原 MMD 软件的输出。已有旧版面部数据会作为原效果保留。

导入包副本、完整资源、工具、任务和缓存统一存于 `%LOCALAPPDATA%/HonkaiRendering`。缓存关联资源文件集合与内容、材质选择、生成参数和脚本版本。强制重新生成保留旧结果，通过原子索引切换到新版本；取消或失败不替换有效配置。模型效果选择与分辨率会保存，恢复时重新验证依赖。设置页可清理生成缓存，保留 Imports 和源资源。

模型页可选择目录导出增强数据，每次使用独立子目录，避免覆盖已有文件。将导出的 `<完整PMX文件名>.toon.json` 和 PNG 放回对应 PMX 同目录后打包，另一台电脑无需 Blender 即可加载。源模型哈希与材质信息必须匹配。

面部数据为线性 RGBA：R/G 为两侧光向阈值，B 为局部鼻描边控制，A 为独立的小块鼻影遮罩。鼻影不是物理 AO。明暗采用硬边两段色块，仅边界使用像素导数抗锯齿；没有宽范围的明暗渐变。旧 RGB 数据的 A 默认为 1，不产生新增鼻影。当前仅支持标准 PMX 朝向与静态头部；非常规拓扑需要人工制作数据。自动数据不是原游戏的美术 FaceMap/LightMap。

离线命令仍可使用：

```powershell
python Scripts/generate_face_map.py '模型完整路径.pmx' --material '面部材质准确名称' --blender 'Blender目录/blender.exe' --mmd-tools '包含mmd_tools的目录'
```

底层协议见 [ENHANCEMENT_WORKER.md](Scripts/ENHANCEMENT_WORKER.md)。

辉光保留标准 Bloom 与明亮笔触的 HDR 信号，面部不额外注入 HDR。屏幕空间接触阴影不能覆盖屏幕外的遮挡物。头发高光形状、暗部分布和金属／衣料差异仍受源模型和缺失的美术数据限制。

## 验证

```powershell
python Scripts/test_archive.py
python -m py_compile Scripts/setup_viewer.py Scripts/test_archive.py Scripts/compare_renders.py Scripts/bake_face_coordinates.py Scripts/generate_face_map.py Scripts/generate_enhancement.py Scripts/test_enhancement.py Scripts/test_enhancement_service.py
```

基础 CI 执行语法、ZIP 与缓存边界检查，不包含 UE 构建或 GPU 画面验证。插件中包含 UE 自动化测试 `VRM4U.PMX.MaterialColorAndOpacity`。本地画面检查可使用以下参数自动加载和截图：

```text
-MmdArchive="完整 ZIP 路径" -MmdModel="包内 PMX 相对路径" -MmdCapture="截图完整路径.png" -MmdTestExit
```

仅在开发构建中，可同时添加 `-MmdCapturePage=Mapping`、`Capture` 或 `Nav`：模型加载后第 6 秒显示映射页、捕捉页或浮动导航，第 8 秒按上述参数截图；此参数不会启动摄像头。

可追加 `-MmdLightYaw=65` 改变光向、`-MmdViewYaw=135` 改变拍摄方向；两者作用于整个场景，不使用模型预设。PowerShell 中使用带引号的 `'-MmdFrameScale=0.22' '-MmdTargetHeight=1.72'` 生成近景，避免小数参数被拆开；默认值均为 1，分别相对聚焦距离和模型中心高度。

首次着色器编译结束前的画面不适合作为效果判断。更多检查与提交规范见 [CONTRIBUTING.md](CONTRIBUTING.md)。

增加 `-MmdBaseColor` 可临时关闭卡通明暗与描边，用基础贴图检查导入；这仍经过 UE 色调映射。已记录的本地验收及限制见 [VALIDATION.md](VALIDATION.md)。

## 对比图

`Scripts/compare_renders.py` 只裁切和等比排版原始截图，不重绘、不改色。它是离线验证工具，需要已有的 Matplotlib 和 Pillow；不属于查看器运行依赖。输入 JSON 的路径相对清单文件，`crop` 为原图像素坐标，可省略。不同姿态、镜头和环境的对比不能作为像素一致性评分。

```json
{"title":"Reference comparison","columns":["Reference","UE"],
 "note":"Different poses and cameras; no retouching.",
 "rows":[{"name":"Model A","images":[{"path":"reference.png"},{"path":"render.png"}]}]}
```

```powershell
python Scripts/compare_renders.py local-manifest.json Saved/Comparisons/comparison.png --font C:/Windows/Fonts/msyh.ttc
```

截图与清单应存于本地 `Saved` 目录；不把未授权素材推送到仓库。

## 许可与第三方

本项目原创代码与原创材质采用 [MIT](LICENSE)。原有 Epic 模板代码保留其版权声明并受相应 Unreal Engine 许可约束；本仓库不重新许可第三方代码。VRM4U、Assimp 等保持各自许可证，详情见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) 和 submodule 内的声明。

模型、贴图、游戏截图及立绘不随仓库或构建分发，请自行准备有权使用的素材。

渲染研究参考：

- [崩铁角色渲染学习笔记](https://zhuanlan.zhihu.com/p/711037262)
- [崩坏3卡通渲染实现方式拆解](https://zhuanlan.zhihu.com/p/120908528)
- [原神三渲二学习笔记](https://zhuanlan.zhihu.com/p/699682089)


## 动捕与 PMX 物理准备

H 显示浮动界面后，可从主面板的“识别结果”或“捕捉 → 打开识别结果”查看摄像头画面、上半身／双手关键点、头部角度及表情数值。预览与动捕共用摄像头，关闭或隐藏面板不停止动捕；停止或过期时清除旧画面。预览属于界面，不包含在角色 Spout 输出中。默认启动仍只显示角色。

没有动捕输入时，角色使用通用的放松手臂姿态（上臂离身体约20°）；追踪接管后按输入运动，失追时平滑返回默认姿态，不修改源模型。

两个项目共用独立的 [HonkaiMotionCapture](https://github.com/furinasdog/HonkaiMotionCapture) 插件。初始化 submodule 后，用插件的 `Scripts/Stage-LocalRuntime.ps1` 按哈希准备本地 SDK DLL 和三种模型。默认在 UE 进程内后台识别，不运行单独 EXE，也不通过 JSON 文件交换快照。它目前使用动态 SDK；静态 SDK 构建未实现。旧 EXE 只作为准备失败时明确报告的回退。SDK 预编译包包含 usage telemetry，进程内模式不具备旧 AppContainer 的网络隔离，详见插件说明。

运行 `Scripts/Prepare-PmxPhysics.ps1` 后再编译 UE，以启用原 PMX 刚体、关节和碰撞组的 Bullet 物理；参见 [PHYSICS.md](PHYSICS.md)。没有这些物理数据的模型不会自动得到布料或头发求解。用户模型、参考图、视频、SDK 二进制、模型权重和本地验收证据均不进入源码仓库。
