#include "SVtuberRigPanel.h"
#include "Mmd/MmdUiStyle.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Input/Reply.h"
#include "Rendering/DrawElementTypes.h"
#include "Styling/CoreStyle.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
    const FLinearColor Ink = FMmdUiStyle::Colors().Text, Muted = FMmdUiStyle::Colors().Muted;
    const FLinearColor Accent = FMmdUiStyle::Colors().Accent, Field = FMmdUiStyle::Colors().Field;
    const FLinearColor Grid = FMmdUiStyle::Colors().Grid, Warning = FMmdUiStyle::Colors().Warning;
    const TCHAR* NoneLabel = TEXT("（未映射）");
    const FName HeadAxes[] = {TEXT("headPitch"), TEXT("headYaw"), TEXT("headRoll")};
    const FName BodyGroups[] = {TEXT("left_arm"), TEXT("right_arm"), TEXT("left_hand"), TEXT("right_hand")};

    TSharedRef<STextBlock> Label(const FString& Value, int32 Size = 14, FLinearColor Color = Ink)
    {
        return SNew(STextBlock).Text(FText::FromString(Value)).ColorAndOpacity(Color)
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", Size)).AutoWrapText(true);
    }

    class SResponseCurve : public SLeafWidget
    {
    public:
        SLATE_BEGIN_ARGS(SResponseCurve) {} SLATE_END_ARGS()
        void Construct(const FArguments&, TFunction<float(float)> InEvaluate,
            TFunction<float()> InInput, TFunction<float()> InOutput, TFunction<bool()> InBipolar,
            TFunction<bool()> InTracked)
        {
            Evaluate = MoveTemp(InEvaluate);
            Input = MoveTemp(InInput);
            Output = MoveTemp(InOutput);
            Bipolar = MoveTemp(InBipolar);
            Tracked = MoveTemp(InTracked);
        }
        virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(320, 150); }
        virtual void Tick(const FGeometry& Geometry, double Time, float DeltaTime) override
        {
            SLeafWidget::Tick(Geometry, Time, DeltaTime);
            Invalidate(EInvalidateWidgetReason::Paint);
        }
        virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&,
            FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle&, bool) const override
        {
            FSlateDrawElement::MakeBox(Elements, Layer, G.ToPaintGeometry(),
                FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, Field);
            const float InputFloor = Bipolar() ? -1.f : 0.f;
            const float OutputFloor = InputFloor;
            const FVector2D Size = G.GetLocalSize();
            const auto Point = [Size](float X, float Y)
            {
                return FVector2D(12 + X * (Size.X - 24), Size.Y - 12 - Y * (Size.Y - 24));
            };
            for (int32 I = 0; I <= 4; ++I)
            {
                const float T = I / 4.f;
                FSlateDrawElement::MakeLines(Elements, Layer + 1, G.ToPaintGeometry(),
                    TArray<FVector2D>{Point(T, 0), Point(T, 1)}, ESlateDrawEffect::None, Grid);
                FSlateDrawElement::MakeLines(Elements, Layer + 1, G.ToPaintGeometry(),
                    TArray<FVector2D>{Point(0, T), Point(1, T)}, ESlateDrawEffect::None, Grid);
            }
            TArray<FVector2D> Curve;
            Curve.Reserve(101);
            for (int32 I = 0; I <= 100; ++I)
            {
                const float X = I / 100.f;
                const float Sample = Evaluate(FMath::Lerp(InputFloor, 1.f, X));
                Curve.Add(Point(X, FMath::Clamp((Sample - OutputFloor) / (1.f - OutputFloor), 0.f, 1.f)));
            }
            FSlateDrawElement::MakeLines(Elements, Layer + 2, G.ToPaintGeometry(), Curve,
                ESlateDrawEffect::None, Ink, true, 2.f);
            if (Tracked())
            {
                const float LiveInput = FMath::Clamp((Input() - InputFloor) / (1.f - InputFloor), 0.f, 1.f);
                const float LiveOutput = FMath::Clamp((Output() - OutputFloor) / (1.f - OutputFloor), 0.f, 1.f);
                const FVector2D Dot = Point(LiveInput, LiveOutput);
                FSlateDrawElement::MakeBox(Elements, Layer + 3,
                    G.ToPaintGeometry(FVector2D(9, 9), FSlateLayoutTransform(Dot - FVector2D(4.5, 4.5))),
                    FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, Accent);
            }
            return Layer + 3;
        }
    private:
        TFunction<float(float)> Evaluate;
        TFunction<float()> Input, Output;
        TFunction<bool()> Bipolar, Tracked;
    };
}

SVtuberRigPanel::~SVtuberRigPanel() { FlushSave(); }

void SVtuberRigPanel::Tick(const FGeometry& Geometry, double Time, float DeltaTime)
{
    SCompoundWidget::Tick(Geometry, Time, DeltaTime);
    if (bSavePending && FPlatformTime::Seconds() - LastChangeTime >= .35) FlushSave();
}

void SVtuberRigPanel::MarkDirty()
{
    bSavePending = true;
    LastChangeTime = FPlatformTime::Seconds();
    SaveStatus = TEXT("正在保存…");
}

