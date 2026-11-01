// SPDX-License-Identifier: MIT
#include "SVtuberCalibrationWizard.h"
#include "Mmd/MmdUiStyle.h"

#include "HAL/PlatformTime.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
    const FLinearColor CalInk = FLinearColor::FromSRGBColor(FColor(32, 41, 56));
    const FLinearColor CalMuted = FLinearColor::FromSRGBColor(FColor(102, 117, 138));
    const FLinearColor CalField = FLinearColor::FromSRGBColor(FColor(248, 250, 253));
    const FLinearColor CalAccent = FLinearColor::FromSRGBColor(FColor(71, 116, 170));
    const TCHAR* CalNoMorph = TEXT("（未映射，仅校准输入）");

    int32 CalIndex(EVtuberCalibrationStep Step) { return static_cast<int32>(Step); }
    float CalQuantile(TArray<float> Values, float Fraction)
    {
        Values.Sort();
        return Values[FMath::FloorToInt((Values.Num() - 1) * Fraction)];
    }
    bool CalValidSamples(const TArray<float>& Values)
    {
        if (Values.Num() < FVtuberCalibrationSession::MinimumFrames) return false;
        for (float Value : Values) if (!FMath::IsFinite(Value) || Value < 0 || Value > 1) return false;
        return true;
    }
    FString CalChannelLabel(FName Channel)
    {
        if (Channel == TEXT("jawOpen")) return TEXT("张嘴");
        if (Channel == TEXT("mouthSmileLeft")) return TEXT("左嘴角");
        if (Channel == TEXT("mouthSmileRight")) return TEXT("右嘴角");
        if (Channel == TEXT("eyeBlinkLeft")) return TEXT("左眨眼");
        return TEXT("右眨眼");
    }
    TSharedRef<STextBlock> CalLabel(const FString& Text, int32 Size = 14, FLinearColor Color = CalInk)
    {
        return SNew(STextBlock).Text(FText::FromString(Text)).Font(FCoreStyle::GetDefaultFontStyle("Regular", Size))
            .ColorAndOpacity(Color).AutoWrapText(true);
    }
    bool CalSameResponse(const FVtuberChannelResponse& A, const FVtuberChannelResponse& B)
    {
        return A.bEnabled == B.bEnabled && A.bInvert == B.bInvert && A.bBipolar == B.bBipolar
            && A.SmoothingSeconds == B.SmoothingSeconds && A.Sensitivity == B.Sensitivity
            && A.Curvature == B.Curvature && A.Precision == B.Precision && A.Lower == B.Lower && A.Upper == B.Upper;
    }
    bool CalSameMapping(const FVtuberMorphMapping& A, const FVtuberMorphMapping& B)
    {
        return A.Channel == B.Channel && A.Morph == B.Morph && A.Curve.Start == B.Curve.Start && A.Curve.Full == B.Curve.Full
            && A.Maximum == B.Maximum && A.Sensitivity == B.Sensitivity && A.bEnabled == B.bEnabled && A.bInvert == B.bInvert;
    }
    // Preserve any valid existing target. Missing targets may use generic PMX/VRM
    // vocabulary; no character/model name or material index participates.
    FName CalMatchMorph(FName Channel, const TArray<FName>& Available)
    {
        FString Aliases;
        if (Channel == TEXT("jawOpen")) Aliases = TEXT("jawOpen|mouthOpen|あ|あ２|ア|aa|张嘴|口開け");
        else if (Channel == TEXT("mouthSmileLeft")) Aliases = TEXT("mouthSmileLeft|mouthSmileL|にやり左|微笑左|口角上げ左|にやり|口角上げ|微笑|happy|joy");
        else if (Channel == TEXT("mouthSmileRight")) Aliases = TEXT("mouthSmileRight|mouthSmileR|にやり右|微笑右|口角上げ右|にやり|口角上げ|微笑|happy|joy");
        else if (Channel == TEXT("eyeBlinkLeft")) Aliases = TEXT("eyeBlinkLeft|blinkL|まばたき左|左眨眼|ウィンク２|ｳｨﾝｸ２|まばたき|blink");
        else Aliases = TEXT("eyeBlinkRight|blinkR|まばたき右|右眨眼|ウィンク２右|ｳｨﾝｸ２右|まばたき|blink");
        TArray<FString> Candidates;
        Aliases.ParseIntoArray(Candidates, TEXT("|"), true);
        for (const FString& Candidate : Candidates)
            for (FName Morph : Available) if (Morph.ToString().Equals(Candidate, ESearchCase::IgnoreCase)) return Morph;
        return NAME_None;
    }
}

