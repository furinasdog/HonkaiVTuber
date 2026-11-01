> 当前默认使用进程内 [HonkaiMotionCapture](Plugins/HonkaiMotionCapture/README.md) 插件。以下章节保留原独立 EXE/AppContainer 回退后端的协议及历史证据；AppContainer 隔离和早期帧率结论不适用于新插件。

# 原生面部追踪预览

当前范围是摄像头、单人面部、头部姿态、MediaPipe 33 点身体姿态和最多双手各 21 点手部姿态。UE 端先提供原始关键点快照；具体骨骼绑定仍由模型通用映射配置决定，不按角色名称特化。

## 准备与运行

```powershell
.\Scripts\Prepare-NativeTracking.ps1
.\Tools\NativeTracking\bin\HonkaiNativeTracking.exe --list-cameras
.\Tools\NativeTracking\bin\HonkaiNativeTracking.exe --camera 0 --width 640 --height 480 --fps 30 --confidence 0.5 --max-seconds 20 --output "$PWD\Saved\Tracking\manual-test\frame.json" --stop-file "$PWD\Saved\Tracking\manual-test\stop"
```

准备脚本复用本机 UE OpenCV 4.5.5 C++ SDK、MSVC x64 工具链和 Windows SDK，不安装 Python 或系统级依赖。其他安装位置使用 `-UEOpenCvRoot`，或从 x64 VS Developer Command Prompt 运行 `Tools/NativeTracking/Build.cmd`。CMake 的 `UE_OPENCV_ROOT` 也可覆盖 SDK 位置。

`Prepare-NativeTracking.ps1` 校验官方 MediaPipe DLL、wheel、FaceLandmarker、HandLandmarker 和 PoseLandmarker Lite 模型的 SHA256。已有正确文件会复用，否则只下载固定版本文件。头文件固定为官方 v0.10.35，模型使用官方 `float16/1`，不使用可变的 `latest` URL。具体哈希、来源和 Apache 2.0 许可保存在 `Tools/NativeTracking/ThirdParty/MediaPipe`。

UE 每次启动传入自己的 `--parent-pid` 和独立 job 目录。`--output` 与 `--stop-file` 必须位于同一 job 目录。启动前删除上次的 stop 文件，停止时创建该文件。没有父进程时必须设置 `--max-seconds`，防止遗留无限运行的相机进程。

## 进程与数据边界

外层原生 C++ broker 使用 UE 已安装的 OpenCV `CAP_DSHOW` 采集；它不加载 MediaPipe。原始 RGB 只通过匿名共享内存、互斥量和事件交给内层 worker，不保存照片或视频。内层 worker 调用官方原生 MediaPipe C API，使用 CPU/XNNPACK 推理。

worker 必须在 Windows AppContainer 中运行。它只获指定 SDK/模型文件的读权限、该次 job 目录的读写权限及 webcam capability，没有互联网或局域网 capability。启动时再次检查进程令牌，隔离失败直接报告错误，不回退到联网进程。外层 Job 使用 `KILL_ON_JOB_CLOSE`，外层退出会结束其 worker。正常停止后清理该次临时 AppContainer profile 和专属访问项。

