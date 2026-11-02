#include "SVtuberCaptureResults.h"
#include "Mmd/MmdUiStyle.h"

#include "Brushes/SlateColorBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "HAL/PlatformTime.h"
#include "Rendering/DrawElementTypes.h"
#include "Rendering/RenderingCommon.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Textures/SlateUpdatableTexture.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
    const FLinearColor Ink = FMmdUiStyle::Colors().Text, Muted = FMmdUiStyle::Colors().Muted;
    const FLinearColor Accent = FMmdUiStyle::Colors().Accent, Field = FMmdUiStyle::Colors().Field;
    const FLinearColor Canvas = FMmdUiStyle::Colors().Canvas;
    const FLinearColor PoseInk = FLinearColor::FromSRGBColor(FColor(111, 222, 241));
    const FLinearColor LeftHandInk = FLinearColor::FromSRGBColor(FColor(255, 197, 107));
    const FLinearColor RightHandInk = FLinearColor::FromSRGBColor(FColor(124, 232, 185));

    FSlateRect ImageRect(FVector2D Available, FIntPoint Pixels)
    {
        if (!FMath::IsFinite(Available.X) || !FMath::IsFinite(Available.Y) || Available.X <= 0 || Available.Y <= 0
            || Pixels.X <= 0 || Pixels.Y <= 0) return FSlateRect(0, 0, 0, 0);
        const double Scale = FMath::Min(Available.X / Pixels.X, Available.Y / Pixels.Y);
        const FVector2D Size(Pixels.X * Scale, Pixels.Y * Scale), Offset = (Available - Size) * .5;
        return FSlateRect(float(Offset.X), float(Offset.Y), float(Offset.X + Size.X), float(Offset.Y + Size.Y));
    }
    bool OverlayPoint(const FVtuberLandmark& Point, const FSlateRect& Rect, FVector2D& Out, bool bPoseConfidence = false)
    {
        const FVector& P = Point.Position;
        if (P.ContainsNaN() || P.X < 0 || P.X > 1 || P.Y < 0 || P.Y > 1 || Rect.Right <= Rect.Left || Rect.Bottom <= Rect.Top
            || (bPoseConfidence && (!FMath::IsFinite(Point.Visibility) || Point.Visibility < .45f))) return false;
        Out = FVector2D(Rect.Left + P.X * (Rect.Right - Rect.Left), Rect.Top + P.Y * (Rect.Bottom - Rect.Top));
        return FMath::IsFinite(Out.X) && FMath::IsFinite(Out.Y);
    }
    bool ValidHand(const FVtuberHandTracking& Hand)
    {
        return FMath::IsFinite(Hand.Score) && Hand.Score >= .45f && Hand.Landmarks.Num() == 21;
    }
    FNativeTrackingPreview DisplayFrame(FNativeTrackingPreview Source, double Now)
    {
        const bool Stale = Source.HasImage() && (!FMath::IsFinite(Source.TimestampSeconds) || Source.TimestampSeconds <= 0
            || !FMath::IsFinite(Now) || Now - Source.TimestampSeconds > .5 || Source.TimestampSeconds > Now + 1.);
        if (!Source.bSupported || !Source.HasImage() || Stale)
        {
            Source.Width = Source.Height = 0; Source.BGRA.Reset(); Source.FaceLandmarks.Reset();
            Source.Tracking = FVtuberTrackingSnapshot();
            if (Stale) Source.Status = TEXT("画面已过期，等待下一帧");
        }
        if (Source.Status.IsEmpty()) Source.Status = TEXT("暂无摄像头画面");
        return Source;
    }
    TSharedRef<STextBlock> Label(const FString& Text, int32 Size = 12, FLinearColor Color = Ink)
    {
        return SNew(STextBlock).Text(FText::FromString(Text)).ColorAndOpacity(Color)
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", Size));
    }

    class FPreviewViewport : public ISlateViewport
    {
    public:
        FSlateUpdatableTexture* Texture = nullptr;
        FIntPoint Size = FIntPoint::ZeroValue;
        virtual FIntPoint GetSize() const override { return Size; }
        virtual FSlateShaderResource* GetViewportRenderTargetTexture() const override { return Texture ? Texture->GetSlateResource() : nullptr; }
        virtual bool RequiresVsync() const override { return false; }
        virtual bool UseSeparateRenderTarget() const override { return true; }
    };
}

