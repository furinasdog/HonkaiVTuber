#include "VtuberOutput.h"

#include "Camera/CameraComponent.h"
#include "ClearQuad.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/Actor.h"
#include "RHI.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SpoutSenderComponent.h"
#include "TextureResource.h"
#include "UnrealClient.h"

namespace
{
constexpr int32 OutputWidth = 1280;
constexpr int32 OutputHeight = 720;
constexpr int32 OutputFPS = 30;
constexpr float FrameInterval = 1.f / OutputFPS;
const TCHAR* SenderName = TEXT("HonkaiVTuber");
}

bool UVtuberOutput::Initialize(AActor* Owner, UCameraComponent* MainCamera, const TArray<UPrimitiveComponent*>& Subjects)
{
    Shutdown();
    LastError.Reset();
    if (!IsValid(Owner) || !Owner->GetWorld() || !IsValid(MainCamera))
    {
        LastError = TEXT("输出初始化失败：缺少场景或相机。");
        return false;
    }

    Camera = MainCamera;
    RenderTarget = NewObject<UTextureRenderTarget2D>(this);
    RenderTarget->ClearColor = FLinearColor(0.025f, 0.03f, 0.04f, 1.f);
    RenderTarget->bAutoGenerateMips = false;
    RenderTarget->InitCustomFormat(OutputWidth, OutputHeight, PF_B8G8R8A8, false);
    RenderTarget->UpdateResourceImmediate(true);

    Capture = NewObject<USceneCaptureComponent2D>(Owner);
    Owner->AddInstanceComponent(Capture);
    Capture->TextureTarget = RenderTarget;
    Capture->CaptureSource = SCS_FinalColorLDR;
    Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    Capture->bCaptureEveryFrame = false;
    Capture->bCaptureOnMovement = false;
    Capture->bAlwaysPersistRenderingState = true;
    // OnRegister resets direct ShowFlags changes to the CDO; persist these overrides instead.
    TArray<FEngineShowFlagsSetting> ShowSettings;
    auto AddShowSetting = [&ShowSettings](const TCHAR* Name, bool bEnabled)
    {
        FEngineShowFlagsSetting& Setting = ShowSettings.AddDefaulted_GetRef();
        Setting.ShowFlagName = Name;
        Setting.Enabled = bEnabled;
    };
    AddShowSetting(TEXT("Atmosphere"), false);
    AddShowSetting(TEXT("Fog"), false);
    AddShowSetting(TEXT("VolumetricFog"), false);
    AddShowSetting(TEXT("MotionBlur"), false);
    AddShowSetting(TEXT("TemporalAA"), true);
    AddShowSetting(TEXT("AntiAliasing"), true);
    AddShowSetting(TEXT("PostProcessing"), true);
    Capture->SetShowFlagSettings(ShowSettings);
    Capture->RegisterComponent();
    ensureMsgf(Capture->ShowFlags.TemporalAA && Capture->ShowFlags.AntiAliasing && Capture->ShowFlags.PostProcessing,
        TEXT("VTuber capture anti-aliasing flags did not survive component registration."));

    Sender = NewObject<USpoutSenderComponent>(Owner);
    Owner->AddInstanceComponent(Sender);
    Sender->Auto_Start = false;
    Sender->StartupPolicy = ESpoutWorldBootstrapPolicy::GameOnly;
    Sender->SetTickAfterActor(Owner);
    Sender->RegisterComponent();
    SetSubjectComponents(Subjects);
    UpdateCamera();
    return true;
}

void UVtuberOutput::SetSubjectComponents(const TArray<UPrimitiveComponent*>& Subjects)
{
    if (!Capture) return;
    Capture->ClearShowOnlyComponents();
    for (UPrimitiveComponent* Subject : Subjects)
    {
        if (IsValid(Subject)) Capture->ShowOnlyComponent(Subject);
    }
    Capture->bCameraCutThisFrame = true;
}

bool UVtuberOutput::SetEnabled(bool bEnable)
{
    if (bEnable == bEnabled) return true;
    if (!bEnable)
    {
        if (Sender) Sender->StopBroadcast();
        bEnabled = false;
        return true;
    }
    if (!Capture || !Sender || !RenderTarget || !Camera.IsValid())
    {
        LastError = TEXT("输出尚未初始化。");
        return false;
    }
    if (!GDynamicRHI || FString(GDynamicRHI->GetName()) != TEXT("D3D12"))
    {
        LastError = TEXT("Spout 输出需要 DirectX 12。");
        return false;
    }

    LastError.Reset();
    UpdateCamera();
    Capture->bCameraCutThisFrame = true;
    CaptureFrame();
    Sender->StartBroadcastFromRenderTarget(RenderTarget, SenderName, OutputFPS, false);
    CaptureElapsed = 0.f;
    bEnabled = true;
    return true;
}

