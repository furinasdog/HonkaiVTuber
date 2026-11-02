// SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "VtuberRig.h"

enum class EVtuberCalibrationStep : uint8
{
    RelaxedMouth, OpenMouth, Smile, OpenEyes, ClosedEyes,
    Pucker, Funnel, Stretch, MouthLeft, MouthRight, Squint, Wide, BrowRaise, BrowDown, MouthClose, Review
};

// Sampling state has no rig/profile writes. Input values are in GetChannelInput's
// existing neutral-adjusted domain; the raw snapshot supplies face/frame validity.
class FVtuberCalibrationSession
{
public:
    static constexpr int32 MinimumFrames = 12;
    static constexpr int32 StepCount = static_cast<int32>(EVtuberCalibrationStep::Review);
    static constexpr double MinimumSeconds = 1.;
    static constexpr float MinimumSpan = .12f;
    static const TArray<FName>& RequiredChannels(EVtuberCalibrationStep Step);
    bool BeginStep(EVtuberCalibrationStep Step, double AfterTimestamp = 0);
    bool PushFrame(const FVtuberTrackingSnapshot& Frame, bool bTracking, double Now,
        const TMap<FName, float>& Inputs, FString& Status);
    bool CompleteStep(FString& Error);
    bool BuildRanges(TMap<FName, FVtuberThresholdCurve>& Ranges, FString& Error) const;
    void RewindTo(EVtuberCalibrationStep Step);
    void Reset();
    bool IsCollecting() const { return bCollecting; }
    bool IsComplete(EVtuberCalibrationStep Step) const;
    bool IsSkipped(EVtuberCalibrationStep Step) const;
    bool SkipStep(EVtuberCalibrationStep Step);
    bool IsReady() const;
    int32 GetFrameCount() const;
    double GetRecordedSeconds() const;
    float GetProgress() const;
    static bool FitRange(const TArray<float>& Neutral, const TArray<float>& Active,
        FVtuberThresholdCurve& Range, FString& Error);
private:
    struct FSamples { TMap<FName, TArray<float>> Values; int32 Count = 0; double First = 0, Last = 0; bool bComplete = false, bSkipped = false; };
    FSamples Steps[StepCount];
    EVtuberCalibrationStep Current = EVtuberCalibrationStep::RelaxedMouth;
    bool bCollecting = false;
    double LastTimestamp = 0;
};

struct FVtuberCalibrationChange
{
    FName Channel;
    FVtuberChannelResponse BeforeResponse, AfterResponse;
    FVtuberMorphMapping BeforeMapping, AfterMapping;
    bool bApply = true;
};

class SVtuberCalibrationWizard : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SVtuberCalibrationWizard) : _IsTracking(false), _IsMirrored(false) {}
        SLATE_ATTRIBUTE(FVtuberTrackingSnapshot, GetSnapshot)
        SLATE_ATTRIBUTE(bool, IsTracking)
        SLATE_ATTRIBUTE(bool, IsMirrored)
        SLATE_EVENT(FSimpleDelegate, OnFinished)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args, UVtuberRigComponent* InRig);
    virtual void Tick(const FGeometry& Geometry, double CurrentTime, float DeltaTime) override;
    // Uses the actual SaveProfile path. Failed writes restore every edited mapping/response.
    static bool ApplyAndSave(UVtuberRigComponent& Rig, const TArray<FVtuberCalibrationChange>& Changes, FString& Error);
private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FVtuberCalibrationWizardTest;
    friend class FVtuberCalibrationDirectionTest;
#endif
    void RebuildBody();
    void BuildReview();
    bool CanRecord() const;
    FText StepTitle() const;
    FText StepInstruction() const;
    FReply Record();
    FReply Next();
    FReply Skip();
    FReply Back();
    FReply Cancel();
    FReply Apply();
    TWeakObjectPtr<UVtuberRigComponent> Rig;
    TAttribute<FVtuberTrackingSnapshot> SnapshotSource;
    TAttribute<bool> TrackingSource;
    TAttribute<bool> MirroredSource;
    FSimpleDelegate Finished;
    FVtuberCalibrationSession Session;
    EVtuberCalibrationStep Step = EVtuberCalibrationStep::RelaxedMouth;
    TArray<FVtuberCalibrationChange> Changes;
    TArray<TSharedPtr<FString>> MorphOptions;
    TSharedPtr<class SVerticalBox> Body;
    FProgressBarStyle ProgressStyle;
    FString Status;
    bool bApplied = false;
    bool bPreparing = false;
    double PrepareUntil = 0, LastTickTime = 0;
};