void SVtuberRigPanel::FlushSave()
{
    if (!bSavePending || !Rig.IsValid()) return;
    FString Error;
    const bool bSaved = Rig->SaveProfile(Error);
    SaveStatus = bSaved ? TEXT("已保存") : TEXT("保存失败：") + Error;
    bSavePending = false;
}

bool SVtuberRigPanel::IsHeadAxis(FName Channel) const
{
    for (FName Axis : HeadAxes) if (Channel == Axis) return true;
    return false;
}

bool SVtuberRigPanel::IsBodyGroup(FName Channel) const
{
    for (FName Group : BodyGroups) if (Channel == Group) return true;
    return false;
}

TArray<FName> SVtuberRigPanel::Members(FName Channel) const
{
    if (IsHeadAxis(Channel))
        return {FName(*(Channel.ToString() + TEXT("Positive"))), FName(*(Channel.ToString() + TEXT("Negative")))};
    if (!IsBodyGroup(Channel)) return {Channel};
    const FString Id = Channel.ToString();
    const bool bLeft = Id.StartsWith(TEXT("left"));
    const bool bArm = Id.EndsWith(TEXT("arm"));
    const FString Suffix = bLeft ? TEXT("_l") : TEXT("_r");
    TArray<FName> Result;
    if (bArm)
    {
        for (FName Bone : UVtuberRigComponent::GetBodyTrackingChannels(false))
            if (Bone.ToString().EndsWith(Suffix)
                && Bone != FName(*(FString(TEXT("hand")) + Suffix))
                && Bone != FName(*(FString(TEXT("shoulder")) + Suffix))) Result.Add(Bone);
    }
    else
    {
        Result.Add(FName(*(FString(TEXT("hand")) + Suffix)));
        for (FName Bone : UVtuberRigComponent::GetBodyTrackingChannels(true))
            if (Bone.ToString().EndsWith(Suffix)) Result.Add(Bone);
    }
    return Result;
}

FName SVtuberRigPanel::Representative(FName Channel) const
{
    const TArray<FName> Targets = Members(Channel);
    if (IsBodyGroup(Channel))
        for (FName Target : Targets) if (AvailableBones.Contains(Rig.IsValid() ? Rig->GetBodyBone(Target) : NAME_None)) return Target;
    return Targets.IsEmpty() ? NAME_None : Targets[0];
}

FVtuberChannelResponse SVtuberRigPanel::Response(FName Channel) const
{
    return Rig.IsValid() ? Rig->GetChannelResponse(Representative(Channel)) : FVtuberChannelResponse();
}

bool SVtuberRigPanel::ApplyResponse(FName Channel, const FVtuberChannelResponse& Value)
{
    if (!Rig.IsValid()) return false;
    bool bChanged = false;
    for (FName Member : Members(Channel)) bChanged |= Rig->SetChannelResponse(Member, Value);
    if (bChanged) MarkDirty();
    return bChanged;
}

float SVtuberRigPanel::EvaluateResponse(FName Channel, float Raw) const
{
    if (!Rig.IsValid()) return 0;
    if (IsHeadAxis(Channel))
    {
        const TArray<FName> Axis = Members(Channel);
        return Raw >= 0 ? Rig->EvaluateChannelResponse(Axis[0], Raw)
            : -Rig->EvaluateChannelResponse(Axis[1], -Raw);
    }
    return Rig->EvaluateChannelResponse(Representative(Channel), Raw);
}

float SVtuberRigPanel::ChannelInput(FName Channel) const
{
    if (!Rig.IsValid()) return 0;
    if (IsHeadAxis(Channel))
    {
        const TArray<FName> Axis = Members(Channel);
        return Rig->GetChannelInput(Axis[0]) - Rig->GetChannelInput(Axis[1]);
    }
    return Rig->GetChannelInput(Representative(Channel));
}

float SVtuberRigPanel::ChannelOutput(FName Channel) const
{
    if (!Rig.IsValid()) return 0;
    if (IsHeadAxis(Channel))
    {
        const TArray<FName> Axis = Members(Channel);
        return Rig->GetChannelOutput(Axis[0]) - Rig->GetChannelOutput(Axis[1]);
    }
    return Rig->GetChannelOutput(Representative(Channel));
}

bool SVtuberRigPanel::ChannelTracked(FName Channel) const
{
    if (!Rig.IsValid()) return false;
    if (IsHeadAxis(Channel) || (!IsBodyGroup(Channel) && !BodyChannels.Contains(Channel))) return Rig->HasTracking();
    return Rig->IsBodyChannelTracked(Representative(Channel));
}

FVtuberMorphMapping SVtuberRigPanel::MorphMapping(FName Channel) const
{
    if (Rig.IsValid())
        for (const FVtuberMorphMapping& Mapping : Rig->GetMappings())
            if (Mapping.Channel == Channel) return Mapping;
    FVtuberMorphMapping Empty;
    Empty.Channel = Channel;
    return Empty;
}

