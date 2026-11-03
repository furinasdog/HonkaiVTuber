#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "VtuberRig.h"

class SVtuberRigPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SVtuberRigPanel) : _InitialTab(0) {}
        SLATE_ARGUMENT(int32, InitialTab)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args, UVtuberRigComponent* InRig);
    virtual ~SVtuberRigPanel() override;
    virtual void Tick(const FGeometry& Geometry, double CurrentTime, float DeltaTime) override;
    void FlushSave();
private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FVtuberMappingPanelTest;
    friend class FMmdOverlayTest;
#endif
    FName ActiveChannel() const { return SelectedTab == 0 ? FaceChannel : BodyChannel; }
    bool IsHeadAxis(FName Channel) const;
    bool IsBodyGroup(FName Channel) const;
    TArray<FName> Members(FName Channel) const;
    FName Representative(FName Channel) const;
    FVtuberChannelResponse Response(FName Channel) const;
    bool ApplyResponse(FName Channel, const FVtuberChannelResponse& Value);
    float EvaluateResponse(FName Channel, float Raw) const;
    float ChannelInput(FName Channel) const;
    float ChannelOutput(FName Channel) const;
    bool ChannelTracked(FName Channel) const;
    bool HasTarget(FName Channel) const;
    FString ChannelLabel(FName Channel) const;
    FVtuberMorphMapping MorphMapping(FName Channel) const;
    float ResponseValue(FName Field) const;
    void SetResponseValue(FName Field, float Value);
    void MarkDirty();
    TWeakObjectPtr<UVtuberRigComponent> Rig;
    FName FaceChannel = TEXT("eyeBlinkLeft"), BodyChannel = TEXT("left_arm");
    int32 SelectedTab = 0;
    TArray<TSharedPtr<FString>> BoneOptions, MorphOptions;
    TSet<FName> AvailableBones, AvailableMorphs, BodyChannels;
    FButtonStyle ButtonStyle;
    FComboBoxStyle ComboStyle;
    FSpinBoxStyle SpinStyle;
    FSliderStyle SliderStyle;
    FCheckBoxStyle CheckStyle;
    FTableRowStyle RowStyle;
    FString SaveStatus = TEXT("设置自动保存");
    bool bSavePending = false;
    double LastChangeTime = 0;
};
