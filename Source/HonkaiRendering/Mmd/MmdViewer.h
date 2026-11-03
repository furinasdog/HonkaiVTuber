#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameModeBase.h"
#include "Camera/CameraActor.h"
#include "MmdEnhancement.h"
#include "MmdPartVisibility.h"
#include "Vtuber/NativeTracking.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "MmdReplaySequence.h"
#endif
#include "MmdViewer.generated.h"

class UVrmAssetListObject;
class UMaterialInterface;
class UHonkaiRenderInstance;
class UMaterialInstanceDynamic;
class SMmdViewer;
class USkeletalMeshComponent;
class UTexture2D;
class USkinnedMeshComponent;
class UVtuberOutput;

UCLASS()
class AMmdViewerController : public APlayerController
{
	GENERATED_BODY()
public:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	void ImportArchive();
	void LoadArchive(const FString& Path);
	void LoadModel(const FString& Path);
	void Orbit(FVector2D Delta, bool Pan, bool Dolly);
	void Zoom(float Delta);
	void FocusModel();
	void SetView(int32 View, bool Reverse);
	void ToggleProjection();
    void GenerateEnhancement(bool bForce = false, int32 Material = INDEX_NONE);
    void CancelEnhancement();
    void SetEnhanced(bool bEnabled);
    void PreviewMaterial(int32 Index);
    void ClearMaterialPreview();
    int32 GetPreviewMaterial() const { return PreviewMaterialIndex; }
    void ExportEnhancement();
    void ClearEnhancementCache();
    void SetBackground(int32 Preset);
    void SetRenderScale(int32 Percent);
    void SetEnhancementResolution(int32 Resolution);
    void ToggleTracking();
    FString TrackingSummary();
    void RecordTrackingFrame(const FVtuberTrackingSnapshot& Snapshot);
    void ToggleSpout();
    void UpdateFaceLight();
    const FMmdPartVisibility& GetPartVisibility() const { return PartVisibility; }
    bool HasParts() const { return !PartVisibility.Parts().IsEmpty(); }
    void SetPartVisibilityEnabled(bool bEnabled);
    void SetPartHidden(const FString& Id, bool bPartHidden);
    void RestoreParts();
    void PreviewPart(const FString& Id = FString());
    const FString& GetPreviewPart() const { return PreviewPartId; }
    FString PartVisibilityNotice;
    void MoveCharacter(FVector Direction, float DeltaSeconds);
    void StopCharacterMovement() { MovementVelocity = FVector::ZeroVector; }
    FNativeTracking Tracking;
    FVtuberTrackingSnapshot LastTrackingSnapshot;
    double LastTrackingFrame = 0;
    bool bFaceSeen = false, bPoseSeen = false;
    int32 HandsSeen = 0;
#if WITH_DEV_AUTOMATION_TESTS
    FVtuberTrackingSnapshot ReplaySnapshot;
    bool bTrackingReplay = false;
    FMmdReplaySequence ReplaySequence;
    double ReplayStartWorldSeconds = 0;
    bool bReplaySequenceRequested = false;
    int32 ReplayFrameIndex = INDEX_NONE;
#endif
    UPROPERTY() TObjectPtr<UVtuberRigComponent> Rig;
    UPROPERTY() TObjectPtr<UVtuberOutput> Output;
    bool HasModel() const { return LoadedAsset != nullptr; }
    bool HasEnhancement() const { return !EnhancedFaceMaps.IsEmpty(); }
    bool IsEnhanced() const { return bEnhanced; }
    bool IsGenerating() const { return Enhancement.IsRunning(); }
    FString GenerationText() const;
    float GenerationProgress() const { return Enhancement.GetStatus().Progress; }
    TArray<FString> MaterialNames;
    int32 SelectedFaceMaterial = INDEX_NONE;
    int32 EnhancementResolution = 512;
    FString Status = TEXT("导入 ZIP 压缩包以开始。");
	FString ModelName;
	FString ModelInfo;
	TArray<FString> ModelChoices;
	bool bBusy = false;
	bool bChoosing = false;
private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FMmdEffectToggleTest;
#endif
    void UpdateCamera();
    void SaveEffectChoice();
    void HandleGenerationStatus(const FMmdEnhancementStatus& Progress);
    bool ApplyGeneratedProfile(const FString& Directory, bool bEnabled, const FMmdModelState& CandidateState);
    bool CommitGeneratedProfile(const TMap<int32, TObjectPtr<UTexture2D>>& Maps, const FMmdModelState& CandidateState, bool bEnabled);
    FMmdEnhancement Enhancement;
    bool SavePartVisibility();
    FMmdPartVisibility PartVisibility;
    FString PartVisibilityPath, PreviewPartId;
    FMmdModelState ModelState;
    FMmdEnhancementRequest GenerationRequest;
    bool bGenerationPending = false;
    bool bHudVisibleBeforeImport = false;
    FString LoadedModelPath;
    FString EnhancementNotice;
    FString LoadedResourceRoot;
    bool bEnhanced = false;
    int32 PreviewMaterialIndex = INDEX_NONE;
    UPROPERTY() TObjectPtr<USkeletalMeshComponent> ModelMesh;
    UPROPERTY() TObjectPtr<USkinnedMeshComponent> OutlineMesh;
    UPROPERTY() TObjectPtr<UHonkaiRenderInstance> Renderer;
    UPROPERTY() TMap<int32, TObjectPtr<UTexture2D>> OriginalFaceMaps;
    UPROPERTY() TMap<int32, TObjectPtr<UTexture2D>> EnhancedFaceMaps;
    TArray<FLinearColor> SurfaceTints;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BackdropMaterial;
	UFUNCTION() void OnModelLoaded();
	TSharedPtr<SMmdViewer> Widget;
	FString ImportDirectory;
	FString PendingPath;
	UPROPERTY() UVrmAssetListObject* PendingAsset = nullptr;
	UPROPERTY() TObjectPtr<UVrmAssetListObject> LoadedAsset;
	UPROPERTY() TObjectPtr<UVrmAssetListObject> ImportTemplate;
	UPROPERTY() TObjectPtr<AActor> ModelActor;
	UPROPERTY() TObjectPtr<ACameraActor> ViewerCamera;
	FVector Target = FVector(0, 0, 80), ModelCenter = FVector(0, 0, 80);
    FVector MovementVelocity = FVector::ZeroVector;
	FVector KeyDirection = FVector::ForwardVector;
	FRotator Rotation = FRotator(-5, 180, 0);
	float Distance = 350, ModelRadius = 100;
	bool bOrthographic = false;
};

UCLASS()
class AMmdViewerGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	AMmdViewerGameMode();
};