class SVtuberCapturePreview : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SVtuberCapturePreview) {} SLATE_END_ARGS()
    void Construct(const FArguments&)
    {
        Viewport = MakeShared<FPreviewViewport>();
        SetClipping(EWidgetClipping::ClipToBounds);
    }
    virtual ~SVtuberCapturePreview() override
    {
        if (Viewport && Viewport->Texture)
        {
            if (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer())
                FSlateApplication::Get().GetRenderer()->ReleaseUpdatableTexture(Viewport->Texture);
            else Viewport->Texture->Cleanup();
            Viewport->Texture = nullptr;
        }
    }
    void SetFrame(TSharedPtr<const FNativeTrackingPreview> InFrame, bool bInLandmarks)
    {
        Frame = MoveTemp(InFrame); bLandmarks = bInLandmarks;
        if (Frame && Frame->HasImage())
        {
            if (!Viewport->Texture && FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer())
                Viewport->Texture = FSlateApplication::Get().GetRenderer()->CreateUpdatableTexture(Frame->Width, Frame->Height);
            if (Viewport->Texture && (Frame->Sequence != UploadedSequence || Frame->TimestampSeconds != UploadedTime
                || Viewport->Size != FIntPoint(Frame->Width, Frame->Height)))
            {
                Viewport->Texture->UpdateTextureThreadSafeRaw(Frame->Width, Frame->Height, Frame->BGRA.GetData());
                Viewport->Size = FIntPoint(Frame->Width, Frame->Height);
                UploadedSequence = Frame->Sequence; UploadedTime = Frame->TimestampSeconds;
            }
        }
        else { UploadedSequence = 0; UploadedTime = 0; }
        Invalidate(EInvalidateWidgetReason::Paint);
    }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(376, 200); }
    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Elements,
        int32 Layer, const FWidgetStyle&, bool) const override
    {
        const auto* White = FCoreStyle::Get().GetBrush("WhiteBrush");
        FSlateDrawElement::MakeBox(Elements, Layer, G.ToPaintGeometry(), White, ESlateDrawEffect::None, Canvas);
        if (!Frame || !Frame->HasImage() || !Viewport->Texture)
        {
            const FText Text = FText::FromString(TEXT("暂无摄像头画面"));
            const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 12);
            if (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer())
            {
                const FVector2D Size = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font);
                const FVector2D Available = G.GetLocalSize();
                FSlateDrawElement::MakeText(Elements, Layer + 1, G.ToPaintGeometry(Size, FSlateLayoutTransform((Available - Size) * .5)),
                    Text, Font, ESlateDrawEffect::None, Muted);
            }
            return Layer + 1;
        }
        const FSlateRect Rect = ImageRect(G.GetLocalSize(), FIntPoint(Frame->Width, Frame->Height));
        FSlateDrawElement::MakeViewport(Elements, Layer + 1,
            G.ToPaintGeometry(FVector2D(Rect.Right - Rect.Left, Rect.Bottom - Rect.Top), FSlateLayoutTransform(FVector2D(Rect.Left, Rect.Top))),
            Viewport, ESlateDrawEffect::NoGamma | ESlateDrawEffect::IgnoreTextureAlpha);
        if (!bLandmarks) return Layer + 1;
        auto Point = [&](const FVtuberLandmark& Value, FLinearColor Color, bool bConfidence = false)
        {
            FVector2D P;
            if (OverlayPoint(Value, Rect, P, bConfidence))
                FSlateDrawElement::MakeBox(Elements, Layer + 3, G.ToPaintGeometry(FVector2D(3, 3), FSlateLayoutTransform(P - FVector2D(1.5, 1.5))),
                    White, ESlateDrawEffect::None, Color);
        };
        auto Line = [&](const TArray<FVtuberLandmark>& Points, int32 A, int32 B, FLinearColor Color, bool bConfidence = false)
        {
            FVector2D PA, PB;
            if (Points.IsValidIndex(A) && Points.IsValidIndex(B) && OverlayPoint(Points[A], Rect, PA, bConfidence) && OverlayPoint(Points[B], Rect, PB, bConfidence))
                FSlateDrawElement::MakeLines(Elements, Layer + 2, G.ToPaintGeometry(), TArray<FVector2D>{PA, PB}, ESlateDrawEffect::None, Color, true, 1.5f);
        };
        if (Frame->Tracking.bPoseTracked)
        {
            const auto& Pose = Frame->Tracking.PoseLandmarks;
            const int32 Edges[][2] = {{11,12},{11,13},{13,15},{12,14},{14,16},{11,23},{12,24},{23,24}};
            for (const auto& Edge : Edges) Line(Pose, Edge[0], Edge[1], PoseInk, true);
            for (int32 Index : {11,12,13,14,15,16,23,24}) if (Pose.IsValidIndex(Index)) Point(Pose[Index], PoseInk, true);
        }
        for (const auto& Hand : Frame->Tracking.Hands)
        {
            if (!ValidHand(Hand)) continue;
            const FLinearColor Color = Hand.Handedness == TEXT("Left") ? LeftHandInk : RightHandInk;
            const int32 Edges[][2] = {{0,1},{1,2},{2,3},{3,4},{0,5},{5,6},{6,7},{7,8},{5,9},{9,10},{10,11},{11,12},
                {9,13},{13,14},{14,15},{15,16},{13,17},{17,18},{18,19},{19,20},{17,0}};
            for (const auto& Edge : Edges) Line(Hand.Landmarks, Edge[0], Edge[1], Color);
            for (const auto& P : Hand.Landmarks) Point(P, Color);
        }
        if (Frame->Tracking.bFaceTracked)
            for (int32 Index : {10,338,332,251,356,454,361,397,379,400,152,176,150,172,132,234,162,54,67,109,33,133,362,263,1,61,291,13,14})
                if (Frame->FaceLandmarks.IsValidIndex(Index)) Point(Frame->FaceLandmarks[Index], FLinearColor::White);
        return Layer + 3;
    }