bool SVtuberRigPanel::HasTarget(FName Channel) const
{
    if (!Rig.IsValid()) return false;
    if (IsHeadAxis(Channel)) return AvailableBones.Contains(Rig->GetHeadBone());
    if (IsBodyGroup(Channel))
    {
        const bool bLeft = Channel.ToString().StartsWith(TEXT("left"));
        const FString Suffix = bLeft ? TEXT("_l") : TEXT("_r");
        const TCHAR* Required[] = {TEXT("upperarm"), TEXT("lowerarm")};
        if (Channel.ToString().EndsWith(TEXT("arm")))
        {
            for (const TCHAR* Bone : Required)
                if (!AvailableBones.Contains(Rig->GetBodyBone(FName(*(FString(Bone) + Suffix))))) return false;
            return true;
        }
        for (const TCHAR* Bone : {TEXT("hand"), TEXT("index1"), TEXT("middle1"), TEXT("pinky1")})
            if (!AvailableBones.Contains(Rig->GetBodyBone(FName(*(FString(Bone) + Suffix))))) return false;
        return true;
    }
    if (BodyChannels.Contains(Channel)) return AvailableBones.Contains(Rig->GetBodyBone(Channel));
    return AvailableMorphs.Contains(MorphMapping(Channel).Morph);
}

FString SVtuberRigPanel::ChannelLabel(FName Channel) const
{
    FString Key = Channel.ToString();
    if (Channel == TEXT("left_arm")) return TEXT("左手臂");
    if (Channel == TEXT("right_arm")) return TEXT("右手臂");
    if (Channel == TEXT("left_hand")) return TEXT("左手与手指");
    if (Channel == TEXT("right_hand")) return TEXT("右手与手指");
    if (Key.StartsWith(TEXT("head")))
    {
        const FString Direction = IsHeadAxis(Channel) ? FString() : Key.EndsWith(TEXT("Positive")) ? TEXT("＋") : TEXT("－");
        if (Key.Contains(TEXT("Yaw"))) return TEXT("头部转向") + Direction;
        if (Key.Contains(TEXT("Pitch"))) return TEXT("头部俯仰") + Direction;
        return TEXT("头部侧倾") + Direction;
    }
    FString Side;
    if (Key.EndsWith(TEXT("_l"))) { Side = TEXT("左"); Key.LeftChopInline(2); }
    else if (Key.EndsWith(TEXT("_r"))) { Side = TEXT("右"); Key.LeftChopInline(2); }
    if (!Side.IsEmpty())
    {
        const TPair<const TCHAR*, const TCHAR*> Bones[] = {
            {TEXT("shoulder"), TEXT("肩")}, {TEXT("upperarm"), TEXT("上臂")},
            {TEXT("lowerarm"), TEXT("前臂")}, {TEXT("hand"), TEXT("手腕")}
        };
        for (const auto& Bone : Bones) if (Key == Bone.Key) return Side + Bone.Value;
        const TPair<const TCHAR*, const TCHAR*> Fingers[] = {
            {TEXT("thumb"), TEXT("拇指")}, {TEXT("index"), TEXT("食指")},
            {TEXT("middle"), TEXT("中指")}, {TEXT("ring"), TEXT("无名指")},
            {TEXT("pinky"), TEXT("小指")}
        };
        for (const auto& Finger : Fingers)
            if (Key.StartsWith(Finger.Key)) return Side + Finger.Value + TEXT("第") + Key.Right(1) + TEXT("节");
    }
    if (Key == TEXT("spine")) return TEXT("躯干");
    if (Key == TEXT("chest")) return TEXT("胸部");
    if (Key == TEXT("neck")) return TEXT("颈部");
    if (Key == TEXT("_neutral")) return TEXT("中立");
    if (Key.EndsWith(TEXT("Left"))) { Side = TEXT("左"); Key.LeftChopInline(4); }
    else if (Key.EndsWith(TEXT("Right"))) { Side = TEXT("右"); Key.LeftChopInline(5); }
    const TPair<const TCHAR*, const TCHAR*> Face[] = {
        {TEXT("browDown"), TEXT("眉下压")}, {TEXT("browInnerUp"), TEXT("眉心上抬")},
        {TEXT("browOuterUp"), TEXT("眉尾上抬")}, {TEXT("cheekPuff"), TEXT("鼓腮")},
        {TEXT("cheekSquint"), TEXT("颧部上抬")}, {TEXT("eyeBlink"), TEXT("眼眨眼")},
        {TEXT("eyeLookDown"), TEXT("眼下看")}, {TEXT("eyeLookIn"), TEXT("眼内看")},
        {TEXT("eyeLookOut"), TEXT("眼外看")}, {TEXT("eyeLookUp"), TEXT("眼上看")},
        {TEXT("eyeSquint"), TEXT("眼眯眼")}, {TEXT("eyeWide"), TEXT("眼睁大")},
        {TEXT("jawForward"), TEXT("下巴前伸")}, {TEXT("jawLeft"), TEXT("下巴左移")},
        {TEXT("jawOpen"), TEXT("嘴部开闭")}, {TEXT("jawRight"), TEXT("下巴右移")},
        {TEXT("mouthClose"), TEXT("闭嘴")}, {TEXT("mouthDimple"), TEXT("口角收紧")},
        {TEXT("mouthFrown"), TEXT("嘴角下压")}, {TEXT("mouthFunnel"), TEXT("嘴部成圆")},
        {TEXT("mouthLeft"), TEXT("嘴左移")}, {TEXT("mouthLowerDown"), TEXT("下唇下压")},
        {TEXT("mouthPress"), TEXT("唇压紧")}, {TEXT("mouthPucker"), TEXT("撅嘴")},
        {TEXT("mouthRight"), TEXT("嘴右移")}, {TEXT("mouthRollLower"), TEXT("下唇内卷")},
        {TEXT("mouthRollUpper"), TEXT("上唇内卷")}, {TEXT("mouthShrugLower"), TEXT("下唇上提")},
        {TEXT("mouthShrugUpper"), TEXT("上唇下压")}, {TEXT("mouthSmile"), TEXT("微笑")},
        {TEXT("mouthStretch"), TEXT("嘴部拉伸")}, {TEXT("mouthUpperUp"), TEXT("上唇上提")},
        {TEXT("noseSneer"), TEXT("鼻翼皱缩")}
    };
    for (const auto& Item : Face) if (Key == Item.Key) return Side + Item.Value;
    return Channel.ToString();
}

