#include "NativeTracking.h"
#include "HonkaiMotionCapture.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/ConfigCacheIni.h"
#include "Serialization/JsonSerializer.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace
{
    FQuat FaceRotationToCanonical(const FQuat& Face)
    {
        // The SDK face frame is right-handed and Y-up; UE vectors are (z,-x,y).
        return FQuat(-Face.Z, Face.X, -Face.Y, Face.W).GetNormalized();
    }

    void MirrorTrackingFrame(FVtuberTrackingSnapshot& Frame)
    {
        auto Reflect = [](TArray<FVtuberLandmark>& Points, bool bNormalized) {
            for (auto& Point : Points) Point.Position.X = bNormalized ? 1.0 - Point.Position.X : -Point.Position.X;
        };
        auto MirrorPose = [&Reflect](TArray<FVtuberLandmark>& Points, bool bNormalized) {
            Reflect(Points, bNormalized);
            static const int32 Pairs[][2] = {{1,4},{2,5},{3,6},{7,8},{9,10},{11,12},{13,14},{15,16},{17,18},{19,20},{21,22},{23,24},{25,26},{27,28},{29,30},{31,32}};
            for (const auto& Pair : Pairs)
                if (Points.IsValidIndex(Pair[1])) Swap(Points[Pair[0]], Points[Pair[1]]);
        };
        // Reflect geometry and swap anatomical sides together, preserving the torso's facing direction.
        MirrorPose(Frame.PoseLandmarks, true);
        MirrorPose(Frame.PoseWorldLandmarks, false);
        for (auto& Hand : Frame.Hands)
        {
            if (Hand.Handedness == TEXT("Left")) Hand.Handedness = TEXT("Right");
            else if (Hand.Handedness == TEXT("Right")) Hand.Handedness = TEXT("Left");
            Reflect(Hand.Landmarks, true);
            Reflect(Hand.WorldLandmarks, false);
        }
        TMap<FName, float> MirroredShapes;
        for (const auto& Shape : Frame.BlendShapes)
        {
            FString Name = Shape.Key.ToString();
            if (Name.EndsWith(TEXT("Left"))) Name = Name.LeftChop(4) + TEXT("Right");
            else if (Name.EndsWith(TEXT("Right"))) Name = Name.LeftChop(5) + TEXT("Left");
            MirroredShapes.Add(FName(*Name), Shape.Value);
        }
        Frame.BlendShapes = MoveTemp(MirroredShapes);
        const FQuat Q = Frame.HeadRotation;
        Frame.HeadRotation = FQuat(-Q.X, Q.Y, -Q.Z, Q.W);
    }

    void ConvertCaptureFrame(const FHonkaiCaptureFrame& Source, bool bMirror, FVtuberTrackingSnapshot& Out)
    {
        Out = FVtuberTrackingSnapshot();
        Out.TimestampSeconds = Source.TimestampSeconds;
        if (Source.State != EHonkaiCaptureState::Running) return;
        const FQuat Face(Source.FaceRotationRH);
        Out.bFaceTracked = Source.bFaceTracked && !Face.ContainsNaN() && Face.SizeSquared() > 1.e-6;
        if (Out.bFaceTracked) Out.HeadRotation = FaceRotationToCanonical(Face.GetNormalized());
        if (Source.BlendShapes.Num() <= 100)
            for (const auto& Shape : Source.BlendShapes)
                if (Shape.Key.ToString().Len() <= 64 && FMath::IsFinite(Shape.Value))
                    Out.BlendShapes.Add(Shape.Key, FMath::Clamp(Shape.Value, 0.f, 1.f));
        auto CopyPoints = [](const TArray<FHonkaiCaptureLandmark>& Points, int32 Count, TArray<FVtuberLandmark>& Target)
        {
            if (Points.Num() != Count) return false;
            TArray<FVtuberLandmark> Parsed;
            Parsed.Reserve(Count);
            for (const auto& Point : Points)
            {
                if (Point.Position.ContainsNaN() || !FMath::IsFinite(Point.Visibility) || !FMath::IsFinite(Point.Presence)) return false;
                FVtuberLandmark& P = Parsed.AddDefaulted_GetRef();
                P.Position = FVector(FMath::Clamp(double(Point.Position.X), -100000., 100000.),
                    FMath::Clamp(double(Point.Position.Y), -100000., 100000.), FMath::Clamp(double(Point.Position.Z), -100000., 100000.));
                P.Visibility = FMath::Clamp(Point.Visibility, 0.f, 1.f); P.Presence = FMath::Clamp(Point.Presence, 0.f, 1.f);
            }
            Target = MoveTemp(Parsed); return true;
        };
        Out.bPoseTracked = Source.bPoseTracked && CopyPoints(Source.PoseLandmarks, 33, Out.PoseLandmarks);
        CopyPoints(Source.PoseWorldLandmarks, 33, Out.PoseWorldLandmarks);
        if (Source.Hands.Num() <= 2)
            for (const auto& SourceHand : Source.Hands)
            {
                if (!FMath::IsFinite(SourceHand.Score)) continue;
                FVtuberHandTracking Hand; Hand.Handedness = SourceHand.Handedness; Hand.Score = FMath::Clamp(SourceHand.Score, 0.f, 1.f);
                if (!CopyPoints(SourceHand.Landmarks, 21, Hand.Landmarks)) continue;
                CopyPoints(SourceHand.WorldLandmarks, 21, Hand.WorldLandmarks);
                Out.Hands.Add(MoveTemp(Hand));
            }
        if (bMirror) MirrorTrackingFrame(Out);
    }

    FString SettingsFile() { return FPaths::ProjectSavedDir() / TEXT("Config/Windows/Vtuber.ini"); }
    bool ReadSnapshot(const FString& Path, FString& Text)
    {
#if PLATFORM_WINDOWS
        HANDLE File = CreateFileW(*Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (File == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER Size{};
        bool Valid = GetFileSizeEx(File, &Size) && Size.QuadPart > 0 && Size.QuadPart <= 65536;
        TArray<uint8> Buffer;
        if (Valid)
        {
            Buffer.SetNumUninitialized(int32(Size.QuadPart)); DWORD Read = 0;
            Valid = ReadFile(File, Buffer.GetData(), Buffer.Num(), &Read, nullptr) && Read == DWORD(Buffer.Num());
            if (Valid) FFileHelper::BufferToString(Text, Buffer.GetData(), Buffer.Num());
        }
        CloseHandle(File); return Valid;
#else
        return false;
#endif
    }
}

FNativeTracking::FNativeTracking() = default;
FNativeTracking::~FNativeTracking()
{
    Stop();
    // The capture module retains its worker until cooperative SDK/camera teardown finishes.
    if (InProcessCapture) return;
    const double Deadline = FPlatformTime::Seconds() + 2.;
    while (IsRunning() && FPlatformTime::Seconds() < Deadline) FPlatformProcess::Sleep(.02f);
    if (IsRunning()) FPlatformProcess::TerminateProc(Process, true);
    if (Process.IsValid()) FPlatformProcess::CloseProc(Process);
}
bool FNativeTracking::IsRunning() { return InProcessCapture ? InProcessCapture->IsRunning() : Process.IsValid() && FPlatformProcess::IsProcRunning(Process); }
bool FNativeTracking::IsStopping() const { return InProcessCapture ? InProcessCapture->IsStopping() : StopRequestedAt > 0; }
bool FNativeTracking::Start()
{
    if (IsStopping()) { Status = TEXT("上一次动捕仍在释放摄像头，请稍候"); return false; }
    if (IsRunning()) return true;
    if (Process.IsValid()) { FPlatformProcess::CloseProc(Process); Process.Reset(); }
    StopRequestedAt = 0;
    bStopKillRequested = false;
    LastTimestamp = LastPoll = 0; InferenceMs = InferenceFps = 0;
    OutputPath.Empty(); StopPath.Empty();
    if (!InProcessCapture) InProcessCapture = MakeUnique<FHonkaiMotionCapture>();
    FHonkaiCaptureSettings Settings;
    Settings.CameraIndex = FMath::Clamp(CameraIndex, 0, 32);
    Settings.Width = FMath::Clamp(Width, 320, 1920); Settings.Height = FMath::Clamp(Height, 240, 1080);
    Settings.FramesPerSecond = FMath::Clamp(FramesPerSecond, 5, 60);
    Settings.Confidence = FMath::IsFinite(Confidence) ? FMath::Clamp(Confidence, .1f, .99f) : .5f;
    FString IntegratedError;
    if (InProcessCapture->Start(Settings, IntegratedError))
    {
        Status = TEXT("进程内动捕 · 正在初始化…");
        UE_LOG(LogTemp, Display, TEXT("VTUBER_CAPTURE backend=in_process startup=queued"));
        SaveSettings(); return true;
    }
    InProcessCapture.Reset();
    UE_LOG(LogTemp, Warning, TEXT("VTUBER_CAPTURE in-process startup unavailable; checking explicit helper fallback: %s"), *IntegratedError.Left(250));
    const FString Exe = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Tools/NativeTracking/bin/HonkaiNativeTracking.exe"));
    const FString Bin = FPaths::GetPath(Exe);
    const TCHAR* Required[] = { TEXT("HonkaiNativeTracking.exe"), TEXT("libmediapipe.dll"), TEXT("Models/face_landmarker.task"),
        TEXT("Models/hand_landmarker.task"), TEXT("Models/pose_landmarker_lite.task") };
    for (const TCHAR* File : Required)
        if (!FPaths::FileExists(Bin / File))
        {
            Status = TEXT("进程内动捕不可用：") + IntegratedError.Left(160) + TEXT("；独立后端依赖缺失，请运行 Scripts/Prepare-NativeTracking.ps1");
            UE_LOG(LogTemp, Error, TEXT("VTUBER_CAPTURE no backend started; helper dependency missing: %s"), File);
            return false;
        }
    const FString JobDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Tracking") / FGuid::NewGuid().ToString(EGuidFormats::Digits));
    IFileManager::Get().MakeDirectory(*JobDir, true);
    OutputPath = JobDir / TEXT("frame.json"); StopPath = JobDir / TEXT("stop");
    const FString Args = FString::Printf(TEXT("--camera %d --width %d --height %d --fps %d --confidence %.3f --output \"%s\" --stop-file \"%s\" --parent-pid %u"),
        Settings.CameraIndex, Settings.Width, Settings.Height, Settings.FramesPerSecond,
        Settings.Confidence, *OutputPath, *StopPath, FPlatformProcess::GetCurrentProcessId());
    Process = FPlatformProcess::CreateProc(*Exe, *Args, false, true, true, nullptr, 0, *FPaths::GetPath(Exe), nullptr);
    LastTimestamp = 0; InferenceMs = InferenceFps = 0;
    Status = Process.IsValid() ? TEXT("独立进程动捕 · 正在打开摄像头…") : TEXT("独立进程动捕启动失败");
    UE_LOG(LogTemp, Display, TEXT("VTUBER_CAPTURE backend=helper startup=%s"), Process.IsValid() ? TEXT("launched") : TEXT("failed"));
    SaveSettings(); return Process.IsValid();
}
void FNativeTracking::Stop()
{
    if (InProcessCapture)
    {
        InProcessCapture->Stop();
        if (InProcessCapture->IsStopping()) Status = TEXT("进程内动捕 · 正在停止并释放摄像头…");
        return;
    }
    if (!Process.IsValid()) return;
    FFileHelper::SaveStringToFile(TEXT("stop"), *StopPath);
    if (StopRequestedAt == 0) StopRequestedAt = FPlatformTime::Seconds();
    Status = TEXT("正在停止摄像头…");
}
bool FNativeTracking::Poll(FVtuberTrackingSnapshot& Out)
{
    const double Now = FPlatformTime::Seconds();
    if (Now - LastPoll < 1.0 / 60) return false;
    LastPoll = Now;
    if (InProcessCapture)
    {
        // A worker may finish before this tick; its final Error/Stopped snapshot still belongs to the consumer.
        FHonkaiCaptureFrame Frame;
        if (!InProcessCapture->Poll(Frame)) return false;
        Status = TEXT("进程内动捕 · ") + (Frame.Error.IsEmpty() ? Frame.Status : TEXT("失败：") + Frame.Error.Left(250));
        if (Frame.State == EHonkaiCaptureState::Error)
            UE_LOG(LogTemp, Error, TEXT("VTUBER_CAPTURE backend=in_process failed: %s"), *Frame.Error.Left(250));
        if (!FMath::IsFinite(Frame.TimestampSeconds) || Frame.TimestampSeconds <= LastTimestamp || Frame.TimestampSeconds > Now + 1.)
        { Status = TEXT("进程内动捕 · 忽略无效的帧时间"); return false; }
        LastTimestamp = Frame.TimestampSeconds;
        InferenceMs = FMath::IsFinite(Frame.InferenceMilliseconds) ? FMath::Clamp(Frame.InferenceMilliseconds, 0.f, 10000.f) : 0;
        InferenceFps = FMath::IsFinite(Frame.ProcessingFPS) ? FMath::Clamp(Frame.ProcessingFPS, 0.f, 240.f) : 0;
        ConvertCaptureFrame(Frame, bMirror, Out);
        return true;
    }
    if (StopRequestedAt > 0)
    {
        // Let the capture driver and isolated worker release resources without stalling UE.
        if (IsRunning() && Now - StopRequestedAt < 3.) return false;
        if (IsRunning() && !bStopKillRequested) { FPlatformProcess::TerminateProc(Process, true); bStopKillRequested = true; }
        if (IsRunning()) { Status = TEXT("追踪已停止，等待摄像头驱动释放；暂不能重新启动"); return false; }
        if (Process.IsValid()) FPlatformProcess::CloseProc(Process);
        Process.Reset(); StopRequestedAt = 0; OutputPath.Empty();
        Status = TEXT("摄像头已停止"); return false;
    }
    FString Text; TSharedPtr<FJsonObject> Object;
    if (!ReadSnapshot(OutputPath, Text) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object) || !Object)
    {
        if (Process.IsValid() && !IsRunning()) Status = TEXT("追踪程序已退出，检查摄像头是否被占用及本地日志");
        return false;
    }
    double Schema = 0, Timestamp = 0;
    if (!Object->TryGetNumberField(TEXT("schema"), Schema) || Schema != 1 || !Object->TryGetNumberField(TEXT("timestamp"), Timestamp)
        || !FMath::IsFinite(Timestamp) || Timestamp <= LastTimestamp) return false;
    LastTimestamp = Timestamp;
    Out = FVtuberTrackingSnapshot();
    Out.TimestampSeconds = Now;
    Object->TryGetBoolField(TEXT("tracked"), Out.bFaceTracked);
    double Ms = 0, Fps = 0;
    Object->TryGetNumberField(TEXT("latency_ms"), Ms); Object->TryGetNumberField(TEXT("fps"), Fps);
    InferenceMs = FMath::IsFinite(Ms) ? FMath::Clamp(float(Ms), 0.f, 10000.f) : 0;
    InferenceFps = FMath::IsFinite(Fps) ? FMath::Clamp(float(Fps), 0.f, 240.f) : 0;
    FString Message; Object->TryGetStringField(TEXT("status"), Message);
    Status = Out.bFaceTracked ? TEXT("面部追踪中") : (Message == TEXT("tracking") || Message.IsEmpty() ? TEXT("未检测到面部，平滑恢复中") : Message);
    if (Message == TEXT("error")) { FString Error; if (Object->TryGetStringField(TEXT("error"), Error)) Status = TEXT("摄像头追踪失败：") + Error.Left(250); }
    else if (Message == TEXT("face_not_found")) Status = TEXT("未检测到人脸，请检查镜头、光线和摄像头隐私开关");
    else if (Message == TEXT("starting")) Status = TEXT("正在打开摄像头…");
    else if (Message == TEXT("stopped")) Status = TEXT("追踪已结束，摄像头正在释放资源");
    const TSharedPtr<FJsonObject>* Head = nullptr;
    if (Out.bFaceTracked && Object->TryGetObjectField(TEXT("head"), Head))
    {
        double Yaw = 0, Pitch = 0, Roll = 0;
        if (!(*Head)->TryGetNumberField(TEXT("yaw"), Yaw) || !(*Head)->TryGetNumberField(TEXT("pitch"), Pitch)
            || !(*Head)->TryGetNumberField(TEXT("roll"), Roll) || !FMath::IsFinite(Yaw) || !FMath::IsFinite(Pitch) || !FMath::IsFinite(Roll)) Out.bFaceTracked = false;
        else
        {
            const FQuat Face = FQuat(FVector(0, 0, 1), FMath::DegreesToRadians(FMath::Clamp(Roll, -60., 60.)))
                * FQuat(FVector(0, 1, 0), FMath::DegreesToRadians(FMath::Clamp(Yaw, -80., 80.)))
                * FQuat(FVector(1, 0, 0), FMath::DegreesToRadians(FMath::Clamp(Pitch, -80., 80.)));
            Out.HeadRotation = FaceRotationToCanonical(Face);
        }
    }
    const TSharedPtr<FJsonObject>* Shapes = nullptr;
    if (Object->TryGetObjectField(TEXT("blendshapes"), Shapes) && (*Shapes)->Values.Num() <= 100)
        for (const auto& Entry : (*Shapes)->Values)
        {
            double Weight;
            if (Entry.Key.Len() <= 64 && Entry.Value->TryGetNumber(Weight) && FMath::IsFinite(Weight))
                Out.BlendShapes.Add(FName(*Entry.Key), FMath::Clamp(float(Weight), 0.f, 1.f));
        }
    const TArray<TSharedPtr<FJsonValue>>* Pose = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* WorldPose = nullptr;
    auto ReadLandmarks = [](const TArray<TSharedPtr<FJsonValue>>& Values, TArray<FVtuberLandmark>& OutLandmarks) {
        if (Values.Num() > 64) return false;
        TArray<FVtuberLandmark> Parsed;
        for (const auto& Value : Values)
        {
            const TSharedPtr<FJsonObject>* Point = nullptr;
            if (!Value->TryGetObject(Point)) return false;
            double X = 0, Y = 0, Z = 0, Visibility = 0, Presence = 0;
            if (!(*Point)->TryGetNumberField(TEXT("x"), X) || !(*Point)->TryGetNumberField(TEXT("y"), Y)
                || !(*Point)->TryGetNumberField(TEXT("z"), Z) || !(*Point)->TryGetNumberField(TEXT("visibility"), Visibility)
                || !(*Point)->TryGetNumberField(TEXT("presence"), Presence)
                || !FMath::IsFinite(X) || !FMath::IsFinite(Y) || !FMath::IsFinite(Z)
                || !FMath::IsFinite(Visibility) || !FMath::IsFinite(Presence)) return false;
            FVtuberLandmark Landmark;
            Landmark.Position = FVector(FMath::Clamp(X, -100000., 100000.), FMath::Clamp(Y, -100000., 100000.), FMath::Clamp(Z, -100000., 100000.));
            Landmark.Visibility = FMath::Clamp(float(Visibility), 0.f, 1.f); Landmark.Presence = FMath::Clamp(float(Presence), 0.f, 1.f);
            Parsed.Add(Landmark);
        }
        OutLandmarks = MoveTemp(Parsed);
        return true;
    };
    if (Object->TryGetArrayField(TEXT("pose_landmarks"), Pose) && ReadLandmarks(*Pose, Out.PoseLandmarks)) Out.bPoseTracked = Out.PoseLandmarks.Num() == 33;
    if (Object->TryGetArrayField(TEXT("pose_world_landmarks"), WorldPose)) ReadLandmarks(*WorldPose, Out.PoseWorldLandmarks);
    if (Out.PoseWorldLandmarks.Num() != 33) Out.PoseWorldLandmarks.Reset();
    const TArray<TSharedPtr<FJsonValue>>* Hands = nullptr;
    if (Object->TryGetArrayField(TEXT("hands"), Hands) && Hands->Num() <= 2)
    {
        for (const auto& Value : *Hands)
        {
            const TSharedPtr<FJsonObject>* HandObject = nullptr;
            if (!Value->TryGetObject(HandObject)) continue;
            FString Label; double Score = 0;
            const TArray<TSharedPtr<FJsonValue>>* HandPoints = nullptr;
            if (!(*HandObject)->TryGetStringField(TEXT("handedness"), Label) || !(*HandObject)->TryGetNumberField(TEXT("score"), Score)
                || !(*HandObject)->TryGetArrayField(TEXT("landmarks"), HandPoints) || !FMath::IsFinite(Score)) continue;
            FVtuberHandTracking Hand; Hand.Handedness = FName(*Label); Hand.Score = FMath::Clamp(float(Score), 0.f, 1.f);
            const bool bValidHand = ReadLandmarks(*HandPoints, Hand.Landmarks) && Hand.Landmarks.Num() == 21;
            if (bValidHand) Out.Hands.Add(MoveTemp(Hand));
            const TArray<TSharedPtr<FJsonValue>>* WorldHandPoints = nullptr;
            if (bValidHand && (*HandObject)->TryGetArrayField(TEXT("world_landmarks"), WorldHandPoints))
            {
                ReadLandmarks(*WorldHandPoints, Out.Hands.Last().WorldLandmarks);
                if (Out.Hands.Last().WorldLandmarks.Num() != 21) Out.Hands.Last().WorldLandmarks.Reset();
            }
        }
    }
    Status = TEXT("独立进程动捕 · ") + Status;
    if (bMirror) MirrorTrackingFrame(Out);
    return true;
}
#if WITH_DEV_AUTOMATION_TESTS
bool FNativeTracking::LoadReplayFrame(const FString& Path, bool bMirrorFrame, FVtuberTrackingSnapshot& Out)
{
    FNativeTracking Replay;
    Replay.OutputPath = Path;
    Replay.bMirror = bMirrorFrame;
    return Replay.Poll(Out);
}
#endif

void FNativeTracking::LoadSettings()
{
    const FString Path = SettingsFile();
    GConfig->GetInt(TEXT("Tracking"), TEXT("Camera"), CameraIndex, Path);
    GConfig->GetInt(TEXT("Tracking"), TEXT("Width"), Width, Path);
    GConfig->GetInt(TEXT("Tracking"), TEXT("Height"), Height, Path);
    GConfig->GetInt(TEXT("Tracking"), TEXT("FPS"), FramesPerSecond, Path);
    GConfig->GetFloat(TEXT("Tracking"), TEXT("Confidence"), Confidence, Path);
    GConfig->GetBool(TEXT("Tracking"), TEXT("Mirror"), bMirror, Path);
}
void FNativeTracking::SaveSettings() const
{
    const FString Path = SettingsFile();
    GConfig->SetInt(TEXT("Tracking"), TEXT("Camera"), CameraIndex, Path);
    GConfig->SetInt(TEXT("Tracking"), TEXT("Width"), Width, Path);
    GConfig->SetInt(TEXT("Tracking"), TEXT("Height"), Height, Path);
    GConfig->SetInt(TEXT("Tracking"), TEXT("FPS"), FramesPerSecond, Path);
    GConfig->SetFloat(TEXT("Tracking"), TEXT("Confidence"), Confidence, Path);
    GConfig->SetBool(TEXT("Tracking"), TEXT("Mirror"), bMirror, Path); GConfig->Flush(false, Path);
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include <limits>
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVtuberInProcessFrameTest, "HonkaiVTuber.Tracking.InProcessFrame",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVtuberInProcessFrameTest::RunTest(const FString& Parameters)
{
    FHonkaiCaptureFrame Source;
    Source.TimestampSeconds = FPlatformTime::Seconds(); Source.State = EHonkaiCaptureState::Running;
    Source.bFaceTracked = Source.bPoseTracked = true;
    const FQuat Raw = FQuat(FVector(0, 0, 1), .2) * FQuat(FVector(0, 1, 0), -.3) * FQuat(FVector(1, 0, 0), .1);
    Source.FaceRotationRH = FQuat4f(Raw);
    Source.BlendShapes.Add(TEXT("eyeBlinkLeft"), .2f); Source.BlendShapes.Add(TEXT("eyeBlinkRight"), .8f);
    Source.PoseLandmarks.SetNum(33); Source.PoseWorldLandmarks.SetNum(33);
    Source.PoseLandmarks[11].Position = FVector3f(.7f, .3f, 0); Source.PoseLandmarks[12].Position = FVector3f(.4f, .3f, 0);
    auto& Hand = Source.Hands.AddDefaulted_GetRef(); Hand.Handedness = TEXT("Left"); Hand.Score = 1;
    Hand.Landmarks.SetNum(21); Hand.WorldLandmarks.SetNum(21);
    Hand.Landmarks[0].Position = FVector3f(.7f, .4f, 0); Hand.WorldLandmarks[0].Position = FVector3f(.2f, -.1f, .3f);
    FVtuberTrackingSnapshot Frame, Mirrored;
    ConvertCaptureFrame(Source, false, Frame); ConvertCaptureFrame(Source, true, Mirrored);
    TestTrue(TEXT("Integrated frame retains capture time in the FPlatformTime clock"), Frame.TimestampSeconds == Source.TimestampSeconds);
    TestTrue(TEXT("Integrated head uses the same canonical basis as the helper"), Frame.bFaceTracked && Frame.HeadRotation.Equals(FaceRotationToCanonical(Raw), 1.e-6));
    TestTrue(TEXT("Face, body and two hand coordinate arrays share a complete snapshot"),
        Frame.bPoseTracked && Frame.PoseLandmarks.Num() == 33 && Frame.PoseWorldLandmarks.Num() == 33
        && Frame.Hands.Num() == 1 && Frame.Hands[0].Landmarks.Num() == 21 && Frame.Hands[0].WorldLandmarks.Num() == 21);
    MirrorTrackingFrame(Frame);
    TestTrue(TEXT("Integrated head and anatomical landmarks mirror exactly once"), Mirrored.HeadRotation.Equals(Frame.HeadRotation, 1.e-6)
        && Mirrored.PoseLandmarks[11].Position.Equals(Frame.PoseLandmarks[11].Position, 1.e-6)
        && Mirrored.Hands[0].Handedness == TEXT("Right") && Mirrored.Hands[0].WorldLandmarks[0].Position.Equals(FVector(-.2, -.1, .3), 1.e-6));
    TestEqual(TEXT("Integrated expression mirrors exactly once"), Mirrored.BlendShapes.FindRef(TEXT("eyeBlinkRight")), .2f);
    TestTrue(TEXT("Conversion leaves the SDK snapshot untouched"), Source.Hands[0].Handedness == TEXT("Left") && Source.PoseLandmarks[11].Position.X == .7f);
    Source.FaceRotationRH = FQuat4f(0, 0, 0, 0);
    Source.PoseLandmarks[4].Position.X = std::numeric_limits<float>::quiet_NaN();
    Source.Hands[0].Score = std::numeric_limits<float>::quiet_NaN();
    ConvertCaptureFrame(Source, false, Frame);
    TestTrue(TEXT("Malformed integrated channels cannot partially drive the rig"), !Frame.bFaceTracked && Frame.HeadRotation.IsIdentity()
        && !Frame.bPoseTracked && Frame.PoseLandmarks.IsEmpty() && Frame.Hands.IsEmpty());
    for (const EHonkaiCaptureState State : {EHonkaiCaptureState::Error, EHonkaiCaptureState::Stopped, EHonkaiCaptureState::Stopping})
    {
        Source.State = State; ConvertCaptureFrame(Source, true, Frame);
        TestTrue(TEXT("Final and stopping states clear previous tracking instead of replaying it"), !Frame.bFaceTracked && !Frame.bPoseTracked
            && Frame.BlendShapes.IsEmpty() && Frame.PoseLandmarks.IsEmpty() && Frame.PoseWorldLandmarks.IsEmpty() && Frame.Hands.IsEmpty());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVtuberHeadBodyBasisTest, "HonkaiVTuber.Tracking.HeadBodyBasis",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVtuberHeadBodyBasisTest::RunTest(const FString& Parameters)
{
    struct FCase { const TCHAR* Name; double Yaw, Pitch, Roll; };
    const FCase Cases[] = {
        {TEXT("Neutral"), 0, 0, 0}, {TEXT("YawPositive"), 20, 0, 0}, {TEXT("YawNegative"), -20, 0, 0},
        {TEXT("PitchPositive"), 0, 15, 0}, {TEXT("PitchNegative"), 0, -15, 0},
        {TEXT("RollPositive"), 0, 0, 10}, {TEXT("RollNegative"), 0, 0, -10},
        {TEXT("Combined"), 23, -14, 8}
    };
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation");
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Path = FPaths::CreateTempFilename(*Directory, TEXT("HeadBodyBasis-"), TEXT(".json"));
    for (const FCase& Case : Cases)
    {
        const double Yaw = FMath::DegreesToRadians(Case.Yaw), Pitch = FMath::DegreesToRadians(Case.Pitch), Roll = FMath::DegreesToRadians(Case.Roll);
        // Independently rotate a rigid torso in the SDK face frame, then encode
        // its landmarks in the pose camera frame (x-right, y-down, z-away).
        auto PosePoint = [=](const FVector& Point)
        {
            const FVector X(Point.X, FMath::Cos(Pitch) * Point.Y - FMath::Sin(Pitch) * Point.Z,
                FMath::Sin(Pitch) * Point.Y + FMath::Cos(Pitch) * Point.Z);
            const FVector Y(FMath::Cos(Yaw) * X.X + FMath::Sin(Yaw) * X.Z, X.Y,
                -FMath::Sin(Yaw) * X.X + FMath::Cos(Yaw) * X.Z);
            const FVector Z(FMath::Cos(Roll) * Y.X - FMath::Sin(Roll) * Y.Y,
                FMath::Sin(Roll) * Y.X + FMath::Cos(Roll) * Y.Y, Y.Z);
            return FVector(Z.X, -Z.Y, -Z.Z);
        };
        TArray<FVector> Points;
        Points.SetNumZeroed(33);
        Points[11] = PosePoint(FVector(.2, .4, 0)); Points[12] = PosePoint(FVector(-.2, .4, 0));
        Points[23] = PosePoint(FVector(.15, -.4, 0)); Points[24] = PosePoint(FVector(-.15, -.4, 0));
        auto Landmark = [](const FVector& Position) -> TSharedPtr<FJsonValue>
        {
            const TSharedRef<FJsonObject> Point = MakeShared<FJsonObject>();
            Point->SetNumberField(TEXT("x"), Position.X); Point->SetNumberField(TEXT("y"), Position.Y); Point->SetNumberField(TEXT("z"), Position.Z);
            Point->SetNumberField(TEXT("visibility"), 1); Point->SetNumberField(TEXT("presence"), 1);
            return MakeShared<FJsonValueObject>(Point);
        };
        TArray<TSharedPtr<FJsonValue>> World, Image;
        for (const FVector& Point : Points)
        {
            World.Add(Landmark(Point));
            Image.Add(Landmark(FVector(.5 + Point.X * .25, .5 + Point.Y * .25, 0)));
        }
        const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>(), Head = MakeShared<FJsonObject>();
        Root->SetNumberField(TEXT("schema"), 1); Root->SetNumberField(TEXT("timestamp"), 1);
        Root->SetBoolField(TEXT("tracked"), true); Root->SetStringField(TEXT("status"), TEXT("tracking"));
        Head->SetNumberField(TEXT("yaw"), Case.Yaw); Head->SetNumberField(TEXT("pitch"), Case.Pitch); Head->SetNumberField(TEXT("roll"), Case.Roll);
        Root->SetObjectField(TEXT("head"), Head);
        Root->SetArrayField(TEXT("pose_landmarks"), Image); Root->SetArrayField(TEXT("pose_world_landmarks"), World);
        FString Json;
        FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Json));
        if (!TestTrue(TEXT("Write native JSON fixture"), FFileHelper::SaveStringToFile(Json, *Path))) break;
        for (const bool bMirror : {false, true})
        {
            FVtuberTrackingSnapshot Snapshot;
            const FString Label = FString::Printf(TEXT("%s mirror=%d"), Case.Name, bMirror);
            if (!TestTrue(Label + TEXT(" parses through native JSON entry"), FNativeTracking::LoadReplayFrame(Path, bMirror, Snapshot))) continue;
            TestTrue(Label + TEXT(" face is tracked"), Snapshot.bFaceTracked);
            TestTrue(Label + TEXT(" pose is tracked"), Snapshot.bPoseTracked);
            if (!TestEqual(Label + TEXT(" metric pose has 33 landmarks"), Snapshot.PoseWorldLandmarks.Num(), 33)) continue;
            auto Canonical = [&Snapshot](int32 Index)
            {
                const FVector P = Snapshot.PoseWorldLandmarks[Index].Position;
                return FVector(-P.Z, -P.X, -P.Y);
            };
            const FVector Up = (Canonical(11) + Canonical(12) - Canonical(23) - Canonical(24)).GetSafeNormal();
            const FVector Forward = FVector::CrossProduct(Up, Canonical(11) - Canonical(12)).GetSafeNormal();
            TestTrue(Label + TEXT(" head and torso face the same direction"), Snapshot.HeadRotation.RotateVector(FVector::ForwardVector).Equals(Forward, .00001));
            TestTrue(Label + TEXT(" head and torso share the same up axis"), Snapshot.HeadRotation.RotateVector(FVector::UpVector).Equals(Up, .00001));
        }
    }
    IFileManager::Get().Delete(*Path);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVtuberFrameMirrorTest, "HonkaiVTuber.Tracking.WholeFrameMirror",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVtuberFrameMirrorTest::RunTest(const FString& Parameters)
{
    FVtuberTrackingSnapshot Frame;
    Frame.PoseLandmarks.SetNum(33); Frame.PoseWorldLandmarks.SetNum(33);
    Frame.PoseLandmarks[11].Position = FVector(.7, .3, 0);
    Frame.PoseLandmarks[12].Position = FVector(.3, .3, 0);
    Frame.PoseWorldLandmarks[11].Position = FVector(.2, -.5, 0);
    Frame.PoseWorldLandmarks[12].Position = FVector(-.2, -.5, 0);
    const FVector Across = Frame.PoseWorldLandmarks[11].Position - Frame.PoseWorldLandmarks[12].Position;
    Frame.HeadRotation = FRotator(5, 20, 10).Quaternion();
    const FQuat Head = Frame.HeadRotation;
    Frame.BlendShapes.Add(TEXT("eyeBlinkLeft"), .8f); Frame.BlendShapes.Add(TEXT("eyeBlinkRight"), .2f);
    FVtuberHandTracking Hand;
    Hand.Handedness = TEXT("Left"); Hand.Score = 1;
    Hand.Landmarks.SetNum(21); Hand.WorldLandmarks.SetNum(21);
    Hand.Landmarks[0].Position = FVector(.7, .4, 0);
    Hand.WorldLandmarks[0].Position = FVector(.2, -.1, .3);
    Frame.Hands.Add(Hand);
    MirrorTrackingFrame(Frame);
    TestTrue(TEXT("A neutral mirrored torso retains its facing direction"),
        (Frame.PoseWorldLandmarks[11].Position - Frame.PoseWorldLandmarks[12].Position).Equals(Across));
    TestTrue(TEXT("Normalized pose reflection swaps anatomical sides"), Frame.PoseLandmarks[11].Position.Equals(FVector(.7, .3, 0)));
    TestEqual(TEXT("Mirrored hand changes anatomical side"), Frame.Hands[0].Handedness, FName(TEXT("Right")));
    TestTrue(TEXT("Hand image and metric coordinates both reflect"),
        Frame.Hands[0].Landmarks[0].Position.Equals(FVector(.3, .4, 0))
        && Frame.Hands[0].WorldLandmarks[0].Position.Equals(FVector(-.2, -.1, .3)));
    TestEqual(TEXT("Mirrored expression changes anatomical side"), Frame.BlendShapes.FindRef(TEXT("eyeBlinkRight")), .8f);
    TestTrue(TEXT("Head mirrors yaw and roll while retaining pitch"), Frame.HeadRotation.Equals(FRotator(5, -20, -10).Quaternion(), .0001f));
    MirrorTrackingFrame(Frame);
    TestTrue(TEXT("Two reflections preserve head rotation"), Frame.HeadRotation.Equals(Head, .0001f));
    TestEqual(TEXT("Two reflections preserve hand side"), Frame.Hands[0].Handedness, FName(TEXT("Left")));
    TestTrue(TEXT("Two reflections preserve metric hand coordinates"),
        Frame.Hands[0].WorldLandmarks[0].Position.Equals(Hand.WorldLandmarks[0].Position));
    return true;
}
#endif
