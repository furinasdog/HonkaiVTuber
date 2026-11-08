# HonkaiVTuber

基于 Unreal Engine 5.8 的 Windows MMD 动捕与角色展示应用。使用摄像头驱动面部、头部、上半身和双手，配合卡通渲染、动作映射和 Spout 输出。

An open-source Windows MMD VTuber application with toon rendering for UE 5.8. This independent project is not affiliated with HoYoverse or Epic Games.

## 项目与能力

| 仓库 | 主要用途 |
| --- | --- |
| [HonkaiRendering](https://github.com/furinasdog/HonkaiRendering) | 以静态角色展示和渲染调节为主 |
| [HonkaiVTuber](https://github.com/furinasdog/HonkaiVTuber) | 以摄像头动捕、表情映射和直播输出为主 |
| [HonkaiToonRenderer](https://github.com/furinasdog/HonkaiToonRenderer) | 可供其他 UE 项目使用的通用卡通渲染插件 |
| [MotionCapture](https://github.com/furinasdog/MotionCapture) | 独立维护的 UE 摄像头识别插件 |
| [VRM4U 适配分支](https://github.com/furinasdog/VRM4U/tree/honkai-rendering/ue5.8) | PMX 导入、材质属性与 Bullet 物理桥接；保留上游归属 |

两个应用目前共享渲染、导入与动捕功能，均默认使用窗口模式、灰色背景并隐藏菜单，不自动开启摄像头。PMX 解析使用 VRM4U/Assimp，物理使用 PMX 自带的刚体、关节与碰撞组，不为特定角色编写修补逻辑。

- 拖入 ZIP，在视口内选择 PMX 并阅读模型使用守则。
- 共享多色阶 Toon Ramp、双层彩色高光及带距离补偿的视空间描边；增强面部与身体采用一致的配色路径。
- Blender 风格的基本视角操作，可调整背景、抗锯齿与默认手臂角度。
- 面部、头部、上半身及双手识别，摄像头关键点预览、手工映射与逐步标定向导。
- 可选的面部增强数据生成、部件显隐及 Spout 输出。
- 默认 TSR；安装官方 NVIDIA 插件并满足运行条件时可选择 DLAA 或 DLSS 质量模式。

这是持续开发的渲染研究与工具项目，不保证与原游戏画面完全一致。模型骨骼、表情、贴图和物理数据会影响结果；极端动作、接触与部分模型仍可能出现穿插或抖动。完整能力边界见[使用说明](doc/USAGE.md)。

## 开始使用

[下载 HonkaiVTuber v0.1.0](https://github.com/furinasdog/HonkaiVTuber/releases/tag/v0.1.0)。下载 Release 附件中的 **Windows x64 ZIP**，完整解压后运行；GitHub 自动生成的 **Source code** 是源码，不能直接启动。

运行发行包不需要安装 UE 编辑器、Python 或 Node.js。首次版本已在 Windows x64 验证；其他平台不受支持，显卡和驱动需支持 DirectX 12。

取得完整的 Windows 发行目录后，双击 `HonkaiRendering.exe`，保留其旁边所有依赖目录。仓库源码不能直接作为应用启动；自行编译见[构建文档](doc/BUILD.md)。

1. 首次运行阅读并确认项目许可、第三方许可和模型使用说明。
2. 将自己的模型 ZIP 拖入窗口，选择 PMX，按提示阅读压缩包中的模型守则后加载。
3. 按 `H` 显示菜单。中键拖动旋转、`Shift + 中键` 平移、滚轮缩放；`WASD` 移动角色，`E` 上移，`Q` 下移。
4. 如需动捕，导入角色后点击“开启动捕”。详细配置见[使用说明](doc/USAGE.md)。

右上角分别显示渲染与识别 FPS。隐藏菜单不会停止动捕；使用“停止”释放摄像头。

从源码开始：

```powershell
git clone --recurse-submodules https://github.com/furinasdog/HonkaiVTuber.git
cd HonkaiVTuber
```

按[构建文档](doc/BUILD.md)准备本地依赖，在 **PowerShell 7.2+** 中运行 `pwsh -NoProfile -File ./Scripts/Build-Release.ps1 -CheckOnly` 检查；依赖缺失时不会自动安装。

## 文档与参与

- [使用说明与常见问题](doc/USAGE.md)
- [Windows 构建](doc/BUILD.md)
- [架构、依赖与开发验证](doc/DEVELOPMENT.md)
- [贡献指南](CONTRIBUTING.md)
- [安全问题报告](SECURITY.md)与[社区行为准则](CODE_OF_CONDUCT.md)

反馈问题时提供“应用 → 关于”中的版本、复现步骤和经过脱敏的日志。不要公开上传无权再分发的模型或录屏。

## 许可与素材

本项目原创代码采用 [MIT](LICENSE)。Unreal Engine、VRM4U、Assimp、NVIDIA SDK 等第三方内容遵循各自许可，详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) 和依赖仓库的原始声明。MIT 不授予角色素材的使用权。

**仓库与发行包不携带角色模型、贴图、增强贴图、立绘、参考游戏截图或用户录像。** 请自行准备有权使用的素材，并遵守模型作者对展示、改造、直播、商用和分发的要求。通用 MediaPipe 识别权重可按其原始许可随运行时提供。