const TArray<FName>& FVtuberCalibrationSession::RequiredChannels(EVtuberCalibrationStep Step)
{
    static const TArray<FName> Neutral{TEXT("jawOpen"), TEXT("mouthSmileLeft"), TEXT("mouthSmileRight")};
    static const TArray<FName> Mouth{TEXT("jawOpen")};
    static const TArray<FName> Smile{TEXT("mouthSmileLeft"), TEXT("mouthSmileRight")};
    static const TArray<FName> Eyes{TEXT("eyeBlinkLeft"), TEXT("eyeBlinkRight")};
    static const TArray<FName> Empty;
    if (Step == EVtuberCalibrationStep::RelaxedMouth) return Neutral;
    if (Step == EVtuberCalibrationStep::OpenMouth) return Mouth;
    if (Step == EVtuberCalibrationStep::Smile) return Smile;
    if (Step == EVtuberCalibrationStep::OpenEyes || Step == EVtuberCalibrationStep::ClosedEyes) return Eyes;
    return Empty;
}

void FVtuberCalibrationSession::Reset()
{
    for (FSamples& Sample : Steps) Sample = FSamples();
    Current = EVtuberCalibrationStep::RelaxedMouth; bCollecting = false; LastTimestamp = 0;
}

void FVtuberCalibrationSession::RewindTo(EVtuberCalibrationStep Step)
{
    if (CalIndex(Step) < 0 || CalIndex(Step) >= 5) return;
    for (int32 I = CalIndex(Step); I < 5; ++I) Steps[I] = FSamples();
    Current = Step; bCollecting = false;
}

bool FVtuberCalibrationSession::BeginStep(EVtuberCalibrationStep Step, double AfterTimestamp)
{
    if (CalIndex(Step) < 0 || CalIndex(Step) >= 5 || !FMath::IsFinite(AfterTimestamp) || AfterTimestamp < 0) return false;
    for (int32 I = 0; I < CalIndex(Step); ++I) if (!Steps[I].bComplete) return false;
    RewindTo(Step);
    LastTimestamp = FMath::Max(LastTimestamp, AfterTimestamp);
    bCollecting = true;
    return true;
}

bool FVtuberCalibrationSession::PushFrame(const FVtuberTrackingSnapshot& Frame, bool bTracking, double Now,
    const TMap<FName, float>& Inputs, FString& Status)
{
    if (!bCollecting) return false;
    auto Stop = [&](const TCHAR* Reason)
    {
        Steps[CalIndex(Current)] = FSamples(); bCollecting = false; Status = Reason; return false;
    };
    if (!bTracking || !Frame.bFaceTracked) return Stop(TEXT("面部追踪已暂停，本步记录已清除。恢复追踪后重新记录。"));
    if (!FMath::IsFinite(Now) || !FMath::IsFinite(Frame.TimestampSeconds) || Frame.TimestampSeconds <= 0
        || Frame.TimestampSeconds > Now + .05 || Now - Frame.TimestampSeconds > .5)
        return Stop(TEXT("面部数据已过期，本步记录已清除。请重新记录。"));
    if (Frame.TimestampSeconds <= LastTimestamp) return false;
    for (FName Channel : RequiredChannels(Current))
    {
        const float* Raw = Frame.BlendShapes.Find(Channel);
        const float* Value = Inputs.Find(Channel);
        if (!Raw || !Value || !FMath::IsFinite(*Raw) || *Raw < 0 || *Raw > 1
            || !FMath::IsFinite(*Value) || *Value < 0 || *Value > 1)
            return Stop(TEXT("面部通道数据不完整，本步记录已清除。请检查动捕状态。"));
    }
    FSamples& Sample = Steps[CalIndex(Current)];
    if (Sample.Count > 0 && Frame.TimestampSeconds - Sample.Last > .5)
        return Stop(TEXT("记录已中断，本步记录已清除。请重新记录。"));
    if (Sample.Count >= 240) return Stop(TEXT("记录时间过长，请保持表情稳定后重新记录。"));
    if (Sample.Count == 0) Sample.First = Frame.TimestampSeconds;
    Sample.Last = LastTimestamp = Frame.TimestampSeconds;
    for (FName Channel : RequiredChannels(Current)) Sample.Values.FindOrAdd(Channel).Add(Inputs.FindChecked(Channel));
    ++Sample.Count;
    return true;
}

