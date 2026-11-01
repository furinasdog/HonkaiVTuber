#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Animation/AnimInstance.h"
#include "VrmAnimInstanceCopy.h"
#include "VtuberRig.generated.h"

class USkeletalMeshComponent;

struct FVtuberLandmark
{
    FVector Position = FVector::ZeroVector;
    float Visibility = 0.0f;
    float Presence = 0.0f;
};

struct FVtuberHandTracking
{
    FName Handedness;
    float Score = 0.0f;
    TArray<FVtuberLandmark> Landmarks;
    TArray<FVtuberLandmark> WorldLandmarks;
};

struct FVtuberThresholdCurve
{
    float Start = 0.0f;
    float Full = 1.0f;
    bool IsValid() const;
    float Evaluate(float Input) const;
};

struct FVtuberMorphMapping
{
    FName Channel;
    FName Morph;
    FVtuberThresholdCurve Curve;
    float Maximum = 1.0f;
    float Sensitivity = 1.0f;
    bool bEnabled = true;
    bool bInvert = false;
};

struct FVtuberChannelResponse
{
    bool bEnabled = true;
    bool bInvert = false;
    bool bBipolar = false;
    float SmoothingSeconds = .02f;
    float Sensitivity = 1.f;
    float Curvature = 0.f;
    int32 Precision = 0;
    float Lower = 0.f;
    float Upper = 1.f;
};

struct FVtuberTrackingSnapshot
{
    // Monotonic local arrival time (FPlatformTime::Seconds), not the helper's Unix timestamp.
    double TimestampSeconds = 0;
    bool bFaceTracked = false;
    bool bPoseTracked = false;
    // Canonical UE coordinates; +yaw turns right, +pitch looks up, +roll tips right.
    FQuat HeadRotation = FQuat::Identity;
    TMap<FName, float> BlendShapes;
    // MediaPipe Pose indices follow the official 33-point order. Coordinates are
    // camera-normalized for PoseLandmarks and metric camera-space for World.
    TArray<FVtuberLandmark> PoseLandmarks;
    TArray<FVtuberLandmark> PoseWorldLandmarks;
    TArray<FVtuberHandTracking> Hands;
};

struct FVtuberRigSettings
{
    bool bHeadEnabled = true;
    bool bExpressionsEnabled = true;
    bool bBodyEnabled = true;
    bool bHandsEnabled = true;
    float SmoothingSeconds = 0.10f;
    float LostTrackingSeconds = 0.30f;
    float ReturnSeconds = 0.35f;
    float YawLimitDegrees = 55;
    float PitchLimitDegrees = 30;
    float RollLimitDegrees = 30;
    // Positive and negative directions are independent amplitudes, never clamped together.
    FVtuberThresholdCurve YawPositive, YawNegative, PitchPositive, PitchNegative, RollPositive, RollNegative;
};

UCLASS(Transient)
class UVtuberAnimInstance : public UVrmAnimInstanceCopy
{
    GENERATED_BODY()
public:
    FName HeadBone;
    FQuat HeadLocalRotation = FQuat::Identity;
    // Component-space targets are converted to local rotations by the native
    // proxy after the ref pose has been reset. This keeps parent/child chains
    // stable without modifying the source skeletal mesh.
    TMap<FName, FQuat> BodyComponentRotations;
protected:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FVtuberProxyLifecycleTest;
#endif
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
};

UCLASS()
class UVtuberRigComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UVtuberRigComponent();
    bool Initialize(USkeletalMeshComponent* InMesh, const FString& InProfilePath = FString());
    void SubmitSnapshot(const FVtuberTrackingSnapshot& Snapshot);
    bool CalibrateNeutral();
    void ResetNeutral();
    bool HasTracking() const;
    const FVtuberRigSettings& GetSettings() const { return Settings; }
    bool SetSettings(const FVtuberRigSettings& Value);
    FVtuberChannelResponse GetChannelResponse(FName Channel) const;
    float EvaluateChannelResponse(FName Channel, float Raw) const;
    bool SetChannelResponse(FName Channel, const FVtuberChannelResponse& Response);
    void ResetChannelResponse(FName Channel);
    void ResetAllChannelResponses();
    const TArray<FVtuberMorphMapping>& GetMappings() const { return Mappings; }
    bool SetMapping(const FVtuberMorphMapping& Mapping);
    bool SetHeadBone(FName Bone);
    FName GetHeadBone() const { return HeadBone; }
    static TArray<FName> GetBodyTrackingChannels(bool bHands);
    FName GetBodyBone(FName Channel) const;
    bool SetBodyBone(FName Channel, FName Bone);
    bool IsBodyChannelTracked(FName Channel) const;
    float GetBodyChannelOutputDegrees(FName Channel) const;
    TArray<FName> GetAvailableBones() const;
    TArray<FName> GetAvailableMorphs() const;
    TArray<FString> GetMissingMappings() const;
    TArray<FString> GetMissingBodyMappings() const;
    float GetChannelInput(FName Channel) const;
    float GetChannelOutput(FName Channel) const;
    bool LoadProfile(const FString& Path, FString& Error);
    bool SaveProfile(const FString& Path, FString& Error) const;
    bool SaveProfile(FString& Error) const { return SaveProfile(ProfilePath, Error); }
    const FString& GetProfileWarning() const { return ProfileWarning; }
    // Call after bone finalization; returned world vector feeds the existing face material parameter.
    FVector GetFaceLightDirection(const FVector& WorldLightDirection) const;
    static const TArray<FName>& GetTrackingChannels();
    static float MapSignedAngle(float Degrees, float SafeLimit, const FVtuberThresholdCurve& Positive, const FVtuberThresholdCurve& Negative);
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FVtuberRuntimeRigTest;
    friend class FVtuberAnatomicalRetargetTest;
#endif
    void UpdateTracking(float DeltaTime);
    void BuildDefaultMappings();
    void BuildBodyMappings();
    void RefreshHeadReference();
    float MapResponse(FName Channel, float Input, float DeltaTime, bool bTracked, bool bRotation, bool bFilter = true);
    UPROPERTY() TObjectPtr<USkeletalMeshComponent> Mesh;
    FVtuberTrackingSnapshot Latest;
    FVtuberRigSettings Settings;
    TArray<FVtuberMorphMapping> Mappings;
    TMap<FName, float> NeutralExpressions, SmoothedExpressions, ChannelOutputs;
    TMap<FName, float> ChannelInputs, ResponseFiltered;
    TMap<FName, FVtuberChannelResponse> ChannelResponses;
    TSet<FName> AppliedMorphs;
    FName HeadBone;
    TMap<FName, FName> BodyBones;
    TMap<FName, FQuat> SmoothedBody;
    TSet<FName> TrackedBodyChannels;
    TArray<FTransform> ReferenceComponentPose;
    FQuat NeutralBodyFrame = FQuat::Identity;
    FQuat SmoothedHeadLocal = FQuat::Identity;
    FQuat NeutralHead = FQuat::Identity;
    FQuat SmoothedHead = FQuat::Identity;
    FQuat CanonicalToComponent = FQuat::Identity;
    FQuat HeadBindCS = FQuat::Identity;
    FQuat ParentBindCS = FQuat::Identity;
    FString ProfilePath, ProfileWarning;
};
