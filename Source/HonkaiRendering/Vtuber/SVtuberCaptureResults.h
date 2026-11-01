#pragma once

#include "CoreMinimal.h"
#include "NativeTracking.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SCompoundWidget.h"

class SVtuberCapturePreview;

class SVtuberCaptureResults : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SVtuberCaptureResults) {}
        SLATE_ATTRIBUTE(FNativeTrackingPreview, GetPreview)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    virtual void Tick(const FGeometry& Geometry, double CurrentTime, float DeltaTime) override;
private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FVtuberCaptureResultsTest;
#endif
    bool FaceTracked() const;
    bool UpperBodyTracked() const;
    int32 TrackedHands() const;
    FText HeadAngle(int32 Axis) const;
    TOptional<float> Expression(FName Channel) const;
    TSharedRef<SWidget> ExpressionRow(const FString& Label, FName Channel);
    TAttribute<FNativeTrackingPreview> PreviewSource;
    TSharedPtr<const FNativeTrackingPreview> Frame;
    TSharedPtr<SVtuberCapturePreview> Camera;
    FProgressBarStyle MeterStyle;
    bool bShowLandmarks = true;
};