int32 FVtuberCalibrationSession::GetFrameCount() const { return Steps[CalIndex(Current)].Count; }
double FVtuberCalibrationSession::GetRecordedSeconds() const
{
    const FSamples& Sample = Steps[CalIndex(Current)];
    return Sample.Count > 1 ? Sample.Last - Sample.First : 0;
}
float FVtuberCalibrationSession::GetProgress() const
{
    return FMath::Clamp(FMath::Min(GetFrameCount() / float(MinimumFrames), float(GetRecordedSeconds() / MinimumSeconds)), 0.f, 1.f);
}
bool FVtuberCalibrationSession::IsComplete(EVtuberCalibrationStep Step) const
{
    return CalIndex(Step) >= 0 && CalIndex(Step) < 5 && Steps[CalIndex(Step)].bComplete;
}
bool FVtuberCalibrationSession::IsReady() const
{
    for (const FSamples& Sample : Steps) if (!Sample.bComplete) return false;
    return true;
}

bool FVtuberCalibrationSession::FitRange(const TArray<float>& Neutral, const TArray<float>& Active,
    FVtuberThresholdCurve& Range, FString& Error)
{
    Error.Reset();
    if (!CalValidSamples(Neutral) || !CalValidSamples(Active)) { Error = TEXT("真实面部帧不足，无法计算范围。"); return false; }
    if (CalQuantile(Neutral, .9f) - CalQuantile(Neutral, .1f) > .18f || CalQuantile(Active, .9f) - CalQuantile(Active, .1f) > .18f)
    { Error = TEXT("表情变化不够稳定，请保持动作后重新记录。"); return false; }
    if (CalQuantile(Active, .5f) - CalQuantile(Neutral, .5f) < .0999f)
    { Error = TEXT("动作与放松状态的变化幅度不足，请重新记录。"); return false; }
    const float Low = CalQuantile(Neutral, .9f) + .015f;
    if (Low > 1.f - MinimumSpan)
    { Error = TEXT("放松时的输入接近上限，请检查面部追踪后重新记录。"); return false; }
    FVtuberThresholdCurve Proposed;
    Proposed.Start = Low;
    Proposed.Full = FMath::Min(1.f, FMath::Max(Low + MinimumSpan, CalQuantile(Active, .8f) + .01f));
    Range = Proposed;
    return true;
}

bool FVtuberCalibrationSession::CompleteStep(FString& Error)
{
    Error.Reset();
    FSamples& Sample = Steps[CalIndex(Current)];
    if (Sample.Count < MinimumFrames || GetRecordedSeconds() + 1.e-6 < MinimumSeconds)
    { Error = TEXT("请保持至少1秒，并采集至少12个不同的面部帧。"); return false; }
    for (FName Channel : RequiredChannels(Current))
    {
        const TArray<float>& Values = Sample.Values.FindChecked(Channel);
        if (!CalValidSamples(Values) || CalQuantile(Values, .9f) - CalQuantile(Values, .1f) > .18f)
        { bCollecting = false; Error = TEXT("表情变化不够稳定，请保持动作后重新记录。"); return false; }
        const int32 NeutralStep = Current == EVtuberCalibrationStep::ClosedEyes ? 3 : 0;
        if (Current == EVtuberCalibrationStep::OpenMouth || Current == EVtuberCalibrationStep::Smile || Current == EVtuberCalibrationStep::ClosedEyes)
        {
            FVtuberThresholdCurve Test;
            if (!FitRange(Steps[NeutralStep].Values.FindChecked(Channel), Values, Test, Error))
            { bCollecting = false; Error = CalChannelLabel(Channel) + TEXT("：") + Error; return false; }
        }
    }
    Sample.bComplete = true; bCollecting = false;
    return true;
}

