#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"
#include "VtuberRig.h"

class FHonkaiMotionCapture;

struct FNativeTrackingPreview
{
    // Image and normalized landmarks already share the consumer's single mirror transform.
    uint64 Sequence = 0;
    double TimestampSeconds = 0;
    int32 Width = 0, Height = 0;
    TArray<uint8> BGRA;
    TArray<FVtuberLandmark> FaceLandmarks;
    FVtuberTrackingSnapshot Tracking;
    bool bSupported = true;
    FString Status = TEXT("摄像头未启动");
    bool HasImage() const
    {
        return Width > 0 && Width <= 320 && Height > 0 && Height <= 240 && BGRA.Num() == Width * Height * 4;
    }
};

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
    void SetPreviewEnabled(bool bEnabled);
    FNativeTrackingPreview GetPreview() const;
    void LoadSettings();
    void SaveSettings() const;
#if WITH_DEV_AUTOMATION_TESTS
    static bool LoadReplayFrame(const FString& Path, bool bMirrorFrame, FVtuberTrackingSnapshot& Out);
    static bool ParseReplayFrameJson(const FString& Json, bool bMirrorFrame, FVtuberTrackingSnapshot& Out);
#endif
    int32 CameraIndex = 0, Width = 640, Height = 480, FramesPerSecond = 30;
    float Confidence = .5f;
    bool bMirror = true;
    FString Status = TEXT("摄像头未启动");
    float InferenceMs = 0, InferenceFps = 0;
private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FVtuberPreviewFrameTest;
    FString ReplayJsonText;
#endif
    FNativeTrackingPreview Preview;
    bool bPreviewEnabled = false;
    TUniquePtr<FHonkaiMotionCapture> InProcessCapture;
    FProcHandle Process;
    FString OutputPath, StopPath;
    double LastTimestamp = 0;
    double LastPoll = 0;
    double StopRequestedAt = 0;
    bool bStopKillRequested = false;
};