float SVtuberRigPanel::ResponseValue(FName FieldName) const
{
    if (!Rig.IsValid()) return 0.f;
    const auto R = Response(ActiveChannel());
    if (FieldName == TEXT("smoothing")) return R.SmoothingSeconds;
    if (FieldName == TEXT("sensitivity")) return R.Sensitivity;
    if (FieldName == TEXT("curvature")) return R.Curvature;
    if (FieldName == TEXT("precision")) return float(R.Precision);
    if (FieldName == TEXT("lower")) return R.Lower;
    return R.Upper;
}

void SVtuberRigPanel::SetResponseValue(FName FieldName, float Value)
{
    if (!Rig.IsValid() || !HasTarget(ActiveChannel())) return;
    auto R = Response(ActiveChannel());
    if (FieldName == TEXT("smoothing")) R.SmoothingSeconds = Value;
    else if (FieldName == TEXT("sensitivity")) R.Sensitivity = Value;
    else if (FieldName == TEXT("curvature")) R.Curvature = Value;
    else if (FieldName == TEXT("precision")) R.Precision = FMath::RoundToInt(Value);
    else if (FieldName == TEXT("lower")) R.Lower = FMath::Clamp(Value, R.bBipolar ? -1.f : 0.f, R.Upper - .01f);
    else R.Upper = FMath::Clamp(Value, R.Lower + .01f, 1.f);
    ApplyResponse(ActiveChannel(), R);
}