bool FVtuberCalibrationSession::BuildRanges(TMap<FName, FVtuberThresholdCurve>& Ranges, FString& Error) const
{
    Error.Reset();
    if (!IsReady()) { Error = TEXT("请先完成全部记录步骤。"); return false; }
    TMap<FName, FVtuberThresholdCurve> Proposed;
    for (int32 ActiveStep : {1, 2, 4})
        for (FName Channel : RequiredChannels(static_cast<EVtuberCalibrationStep>(ActiveStep)))
        {
            FVtuberThresholdCurve Range;
            if (!FitRange(Steps[ActiveStep == 4 ? 3 : 0].Values.FindChecked(Channel), Steps[ActiveStep].Values.FindChecked(Channel), Range, Error)) return false;
            Proposed.Add(Channel, Range);
        }
    Ranges = MoveTemp(Proposed);
    return true;
}

bool SVtuberCalibrationWizard::ApplyAndSave(UVtuberRigComponent& Target, const TArray<FVtuberCalibrationChange>& Changes, FString& Error)
{
    Error.Reset();
    const auto CurrentMappings = Target.GetMappings();
    const auto Available = Target.GetAvailableMorphs();
    int32 Count = 0;
    TSet<FName> Seen;
    for (const auto& Change : Changes) if (Change.bApply)
    {
        const auto* Existing = CurrentMappings.FindByPredicate([&](const auto& M) { return M.Channel == Change.Channel; });
        if (!Existing || Seen.Contains(Change.Channel) || Change.AfterMapping.Channel != Change.Channel
            || !CalSameMapping(*Existing, Change.BeforeMapping)
            || !CalSameResponse(Target.GetChannelResponse(Change.Channel), Change.BeforeResponse)
            || (!Change.AfterMapping.Morph.IsNone() && !Available.Contains(Change.AfterMapping.Morph)))
        { Error = TEXT("模型或映射已更改，请返回重新生成校准预览。"); return false; }
        Seen.Add(Change.Channel);
        ++Count;
    }
    if (Count == 0) { Error = TEXT("请至少选择一个要应用的通道。"); return false; }
    auto Restore = [&]
    {
        for (const auto& Change : Changes) if (Change.bApply)
        {
            Target.SetMapping(Change.BeforeMapping);
            Target.SetChannelResponse(Change.Channel, Change.BeforeResponse);
        }
    };
    for (const auto& Change : Changes) if (Change.bApply)
        if (!Target.SetMapping(Change.AfterMapping) || !Target.SetChannelResponse(Change.Channel, Change.AfterResponse))
        { Restore(); Error = TEXT("校准设置未通过验证，已恢复原设置。"); return false; }
    if (!Target.SaveProfile(Error))
    {
        Restore();
        Error = TEXT("保存失败，已恢复原设置：") + Error;
        return false;
    }
    return true;
}

void SVtuberCalibrationWizard::Construct(const FArguments& Args, UVtuberRigComponent* InRig)
{
    Rig = InRig; SnapshotSource = Args._GetSnapshot; TrackingSource = Args._IsTracking; Finished = Args._OnFinished;
    ProgressStyle = FCoreStyle::Get().GetWidgetStyle<FProgressBarStyle>(TEXT("ProgressBar"));
    ProgressStyle.SetBackgroundImage(FSlateRoundedBoxBrush(CalField, 3.f))
        .SetFillImage(FSlateRoundedBoxBrush(CalAccent, 3.f));
    ChildSlot [SNew(SBox).MinDesiredWidth(600).MaxDesiredWidth(790)
        [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(FLinearColor::White).Padding(20)
            [SAssignNew(Body, SVerticalBox)]]];
    Status = TEXT("按提示记录表情。完成前不会修改映射。");
    LastTickTime = FPlatformTime::Seconds();
    RebuildBody();
}

