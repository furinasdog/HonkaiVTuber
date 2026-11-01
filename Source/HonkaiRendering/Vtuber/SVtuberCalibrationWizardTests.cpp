// SPDX-License-Identifier: MIT
#include "SVtuberCalibrationWizard.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace HonkaiCalibrationTests
{
using FSession = FVtuberCalibrationSession;
using EStep = EVtuberCalibrationStep;
constexpr EStep CalibrationSteps[] = {
    EStep::RelaxedMouth, EStep::OpenMouth, EStep::Smile, EStep::OpenEyes, EStep::ClosedEyes
};

TArray<float> StableSamples(float Center, int32 Count = FSession::MinimumFrames + 1)
{
    TArray<float> Result;
    for (int32 Index = 0; Index < Count; ++Index) Result.Add(Center + (Index % 3 - 1) * .002f);
    return Result;
}

FVtuberTrackingSnapshot ValidFrame(EStep Step, double Timestamp, TMap<FName, float>& Inputs, float Value = .1f)
{
    FVtuberTrackingSnapshot Frame;
    Frame.TimestampSeconds = Timestamp;
    Frame.bFaceTracked = true;
    Inputs.Reset();
    for (FName Channel : FSession::RequiredChannels(Step))
    {
        // Raw existence validates recognition. Calibration must fit the existing input domain,
        // not accidentally fit this deliberately different, constant raw SDK score.
        Frame.BlendShapes.Add(Channel, .93f);
        Inputs.Add(Channel, Value);
    }
    return Frame;
}

bool RecordStep(FSession& Session, EStep Step, double FirstTimestamp, float Value, FString& Error,
                int32 Count = FSession::MinimumFrames + 1, double Spacing = .1)
{
    if (FSession::RequiredChannels(Step).IsEmpty() || !Session.BeginStep(Step, FirstTimestamp - .01))
    {
        Error = TEXT("Cannot begin a required calibration step.");
        return false;
    }
    for (int32 Index = 0; Index < Count; ++Index)
    {
        TMap<FName, float> Inputs;
        const double Timestamp = FirstTimestamp + Index * Spacing;
        auto Frame = ValidFrame(Step, Timestamp, Inputs, Value + (Index % 3 - 1) * .002f);
        if (!Session.PushFrame(Frame, true, Timestamp + .02, Inputs, Error)) return false;
    }
    return true;
}

bool SameCurve(const FVtuberThresholdCurve& A, const FVtuberThresholdCurve& B)
{
    return FMath::IsNearlyEqual(A.Start, B.Start) && FMath::IsNearlyEqual(A.Full, B.Full);
}

bool SameResponse(const FVtuberChannelResponse& A, const FVtuberChannelResponse& B)
{
    return A.bEnabled == B.bEnabled && A.bInvert == B.bInvert && A.bBipolar == B.bBipolar
        && FMath::IsNearlyEqual(A.SmoothingSeconds, B.SmoothingSeconds)
        && FMath::IsNearlyEqual(A.Sensitivity, B.Sensitivity)
        && FMath::IsNearlyEqual(A.Curvature, B.Curvature) && A.Precision == B.Precision
        && FMath::IsNearlyEqual(A.Lower, B.Lower) && FMath::IsNearlyEqual(A.Upper, B.Upper);
}

bool SameMapping(const FVtuberMorphMapping& A, const FVtuberMorphMapping& B)
{
    return A.Channel == B.Channel && A.Morph == B.Morph && SameCurve(A.Curve, B.Curve)
        && FMath::IsNearlyEqual(A.Maximum, B.Maximum) && FMath::IsNearlyEqual(A.Sensitivity, B.Sensitivity)
        && A.bEnabled == B.bEnabled && A.bInvert == B.bInvert;
}

struct FRigState
{
    TArray<FVtuberMorphMapping> Mappings;
    TMap<FName, FVtuberChannelResponse> Responses;

    explicit FRigState(const UVtuberRigComponent& Rig) : Mappings(Rig.GetMappings())
    {
        for (FName Channel : UVtuberRigComponent::GetTrackingChannels()) Responses.Add(Channel, Rig.GetChannelResponse(Channel));
    }

    bool Matches(const UVtuberRigComponent& Rig) const
    {
        if (Mappings.Num() != Rig.GetMappings().Num()) return false;
        for (int32 Index = 0; Index < Mappings.Num(); ++Index)
            if (!SameMapping(Mappings[Index], Rig.GetMappings()[Index])) return false;
        for (const auto& Pair : Responses)
            if (!SameResponse(Pair.Value, Rig.GetChannelResponse(Pair.Key))) return false;
        return true;
    }
};

const FVtuberMorphMapping* FindMapping(const UVtuberRigComponent& Rig, FName Channel)
{
    return Rig.GetMappings().FindByPredicate([Channel](const FVtuberMorphMapping& Mapping) { return Mapping.Channel == Channel; });
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVtuberCalibrationFitRangeTest, "HonkaiVTuber.Calibration.FitRange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVtuberCalibrationFitRangeTest::RunTest(const FString& Parameters)
{
    using namespace HonkaiCalibrationTests;
    FString Error;
    FVtuberThresholdCurve Range{.2f, .8f};
    const auto Neutral = StableSamples(.1f), Active = StableSamples(.7f);
    TestTrue(TEXT("Distinct stable neutral and active samples fit"), FSession::FitRange(Neutral, Active, Range, Error));
    TestTrue(TEXT("Successful range is finite, ordered and within the input domain"), Range.IsValid() && Range.Start >= 0 && Range.Full <= 1);
    TestTrue(TEXT("Fitted range retains the minimum usable span"), Range.Full - Range.Start >= FSession::MinimumSpan - KINDA_SMALL_NUMBER);
    TestTrue(TEXT("Neutral remains below the response threshold"), Range.Evaluate(.102f) <= .2f);
    TestTrue(TEXT("An active expression reaches the response"), Range.Evaluate(.7f) >= .8f);

    const FVtuberThresholdCurve Sentinel{.24f, .81f};
    Range = Sentinel;
    TestFalse(TEXT("A weak expression does not fit a misleading range"), FSession::FitRange(Neutral, StableSamples(.18f), Range, Error));
    TestTrue(TEXT("Weak expression leaves the caller's range untouched"), SameCurve(Range, Sentinel));
    TestFalse(TEXT("Weak expression includes an actionable failure"), Error.IsEmpty());
    Range = Sentinel;
    TestFalse(TEXT("Insufficient neutral frames are rejected"), FSession::FitRange(StableSamples(.1f, FSession::MinimumFrames - 1), Active, Range, Error));
    TestTrue(TEXT("Insufficient neutral frames do not write a range"), SameCurve(Range, Sentinel));
    TestFalse(TEXT("Insufficient active frames are rejected"), FSession::FitRange(Neutral, StableSamples(.7f, FSession::MinimumFrames - 1), Range, Error));

    TArray<float> Unstable;
    for (int32 Index = 0; Index < FSession::MinimumFrames + 1; ++Index) Unstable.Add(Index % 2 ? .05f : .35f);
    TestFalse(TEXT("Unstable neutral samples are rejected by their spread"), FSession::FitRange(Unstable, Active, Range, Error));
    for (float& Value : Unstable) Value += .4f;
    TestFalse(TEXT("Unstable active samples are rejected by their spread"), FSession::FitRange(Neutral, Unstable, Range, Error));
    TestTrue(TEXT("All failed fits preserve the previous range"), SameCurve(Range, Sentinel));

    TestTrue(TEXT("Small but usable movement still fits"), FSession::FitRange(StableSamples(.05f), StableSamples(.19f), Range, Error));
    TestTrue(TEXT("A narrow successful fit still has at least the minimum span"), Range.Full - Range.Start >= FSession::MinimumSpan - KINDA_SMALL_NUMBER);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVtuberCalibrationSamplingTest, "HonkaiVTuber.Calibration.SamplingGates",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVtuberCalibrationSamplingTest::RunTest(const FString& Parameters)
{
    using namespace HonkaiCalibrationTests;
    FSession Session;
    FString Status;
    TMap<FName, float> Inputs;
    TestTrue(TEXT("Begin sampling after an already displayed frame"), Session.BeginStep(EStep::RelaxedMouth, 10));
    auto Frame = ValidFrame(EStep::RelaxedMouth, 10, Inputs);
    TestFalse(TEXT("Frame at the starting watermark is not a new observation"), Session.PushFrame(Frame, true, 10.02, Inputs, Status));
    TestEqual(TEXT("Starting watermark contributes no samples"), Session.GetFrameCount(), 0);
    Frame.TimestampSeconds = 10.1;
    TestTrue(TEXT("A distinct fresh face frame is accepted"), Session.PushFrame(Frame, true, 10.12, Inputs, Status));
    for (int32 Index = 0; Index < 30; ++Index)
        Session.PushFrame(Frame, true, 10.13 + Index * .005, Inputs, Status);
    TestEqual(TEXT("Repainting the same frame never increases the sample count"), Session.GetFrameCount(), 1);
    TestTrue(TEXT("Repeated frames do not accumulate recording duration"), FMath::IsNearlyZero(Session.GetRecordedSeconds()));
    TestFalse(TEXT("Duplicate frames cannot finish a step"), Session.CompleteStep(Status));

    TestTrue(TEXT("Restart for minimum-count test"), RecordStep(Session, EStep::RelaxedMouth, 20, .1f, Status, FSession::MinimumFrames - 1, .11));
    TestTrue(TEXT("One second cannot replace the minimum number of distinct frames"), Session.GetProgress() < 1.f);
    TestFalse(TEXT("Too few frames cannot complete"), Session.CompleteStep(Status));
    TestTrue(TEXT("Restart for minimum-duration test"), RecordStep(Session, EStep::RelaxedMouth, 30, .1f, Status, FSession::MinimumFrames + 1, .01));
    TestTrue(TEXT("A burst of frames cannot replace the minimum recording duration"), Session.GetProgress() < 1.f);
    TestFalse(TEXT("A short burst cannot complete"), Session.CompleteStep(Status));

    for (int32 InvalidKind = 0; InvalidKind < 3; ++InvalidKind)
    {
        const double Timestamp = 40 + InvalidKind * 10;
        TestTrue(TEXT("Restart before tracking gate check"), Session.BeginStep(EStep::RelaxedMouth, Timestamp - 1));
        Frame = ValidFrame(EStep::RelaxedMouth, Timestamp, Inputs);
        TestTrue(TEXT("Current step first receives a valid sample"), Session.PushFrame(Frame, true, Timestamp + .02, Inputs, Status));
        Frame.TimestampSeconds += .1;
        if (InvalidKind == 1) Frame.bFaceTracked = false;
        const bool Accepted = Session.PushFrame(Frame, InvalidKind != 2,
            Frame.TimestampSeconds + (InvalidKind == 0 ? .51 : .02), Inputs, Status);
        TestFalse(TEXT("Stale, missing-face or stopped tracking frames are rejected"), Accepted);
        TestEqual(TEXT("Tracking loss clears the current step's samples"), Session.GetFrameCount(), 0);
        TestFalse(TEXT("Tracking loss stops recording until the user starts again"), Session.IsCollecting());
        TestFalse(TEXT("A lost step is not marked complete"), Session.IsComplete(EStep::RelaxedMouth));
    }
    Session.BeginStep(EStep::RelaxedMouth, 99);
    Frame = ValidFrame(EStep::RelaxedMouth, 100, Inputs);
    TestFalse(TEXT("There are raw channels to validate"), Frame.BlendShapes.IsEmpty());
    if (FSession::RequiredChannels(EStep::RelaxedMouth).IsEmpty()) return false;
    Frame.BlendShapes.Remove(FSession::RequiredChannels(EStep::RelaxedMouth)[0]);
    TestFalse(TEXT("Processed input alone cannot stand in for a missing raw SDK channel"), Session.PushFrame(Frame, true, 100.02, Inputs, Status));
    TestEqual(TEXT("Missing raw recognition contributes no samples"), Session.GetFrameCount(), 0);
    Session.BeginStep(EStep::RelaxedMouth, 104);
    Frame = ValidFrame(EStep::RelaxedMouth, 105, Inputs);
    TestTrue(TEXT("A face frame just within the freshness window remains usable"), Session.PushFrame(Frame, true, 105.49, Inputs, Status));
    TestTrue(TEXT("Exactly the minimum distinct-frame count can be recorded"), RecordStep(Session, EStep::RelaxedMouth, 110, .1f, Status, FSession::MinimumFrames));
    TestTrue(TEXT("The exact minimum count completes once it spans a full second"), Session.CompleteStep(Status));
    Session.BeginStep(EStep::RelaxedMouth, 119);
    Frame = ValidFrame(EStep::RelaxedMouth, 120, Inputs);
    TestTrue(TEXT("Continuity control begins with a fresh real frame"), Session.PushFrame(Frame, true, 120.02, Inputs, Status));
    Frame.TimestampSeconds = 120.6;
    TestFalse(TEXT("A fresh frame after a recording gap cannot reuse old partial samples"), Session.PushFrame(Frame, true, 120.62, Inputs, Status));
    TestEqual(TEXT("An interrupted recording clears old samples"), Session.GetFrameCount(), 0);
    TestFalse(TEXT("An interrupted recording requires restarting"), Session.IsCollecting());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVtuberCalibrationFiveStepsTest, "HonkaiVTuber.Calibration.FiveSteps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVtuberCalibrationFiveStepsTest::RunTest(const FString& Parameters)
{
    using namespace HonkaiCalibrationTests;
    FSession Session;
    FString Error;
    TMap<FName, FVtuberThresholdCurve> Ranges;
    TestFalse(TEXT("An incomplete session offers no applicable ranges"), Session.BuildRanges(Ranges, Error));
    TestTrue(TEXT("An incomplete session leaves the proposal empty"), Ranges.IsEmpty());
    const float Values[] = {.1f, .7f, .75f, .12f, .78f};
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(CalibrationSteps); ++Index)
    {
        const EStep Step = CalibrationSteps[Index];
        if (!TestTrue(TEXT("Each step accepts thirteen distinct, fresh frames"), RecordStep(Session, Step, 100 + Index * 5, Values[Index], Error))) return false;
        TestEqual(TEXT("Every step contains all distinct frames"), Session.GetFrameCount(), FSession::MinimumFrames + 1);
        TestTrue(TEXT("Every step spans at least one second"), Session.GetRecordedSeconds() >= FSession::MinimumSeconds);
        TestTrue(TEXT("Adequate samples fill the current step's progress"), Session.GetProgress() >= 1.f - KINDA_SMALL_NUMBER);
        TestTrue(TEXT("Ready step completes"), Session.CompleteStep(Error));
        TestTrue(TEXT("The completed step remains recorded"), Session.IsComplete(Step));
        TestFalse(TEXT("Completed step is no longer collecting"), Session.IsCollecting());
    }
    TestTrue(TEXT("All five completed steps make the session ready to apply"), Session.IsReady());
    TestTrue(TEXT("All five completed steps build a proposal from input-domain values"), Session.BuildRanges(Ranges, Error));
    TestEqual(TEXT("The proposal only includes the five calibrated expression channels"), Ranges.Num(), 5);
    const FName Channels[] = {TEXT("jawOpen"), TEXT("mouthSmileLeft"), TEXT("mouthSmileRight"), TEXT("eyeBlinkLeft"), TEXT("eyeBlinkRight")};
    for (FName Channel : Channels)
    {
        const auto* Range = Ranges.Find(Channel);
        TestTrue(*FString::Printf(TEXT("Proposal contains %s"), *Channel.ToString()), Range != nullptr);
        if (!Range) continue;
        TestTrue(TEXT("Every proposed threshold is valid and usable"), Range->IsValid() && Range->Full - Range->Start >= FSession::MinimumSpan - KINDA_SMALL_NUMBER);
        TestTrue(TEXT("Neutral input maps near zero"), Range->Evaluate(.12f) <= .2f);
        TestTrue(TEXT("Active input maps near full"), Range->Evaluate(.78f) >= .8f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVtuberCalibrationWeakRangeTest, "HonkaiVTuber.Calibration.WeakRange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVtuberCalibrationWeakRangeTest::RunTest(const FString& Parameters)
{
    using namespace HonkaiCalibrationTests;
    FSession Session;
    FString Error;
    if (!TestTrue(TEXT("Record genuine neutral frames"), RecordStep(Session, EStep::RelaxedMouth, 200, .1f, Error))) return false;
    if (!TestTrue(TEXT("Complete neutral step"), Session.CompleteStep(Error))) return false;
    if (!TestTrue(TEXT("Record a weak expression with enough real frames and time"), RecordStep(Session, EStep::OpenMouth, 205, .17f, Error))) return false;
    TestFalse(TEXT("Timing validity cannot approve an expression with insufficient movement"), Session.CompleteStep(Error));
    TestFalse(TEXT("Weak expression is not marked complete"), Session.IsComplete(EStep::OpenMouth));
    TestFalse(TEXT("Weak expression has an actionable error"), Error.IsEmpty());
    TMap<FName, FVtuberThresholdCurve> Ranges;
    TestFalse(TEXT("Too little mouth movement prevents an applicable proposal"), Session.BuildRanges(Ranges, Error));
    TestTrue(TEXT("A rejected step does not expose a partial proposal"), Ranges.IsEmpty());
    TestFalse(TEXT("The failed proposal explains the problem"), Error.IsEmpty());
    TestTrue(TEXT("Retry preserves the accepted neutral recording"), Session.IsComplete(EStep::RelaxedMouth));
    if (!TestTrue(TEXT("Record sufficient movement on retry"), RecordStep(Session, EStep::OpenMouth, 210, .7f, Error))) return false;
    TestTrue(TEXT("A sufficient retry can complete normally"), Session.CompleteStep(Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVtuberCalibrationWizardTest, "HonkaiVTuber.Calibration.CancelAndRollback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVtuberCalibrationWizardTest::RunTest(const FString& Parameters)
{
    using namespace HonkaiCalibrationTests;
    auto* Rig = NewObject<UVtuberRigComponent>();
    TArray<FVtuberCalibrationChange> Proposed;
    for (FName Channel : {FName(TEXT("jawOpen")), FName(TEXT("eyeBlinkLeft"))})
    {
        const auto* Existing = FindMapping(*Rig, Channel);
        if (!TestTrue(TEXT("Default constructor provides calibration mappings without a mesh"), Existing != nullptr)) return false;
        auto Mapping = *Existing;
        Mapping.Morph = NAME_None;
        Mapping.Curve = {.1f, .9f}; Mapping.Maximum = .7f; Mapping.Sensitivity = 1.3f;
        Mapping.bEnabled = false; Mapping.bInvert = true;
        if (!TestTrue(TEXT("Configure original mapping without requiring a model morph"), Rig->SetMapping(Mapping))) return false;
        auto Response = Rig->GetChannelResponse(Channel);
        Response.bEnabled = false; Response.bInvert = true;
        Response.Lower = .15f; Response.Upper = .85f; Response.SmoothingSeconds = .11f;
        Response.Sensitivity = 1.4f; Response.Curvature = .25f; Response.Precision = 9;
        if (!TestTrue(TEXT("Configure nondefault original response"), Rig->SetChannelResponse(Channel, Response))) return false;
        FVtuberCalibrationChange Change;
        Change.Channel = Channel;
        Change.BeforeMapping = *FindMapping(*Rig, Channel);
        Change.BeforeResponse = Rig->GetChannelResponse(Channel);
        Change.AfterMapping = Change.BeforeMapping;
        Change.AfterMapping.Curve = {.25f, .65f}; Change.AfterMapping.Maximum = .9f;
        Change.AfterMapping.Sensitivity = .8f; Change.AfterMapping.bEnabled = true; Change.AfterMapping.bInvert = false;
        Change.AfterResponse = Change.BeforeResponse;
        Change.AfterResponse.bEnabled = true; Change.AfterResponse.bInvert = false;
        Change.AfterResponse.Lower = .25f; Change.AfterResponse.Upper = .65f;
        Change.AfterResponse.SmoothingSeconds = .07f; Change.AfterResponse.Sensitivity = 1.1f;
        Change.AfterResponse.Curvature = .8f; Change.AfterResponse.Precision = 21;
        Proposed.Add(Change);
    }
    const FRigState Before(*Rig);
    int32 FinishedCount = 0;
    auto Wizard = SNew(SVtuberCalibrationWizard, Rig)
        .OnFinished(FSimpleDelegate::CreateLambda([&FinishedCount] { ++FinishedCount; }));
    TestTrue(TEXT("Opening the wizard does not modify any mapping or channel response"), Before.Matches(*Rig));
    Wizard->Changes = Proposed;
    Wizard->Session.BeginStep(EStep::RelaxedMouth, 299);
    TMap<FName, float> Inputs;
    auto Frame = ValidFrame(EStep::RelaxedMouth, 300, Inputs);
    FString Error;
    TestTrue(TEXT("Cancellation fixture includes an in-progress recording"), Wizard->Session.PushFrame(Frame, true, 300.02, Inputs, Error));
    Wizard->Cancel();
    TestEqual(TEXT("Cancel invokes its completion callback once"), FinishedCount, 1);
    TestTrue(TEXT("Cancel discards every unapplied proposal"), Wizard->Changes.IsEmpty());
    TestFalse(TEXT("Cancel stops recording"), Wizard->Session.IsCollecting());
    TestEqual(TEXT("Cancel clears recorded samples"), Wizard->Session.GetFrameCount(), 0);
    TestFalse(TEXT("Cancel never marks calibration as applied"), Wizard->bApplied);
    TestTrue(TEXT("Cancel preserves every mapping and response field"), Before.Matches(*Rig));

    // Validate the proposed values independently so a rejected edit cannot masquerade as rollback.
    auto* ValidationRig = NewObject<UVtuberRigComponent>();
    for (const auto& Change : Proposed)
    {
        TestTrue(TEXT("Proposed mapping is accepted by the real rig validator"), ValidationRig->SetMapping(Change.AfterMapping));
        TestTrue(TEXT("Proposed response is accepted by the real rig validator"), ValidationRig->SetChannelResponse(Change.Channel, Change.AfterResponse));
    }
    FString ExpectedSaveError;
    TestFalse(TEXT("Default rig has no profile path and rejects saving before file IO"), Rig->SaveProfile(ExpectedSaveError));
    TestFalse(TEXT("The real empty-path save reports an error"), ExpectedSaveError.IsEmpty());
    TestFalse(TEXT("Actual save failure rejects applying the calibration"), SVtuberCalibrationWizard::ApplyAndSave(*Rig, Proposed, Error));
    TestTrue(TEXT("Apply reports the actual profile-save failure"), !ExpectedSaveError.IsEmpty() && Error.Contains(ExpectedSaveError));
    TestTrue(TEXT("Save failure restores all mapping and response fields, including enabled coupling"), Before.Matches(*Rig));
    auto ManualResponse = Rig->GetChannelResponse(TEXT("jawOpen"));
    ManualResponse.Upper = .95f;
    TestTrue(TEXT("A manual edit can occur while a proposal is being reviewed"), Rig->SetChannelResponse(TEXT("jawOpen"), ManualResponse));
    const FRigState AfterManual(*Rig);
    TestFalse(TEXT("A stale proposal cannot overwrite a newer manual edit"), SVtuberCalibrationWizard::ApplyAndSave(*Rig, Proposed, Error));
    TestTrue(TEXT("Rejecting a stale proposal preserves all current state"), AfterManual.Matches(*Rig));
    return true;
}
#if PLATFORM_WINDOWS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVtuberCalibrationLockedProfileTest, "HonkaiVTuber.Calibration.LockedProfileRollback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVtuberCalibrationLockedProfileTest::RunTest(const FString& Parameters)
{
    using namespace HonkaiCalibrationTests;
    auto* Rig = NewObject<UVtuberRigComponent>();
    const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Tests/CalibrationSave"));
    const FString Path = Directory / (FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".json"));
    FString Error;
    if (!TestTrue(TEXT("Create an actual old profile"), Rig->SaveProfile(Path, Error))) return false;
    if (!TestTrue(TEXT("Bind the rig to the real saved path"), Rig->LoadProfile(Path, Error)))
    { IFileManager::Get().Delete(*Path); return false; }
    TArray<uint8> OldBytes;
    if (!TestTrue(TEXT("Read the exact original bytes"), FFileHelper::LoadFileToArray(OldBytes, *Path)))
    { IFileManager::Get().Delete(*Path); return false; }

    FVtuberCalibrationChange Change;
    Change.Channel = TEXT("jawOpen");
    Change.BeforeMapping = Change.AfterMapping = *FindMapping(*Rig, Change.Channel);
    Change.BeforeResponse = Change.AfterResponse = Rig->GetChannelResponse(Change.Channel);
    Change.AfterMapping.bEnabled = true;
    Change.AfterMapping.Curve = {.15f, .75f};
    Change.AfterResponse.bEnabled = true;
    Change.AfterResponse.Lower = .15f; Change.AfterResponse.Upper = .75f;
    const TArray<FVtuberCalibrationChange> Proposed{Change};
    const FRigState Before(*Rig);

    // A real handle denying delete/rename sharing reproduces a consumer holding the profile open.
    HANDLE Locked = ::CreateFileW(*Path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!TestTrue(TEXT("Hold the existing destination against replacement"), Locked != INVALID_HANDLE_VALUE))
    { IFileManager::Get().Delete(*Path); return false; }
    const bool bSavedWhileLocked = SVtuberCalibrationWizard::ApplyAndSave(*Rig, Proposed, Error);
    TestFalse(TEXT("The actual file sharing violation rejects saving"), bSavedWhileLocked);
    TestFalse(TEXT("The real save failure reaches the caller"), Error.IsEmpty());
    TestTrue(TEXT("Failure rolls back every mapping and response"), Before.Matches(*Rig));
    TArray<uint8> LockedBytes;
    TestTrue(TEXT("Old profile remains readable during the lock"), FFileHelper::LoadFileToArray(LockedBytes, *Path));
    TestTrue(TEXT("Failure preserves every original disk byte"), LockedBytes == OldBytes);
    ::CloseHandle(Locked);
    TArray<uint8> UnlockedBytes;
    TestTrue(TEXT("Old profile survives unlocking"), FFileHelper::LoadFileToArray(UnlockedBytes, *Path));
    TestTrue(TEXT("Unlocking does not expose a deferred deletion"), UnlockedBytes == OldBytes);

    TestTrue(TEXT("The unchanged proposal saves successfully after unlock"), SVtuberCalibrationWizard::ApplyAndSave(*Rig, Proposed, Error));
    TestTrue(TEXT("Retry publishes the proposed mapping"), SameMapping(*FindMapping(*Rig, Change.Channel), Change.AfterMapping));
    TestTrue(TEXT("Retry publishes the proposed response"), SameResponse(Rig->GetChannelResponse(Change.Channel), Change.AfterResponse));
    TArray<uint8> NewBytes;
    TestTrue(TEXT("Read successfully replaced profile"), FFileHelper::LoadFileToArray(NewBytes, *Path));
    TestTrue(TEXT("Successful retry writes new disk bytes"), NewBytes != OldBytes);
    auto* Reloaded = NewObject<UVtuberRigComponent>();
    TestTrue(TEXT("The replaced profile reloads through the real parser"), Reloaded->LoadProfile(Path, Error));
    TestTrue(TEXT("Reloaded mapping is the proposed mapping"), SameMapping(*FindMapping(*Reloaded, Change.Channel), Change.AfterMapping));
    TestTrue(TEXT("Reloaded response is the proposed response"), SameResponse(Reloaded->GetChannelResponse(Change.Channel), Change.AfterResponse));
    TestFalse(TEXT("Successful replacement consumes the temporary file"), IFileManager::Get().FileExists(*(Path + TEXT(".tmp"))));
    IFileManager::Get().Delete(*Path);
    IFileManager::Get().Delete(*(Path + TEXT(".tmp")));
    return true;
}
#endif
#endif