void SVtuberRigPanel::Construct(const FArguments& Args, UVtuberRigComponent* InRig)
{
    Rig = InRig;
    SelectedTab = FMath::Clamp(Args._InitialTab, 0, 2);
    const auto& Theme = FMmdUiStyle::Get();
    ButtonStyle = Theme.Button; ComboStyle = Theme.Combo; SpinStyle = Theme.Spin;
    SliderStyle = Theme.Slider; CheckStyle = Theme.Check; RowStyle = Theme.Row;
    BoneOptions.Add(MakeShared<FString>(NoneLabel));
    MorphOptions.Add(MakeShared<FString>(NoneLabel));
    if (Rig.IsValid())
    {
        for (FName Name : Rig->GetAvailableBones()) { AvailableBones.Add(Name); BoneOptions.Add(MakeShared<FString>(Name.ToString())); }
        for (FName Name : Rig->GetAvailableMorphs()) { AvailableMorphs.Add(Name); MorphOptions.Add(MakeShared<FString>(Name.ToString())); }
    }
    for (FName Name : UVtuberRigComponent::GetBodyTrackingChannels(false)) BodyChannels.Add(Name);
    for (FName Name : UVtuberRigComponent::GetBodyTrackingChannels(true)) BodyChannels.Add(Name);
    const auto Choice = [](TSharedPtr<FString> Item) -> TSharedRef<SWidget>
    {
        return Label(Item.IsValid() ? *Item : FString());
    };
    const auto AddRow = [](const TSharedRef<SVerticalBox>& Box, const TSharedRef<SWidget>& Widget, float Bottom = 8.f)
    {
        Box->AddSlot().AutoHeight().Padding(0, 0, 0, Bottom)[Widget];
    };

    auto Tabs = SNew(SHorizontalBox);
    const TCHAR* TabNames[] = {TEXT("面部"), TEXT("手臂与手"), TEXT("选项")};
    for (int32 Index = 0; Index < 3; ++Index)
    {
        Tabs->AddSlot().FillWidth(1).Padding(0, 0, 8, 0)
            [SNew(SButton).ButtonStyle(&ButtonStyle).ContentPadding(FMargin(8, 7))
                .OnClicked_Lambda([this, Index] { SelectedTab = Index; return FReply::Handled(); })
                [SNew(STextBlock).Text(FText::FromString(TabNames[Index]))
                    .Justification(ETextJustify::Center)
                    .ColorAndOpacity_Lambda([this, Index] { return SelectedTab == Index ? Accent : Ink; })]];
    }

    auto FaceRows = SNew(SVerticalBox);
    auto BodyRows = SNew(SVerticalBox);
    const auto AddChannel = [this](const TSharedRef<SVerticalBox>& Rows, FName Channel, bool bFace)
    {
        Rows->AddSlot().AutoHeight().Padding(0, 0, 0, 2)
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1)
                    [SNew(SButton).ButtonStyle(&ButtonStyle).ContentPadding(FMargin(8, 6))
                        .OnClicked_Lambda([this, Channel, bFace]
                        {
                            if (bFace) FaceChannel = Channel; else BodyChannel = Channel;
                            return FReply::Handled();
                        })
                        [SNew(STextBlock).Text(FText::FromString(ChannelLabel(Channel)))
                            .ColorAndOpacity_Lambda([this, Channel]
                            {
                                return ActiveChannel() == Channel ? Accent : (HasTarget(Channel) ? Ink : Muted);
                            })]]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(7, 0)
                    [SNew(SCheckBox).Style(&CheckStyle).IsEnabled_Lambda([this, Channel] { return HasTarget(Channel); })
                        .IsChecked_Lambda([this, Channel]
                        {
                            return Rig.IsValid() && Response(Channel).bEnabled
                                ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                        })
                        .OnCheckStateChanged_Lambda([this, Channel](ECheckBoxState State)
                        {
                            if (!Rig.IsValid() || !HasTarget(Channel)) return;
                            auto Value = Response(Channel);
                            Value.bEnabled = State == ECheckBoxState::Checked;
                            ApplyResponse(Channel, Value);
                        })]];
    };
    AddRow(FaceRows, Label(TEXT("头部")), 5);
    for (FName Channel : HeadAxes) AddChannel(FaceRows, Channel, true);
    AddRow(FaceRows, Label(TEXT("表情")), 5);
    for (FName Channel : UVtuberRigComponent::GetTrackingChannels())
        if (Channel != TEXT("_neutral")) AddChannel(FaceRows, Channel, true);
    AddRow(BodyRows, Label(TEXT("动作组")), 5);
    for (FName Channel : BodyGroups) AddChannel(BodyRows, Channel, false);
    AddRow(BodyRows, Label(TEXT("高级：单骨骼映射")), 5);
    for (FName Channel : UVtuberRigComponent::GetBodyTrackingChannels(false)) AddChannel(BodyRows, Channel, false);
    AddRow(BodyRows, Label(TEXT("手与手指")), 5);
    for (FName Channel : UVtuberRigComponent::GetBodyTrackingChannels(true)) AddChannel(BodyRows, Channel, false);

    auto Details = SNew(SVerticalBox);
    AddRow(Details, SNew(STextBlock)
        .Text_Lambda([this] { return FText::FromString(ChannelLabel(ActiveChannel())); })
        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 21)).ColorAndOpacity(Accent), 4);
    AddRow(Details, SNew(STextBlock)
        .Text_Lambda([this]
        {
            if (!Rig.IsValid()) return FText::FromString(TEXT("请先导入角色。"));
            if (!HasTarget(ActiveChannel())) return FText::FromString(TEXT("当前模型缺少对应骨链或目标。可在高级列表中指定已有骨骼，再编辑响应。"));
            if (IsBodyGroup(ActiveChannel())) return FText::FromString(TEXT("组参数写入该侧全部关联关节；曲线与圆点显示首个已映射关节。可在高级列表中逐关节调整。"));
            if (IsHeadAxis(ActiveChannel())) return FText::FromString(TEXT("正反两个方向同时调整；曲线与圆点显示双向合成结果。"));
            return FText::FromString(TEXT("圆点表示当前输入与输出。"));
        }).ColorAndOpacity(Muted).AutoWrapText(true), 7);
    AddRow(Details, Label(TEXT("目标骨骼或表情"), 13, Muted), 4);
    AddRow(Details, SNew(SComboBox<TSharedPtr<FString>>)
        .Visibility_Lambda([this] { return (SelectedTab == 1 && !IsBodyGroup(ActiveChannel())) || IsHeadAxis(ActiveChannel()) ? EVisibility::Visible : EVisibility::Collapsed; })
        .OptionsSource(&BoneOptions).ComboBoxStyle(&ComboStyle).ItemStyle(&RowStyle)
        .OnGenerateWidget_Lambda(Choice)
        .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type)
        {
            if (!Rig.IsValid() || !Item) return;
            const FName Bone = *Item == NoneLabel ? NAME_None : FName(**Item);
            const bool bChanged = SelectedTab == 1
                ? Rig->SetBodyBone(ActiveChannel(), Bone) : Rig->SetHeadBone(Bone);
            if (bChanged) MarkDirty();
        })
        [SNew(STextBlock).Text_Lambda([this]
        {
            if (!Rig.IsValid()) return FText::FromString(NoneLabel);
            const FName Bone = SelectedTab == 1 ? Rig->GetBodyBone(ActiveChannel()) : Rig->GetHeadBone();
            return FText::FromString(Bone.IsNone() ? NoneLabel : Bone.ToString());
        }).ColorAndOpacity(Ink)], 6);
    AddRow(Details, SNew(SComboBox<TSharedPtr<FString>>)
        .Visibility_Lambda([this] { return SelectedTab == 0 && !IsHeadAxis(ActiveChannel()) ? EVisibility::Visible : EVisibility::Collapsed; })
        .OptionsSource(&MorphOptions).ComboBoxStyle(&ComboStyle).ItemStyle(&RowStyle)
        .OnGenerateWidget_Lambda(Choice)
        .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type)
        {
            if (!Rig.IsValid() || !Item) return;
            auto Mapping = MorphMapping(ActiveChannel());
            Mapping.Morph = *Item == NoneLabel ? NAME_None : FName(**Item);
            if (Rig->SetMapping(Mapping)) MarkDirty();
        })
        [SNew(STextBlock).Text_Lambda([this]
        {
            const FName Morph = MorphMapping(ActiveChannel()).Morph;
            return FText::FromString(Morph.IsNone() ? NoneLabel : Morph.ToString());
        }).ColorAndOpacity(Ink)], 8);
    AddRow(Details, SNew(SCheckBox).Style(&CheckStyle).IsEnabled_Lambda([this] { return HasTarget(ActiveChannel()); })
        .IsChecked_Lambda([this]
        {
            return Rig.IsValid() && Response(ActiveChannel()).bInvert
                ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
        })
        .OnCheckStateChanged_Lambda([this](ECheckBoxState State)
        {
            if (!Rig.IsValid() || !HasTarget(ActiveChannel())) return;
            auto Value = Response(ActiveChannel());
            Value.bInvert = State == ECheckBoxState::Checked;
            ApplyResponse(ActiveChannel(), Value);
        })[Label(TEXT("反转"))], 10);

    const auto AddResponseRow = [this, Details, AddRow](FName FieldName, const TCHAR* Caption, float Low, float High, float Delta)
    {
        const auto Minimum = [this, FieldName, Low]
        {
            if (!Rig.IsValid()) return Low;
            const auto Value = Response(ActiveChannel());
            if (FieldName == TEXT("lower") && Value.bBipolar) return -1.f;
            if (FieldName == TEXT("upper")) return Value.Lower + .01f;
            return Low;
        };
        AddRow(Details, SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [SNew(SBox).WidthOverride(96)
                    [SNew(STextBlock).ColorAndOpacity(Ink).Font(FCoreStyle::GetDefaultFontStyle("Regular", 13))
                        .Text_Lambda([this, FieldName, Caption]
                        {
                            const bool bEye = ActiveChannel() == TEXT("eyeBlinkLeft") || ActiveChannel() == TEXT("eyeBlinkRight");
                            return FText::FromString(bEye && FieldName == TEXT("lower") ? TEXT("睁眼阈值")
                                : bEye && FieldName == TEXT("upper") ? TEXT("闭眼阈值") : Caption);
                        })]]
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(4, 0, 8, 0)
                [SNew(SSlider).Style(&SliderStyle).IsEnabled_Lambda([this] { return HasTarget(ActiveChannel()); })
                    .Value_Lambda([this, FieldName, Minimum, High]
                    {
                        const float Low = Minimum();
                        return High > Low ? FMath::Clamp(FMath::GetRangePct(Low, High, ResponseValue(FieldName)), 0.f, 1.f) : 0.f;
                    })
                    .OnValueChanged_Lambda([this, FieldName, Minimum, High](float T)
                    {
                        SetResponseValue(FieldName, FMath::Lerp(Minimum(), High, T));
                    })]
            + SHorizontalBox::Slot().AutoWidth()
                [SNew(SBox).WidthOverride(86)[SNew(SSpinBox<float>).Style(&SpinStyle)
                    .IsEnabled_Lambda([this] { return HasTarget(ActiveChannel()); })
                    .MinValue_Lambda(Minimum).MaxValue(High).Delta(Delta)
                    .Value_Lambda([this, FieldName] { return ResponseValue(FieldName); })
                    .OnValueChanged_Lambda([this, FieldName](float Value) { SetResponseValue(FieldName, Value); })]], 7);
    };
    AddResponseRow(TEXT("smoothing"), TEXT("平滑时间"), 0.f, .5f, .01f);
    AddRow(Details, SNew(STextBlock).Text(FText::FromString(TEXT("躯干和肩臂启用静止抑制，实际平滑不低于 0.08 秒。")))
        .ColorAndOpacity(Muted).AutoWrapText(true)
        .Visibility_Lambda([this]
        {
            const FString Channel = Representative(ActiveChannel()).ToString();
            return Channel == TEXT("spine") || Channel == TEXT("chest") || Channel.StartsWith(TEXT("shoulder_"))
                || Channel.StartsWith(TEXT("upperarm_")) || Channel.StartsWith(TEXT("lowerarm_"))
                ? EVisibility::Visible : EVisibility::Collapsed;
        }), 5);
    AddResponseRow(TEXT("sensitivity"), TEXT("灵敏度"), .1f, 3.f, .05f);
    AddResponseRow(TEXT("curvature"), TEXT("曲率"), 0.f, 1.f, .01f);
    AddResponseRow(TEXT("precision"), TEXT("精度"), 0.f, 200.f, 1.f);
    AddRow(Details, SNew(SResponseCurve,
        [this](float Raw) { return EvaluateResponse(ActiveChannel(), Raw); },
        [this] { return ChannelInput(ActiveChannel()); },
        [this] { return ChannelOutput(ActiveChannel()); },
        [this] { return IsHeadAxis(ActiveChannel()) || Response(ActiveChannel()).bBipolar; },
        [this] { return ChannelTracked(ActiveChannel()); }), 7);
    AddResponseRow(TEXT("lower"), TEXT("输入下限"), 0.f, 1.f, .01f);
    AddResponseRow(TEXT("upper"), TEXT("输入上限"), 0.f, 1.f, .01f);
    AddRow(Details, SNew(STextBlock).Text_Lambda([this]
    {
        return FText::FromString(Rig.IsValid()
            ? FString::Printf(TEXT("输入 %.3f    输出 %.3f"), ChannelInput(ActiveChannel()), ChannelOutput(ActiveChannel()))
            : FString(TEXT("输入 0.000    输出 0.000")));
    }).ColorAndOpacity(Accent), 8);
    AddRow(Details, SNew(SButton).ButtonStyle(&ButtonStyle)
        .OnClicked_Lambda([this]
        {
            if (Rig.IsValid()) { for (FName Member : Members(ActiveChannel())) Rig->ResetChannelResponse(Member); MarkDirty(); }
            return FReply::Handled();
        })[Label(TEXT("重置当前通道"))], 0);

    auto Options = SNew(SVerticalBox);
    AddRow(Options, Label(TEXT("选项"), 21, Accent));
    AddRow(Options, Label(TEXT("更改会实时生效，并自动保存到当前角色。"), 13, Muted), 14);
    AddRow(Options, SNew(SButton).ButtonStyle(&ButtonStyle).OnClicked_Lambda([this]
        {
            if (Rig.IsValid())
            {
                if (Rig->CalibrateNeutral()) MarkDirty();
                else SaveStatus = TEXT("没有可校准的实时追踪帧");
            }
            return FReply::Handled();
        })[Label(TEXT("校准当前中立姿态"))], 12);
    const TCHAR* CategoryNames[] = {TEXT("头部"), TEXT("表情"), TEXT("上半身"), TEXT("手与手指")};
    for (int32 Index = 0; Index < 4; ++Index)
    {
        AddRow(Options, SNew(SCheckBox).Style(&CheckStyle)
            .IsChecked_Lambda([this, Index]
            {
                if (!Rig.IsValid()) return ECheckBoxState::Unchecked;
                const auto& S = Rig->GetSettings();
                const bool bEnabled = Index == 0 ? S.bHeadEnabled : Index == 1 ? S.bExpressionsEnabled
                    : Index == 2 ? S.bBodyEnabled : S.bHandsEnabled;
                return bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
            })
            .OnCheckStateChanged_Lambda([this, Index](ECheckBoxState State)
            {
                if (!Rig.IsValid()) return;
                auto S = Rig->GetSettings();
                const bool bEnabled = State == ECheckBoxState::Checked;
                if (Index == 0) S.bHeadEnabled = bEnabled;
                else if (Index == 1) S.bExpressionsEnabled = bEnabled;
                else if (Index == 2) S.bBodyEnabled = bEnabled;
                else S.bHandsEnabled = bEnabled;
                if (Rig->SetSettings(S)) MarkDirty();
            })[Label(CategoryNames[Index])], 8);
    }
    AddRow(Options, Label(TEXT("默认手臂展开角（度）"), 13, Muted), 4);
    AddRow(Options, SNew(SSpinBox<float>).Style(&SpinStyle).MinValue(0.f).MaxValue(90.f).MinSliderValue(0.f).MaxSliderValue(90.f).Delta(1.f)
        .Value_Lambda([this] { return Rig.IsValid() ? Rig->GetSettings().IdleArmAngleDegrees : 20.f; })
        .OnValueChanged_Lambda([this](float Value)
        {
            if (Rig.IsValid()) { auto S = Rig->GetSettings(); S.IdleArmAngleDegrees = Value; if (Rig->SetSettings(S)) MarkDirty(); }
        }), 4);
    AddRow(Options, Label(TEXT("0° 下垂，90° 平举；仅在手臂未被有效跟踪时使用。"), 11, Muted), 12);
    AddRow(Options, Label(TEXT("丢失追踪回退（秒）"), 13, Muted), 4);
    AddRow(Options, SNew(SSpinBox<float>).Style(&SpinStyle).MinValue(.05f).MaxValue(5.f).Delta(.01f)
        .Value_Lambda([this] { return Rig.IsValid() ? Rig->GetSettings().LostTrackingSeconds : .3f; })
        .OnValueChanged_Lambda([this](float Value)
        {
            if (Rig.IsValid()) { auto S = Rig->GetSettings(); S.LostTrackingSeconds = Value; if (Rig->SetSettings(S)) MarkDirty(); }
        }), 8);
    AddRow(Options, Label(TEXT("回中时间（秒）"), 13, Muted), 4);
    AddRow(Options, SNew(SSpinBox<float>).Style(&SpinStyle).MinValue(.05f).MaxValue(5.f).Delta(.01f)
        .Value_Lambda([this] { return Rig.IsValid() ? Rig->GetSettings().ReturnSeconds : .35f; })
        .OnValueChanged_Lambda([this](float Value)
        {
            if (Rig.IsValid()) { auto S = Rig->GetSettings(); S.ReturnSeconds = Value; if (Rig->SetSettings(S)) MarkDirty(); }
        }), 12);
    AddRow(Options, SNew(SButton).ButtonStyle(&ButtonStyle).OnClicked_Lambda([this]
        {
            if (Rig.IsValid()) { Rig->ResetAllChannelResponses(); MarkDirty(); }
            return FReply::Handled();
        })[Label(TEXT("恢复所有通道默认设置"))], 0);

    ChildSlot[SNew(SBox).MaxDesiredWidth(830).HeightOverride(480)
        [SNew(SScrollBox).Orientation(Orient_Horizontal).ScrollBarThickness(FVector2D(6, 6))
            + SScrollBox::Slot()[SNew(SBox).MinDesiredWidth(760)[SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)[Tabs]
            + SVerticalBox::Slot().FillHeight(1)
                [SNew(SOverlay)
                    + SOverlay::Slot()
                        [SNew(SHorizontalBox).Visibility_Lambda([this] { return SelectedTab < 2 ? EVisibility::Visible : EVisibility::Collapsed; })
                            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 14, 0)
                                [SNew(SBox).WidthOverride(238)
                                    [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Field).Padding(8)
                                        [SNew(SOverlay)
                                            + SOverlay::Slot()
                                                [SNew(SScrollBox).Visibility_Lambda([this] { return SelectedTab == 0 ? EVisibility::Visible : EVisibility::Collapsed; })
                                                    + SScrollBox::Slot()[FaceRows]]
                                            + SOverlay::Slot()
                                                [SNew(SScrollBox).Visibility_Lambda([this] { return SelectedTab == 1 ? EVisibility::Visible : EVisibility::Collapsed; })
                                                    + SScrollBox::Slot()[BodyRows]]]]]
                            + SHorizontalBox::Slot().FillWidth(1)
                                [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FMmdUiStyle::Colors().Panel).Padding(8)
                                    [SNew(SScrollBox) + SScrollBox::Slot()[Details]]]]
                    + SOverlay::Slot()
                        [SNew(SScrollBox).Visibility_Lambda([this] { return SelectedTab == 2 ? EVisibility::Visible : EVisibility::Collapsed; })
                            + SScrollBox::Slot()[Options]]]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
                [SNew(STextBlock).Text_Lambda([this] { return FText::FromString(SaveStatus); }).ColorAndOpacity(Muted).AutoWrapText(true)]]]]];
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVtuberMappingPanelTest, "HonkaiVTuber.UI.MappingGroupRouting",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVtuberMappingPanelTest::RunTest(const FString& Parameters)
{
    auto* Rig = NewObject<UVtuberRigComponent>();
    auto Panel = SNew(SVtuberRigPanel, Rig);
    const auto Arm = Panel->Members(TEXT("left_arm"));
    TestTrue(TEXT("Left arm includes both driven joints"), Arm.Contains(FName(TEXT("upperarm_l"))) && Arm.Contains(FName(TEXT("lowerarm_l"))));
    TestFalse(TEXT("Arm group leaves wrist to hand group"), Arm.Contains(FName(TEXT("hand_l"))));
    const auto Hand = Panel->Members(TEXT("right_hand"));
    TestTrue(TEXT("Hand group includes wrist and fingers"), Hand.Contains(FName(TEXT("hand_r")))
        && Hand.Contains(FName(TEXT("thumb1_r"))) && Hand.Contains(FName(TEXT("pinky3_r"))));
    auto ArmResponse = Rig->GetChannelResponse(TEXT("upperarm_l"));
    ArmResponse.Sensitivity = 1.4f;
    TestTrue(TEXT("Group edits accepted"), Panel->ApplyResponse(TEXT("left_arm"), ArmResponse));
    TestEqual(TEXT("Upper arm receives group setting"), Rig->GetChannelResponse(TEXT("upperarm_l")).Sensitivity, 1.4f);
    TestEqual(TEXT("Lower arm receives group setting"), Rig->GetChannelResponse(TEXT("lowerarm_l")).Sensitivity, 1.4f);
    auto HeadResponse = Rig->GetChannelResponse(TEXT("headPitchPositive"));
    HeadResponse.Curvature = .3f;
    TestTrue(TEXT("Head axis edit accepted"), Panel->ApplyResponse(TEXT("headPitch"), HeadResponse));
    TestEqual(TEXT("Positive head response receives axis setting"), Rig->GetChannelResponse(TEXT("headPitchPositive")).Curvature, .3f);
    TestEqual(TEXT("Negative head response receives axis setting"), Rig->GetChannelResponse(TEXT("headPitchNegative")).Curvature, .3f);
    Panel->bSavePending = false;
    return true;
}
#endif
