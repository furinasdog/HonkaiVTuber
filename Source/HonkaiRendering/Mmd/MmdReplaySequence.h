#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Vtuber/NativeTracking.h"

// QA-only offline tracking fixtures. No capture backend or per-frame file exchange.
class FMmdReplaySequence
{
public:
    static constexpr int32 MaxBytes = 8 * 1024 * 1024;
    static constexpr int32 MaxFrames = 1800;

    bool Load(const FString& Path, float Fps, bool bLoop, bool bMirror, FString& Error);
    bool Parse(const FString& JsonLines, float Fps, bool bLoop, bool bMirror, FString& Error);
    int32 FrameIndex(double ElapsedWorldSeconds) const;
    bool Sample(double ElapsedWorldSeconds, FVtuberTrackingSnapshot& Out) const;
    int32 Num() const { return Frames.Num(); }
private:
    TArray<FVtuberTrackingSnapshot> Frames;
    float FramesPerSecond = 60;
    bool bLooping = false;
};

float MmdQaCaptureDelay(const TCHAR* CommandLine);
#endif
