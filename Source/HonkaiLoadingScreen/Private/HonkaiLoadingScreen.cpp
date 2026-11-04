#include "CoreMinimal.h"
#include "Misc/App.h"
#include "Misc/CoreMisc.h"
#include "Modules/ModuleManager.h"
#include "PreLoadScreenBase.h"
#include "PreLoadScreenManager.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

DEFINE_LOG_CATEGORY_STATIC(LogHonkaiLoadingScreen, Log, All);

class FHonkaiLoadingScreen : public FPreLoadScreenBase
{
public:
    virtual void Init() override
    {
        Widget = SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(FLinearColor::FromSRGBColor(FColor(48,48,48)))
            .HAlign(HAlign_Center).VAlign(VAlign_Center)
            [SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,0,0,16)
                    [SNew(STextBlock).Text(FText::FromString(TEXT("Honkai Render")))
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular",28)).ColorAndOpacity(FLinearColor(.8f,.8f,.8f))]
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                    [SNew(STextBlock).Text(FText::FromString(TEXT("正在初始化 Unreal Engine…")))
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular",12)).ColorAndOpacity(FLinearColor(.5f,.5f,.5f))]];
    }
    virtual TSharedPtr<SWidget> GetWidget() override { return Widget; }
    virtual const TSharedPtr<const SWidget> GetWidget() const override { return Widget; }
    virtual void CleanUp() override { Widget.Reset(); FPreLoadScreenBase::CleanUp(); }
    virtual void OnPlay(TWeakPtr<SWindow> Window) override
    { FPreLoadScreenBase::OnPlay(Window); UE_LOG(LogHonkaiLoadingScreen,Display,TEXT("LOADING_SCREEN_PLAY")); }
    virtual void OnStop() override { UE_LOG(LogHonkaiLoadingScreen,Display,TEXT("LOADING_SCREEN_STOP")); }
private:
    TSharedPtr<SWidget> Widget;
};

class FHonkaiLoadingScreenModule : public IModuleInterface
{
    virtual void StartupModule() override
    {
        if (!GIsEditor && !IsRunningCommandlet() && !IsRunningDedicatedServer() && FApp::CanEverRender())
            if (auto* Manager = FPreLoadScreenManager::Get())
                Manager->RegisterPreLoadScreen(MakeShared<FHonkaiLoadingScreen>());
    }
};

IMPLEMENT_MODULE(FHonkaiLoadingScreenModule, HonkaiLoadingScreen)
