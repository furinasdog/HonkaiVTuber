#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "VtuberOutput.generated.h"

class AActor;
class UCameraComponent;
class UPrimitiveComponent;
class USceneCaptureComponent2D;
class USpoutSenderComponent;
class UTextureRenderTarget2D;

UCLASS()
class UVtuberOutput : public UObject
{
    GENERATED_BODY()

public:
    bool Initialize(AActor* Owner, UCameraComponent* MainCamera, const TArray<UPrimitiveComponent*>& Subjects);
    void SetSubjectComponents(const TArray<UPrimitiveComponent*>& Subjects);
    bool SetEnabled(bool bEnable);
    void Tick(float DeltaSeconds);
    void UpdateCamera();
    void Shutdown();

    bool IsEnabled() const { return bEnabled; }
    FString GetStatus() const;
    UTextureRenderTarget2D* GetRenderTarget() const { return RenderTarget; }
    // Explicit diagnostic only: reading pixels synchronizes the GPU.
    FString InspectAlpha() const;

private:
    void CaptureFrame();
    UPROPERTY() TObjectPtr<USceneCaptureComponent2D> Capture;
    UPROPERTY() TObjectPtr<USpoutSenderComponent> Sender;
    UPROPERTY() TObjectPtr<UTextureRenderTarget2D> RenderTarget;
    TWeakObjectPtr<UCameraComponent> Camera;
    FString LastError;
    float CaptureElapsed = 0.f;
    bool bEnabled = false;
};
