#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"
#include "VtuberRig.h"

class FHonkaiMotionCapture;

// Uses in-process capture first; owns a helper only when integrated startup is unavailable.
class FNativeTracking
{
public:
    FNativeTracking();
    ~FNativeTracking();
    bool Start();
    void Stop();
    bool Poll(FVtuberTrackingSnapshot& Out);
    bool IsRunning();
    bool IsStopping() const;
    void LoadSettings();
    void SaveSettings() const;
#if WITH_DEV_AUTOMATION_TESTS
    static bool LoadReplayFrame(const FString& Path, bool bMirrorFrame, FVtuberTrackingSnapshot& Out);
#endif
    int32 CameraIndex = 0, Width = 640, Height = 480, FramesPerSecond = 30;
    float Confidence = .5f;
    bool bMirror = true;
    FString Status = TEXT("摄像头未启动");
    float InferenceMs = 0, InferenceFps = 0;
private:
    TUniquePtr<FHonkaiMotionCapture> InProcessCapture;
    FProcHandle Process;
    FString OutputPath, StopPath;
    double LastTimestamp = 0;
    double LastPoll = 0;
    double StopRequestedAt = 0;
    bool bStopKillRequested = false;
};
