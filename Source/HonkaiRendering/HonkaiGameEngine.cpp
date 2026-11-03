#include "HonkaiGameEngine.h"

#include "HonkaiRendering.h"

TSharedRef<SViewport> UHonkaiGameEngine::CreateGameViewportWidget()
{
    TSharedRef<SViewport> Widget = Super::CreateGameViewportWidget();
    // Keep post-processing independent of the window backbuffer during resize/minimize.
    Widget->SetRenderDirectlyToWindow(false);
    UE_LOG(LogHonkaiRendering, Display, TEXT("VIEWER_VIEWPORT direct_to_window=%d"), Widget->ShouldRenderDirectly());
    return Widget;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Slate/SceneViewport.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHonkaiSeparateViewportTest, "HonkaiRendering.Output.SeparateSceneTarget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FHonkaiSeparateViewportTest::RunTest(const FString& Parameters)
{
    FString ConfiguredEngine;
    TestTrue(TEXT("Game engine configuration exists"), GConfig->GetString(TEXT("/Script/Engine.Engine"), TEXT("GameEngine"), ConfiguredEngine, GEngineIni));
    TestEqual(TEXT("Game uses the separate-target engine"), ConfiguredEngine, FString(TEXT("/Script/HonkaiRendering.HonkaiGameEngine")));

    // A disposable UEngine clears global GEngine in FinishDestroy; this factory is stateless.
    auto* Engine = GetMutableDefault<UHonkaiGameEngine>();
    TSharedPtr<SViewport> Widget = Engine->CreateGameViewportWidget();
    TestFalse(TEXT("Scene post-processing does not render directly into the window"), Widget->ShouldRenderDirectly());
    FSceneViewport Viewport(Widget);
    TestTrue(TEXT("FSceneViewport initializes a separate scene render target"), static_cast<const ISlateViewport&>(Viewport).UseSeparateRenderTarget());
    return true;
}
#endif