bool SVtuberCalibrationWizard::CanRecord() const
{
    if (!Rig.IsValid() || !Rig->HasTracking() || !TrackingSource.Get(false) || !SnapshotSource.IsSet() || bApplied) return false;
    const auto Frame = SnapshotSource.Get();
    const double Now = FPlatformTime::Seconds();
    return Frame.bFaceTracked && FMath::IsFinite(Frame.TimestampSeconds) && Frame.TimestampSeconds > 0
        && Frame.TimestampSeconds <= Now + .05 && Now - Frame.TimestampSeconds <= .5;
}

void SVtuberCalibrationWizard::Tick(const FGeometry& Geometry, double CurrentTime, float DeltaTime)
{
    SCompoundWidget::Tick(Geometry, CurrentTime, DeltaTime);
    const double Now = FPlatformTime::Seconds();
    if ((bPreparing || Session.IsCollecting()) && Now - LastTickTime > .5)
    {
        bPreparing = false; Session.RewindTo(Step);
        Status = TEXT("记录已中断，本步记录已清除。请重新记录。");
    }
    LastTickTime = Now;
    if (bPreparing)
    {
        if (!CanRecord())
        {
            bPreparing = false; Session.RewindTo(Step);
            Status = TEXT("面部追踪已暂停，请恢复追踪后重新记录。");
        }
        else if (Now < PrepareUntil)
            Status = FString::Printf(TEXT("准备中，%.1f秒后开始记录。请保持本步表情。"), PrepareUntil - Now);
        else
        {
            bPreparing = false;
            Session.BeginStep(Step, SnapshotSource.Get().TimestampSeconds);
            Status = TEXT("正在记录，请保持当前表情稳定。");
        }
        return;
    }
    if (!Session.IsCollecting()) return;
    const auto Frame = SnapshotSource.Get(FVtuberTrackingSnapshot());
    TMap<FName, float> Inputs;
    if (Rig.IsValid()) for (FName Channel : FVtuberCalibrationSession::RequiredChannels(Step))
        Inputs.Add(Channel, Rig->GetChannelInput(Channel));
    if (!Session.PushFrame(Frame, Rig.IsValid() && Rig->HasTracking() && TrackingSource.Get(false), Now, Inputs, Status)) return;
    if (Session.GetProgress() >= 1.f)
    {
        FString Error;
        Status = Session.CompleteStep(Error) ? TEXT("本步记录完成。可以进入下一步，或重新记录。") : Error;
    }
}

FText SVtuberCalibrationWizard::StepTitle() const
{
    static const TCHAR* Titles[]{TEXT("放松闭嘴"), TEXT("舒适张嘴"), TEXT("嘴角上扬"), TEXT("自然睁眼"), TEXT("双眼闭合"), TEXT("检查校准建议")};
    return FText::FromString(Titles[CalIndex(Step)]);
}
FText SVtuberCalibrationWizard::StepInstruction() const
{
    static const TCHAR* Instructions[]{
        TEXT("双唇自然合拢，放松嘴角。准备好后开始记录，并保持至少1秒。"),
        TEXT("张嘴到平时说话时舒适的幅度，无需张到极限。保持动作后记录。"),
        TEXT("闭嘴并自然微笑，让两侧嘴角上扬。保持笑容后记录。"),
        TEXT("恢复放松表情，正常睁眼并看向镜头。无需睁大眼睛。"),
        TEXT("自然闭合双眼并保持，不要用力挤眼。记录完成后再睁眼。"),
        TEXT("勾选要应用的通道，检查模型表情目标。应用前可以返回重录。")};
    return FText::FromString(Instructions[CalIndex(Step)]);
}