private:
    TSharedPtr<FPreviewViewport> Viewport;
    TSharedPtr<const FNativeTrackingPreview> Frame;
    uint64 UploadedSequence = 0;
    double UploadedTime = 0;
    bool bLandmarks = true;
};

void SVtuberCaptureResults::Construct(const FArguments& Args)
{
    PreviewSource = Args._GetPreview;
    Frame = MakeShared<FNativeTrackingPreview>();
    MeterStyle = FCoreStyle::Get().GetWidgetStyle<FProgressBarStyle>("ProgressBar");
    MeterStyle.SetBackgroundImage(FSlateColorBrush(Field)).SetFillImage(FSlateColorBrush(Accent));
    auto Status = [](const FString& Name, TFunction<FString()> Value, TFunction<bool()> Active) -> TSharedRef<SWidget>
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()[Label(Name, 11, Muted)]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)
            [SNew(STextBlock).Text_Lambda([Value] { return FText::FromString(Value()); })
                .ColorAndOpacity_Lambda([Active] { return FSlateColor(Active() ? FMmdUiStyle::Colors().AccentText : Muted); }).Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))];
    };
    auto Angle = [this](const FString& Name, int32 Axis) -> TSharedRef<SWidget>
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()[Label(Name, 11, Muted)]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)
            [SNew(STextBlock).Text_Lambda([this, Axis] { return HeadAngle(Axis); }).ColorAndOpacity(Ink)
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))];
    };
    ChildSlot
    [SNew(SBox).MaxDesiredWidth(400).MaxDesiredHeight(570)
        [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FMmdUiStyle::Colors().Panel).Padding(12)
            [SNew(SScrollBox)
                + SScrollBox::Slot()
                [SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(200)[SAssignNew(Camera, SVtuberCapturePreview)]]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
                    [SNew(SCheckBox).Style(&FMmdUiStyle::Get().Check)
                        .IsChecked_Lambda([this] { return bShowLandmarks ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                        .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bShowLandmarks = State == ECheckBoxState::Checked; Camera->SetFrame(Frame, bShowLandmarks); })
                        [Label(TEXT("显示关键点"))]]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
                    [SNew(STextBlock).Text_Lambda([this] { return FText::FromString(Frame->Status); }).AutoWrapText(true)
                        .ColorAndOpacity(Muted).Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)
                    [SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(1)[Status(TEXT("面部"), [this]() -> FString { return FaceTracked() ? TEXT("已识别") : TEXT("未识别"); }, [this] { return FaceTracked(); })]
                        + SHorizontalBox::Slot().FillWidth(1)[Status(TEXT("上半身"), [this]() -> FString { return UpperBodyTracked() ? TEXT("已识别") : TEXT("未识别"); }, [this] { return UpperBodyTracked(); })]
                        + SHorizontalBox::Slot().FillWidth(1)[Status(TEXT("手部"), [this] { return FString::Printf(TEXT("%d / 2"), TrackedHands()); }, [this] { return TrackedHands() > 0; })]]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 8)[Label(TEXT("头部角度"))]
                    + SVerticalBox::Slot().AutoHeight()
                    [SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(1)[Angle(TEXT("俯仰"), 0)]
                        + SHorizontalBox::Slot().FillWidth(1)[Angle(TEXT("左右转头"), 1)]
                        + SHorizontalBox::Slot().FillWidth(1)[Angle(TEXT("侧倾"), 2)]]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 4)[Label(TEXT("表情"))]
                    + SVerticalBox::Slot().AutoHeight()[ExpressionRow(TEXT("左眼闭合"), TEXT("eyeBlinkLeft"))]
                    + SVerticalBox::Slot().AutoHeight()[ExpressionRow(TEXT("右眼闭合"), TEXT("eyeBlinkRight"))]
                    + SVerticalBox::Slot().AutoHeight()[ExpressionRow(TEXT("张嘴"), TEXT("jawOpen"))]
                ]
            ]
        ]
    ];
}