void UVtuberOutput::UpdateCamera()
{
    const UCameraComponent* MainCamera = Camera.Get();
    if (!Capture || !MainCamera) return;
    Capture->SetWorldTransform(MainCamera->GetComponentTransform());
    Capture->FOVAngle = MainCamera->FieldOfView;
    Capture->ProjectionType = MainCamera->ProjectionMode;
    Capture->OrthoWidth = MainCamera->OrthoWidth;
    Capture->PostProcessSettings = MainCamera->PostProcessSettings;
    Capture->PostProcessBlendWeight = MainCamera->PostProcessBlendWeight;
}

void UVtuberOutput::Tick(float DeltaSeconds)
{
    if (!bEnabled || !Capture) return;
    if (!Camera.IsValid())
    {
        SetEnabled(false);
        LastError = TEXT("输出已停止：相机已释放。");
        return;
    }
    CaptureElapsed += DeltaSeconds;
    if (CaptureElapsed < FrameInterval) return;
    CaptureElapsed = FMath::Fmod(CaptureElapsed, FrameInterval);
    UpdateCamera();
    CaptureFrame();
}

void UVtuberOutput::CaptureFrame()
{
    Capture->CaptureScene();
    FTextureRenderTargetResource* Resource = RenderTarget->GameThread_GetRenderTargetResource();
    ENQUEUE_RENDER_COMMAND(VtuberOpaqueOutput)([Resource](FRHICommandListImmediate& RHICmdList)
    {
        if (!Resource || !Resource->GetRenderTargetTexture()) return;
        FRDGBuilder GraphBuilder(RHICmdList);
        FRDGTextureRef Target = RegisterExternalTexture(GraphBuilder, Resource->GetRenderTargetTexture(), TEXT("VtuberOpaqueOutput"));
        auto* Parameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
        Parameters->RenderTargets[0] = FRenderTargetBinding(Target, ERenderTargetLoadAction::ELoad);
        GraphBuilder.AddPass(RDG_EVENT_NAME("VtuberOpaqueAlpha"), Parameters, ERDGPassFlags::Raster,
            [](FRHICommandList& CommandList)
            {
                CommandList.SetViewport(0, 0, 0.f, OutputWidth, OutputHeight, 1.f);
                // Only CW_ALPHA is written; the captured RGB, lighting and bloom are untouched.
                DrawClearQuadAlpha(CommandList, 1.f);
            });
        GraphBuilder.SetTextureAccessFinal(Target, ERHIAccess::RTV);
        GraphBuilder.Execute();
    });
}

void UVtuberOutput::Shutdown()
{
    if (Sender)
    {
        Sender->StopBroadcast();
        Sender->DestroyComponent();
        Sender = nullptr;
    }
    if (Capture)
    {
        Capture->TextureTarget = nullptr;
        Capture->DestroyComponent();
        Capture = nullptr;
    }
    RenderTarget = nullptr;
    Camera.Reset();
    bEnabled = false;
    CaptureElapsed = 0.f;
}

FString UVtuberOutput::GetStatus() const
{
    if (!LastError.IsEmpty()) return LastError;
    if (!bEnabled) return TEXT("Spout 输出未开启。");
    // shortcut: transparency remains unadvertised until pixel readback and OBS edge tests pass.
    return TEXT("Spout 发送已请求：HonkaiVTuber · 1280×720 / 30 FPS · 接收端请选择不透明合成。");
}

FString UVtuberOutput::InspectAlpha() const
{
    if (!RenderTarget || !bEnabled) return TEXT("Alpha 检查失败：请先开启输出。");
    FTextureRenderTargetResource* Resource = RenderTarget->GameThread_GetRenderTargetResource();
    if (!Resource) return TEXT("Alpha 检查失败：输出纹理尚未就绪。");
    TArray<FColor> Pixels;
    FReadSurfaceDataFlags Flags(RCM_UNorm);
    Flags.SetLinearToGamma(false);
    if (!Resource->ReadPixels(Pixels, Flags) || Pixels.Num() != OutputWidth * OutputHeight)
        return TEXT("Alpha 检查失败：读取纹理失败。");

    uint8 Minimum = 255, Maximum = 0;
    int32 Zero = 0, Full = 0;
    for (const FColor& Pixel : Pixels)
    {
        Minimum = FMath::Min(Minimum, Pixel.A);
        Maximum = FMath::Max(Maximum, Pixel.A);
        Zero += Pixel.A == 0;
        Full += Pixel.A == 255;
    }
    const int32 Center = (OutputHeight / 2) * OutputWidth + OutputWidth / 2;
    return FString::Printf(TEXT("Alpha [0..255] min=%d max=%d zero=%d full=%d partial=%d corners=%d,%d,%d,%d center=%d。此为原始读回，透明输出仍未验收。"),
        Minimum, Maximum, Zero, Full, Pixels.Num() - Zero - Full,
        Pixels[0].A, Pixels[OutputWidth - 1].A, Pixels[(OutputHeight - 1) * OutputWidth].A, Pixels.Last().A, Pixels[Center].A);
}