void SVtuberCalibrationWizard::RebuildBody()
{
    Body->ClearChildren();
    Body->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
        [SNew(STextBlock).Text(StepTitle()).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 20)).ColorAndOpacity(CalInk)];
    Body->AddSlot().AutoHeight().Padding(0, 0, 0, 18)
        [CalLabel(Step == EVtuberCalibrationStep::Review ? TEXT("记录已完成") : FString::Printf(TEXT("步骤 %d / 5"), CalIndex(Step) + 1), 12, CalMuted)];
    Body->AddSlot().AutoHeight().Padding(0, 0, 0, 16)
        [SNew(STextBlock).Text(StepInstruction()).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 14)).ColorAndOpacity(CalInk).AutoWrapText(true)];
    if (Step == EVtuberCalibrationStep::Review) BuildReview();
    else
    {
        Body->AddSlot().AutoHeight().Padding(0, 0, 0, 12)
            [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(CalField).Padding(16)
                [SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
                        [SNew(STextBlock).Text_Lambda([this] { return FText::FromString(FString::Printf(TEXT("已记录 %.1f 秒  /  %d 帧"), Session.GetRecordedSeconds(), Session.GetFrameCount())); })
                            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 16)).ColorAndOpacity(CalInk)]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
                        [SNew(SBox).HeightOverride(6)[SNew(SProgressBar).Style(&ProgressStyle).FillColorAndOpacity(CalAccent)
                            .Percent_Lambda([this]() -> TOptional<float> { return Session.GetProgress(); })]]
                    + SVerticalBox::Slot().AutoHeight()
                        [SNew(STextBlock).Text_Lambda([this] { return FText::FromString(CanRecord() ? TEXT("面部追踪有效") : TEXT("请先加载模型并保持面部追踪有效")); })
                            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 13)).ColorAndOpacity(CalMuted)]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
                        [CalLabel(TEXT("开始后预留1.5秒准备时间，随后保持表情至少1秒。"), 12, CalMuted)]]];
        Body->AddSlot().AutoHeight().Padding(0, 0, 0, 12)
            [SNew(SButton).ButtonStyle(&FMmdUiStyle::Get().Primary).HAlign(HAlign_Center).ContentPadding(FMargin(16, 9))
                .IsEnabled_Lambda([this] { return CanRecord() && !bPreparing && !Session.IsCollecting(); }).OnClicked(this, &SVtuberCalibrationWizard::Record)
                [SNew(STextBlock).Text_Lambda([this] { return FText::FromString(Session.GetFrameCount() > 0 ? TEXT("重新记录本步") : TEXT("开始记录")); })
                    .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 14))]];
    }
    Body->AddSlot().AutoHeight().Padding(0, 6, 0, 14)
        [SNew(STextBlock).Text_Lambda([this] { return FText::FromString(Status); }).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 13))
            .ColorAndOpacity(CalMuted).AutoWrapText(true)];
    Body->AddSlot().AutoHeight()
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth()
                [SNew(SButton).ButtonStyle(&FMmdUiStyle::Get().Button).HAlign(HAlign_Center).ContentPadding(FMargin(16, 9))
                    .OnClicked(this, &SVtuberCalibrationWizard::Cancel)
                    [SNew(STextBlock).Text_Lambda([this] { return FText::FromString(bApplied ? TEXT("关闭") : TEXT("取消")); })
                        .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 14)).ColorAndOpacity(CalInk)]]
            + SHorizontalBox::Slot().FillWidth(1)[SNew(SBox)]
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
                [SNew(SButton).ButtonStyle(&FMmdUiStyle::Get().Button).HAlign(HAlign_Center).ContentPadding(FMargin(16, 9))
                    .IsEnabled_Lambda([this] { return CalIndex(Step) > 0 && !bApplied; }).OnClicked(this, &SVtuberCalibrationWizard::Back)
                    [SNew(STextBlock).Text(FText::FromString(TEXT("上一步")))
                        .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 14)).ColorAndOpacity(CalInk)]]
            + SHorizontalBox::Slot().AutoWidth()
                [SNew(SButton).ButtonStyle(&FMmdUiStyle::Get().Primary).HAlign(HAlign_Center).ContentPadding(FMargin(16, 9))
                    .IsEnabled_Lambda([this] { return !bApplied && (Step == EVtuberCalibrationStep::Review ? Changes.Num() > 0 : Session.IsComplete(Step) && CanRecord()); })
                    .OnClicked_Lambda([this] { return Step == EVtuberCalibrationStep::Review ? Apply() : Next(); })
                    [SNew(STextBlock).Text(FText::FromString(Step == EVtuberCalibrationStep::Review ? TEXT("应用并保存") : TEXT("下一步")))
                        .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 14))]]];
}