void SVtuberCaptureResults::Tick(const FGeometry& Geometry, double CurrentTime, float DeltaTime)
{
    SCompoundWidget::Tick(Geometry, CurrentTime, DeltaTime);
    Frame = MakeShared<FNativeTrackingPreview>(DisplayFrame(PreviewSource.Get(FNativeTrackingPreview()), FPlatformTime::Seconds()));
    Camera->SetFrame(Frame, bShowLandmarks);
}
bool SVtuberCaptureResults::FaceTracked() const { return Frame && Frame->HasImage() && Frame->Tracking.bFaceTracked; }
bool SVtuberCaptureResults::UpperBodyTracked() const
{
    if (!Frame || !Frame->HasImage() || !Frame->Tracking.bPoseTracked || Frame->Tracking.PoseLandmarks.Num() != 33) return false;
    FVector2D Point;
    const FSlateRect Unit(0, 0, 1, 1);
    return OverlayPoint(Frame->Tracking.PoseLandmarks[11], Unit, Point, true) && OverlayPoint(Frame->Tracking.PoseLandmarks[12], Unit, Point, true);
}
int32 SVtuberCaptureResults::TrackedHands() const
{
    int32 Count = 0;
    if (Frame && Frame->HasImage()) for (const auto& Hand : Frame->Tracking.Hands) if (ValidHand(Hand)) ++Count;
    return FMath::Min(Count, 2);
}
FText SVtuberCaptureResults::HeadAngle(int32 Axis) const
{
    if (!FaceTracked() || Frame->Tracking.HeadRotation.ContainsNaN() || Frame->Tracking.HeadRotation.SizeSquared() < .5) return FText::FromString(TEXT("—"));
    const FRotator Angles = Frame->Tracking.HeadRotation.Rotator().GetNormalized();
    const double Value = Axis == 0 ? Angles.Pitch : Axis == 1 ? Angles.Yaw : Angles.Roll;
    return FMath::IsFinite(Value) ? FText::FromString(FString::Printf(TEXT("%+.1f°"), Value)) : FText::FromString(TEXT("—"));
}
TOptional<float> SVtuberCaptureResults::Expression(FName Channel) const
{
    if (FaceTracked()) if (const float* Value = Frame->Tracking.BlendShapes.Find(Channel))
        if (FMath::IsFinite(*Value)) return FMath::Clamp(*Value, 0.f, 1.f);
    return {};
}
TSharedRef<SWidget> SVtuberCaptureResults::ExpressionRow(const FString& Name, FName Channel)
{
    return SNew(SBox).HeightOverride(26)
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(82)[Label(Name, 11)]]
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
            [SNew(SBox).HeightOverride(5)[SNew(SProgressBar).Style(&MeterStyle).BarFillStyle(EProgressBarFillStyle::Scale)
                .Percent_Lambda([this, Channel] { return TOptional<float>(Expression(Channel).Get(0.f)); })]]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10, 0, 0, 0)
            [SNew(SBox).WidthOverride(42)[SNew(STextBlock).Justification(ETextJustify::Right).ColorAndOpacity(Muted)
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11)).Text_Lambda([this, Channel]
                {
                    const auto Value = Expression(Channel);
                    return Value.IsSet() ? FText::FromString(FString::Printf(TEXT("%.0f%%"), Value.GetValue() * 100)) : FText::FromString(TEXT("—"));
                })]]];
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include <limits>
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVtuberCaptureResultsTest, "HonkaiVTuber.UI.CaptureResults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVtuberCaptureResultsTest::RunTest(const FString& Parameters)
{
    const FSlateRect Wide = ImageRect(FVector2D(400, 200), FIntPoint(320, 240));
    TestTrue(TEXT("A 4:3 image is centered without stretching in the preview canvas"), FMath::IsNearlyEqual(double(Wide.Left), 200. / 3., .001)
        && FMath::IsNearlyEqual(double(Wide.Right), 1000. / 3., .001) && Wide.Top == 0 && Wide.Bottom == 200);
    const FSlateRect Tall = ImageRect(FVector2D(400, 200), FIntPoint(120, 240));
    TestTrue(TEXT("Portrait preview letterboxes on both sides"), Tall.Left == 150 && Tall.Right == 250);
    FVtuberLandmark Landmark; Landmark.Position = FVector(.25, .75, 0);
    FVector2D P;
    TestTrue(TEXT("Normalized landmarks use the exact image rectangle"), OverlayPoint(Landmark, Tall, P) && P.Equals(FVector2D(175, 150)));
    TestTrue(TEXT("Face points do not require absent visibility metadata"), OverlayPoint(Landmark, Wide, P));
    TestFalse(TEXT("Pose points require recognition confidence"), OverlayPoint(Landmark, Wide, P, true));
    for (double Invalid : {-0.001, 1.001, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
    {
        Landmark.Position.X = Invalid;
        TestFalse(TEXT("Off-image and nonfinite landmarks never reach paint coordinates"), OverlayPoint(Landmark, Wide, P));
    }
    FNativeTrackingPreview Source; Source.Width = 2; Source.Height = 1; Source.BGRA.SetNumZeroed(8);
    Source.TimestampSeconds = 10; Source.Tracking.bFaceTracked = true; Source.FaceLandmarks.SetNum(478);
    const auto Fresh = DisplayFrame(Source, 10.1), Stale = DisplayFrame(Source, 11.);
    TestTrue(TEXT("Fresh preview data is retained without mirroring again"), Fresh.HasImage() && Fresh.BGRA == Source.BGRA);
    TestTrue(TEXT("Stale image and recognition overlays are cleared together"), !Stale.HasImage() && !Stale.Tracking.bFaceTracked && Stale.FaceLandmarks.IsEmpty());
    Source.BGRA.Reset();
    const auto Empty = DisplayFrame(Source, 10.1);
    TestFalse(TEXT("A state-only stopped frame cannot retain recognized face state"), Empty.HasImage() || Empty.Tracking.bFaceTracked);
    int32 Reads = 0;
    auto Panel = SNew(SVtuberCaptureResults).GetPreview_Lambda([&Reads] { ++Reads; return FNativeTrackingPreview(); });
    Panel->Tick(FGeometry(), 0, 1.f / 60.f);
    Panel->FaceTracked(); Panel->UpperBodyTracked(); Panel->TrackedHands(); Panel->HeadAngle(0); Panel->Expression(TEXT("jawOpen"));
    TestEqual(TEXT("One tick reads the preview source once regardless of result labels"), Reads, 1);
    return true;
}
#endif