官方预编译 SDK 会尝试发送使用和性能统计，且没有关闭 API；官方声明不会上传输入图像/视频。本实现用操作系统隔离阻断 worker 网络，测试日志中可见 Clearcut 连接失败。此限制来自 [MediaPipe 官方说明](https://github.com/google-ai-edge/mediapipe/issues/6291)和[API 条款](https://developers.google.com/edge/mediapipe/legal/tos)，隔离机制依据 [Microsoft AppContainer 文档](https://learn.microsoft.com/en-us/windows/win32/secauthz/implementing-an-appcontainer)。未改变系统代理、防火墙策略或相机隐私设置。

本机 Media Foundation 直接激活摄像头在容器中返回 `0x80070005`，桌面诊断还遇到驱动初始化/退出阻塞，因此正式采集选择经过实际读帧验证的 OpenCV DirectShow 后端。SDK关闭可能等待被阻断的遥测连接；收到停止后，外层最多给 worker 约一秒释放时间，再结束该受控 worker。该摄像头的 DirectShow 资源回收同样有阻塞，因此在 worker/profile 完成清理、统计写入后，独立 broker 使用进程级退出；这不会退出 UE。但本机实测中 Windows 仍未在6秒内完成 broker 的退出信号，不能保证当前设备即时释放，稳定停止尚未通过验收。

## 输出协议

每帧用同目录临时文件原子替换 `frame.json`，读取者应立即关闭文件句柄。

```json
{"schema":1,"timestamp":1793494284840,"tracked":true,"head":{"yaw":1.81152,"pitch":4.01414,"roll":0.90214},"blendshapes":{"eyeBlinkLeft":0.26726},"latency_ms":18.4558,"fps":0,"width":640,"height":480,"status":"tracking","error":""}
```

示例省略其余表情通道。实际成功检测返回 MediaPipe 的 52 个命名权重（包含 `_neutral`）；未检测到人脸时 `tracked=false`、表情对象为空，不能将默认值当作真人追踪。`pose_landmarks` 为官方 33 个归一化图像坐标，`pose_world_landmarks` 为同序的米制相机空间坐标；`hands` 最多包含左右各一组，每组 21 个归一化关键点和对应 `world_landmarks` 米制相机空间关键点、`handedness` 与分类分数。姿态数组存在不代表每个点可见，绑定端必须检查 `visibility`/`presence`。`timestamp` 是 Unix 毫秒，视频模式取 broker 采集帧的时间。`latency_ms` 只计图像包装和三类推理，不是摄像头到 UE 显示的端到端延迟。`fps` 是 worker 实际处理频率，不能用请求帧率代替。

`head` 从官方列主序 4×4 面部变换矩阵提取，采用右手系 `R = Rz(roll) Ry(yaw) Rx(pitch)`，单位为度，输入不镜像。它不是已经校准的 UE 骨骼旋转。调用端必须完成中立姿态、模型骨骼坐标、镜像和动作方向转换。`--self-test` 验证矩阵分解和 JSON 转义，真人动作正负方向仍需实机验收。

通用上半身绑定使用官方 Pose 索引，不依赖角色名称：左/右肩为 `11/12`，左/右肘为 `13/14`，左/右腕为 `15/16`，左/右髋为 `23/24`，膝为 `25/26`，踝为 `27/28`。手部索引固定为腕 `0`，拇指 `1–4`，食指 `5–8`，中指 `9–12`，无名指 `13–16`，小指 `17–20`。绑定端优先使用世界坐标计算骨骼方向，以相邻关键点长度归一化晃动幅度，并在缺失或可见性过低时保持上一帧后平滑回中；不要把归一化图像坐标直接当作 UE 世界单位。

状态包括 `starting`、`tracking`、`face_not_found`、`stopped` 和 `error`。错误写入 JSON，不只出现在控制台。worker 诊断保存在该 job 的 `worker.log`；`broker_metrics.json` 只保存帧数、帧率和全帧像素均值/极值，不含原始图像。

## 本机验证记录（2026-11-01）

交互预览打开后，界面中观察到卡巴斯基的 `HonkaiNativeTracking.exe` 摄像头访问确认提示。此前黑帧、低帧率及退出超时是在该提示尚未处理时发生，不能直接归因于摄像头硬件或驱动。自动界面工具无法选中该受保护通知，已请求用户手动确认本次访问；没有关闭卡巴斯基或修改全局防护。允许后的真实采集、动作和停止表现仍需重新测量。

- 原生 C++ 自检通过；`--list-cameras` 枚举到 `Integrated Camera`。
- Google 官方测试 `portrait.jpg` 在无网络 AppContainer 中真实推理成功：52 个权重；yaw 1.81152、pitch 4.01414、roll 0.90214；该次推理 18.46 ms。不是模拟数据。
- 官方 `cat.jpg` 的基础原生推理返回 `face_not_found`，没有伪造人脸。
- 官方 `portrait.jpg` 的 PoseLandmarker 真实输出为 33 个图像关键点和 33 个世界关键点；照片中没有可用的手部实例，因此 `hands=[]` 是正确结果，不代表手部通道缺失。
- MediaPipe 官方 `woman_hands.jpg` 在无网络 AppContainer 中真实输出 2 只手、每只 21 点（总计42点），首轮三任务推理约67.7 ms；分类示例返回 `Left`，说明 handedness 来自模型而不是代码猜测。
- 实际摄像头 640×480 运行20秒：一次得到20帧、约1.003 fps、平均推理2.08 ms；启用设备接受的MJPG与30fps请求后，另一轮17帧、worker约0.996 fps、平均推理2.22 ms。请求和设备读回均为30fps，但实际结果仍约1fps。
- 后一轮全帧像素均值约0.212/255，最小0、最大255，图像整体接近黑色。没有保存原始摄像头图像，不能仅凭统计断言具体是遮挡、低光还是驱动设置。
- 上述真实摄像头测试 `tracked_frames=0`。采集和推理管线已实测；真人表情、转头方向、最终角色驱动效果和端到端延迟尚未验收。
- 主动 stop 文件测试：worker退出、剩余worker为0，状态与统计写入成功；launcher的Windows退出信号超过6秒，未通过稳定停止验收。证据为 `validation/stop-final-f38a52a0ed2f4fe0b55337c0500e3ce2/stop-result.json`。不要据此把整条动捕链路标为完成。

本地证据位于被 Git 忽略的 `Tools/NativeTracking/validation`，其中 `isolated-portrait/frame.json`、`broker-camera/worker.log`、`broker-final/worker.log` 和 `broker-final/broker_metrics.json` 对应上述记录。官方验证照片与下载的 SDK/模型不提交到仓库。

仅验证了本机一个摄像头。多摄像头枚举顺序、不同硬件的曝光/帧率设置及重开稳定性需要后续验证。生产采集使用 DirectShow，设备名称枚举沿用 Media Foundation；多设备环境不应假设两个后端的顺序必然一致。