void SVtuberCalibrationWizard::BuildReview()
{
    Changes.Reset(); MorphOptions.Reset(); MorphOptions.Add(MakeShared<FString>(CalNoMorph));
    if (!Rig.IsValid()) { Status = TEXT("模型已不可用，请重新打开校准。"); return; }
    TMap<FName, FVtuberThresholdCurve> Ranges;
    if (!Session.BuildRanges(Ranges, Status)) return;
    const auto Available = Rig->GetAvailableMorphs();
    for (FName Morph : Available) MorphOptions.Add(MakeShared<FString>(Morph.ToString()));
    const FName Order[]{TEXT("jawOpen"), TEXT("mouthSmileLeft"), TEXT("mouthSmileRight"), TEXT("eyeBlinkLeft"), TEXT("eyeBlinkRight")};
    TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
    for (FName Channel : Order)
    {
        const auto* Mapping = Rig->GetMappings().FindByPredicate([&](const auto& M) { return M.Channel == Channel; });
        if (!Mapping) continue;
        FVtuberCalibrationChange Change;
        Change.Channel = Channel; Change.BeforeMapping = Change.AfterMapping = *Mapping;
        Change.BeforeResponse = Change.AfterResponse = Rig->GetChannelResponse(Channel);
        Change.AfterResponse.Lower = Ranges.FindChecked(Channel).Start;
        Change.AfterResponse.Upper = Ranges.FindChecked(Channel).Full;
        Change.AfterResponse.bEnabled = true; Change.AfterResponse.bInvert = false;
        Change.AfterResponse.bBipolar = false; Change.AfterResponse.Sensitivity = 1;
        Change.AfterResponse.Curvature = 0; Change.AfterResponse.Precision = 0;
        Change.AfterMapping.Curve = FVtuberThresholdCurve();
        Change.AfterMapping.Maximum = Change.AfterMapping.Sensitivity = 1;
        Change.AfterMapping.bEnabled = true; Change.AfterMapping.bInvert = false;
        if (!Available.Contains(Change.AfterMapping.Morph)) Change.AfterMapping.Morph = CalMatchMorph(Channel, Available);
        const int32 I = Changes.Add(Change);
        Rows->AddSlot().AutoHeight().Padding(0, 0, 0, 10)
            [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(CalField).Padding(12)
                [SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                        [SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
                                [SNew(SCheckBox).Style(&FMmdUiStyle::Get().Check)
                                    .IsEnabled_Lambda([this] { return !bApplied; })
                                    .IsChecked_Lambda([this, I] { return Changes.IsValidIndex(I) && Changes[I].bApply ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                                    .OnCheckStateChanged_Lambda([this, I](ECheckBoxState State) { if (Changes.IsValidIndex(I)) Changes[I].bApply = State == ECheckBoxState::Checked; })]
                            + SHorizontalBox::Slot().FillWidth(1)[CalLabel(CalChannelLabel(Channel))]
                            + SHorizontalBox::Slot().AutoWidth()
                                [CalLabel(FString::Printf(TEXT("原 %.3f–%.3f，建议 %.3f–%.3f"), Change.BeforeResponse.Lower, Change.BeforeResponse.Upper,
                                    Change.AfterResponse.Lower, Change.AfterResponse.Upper), 13, CalMuted)]]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
                        [SNew(SComboBox<TSharedPtr<FString>>).ComboBoxStyle(&FMmdUiStyle::Get().Combo).ItemStyle(&FMmdUiStyle::Get().Row).OptionsSource(&MorphOptions)
                            .IsEnabled_Lambda([this] { return !bApplied; })
                            .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) { return CalLabel(Item.IsValid() ? *Item : FString()); })
                            .OnSelectionChanged_Lambda([this, I](TSharedPtr<FString> Item, ESelectInfo::Type)
                            { if (Changes.IsValidIndex(I) && Item.IsValid()) Changes[I].AfterMapping.Morph = *Item == CalNoMorph ? NAME_None : FName(**Item); })
                            [SNew(STextBlock).Text_Lambda([this, I] { return FText::FromString(!Changes.IsValidIndex(I) || Changes[I].AfterMapping.Morph.IsNone() ? FString(CalNoMorph) : Changes[I].AfterMapping.Morph.ToString()); })
                                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 13)).ColorAndOpacity(CalInk)]]]];
    }
    Body->AddSlot().MaxHeight(360).Padding(0, 0, 0, 12)[SNew(SScrollBox) + SScrollBox::Slot()[Rows]];
    Body->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
        [CalLabel(TEXT("所选通道将启用线性校准范围，灵敏度设为1并重置附加曲线。未映射的通道只保存输入范围，可在上方选择模型表情。"), 12, CalMuted)];
    Body->AddSlot().AutoHeight()[CalLabel(TEXT("睁大眼、眯眼和其他未采集通道保持当前设置。"), 12, CalMuted)];
    if (!Rig->GetSettings().bExpressionsEnabled)
        Body->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[CalLabel(TEXT("表情驱动当前已关闭；校准后可在捕捉设置中开启。"), 12, CalMuted)];
    Status = TEXT("尚未应用。取消会保留当前全部设置。");
}

FReply SVtuberCalibrationWizard::Record()
{
    if (!CanRecord()) { Status = TEXT("请先恢复面部追踪。"); return FReply::Handled(); }
    Session.RewindTo(Step);
    bPreparing = true; LastTickTime = FPlatformTime::Seconds(); PrepareUntil = LastTickTime + 1.5;
    Status = TEXT("准备中，1.5秒后开始记录。请保持本步表情。");
    return FReply::Handled();
}
FReply SVtuberCalibrationWizard::Next()
{
    if (!Session.IsComplete(Step) || !CanRecord()) { Status = TEXT("请先完成有效的面部记录。"); return FReply::Handled(); }
    Step = static_cast<EVtuberCalibrationStep>(CalIndex(Step) + 1);
    if (Step != EVtuberCalibrationStep::Review) Session.RewindTo(Step);
    Status = TEXT("准备好后开始记录。");
    RebuildBody();
    return FReply::Handled();
}
FReply SVtuberCalibrationWizard::Back()
{
    if (CalIndex(Step) > 0 && !bApplied)
    {
        bPreparing = false;
        Step = static_cast<EVtuberCalibrationStep>(CalIndex(Step) - 1);
        Session.RewindTo(Step); Changes.Reset();
        Status = TEXT("本步及后续记录已清除，请重新记录。"); RebuildBody();
    }
    return FReply::Handled();
}
FReply SVtuberCalibrationWizard::Cancel()
{
    bPreparing = false; Session.Reset(); Changes.Reset();
    Finished.ExecuteIfBound();
    return FReply::Handled();
}
FReply SVtuberCalibrationWizard::Apply()
{
    if (bApplied || !Rig.IsValid()) { Status = TEXT("模型已不可用，请重新打开校准。"); return FReply::Handled(); }
    if (ApplyAndSave(*Rig.Get(), Changes, Status))
    {
        bApplied = true; Status = TEXT("校准已应用并保存。"); Finished.ExecuteIfBound();
    }
    return FReply::Handled();
}
