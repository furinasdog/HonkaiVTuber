#include "VtuberRig.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/MorphTarget.h"
#include "AnimationRuntime.h"
#include "BonePose.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "VrmUtil.h"
#include "VrmAssetListObject.h"
#include "VrmMetaObject.h"
#include "VrmPmxPhysics.h"
#include "VrmPmxPhysicsData.h"
#include "AnimNode_VrmSpringBone.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Math/RotationMatrix.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    struct FChannelAliases { const TCHAR* Channel; const TCHAR* Aliases; };
    // Generic PMX/VRM vocabulary only. A shared bilateral morph is combined with Max, not added twice.
    const FChannelAliases Channels[] = {
        {TEXT("_neutral"), TEXT("")},
        {TEXT("browDownLeft"), TEXT("怒り左|怒り|困る|眉下左|眉下|browDownL|angry")},
        {TEXT("browDownRight"), TEXT("怒り右|怒り|困る|眉下右|眉下|browDownR|angry")},
        {TEXT("browInnerUp"), TEXT("困る|眉上|browUp|troubled")},
        {TEXT("browOuterUpLeft"), TEXT("上左|上|眉上左|眉上|browUpL")},
        {TEXT("browOuterUpRight"), TEXT("上右|上|眉上右|眉上|browUpR")},
        {TEXT("cheekPuff"), TEXT("頬膨らみ|ぷくー|鼓腮")},
        {TEXT("cheekSquintLeft"), TEXT("頬上げ左|cheekSquintL")},
        {TEXT("cheekSquintRight"), TEXT("頬上げ右|cheekSquintR")},
        {TEXT("eyeBlinkLeft"), TEXT("ウィンク|ウィンク２|まばたき左|まばたき|左眨眼|眨眼|blinkL|blink")},
        {TEXT("eyeBlinkRight"), TEXT("ウィンク右|ウィンク２右|まばたき右|まばたき|右眨眼|眨眼|blinkR|blink")},
        {TEXT("eyeLookDownLeft"), TEXT("目下左|eyeLookDownL")},
        {TEXT("eyeLookDownRight"), TEXT("目下右|eyeLookDownR")},
        {TEXT("eyeLookInLeft"), TEXT("目内左|eyeLookInL")},
        {TEXT("eyeLookInRight"), TEXT("目内右|eyeLookInR")},
        {TEXT("eyeLookOutLeft"), TEXT("目外左|eyeLookOutL")},
        {TEXT("eyeLookOutRight"), TEXT("目外右|eyeLookOutR")},
        {TEXT("eyeLookUpLeft"), TEXT("目上左|eyeLookUpL")},
        {TEXT("eyeLookUpRight"), TEXT("目上右|eyeLookUpR")},
        {TEXT("eyeSquintLeft"), TEXT("笑い左|笑い|細目|eyeSquintL")},
        {TEXT("eyeSquintRight"), TEXT("笑い右|笑い|細目|eyeSquintR")},
        {TEXT("eyeWideLeft"), TEXT("見開き左|見開き|びっくり|睁大左|睁大|eyeWideL")},
        {TEXT("eyeWideRight"), TEXT("見開き右|見開き|びっくり|睁大右|睁大|eyeWideR")},
        {TEXT("jawForward"), TEXT("顎前|下巴前")},
        {TEXT("jawLeft"), TEXT("顎左|下巴左")},
        {TEXT("jawOpen"), TEXT("あ|あ２|ア|A|口開け|张嘴|mouthOpen|aa")},
        {TEXT("jawRight"), TEXT("顎右|下巴右")},
        {TEXT("mouthClose"), TEXT("口閉じ|闭嘴")},
        {TEXT("mouthDimpleLeft"), TEXT("口角左|mouthDimpleL")},
        {TEXT("mouthDimpleRight"), TEXT("口角右|mouthDimpleR")},
        {TEXT("mouthFrownLeft"), TEXT("口角下げ左|への字|mouthFrownL")},
        {TEXT("mouthFrownRight"), TEXT("口角下げ右|への字|mouthFrownR")},
        {TEXT("mouthFunnel"), TEXT("お|オ|O|oh")},
        {TEXT("mouthLeft"), TEXT("口左")},
        {TEXT("mouthLowerDownLeft"), TEXT("下唇下左|mouthLowerDownL")},
        {TEXT("mouthLowerDownRight"), TEXT("下唇下右|mouthLowerDownR")},
        {TEXT("mouthPressLeft"), TEXT("唇押左|mouthPressL")},
        {TEXT("mouthPressRight"), TEXT("唇押右|mouthPressR")},
        {TEXT("mouthPucker"), TEXT("う|ウ|U|すぼめ|嘟嘴|ou")},
        {TEXT("mouthRight"), TEXT("口右")},
        {TEXT("mouthRollLower"), TEXT("下唇巻込")},
        {TEXT("mouthRollUpper"), TEXT("上唇巻込")},
        {TEXT("mouthShrugLower"), TEXT("下唇上")},
        {TEXT("mouthShrugUpper"), TEXT("上唇上")},
        {TEXT("mouthSmileLeft"), TEXT("にやり左|にやり|口角上げ|微笑左|微笑|mouthSmileL|happy|joy")},
        {TEXT("mouthSmileRight"), TEXT("にやり右|にやり|口角上げ|微笑右|微笑|mouthSmileR|happy|joy")},
        {TEXT("mouthStretchLeft"), TEXT("い左|い|イ|I|mouthStretchL|ih")},
        {TEXT("mouthStretchRight"), TEXT("い右|い|イ|I|mouthStretchR|ih")},
        {TEXT("mouthUpperUpLeft"), TEXT("上唇上左|mouthUpperUpL")},
        {TEXT("mouthUpperUpRight"), TEXT("上唇上右|mouthUpperUpR")},
        {TEXT("noseSneerLeft"), TEXT("鼻左|noseSneerL")},
        {TEXT("noseSneerRight"), TEXT("鼻右|noseSneerR")}
    };

    // MediaPipe pose/hand landmark pairs. The names are deliberately generic
    // so the same map works for PMX, VRM and Mixamo skeletons.
    struct FBodyTargetDefinition
    {
        const TCHAR* Key;
        const TCHAR* Aliases;
        uint8 Domain; // 0 = pose, 1 = left hand, 2 = right hand
        int32 A;
        int32 B;
        bool bHand;
    };

    const FBodyTargetDefinition BodyTargets[] = {
        {TEXT("spine"), TEXT("spine|上半身|body|mixamorig:Spine|J_Bip_C_Spine"), 0, 23, 11, false},
        {TEXT("chest"), TEXT("chest|upperchest|胸|上半身2|胸椎|mixamorig:Spine2|J_Bip_C_Chest"), 0, 23, 11, false},
        {TEXT("neck"), TEXT("neck|首|頸|mixamorig:Neck|J_Bip_C_Neck"), 0, 11, 0, false},
        {TEXT("shoulder_l"), TEXT("shoulder_l|leftshoulder|lshoulder|左肩|肩左|mixamorig:LeftShoulder|J_Bip_L_Shoulder"), 0, 11, 13, false},
        {TEXT("shoulder_r"), TEXT("shoulder_r|rightshoulder|rshoulder|右肩|肩右|mixamorig:RightShoulder|J_Bip_R_Shoulder"), 0, 12, 14, false},
        {TEXT("upperarm_l"), TEXT("upperarm_l|leftupperarm|lupperarm|左腕|腕左|腕|mixamorig:LeftArm|J_Bip_L_UpperArm"), 0, 11, 13, false},
        {TEXT("upperarm_r"), TEXT("upperarm_r|rightupperarm|rupperarm|右腕|腕右|mixamorig:RightArm|J_Bip_R_UpperArm"), 0, 12, 14, false},
        {TEXT("lowerarm_l"), TEXT("lowerarm_l|leftlowerarm|llowerarm|左ひじ|左肘|肘左|mixamorig:LeftForeArm|J_Bip_L_LowerArm"), 0, 13, 15, false},
        {TEXT("lowerarm_r"), TEXT("lowerarm_r|rightlowerarm|rlowerarm|右ひじ|右肘|肘右|mixamorig:RightForeArm|J_Bip_R_LowerArm"), 0, 14, 16, false},
        {TEXT("hand_l"), TEXT("hand_l|lefthand|lhand|左手首|左手|手左|mixamorig:LeftHand|J_Bip_L_Hand"), 0, 15, 17, false},
        {TEXT("hand_r"), TEXT("hand_r|righthand|rhand|右手首|右手|手右|mixamorig:RightHand|J_Bip_R_Hand"), 0, 16, 18, false},
        {TEXT("thumb1_l"), TEXT("thumb1_l|leftthumb1|lthumb1|左親指0|左親指０|拇指1左|mixamorig:LeftHandThumb1|J_Bip_L_Thumb1"), 1, 1, 2, true},
        {TEXT("thumb2_l"), TEXT("thumb2_l|leftthumb2|lthumb2|左親指1|左親指１|拇指2左|mixamorig:LeftHandThumb2|J_Bip_L_Thumb2"), 1, 2, 3, true},
        {TEXT("thumb3_l"), TEXT("thumb3_l|leftthumb3|lthumb3|左親指2|左親指２|拇指3左|mixamorig:LeftHandThumb3|J_Bip_L_Thumb3"), 1, 3, 4, true},
        {TEXT("index1_l"), TEXT("index1_l|leftindex1|lindex1|左人指1|左人指１|食指1左"), 1, 5, 6, true},
        {TEXT("index2_l"), TEXT("index2_l|leftindex2|lindex2|左人指2|左人指２|食指2左"), 1, 6, 7, true},
        {TEXT("index3_l"), TEXT("index3_l|leftindex3|lindex3|左人指3|左人指３|食指3左"), 1, 7, 8, true},
        {TEXT("middle1_l"), TEXT("middle1_l|leftmiddle1|lmiddle1|左中指1|左中指１|中指1左"), 1, 9, 10, true},
        {TEXT("middle2_l"), TEXT("middle2_l|leftmiddle2|lmiddle2|左中指2|左中指２|中指2左"), 1, 10, 11, true},
        {TEXT("middle3_l"), TEXT("middle3_l|leftmiddle3|lmiddle3|左中指3|左中指３|中指3左"), 1, 11, 12, true},
        {TEXT("ring1_l"), TEXT("ring1_l|leftring1|lring1|左薬指1|左薬指１|无名指1左"), 1, 13, 14, true},
        {TEXT("ring2_l"), TEXT("ring2_l|leftring2|lring2|左薬指2|左薬指２|无名指2左"), 1, 14, 15, true},
        {TEXT("ring3_l"), TEXT("ring3_l|leftring3|lring3|左薬指3|左薬指３|无名指3左"), 1, 15, 16, true},
        {TEXT("pinky1_l"), TEXT("pinky1_l|leftpinky1|lpinky1|左小指1|左小指１|小指1左"), 1, 17, 18, true},
        {TEXT("pinky2_l"), TEXT("pinky2_l|leftpinky2|lpinky2|左小指2|左小指２|小指2左"), 1, 18, 19, true},
        {TEXT("pinky3_l"), TEXT("pinky3_l|leftpinky3|lpinky3|左小指3|左小指３|小指3左"), 1, 19, 20, true},
        {TEXT("thumb1_r"), TEXT("thumb1_r|rightthumb1|rthumb1|右親指0|右親指０|拇指1右|mixamorig:RightHandThumb1|J_Bip_R_Thumb1"), 2, 1, 2, true},
        {TEXT("thumb2_r"), TEXT("thumb2_r|rightthumb2|rthumb2|右親指1|右親指１|拇指2右|mixamorig:RightHandThumb2|J_Bip_R_Thumb2"), 2, 2, 3, true},
        {TEXT("thumb3_r"), TEXT("thumb3_r|rightthumb3|rthumb3|右親指2|右親指２|拇指3右|mixamorig:RightHandThumb3|J_Bip_R_Thumb3"), 2, 3, 4, true},
        {TEXT("index1_r"), TEXT("index1_r|rightindex1|rindex1|右人指1|右人指１|食指1右"), 2, 5, 6, true},
        {TEXT("index2_r"), TEXT("index2_r|rightindex2|rindex2|右人指2|右人指２|食指2右"), 2, 6, 7, true},
        {TEXT("index3_r"), TEXT("index3_r|rightindex3|rindex3|右人指3|右人指３|食指3右"), 2, 7, 8, true},
        {TEXT("middle1_r"), TEXT("middle1_r|rightmiddle1|rmiddle1|右中指1|右中指１|中指1右"), 2, 9, 10, true},
        {TEXT("middle2_r"), TEXT("middle2_r|rightmiddle2|rmiddle2|右中指2|右中指２|中指2右"), 2, 10, 11, true},
        {TEXT("middle3_r"), TEXT("middle3_r|rightmiddle3|rmiddle3|右中指3|右中指３|中指3右"), 2, 11, 12, true},
        {TEXT("ring1_r"), TEXT("ring1_r|rightring1|rring1|右薬指1|右薬指１|无名指1右"), 2, 13, 14, true},
        {TEXT("ring2_r"), TEXT("ring2_r|rightring2|rring2|右薬指2|右薬指２|无名指2右"), 2, 14, 15, true},
        {TEXT("ring3_r"), TEXT("ring3_r|rightring3|rring3|右薬指3|右薬指３|无名指3右"), 2, 15, 16, true},
        {TEXT("pinky1_r"), TEXT("pinky1_r|rightpinky1|rpinky1|右小指1|右小指１|小指1右"), 2, 17, 18, true},
        {TEXT("pinky2_r"), TEXT("pinky2_r|rightpinky2|rpinky2|右小指2|右小指２|小指2右"), 2, 18, 19, true},
        {TEXT("pinky3_r"), TEXT("pinky3_r|rightpinky3|rpinky3|右小指3|右小指３|小指3右"), 2, 19, 20, true}
    };

    struct FBodyAnimTarget
    {
        int32 MeshBoneIndex = INDEX_NONE;
        int32 Depth = 0;
        FQuat ComponentRotation = FQuat::Identity;
    };

    FVector TrackingToCanonical(const FVector& Value)
    {
        // MediaPipe camera coordinates are x-right, y-down, z-depth. The
        // rig's canonical basis is +X-forward, +Y-right, +Z-up.
        return FVector(-Value.Z, -Value.X, -Value.Y);
    }

    bool ReadPosePoint(const FVtuberTrackingSnapshot& Snapshot, int32 Index, FVector& Out)
    {
        const TArray<FVtuberLandmark>& Points = Snapshot.PoseWorldLandmarks;
        if (!Points.IsValidIndex(Index)) return false;
        const FVtuberLandmark& Point = Points[Index];
        if (!FMath::IsFinite(Point.Visibility) || Point.Visibility < .45f) return false;
        if (Snapshot.PoseLandmarks.IsValidIndex(Index))
        {
            const auto& Image = Snapshot.PoseLandmarks[Index];
            if (Image.Position.ContainsNaN() || Image.Position.X < 0 || Image.Position.X > 1 || Image.Position.Y < 0 || Image.Position.Y > 1
                || !FMath::IsFinite(Image.Visibility) || Image.Visibility < .45f) return false;
        }
        Out = TrackingToCanonical(Point.Position);
        return !Out.ContainsNaN();
    }

    bool ReadHandPoint(const FVtuberTrackingSnapshot& Snapshot, uint8 Domain, int32 Index, FVector& Out)
    {
        const FName Wanted = Domain == 1 ? FName(TEXT("Left")) : FName(TEXT("Right"));
        for (const FVtuberHandTracking& Hand : Snapshot.Hands)
        {
            if (!Hand.Handedness.ToString().Equals(Wanted.ToString(), ESearchCase::IgnoreCase)) continue;
            const TArray<FVtuberLandmark>& Points = Hand.WorldLandmarks;
            if (!Points.IsValidIndex(Index) || !FMath::IsFinite(Hand.Score) || Hand.Score < .45f) continue;
            if (Hand.Landmarks.IsValidIndex(Index))
            {
                const FVector& Image = Hand.Landmarks[Index].Position;
                if (Image.ContainsNaN() || Image.X < 0 || Image.X > 1 || Image.Y < 0 || Image.Y > 1) continue;
            }
            Out = TrackingToCanonical(Points[Index].Position);
            return !Out.ContainsNaN();
        }
        return false;
    }

    bool ReadTargetDirection(const FVtuberTrackingSnapshot& Snapshot, const FBodyTargetDefinition& Definition, FVector& Out)
    {
        FVector A, B;
        const bool GoodA = Definition.Domain == 0 ? ReadPosePoint(Snapshot, Definition.A, A) : ReadHandPoint(Snapshot, Definition.Domain, Definition.A, A);
        const bool GoodB = Definition.Domain == 0 ? ReadPosePoint(Snapshot, Definition.B, B) : ReadHandPoint(Snapshot, Definition.Domain, Definition.B, B);
        return GoodA && GoodB && (Out = (B - A).GetSafeNormal()).IsNearlyZero() == false;
    }

    const FName HeadChannels[] = {TEXT("headYawPositive"), TEXT("headYawNegative"), TEXT("headPitchPositive"),
        TEXT("headPitchNegative"), TEXT("headRollPositive"), TEXT("headRollNegative")};

    bool IsBodyChannel(FName Name)
    {
        for (const auto& Definition : BodyTargets) if (Name == Definition.Key) return true;
        return false;
    }

    bool IsKnownChannel(FName Name)
    {
        if (IsBodyChannel(Name) || UVtuberRigComponent::GetTrackingChannels().Contains(Name)) return true;
        for (FName HeadChannel : HeadChannels) if (HeadChannel == Name) return true;
        return false;
    }

    bool MakeFrame(const FVector& Across, const FVector& Along, FQuat& Out)
    {
        if (Across.ContainsNaN() || Along.ContainsNaN() || Across.SizeSquared() < 1.e-6 || Along.SizeSquared() < 1.e-6) return false;
        const FVector Y = Along.GetSafeNormal();
        const FVector X = (Across - Y * FVector::DotProduct(Across, Y)).GetSafeNormal();
        if (X.IsNearlyZero()) return false;
        Out = FRotationMatrix::MakeFromXY(X, Y).ToQuat().GetNormalized();
        return !Out.ContainsNaN();
    }

    bool ReadBodyFrame(const FVtuberTrackingSnapshot& Snapshot, FQuat& Out)
    {
        FVector Left, Right, HipLeft, HipRight;
        if (!ReadPosePoint(Snapshot, 11, Left) || !ReadPosePoint(Snapshot, 12, Right)) return false;
        FVector Up = FVector::UpVector;
        if (ReadPosePoint(Snapshot, 23, HipLeft) && ReadPosePoint(Snapshot, 24, HipRight))
            Up = (Left + Right - HipLeft - HipRight) * .5;
        Up = Up.GetSafeNormal();
        const FVector Forward = FVector::CrossProduct(Up, Left - Right).GetSafeNormal();
        if (Forward.IsNearlyZero() || Up.IsNearlyZero()) return false;
        Out = FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat().GetNormalized();
        return !Out.ContainsNaN();
    }

    FQuat BoundedTorso(const FQuat& Rotation)
    {
        const FRotator A = Rotation.Rotator().GetNormalized();
        return FRotator(FMath::Clamp(A.Pitch, -20.f, 20.f), FMath::Clamp(A.Yaw, -37.f, 37.f),
            FMath::Clamp(A.Roll, -17.f, 17.f)).Quaternion();
    }

    void ShortAxisAngle(FQuat Rotation, FVector& Axis, float& Angle)
    {
        Rotation.Normalize();
        if (Rotation.W < 0) Rotation = Rotation * -1.;
        const FVector V(Rotation.X, Rotation.Y, Rotation.Z);
        Angle = 2.f * FMath::Atan2(V.Size(), Rotation.W);
        Axis = Angle > 1.e-7f ? V.GetSafeNormal() : FVector::ForwardVector;
    }

    float LocalLimit(FName Channel)
    {
        const FString Key = Channel.ToString();
        if (Key.StartsWith(TEXT("upperarm"))) return 150.f;
        if (Key.StartsWith(TEXT("lowerarm"))) return 160.f;
        if (Key.StartsWith(TEXT("hand"))) return 85.f;
        if (Key == TEXT("neck")) return 50.f;
        return IsBodyChannel(Channel) && (Key == TEXT("spine") || Key == TEXT("chest") || Key.StartsWith(TEXT("shoulder"))) ? 65.f : 110.f;
    }

    bool FiniteRange(float Value, float Min, float Max)
    {
        return FMath::IsFinite(Value) && Value >= Min && Value <= Max;
    }

    FString NormalizeName(FString Name)
    {
        Name.ToLowerInline();
        for (const TCHAR* Separator : {TEXT(" "), TEXT("_"), TEXT("-"), TEXT(".")})
            Name.ReplaceInline(Separator, TEXT(""));
        return Name;
    }

    FName FindAlias(const TArray<FName>& Available, const FString& Aliases)
    {
        TArray<FString> Candidates;
        Aliases.ParseIntoArray(Candidates, TEXT("|"), true);
        for (const FString& Candidate : Candidates)
            for (FName Name : Available)
                if (NormalizeName(Name.ToString()) == NormalizeName(Candidate)) return Name;
        return NAME_None;
    }

    bool ValidSettings(const FVtuberRigSettings& S)
    {
        return FiniteRange(S.SmoothingSeconds, 0, 2) && FiniteRange(S.LostTrackingSeconds, .05f, 5)
            && FiniteRange(S.ReturnSeconds, .05f, 5) && FiniteRange(S.YawLimitDegrees, 1, 90)
            && FiniteRange(S.PitchLimitDegrees, 1, 60) && FiniteRange(S.RollLimitDegrees, 1, 60)
            && S.YawPositive.IsValid() && S.YawNegative.IsValid() && S.PitchPositive.IsValid()
            && S.PitchNegative.IsValid() && S.RollPositive.IsValid() && S.RollNegative.IsValid();
    }

    bool ValidMapping(const FVtuberMorphMapping& M)
    {
        return UVtuberRigComponent::GetTrackingChannels().Contains(M.Channel)
            && M.Curve.IsValid() && FiniteRange(M.Maximum, 0, 1) && FiniteRange(M.Sensitivity, 0, 2);
    }

    TSharedPtr<FJsonObject> CurveJson(const FVtuberThresholdCurve& C)
    {
        auto O = MakeShared<FJsonObject>();
        O->SetNumberField(TEXT("a"), C.Start); O->SetNumberField(TEXT("b"), C.Full);
        return O;
    }

    bool ReadNumber(const FJsonObject& O, const TCHAR* Key, float& Out)
    {
        double Number;
        if (!O.TryGetNumberField(Key, Number) || !FMath::IsFinite(Number) || FMath::Abs(Number) > MAX_flt) return false;
        Out = static_cast<float>(Number);
        return true;
    }

    bool ReadCurve(const FJsonObject& O, const TCHAR* Key, FVtuberThresholdCurve& Out)
    {
        const TSharedPtr<FJsonObject>* C;
        return O.TryGetObjectField(Key, C) && ReadNumber(**C, TEXT("a"), Out.Start)
            && ReadNumber(**C, TEXT("b"), Out.Full) && Out.IsValid();
    }

    class FVtuberAnimProxy : public FVrmAnimInstanceCopyProxy
    {
    public:
        explicit FVtuberAnimProxy(UAnimInstance* Instance) : FVrmAnimInstanceCopyProxy(Instance) {}
        virtual void PreUpdate(UAnimInstance* Instance, float DeltaSeconds) override
        {
            FVrmAnimInstanceCopyProxy::PreUpdate(Instance, DeltaSeconds);
            const auto* Driver = CastChecked<UVtuberAnimInstance>(Instance);
            UVrmMetaObject* Meta = Driver->DstVrmAssetList ? Driver->DstVrmAssetList->VrmMetaObject : nullptr;
            auto* Component = Driver->GetSkelMeshComponent();
            auto* SkelMesh = Component ? Component->GetSkeletalMeshAsset() : nullptr;
            UVrmPmxPhysicsData* Data = Driver->DstVrmAssetList ? Driver->DstVrmAssetList->PmxPhysicsData : nullptr;
            CachePmxPhysics(Data, SkelMesh, Component ? Component->GetComponentTransform() : FTransform::Identity);
            PmxDeltaSeconds = DeltaSeconds;
            bHasSpringMeta = Meta != nullptr && !bHasPmxData;
            SpringDeltaSeconds = FMath::Clamp(DeltaSeconds, 0.f, .05f);
            if (!bHasPmxData && Node_SpringBone.IsValid() && Node_SpringBone->VrmMetaObject_Internal.Get() != Meta)
            {
                Node_SpringBone->VrmMetaObject_Internal = Meta;
                Node_SpringBone->ResetDynamics(ETeleportType::ResetPhysics);
            }
            MeshBoneIndex = SkelMesh ? SkelMesh->GetRefSkeleton().FindBoneIndex(Driver->HeadBone) : INDEX_NONE;
            Rotation = Driver->HeadLocalRotation;
            BodyTargets.Reset();
            if (SkelMesh)
            {
                const FReferenceSkeleton& Ref = SkelMesh->GetRefSkeleton();
                for (const auto& Entry : Driver->BodyComponentRotations)
                {
                    const int32 Index = Ref.FindBoneIndex(Entry.Key);
                    if (Index == INDEX_NONE || Entry.Value.ContainsNaN() || Entry.Value.SizeSquared() < .5 || Entry.Value.SizeSquared() > 1.5) continue;
                    FBodyAnimTarget Target;
                    Target.MeshBoneIndex = Index;
                    Target.ComponentRotation = Entry.Value.GetNormalized();
                    for (int32 Parent = Ref.GetParentIndex(Index); Parent != INDEX_NONE; Parent = Ref.GetParentIndex(Parent)) ++Target.Depth;
                    BodyTargets.Add(Target);
                }
                BodyTargets.Sort([](const FBodyAnimTarget& A, const FBodyAnimTarget& B) { return A.Depth < B.Depth; });
            }
        }
        virtual bool Evaluate(FPoseContext& Output) override
        {
            Output.ResetToRefPose();
            if (MeshBoneIndex != INDEX_NONE && !Rotation.ContainsNaN() && Rotation.SizeSquared() >= .5 && Rotation.SizeSquared() <= 1.5)
            {
                const FCompactPoseBoneIndex Index = Output.Pose.GetBoneContainer().MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshBoneIndex));
                if (Index != INDEX_NONE) Output.Pose[Index].SetRotation(Rotation.GetNormalized());
            }
            FCSPose<FCompactPose> ComponentPose;
            ComponentPose.InitPose(Output.Pose);
            for (const FBodyAnimTarget& Target : BodyTargets)
            {
                const FCompactPoseBoneIndex Index = Output.Pose.GetBoneContainer().MakeCompactPoseIndex(FMeshPoseBoneIndex(Target.MeshBoneIndex));
                if (Index == INDEX_NONE) continue;
                const FCompactPoseBoneIndex Parent = Output.Pose.GetParentBoneIndex(Index);
                const FQuat ParentCS = Parent != INDEX_NONE ? ComponentPose.GetComponentSpaceTransform(Parent).GetRotation() : FQuat::Identity;
                Output.Pose[Index].SetRotation((ParentCS.Inverse() * Target.ComponentRotation).GetNormalized());
                ComponentPose.InitPose(Output.Pose);
            }
            if (bHasPmxData)
            {
                ApplyPmxPhysics(Output);
                return true;
            }
            if (bHasSpringMeta && !bIgnoreVRMSwingBone && Node_SpringBone.IsValid())
            {
                auto& Spring = *Node_SpringBone;
                Spring.bCallByAnimInstance = true;
                Spring.CurrentDeltaTime = SpringDeltaSeconds;
                if (!Spring.IsSpringInit())
                {
                    FAnimationInitializeContext Context(this);
                    Spring.Initialize_AnyThread_local(Context);
                    Spring.ComponentPose.SetLinkNode(&Spring);
                }
                FComponentSpacePoseContext SpringPose(this);
                SpringPose.Pose.InitPose(Output.Pose);
                Spring.EvaluateComponentSpace_AnyThread(SpringPose);
                FCSPose<FCompactPose>::ConvertComponentPosesToLocalPosesSafe(SpringPose.Pose, Output.Pose);
            }
            return true;
        }
    private:
        void ReportPmxFailure(const TCHAR* Stage, const FString& Error)
        {
            if (bPmxFailureReported) return;
            bPmxFailureReported = true;
            UE_LOG(LogTemp, Warning, TEXT("PMX_PHYSICS %s failed: %s; animated pose retained."), Stage, *Error);
        }
        void CachePmxPhysics(UVrmPmxPhysicsData* Data, USkeletalMesh* SkelMesh, const FTransform& ToWorld)
        {
            bHasPmxData = Data != nullptr;
            PmxComponentToWorld = ToWorld;
            const bool bNewModel = PmxData.Get() != Data || PmxMesh.Get() != SkelMesh;
            if (!bNewModel && PmxScale.Equals(ToWorld.GetScale3D(), .000001)) return;
            if (bNewModel) bPmxFailureReported = false;
            PmxData = Data; PmxMesh = SkelMesh; PmxScale = ToWorld.GetScale3D();
            PmxPhysics.Reset(); PmxReferencePose.Reset(); PmxParents.Reset(); bPmxNeedsPoseReset = false;
            if (!Data) return;
            if (!SkelMesh)
            {
                ReportPmxFailure(TEXT("Initialize"), TEXT("PMX metadata has no skeletal mesh."));
                return;
            }
            const FReferenceSkeleton& Ref = SkelMesh->GetRefSkeleton();
            PmxReferencePose = Ref.GetRefBonePose();
            for (int32 Index = 0; Index < Ref.GetNum(); ++Index) PmxParents.Add(Ref.GetParentIndex(Index));
            PmxPhysics = MakeUnique<FVrmPmxPhysics>();
            FString Error;
            if (!PmxPhysics->Initialize(*Data, Ref, ToWorld, Error))
            {
                ReportPmxFailure(TEXT("Initialize"), Error);
                PmxPhysics.Reset();
                return;
            }
            bPmxNeedsPoseReset = true;
            UE_LOG(LogTemp, Display, TEXT("PMX_PHYSICS initialized bodies=%d joints=%d"), Data->RigidBodies.Num(), Data->Joints.Num());
        }
        static bool ValidPhysicsPose(const FTransform& Transform)
        {
            return !Transform.ContainsNaN() && Transform.GetRotation().IsNormalized();
        }
        void ApplyPmxPhysics(FPoseContext& Output)
        {
            if (!PmxPhysics || !PmxPhysics->IsInitialized()) return;
            const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
            if (Bones.GetReferenceSkeleton().GetNum() != PmxReferencePose.Num())
            {
                ReportPmxFailure(TEXT("Evaluate"), TEXT("The pose bone container does not match the PMX skeleton."));
                return;
            }
            TArray<FTransform> Pose;
            Pose.SetNum(PmxReferencePose.Num());
            for (int32 Index = 0; Index < Pose.Num(); ++Index)
            {
                const FCompactPoseBoneIndex Compact = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Index));
                const FTransform& Local = Compact == INDEX_NONE ? PmxReferencePose[Index] : Output.Pose[Compact];
                const int32 Parent = PmxParents[Index];
                if (!ValidPhysicsPose(Local))
                {
                    ReportPmxFailure(TEXT("Evaluate"), TEXT("The animated PMX pose contains an invalid local transform."));
                    return;
                }
                Pose[Index] = Parent == INDEX_NONE ? Local : Local * Pose[Parent];
                if (!ValidPhysicsPose(Pose[Index]))
                {
                    ReportPmxFailure(TEXT("Evaluate"), TEXT("The animated PMX pose contains an invalid transform."));
                    return;
                }
            }
            if (bPmxNeedsPoseReset)
            {
                if (!PmxPhysics->Reset(Pose, PmxComponentToWorld))
                {
                    ReportPmxFailure(TEXT("Reset"), TEXT("Cannot initialize PMX bodies from the current animated pose."));
                    return;
                }
                bPmxNeedsPoseReset = false;
            }
            FString Error;
            if (!PmxPhysics->Simulate(PmxDeltaSeconds, PmxComponentToWorld, Pose, Error))
            {
                ReportPmxFailure(TEXT("Simulate"), Error.IsEmpty() ? TEXT("The PMX simulator rejected the current frame.") : Error);
                return;
            }
            TArray<FTransform> LocalPose;
            LocalPose.SetNum(Pose.Num());
            for (int32 Index = 0; Index < Pose.Num(); ++Index)
            {
                if (!ValidPhysicsPose(Pose[Index]))
                {
                    ReportPmxFailure(TEXT("Evaluate"), TEXT("The PMX result contains an invalid component transform."));
                    return;
                }
                const int32 Parent = PmxParents[Index];
                LocalPose[Index] = Parent == INDEX_NONE ? Pose[Index] : Pose[Index].GetRelativeTransform(Pose[Parent]);
                LocalPose[Index].NormalizeRotation();
                if (!ValidPhysicsPose(LocalPose[Index]))
                {
                    ReportPmxFailure(TEXT("Evaluate"), TEXT("The PMX result contains an invalid local transform."));
                    return;
                }
            }
            for (int32 Index = 0; Index < LocalPose.Num(); ++Index)
            {
                const FCompactPoseBoneIndex Compact = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Index));
                if (Compact != INDEX_NONE) Output.Pose[Compact] = LocalPose[Index];
            }
        }
        TWeakObjectPtr<UVrmPmxPhysicsData> PmxData;
        TWeakObjectPtr<USkeletalMesh> PmxMesh;
        TUniquePtr<FVrmPmxPhysics> PmxPhysics;
        TArray<FTransform> PmxReferencePose;
        TArray<int32> PmxParents;
        FTransform PmxComponentToWorld = FTransform::Identity;
        FVector PmxScale = FVector::OneVector;
        float PmxDeltaSeconds = 0.f;
        bool bHasPmxData = false, bPmxNeedsPoseReset = false, bPmxFailureReported = false;
        bool bHasSpringMeta = false;
        float SpringDeltaSeconds = 0.f;
        int32 MeshBoneIndex = INDEX_NONE;
        FQuat Rotation = FQuat::Identity;
        TArray<FBodyAnimTarget> BodyTargets;
    };
}

bool FVtuberThresholdCurve::IsValid() const
{
    return FiniteRange(Start, 0, 1) && FiniteRange(Full, 0, 1) && Full > Start;
}

float FVtuberThresholdCurve::Evaluate(float Input) const
{
    if (!IsValid() || !FMath::IsFinite(Input)) return 0;
    if (Input <= Start) return 0;
    if (Input >= Full) return 1;
    return (Input - Start) / (Full - Start);
}

FAnimInstanceProxy* UVtuberAnimInstance::CreateAnimInstanceProxy()
{
    myProxy = new FVtuberAnimProxy(this);
    myProxy->bIgnoreVRMSwingBone = bIgnoreVRMSwingBone;
    myProxy->bIgnoreWindDirectionalSource = bIgnoreWindDirectionalSource;
    return myProxy;
}
void UVtuberAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
    if (myProxy == InProxy) myProxy = nullptr;
    delete InProxy;
}

UVtuberRigComponent::UVtuberRigComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
    BuildDefaultMappings();
    ResetAllChannelResponses();
}

const TArray<FName>& UVtuberRigComponent::GetTrackingChannels()
{
    static const TArray<FName> Names = [] { TArray<FName> Result; for (const auto& C : Channels) Result.Add(C.Channel); return Result; }();
    return Names;
}

TArray<FName> UVtuberRigComponent::GetBodyTrackingChannels(bool bHands)
{
    TArray<FName> Names;
    for (const FBodyTargetDefinition& Definition : BodyTargets)
        if (Definition.bHand == bHands) Names.Add(Definition.Key);
    return Names;
}

bool UVtuberRigComponent::Initialize(USkeletalMeshComponent* InMesh, const FString& InProfilePath)
{
    check(IsInGameThread());
    if (!InMesh || !InMesh->GetSkeletalMeshAsset()) return false;
    if (Mesh) Mesh->RemoveTickPrerequisiteComponent(this);
    Mesh = InMesh;
    CanonicalToComponent = Mesh->GetComponentQuat().Inverse().GetNormalized();
    ProfilePath = InProfilePath;
    Latest = FVtuberTrackingSnapshot();
    Settings = FVtuberRigSettings();
    ResetNeutral();
    SmoothedExpressions.Reset(); ChannelOutputs.Reset(); ChannelInputs.Reset(); ResponseFiltered.Reset(); AppliedMorphs.Reset(); SmoothedBody.Reset();
    TrackedBodyChannels.Reset();
    SmoothedHead = FQuat::Identity;
    BuildDefaultMappings();
    ResetAllChannelResponses();
    ProfileWarning.Reset();
    if (!ProfilePath.IsEmpty() && FPaths::FileExists(ProfilePath)) LoadProfile(ProfilePath, ProfileWarning);
    const auto& Rest = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
    for (const auto& Idle : IdleArmLocalRotations)
    {
        const int32 Index = Rest.FindBoneIndex(BodyBones.FindRef(Idle.Key));
        if (Index != INDEX_NONE)
            SmoothedBody.Add(Idle.Key, (Rest.GetRefBonePose()[Index].GetRotation().Inverse() * Idle.Value).GetNormalized());
    }
    Mesh->bPauseAnims = false;
    Mesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
    Mesh->SetAnimInstanceClass(UVtuberAnimInstance::StaticClass());
    Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Mesh->AddTickPrerequisiteComponent(this);
    if (auto* Instance = Cast<UVtuberAnimInstance>(Mesh->GetAnimInstance()))
    {
        Instance->HeadBone = HeadBone;
        Instance->HeadLocalRotation = (ParentBindCS.Inverse() * HeadBindCS).GetNormalized();
        Instance->BodyComponentRotations.Reset();
        if (UVrmAssetListObject* AssetList = VRMUtil::GetAssetListObject(Mesh->GetSkeletalMeshAsset()))
        {
            Instance->DstVrmAssetList = AssetList;
        }
        // Avatar collision uses imported colliders; world queries do not belong on animation workers.
        Instance->SetVrmSpringBoneBool(false, true, false, true);
    }
    // Publish the resting arms before the proxy seeds its secondary-physics pose.
    UpdateTracking(1.f / 60.f);
    return true;
}

void UVtuberRigComponent::BuildDefaultMappings()
{
    HeadBone = FindAlias(GetAvailableBones(), TEXT("head|頭|头|頭部|头部|J_Bip_C_Head|mixamorig:Head|bip01 head"));
    RefreshHeadReference();
    BuildBodyMappings();
    Mappings.Reset();
    const TArray<FName> Morphs = GetAvailableMorphs();
    for (const auto& C : Channels)
    {
        FVtuberMorphMapping M;
        M.Channel = C.Channel;
        M.Morph = FindAlias(Morphs, FString(C.Channel) + TEXT("|") + C.Aliases);
        M.bEnabled = M.Channel != FName(TEXT("_neutral"));
        Mappings.Add(M);
    }
}

void UVtuberRigComponent::BuildBodyMappings()
{
    BodyBones.Reset();
    const TArray<FName> Available = GetAvailableBones();
    for (const FBodyTargetDefinition& Definition : BodyTargets)
    {
        const FName Found = FindAlias(Available, Definition.Aliases);
        if (!Found.IsNone()) BodyBones.Add(Definition.Key, Found);
    }
    BuildIdleArmPose();
}

void UVtuberRigComponent::BuildIdleArmPose()
{
    IdleArmLocalRotations.Reset();
    if (!Mesh || !Mesh->GetSkeletalMeshAsset()) return;
    const auto& Ref = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
    if (ReferenceComponentPose.Num() != Ref.GetNum()) return;
    auto Index = [&](FName Channel) { return Ref.FindBoneIndex(BodyBones.FindRef(Channel)); };
    const int32 Left = Index(TEXT("upperarm_l")), Right = Index(TEXT("upperarm_r"));
    if (Left == INDEX_NONE || Right == INDEX_NONE || Left == Right) return;
    const FVector Across = (ReferenceComponentPose[Left].GetLocation() - ReferenceComponentPose[Right].GetLocation()).GetSafeNormal();
    if (Across.IsNearlyZero()) return;
    FVector Up = FVector::UpVector;
    const int32 Spine = Index(TEXT("spine"));
    const int32 Top = !HeadBone.IsNone() ? Ref.FindBoneIndex(HeadBone) : Index(TEXT("neck"));
    if (Spine != INDEX_NONE && Top != INDEX_NONE)
        Up = ReferenceComponentPose[Top].GetLocation() - ReferenceComponentPose[Spine].GetLocation();
    Up = (Up - Across * FVector::DotProduct(Up, Across)).GetSafeNormal();
    if (Up.IsNearlyZero()) return;
    const float Angle = FMath::DegreesToRadians(20.f);
    for (int32 Side = 0; Side < 2; ++Side)
    {
        const FName Channel = Side == 0 ? TEXT("upperarm_l") : TEXT("upperarm_r");
        const int32 Upper = Side == 0 ? Left : Right;
        const int32 Lower = Index(Side == 0 ? TEXT("lowerarm_l") : TEXT("lowerarm_r"));
        if (Lower == INDEX_NONE) continue;
        int32 Ancestor = Ref.GetParentIndex(Lower);
        while (Ancestor != INDEX_NONE && Ancestor != Upper) Ancestor = Ref.GetParentIndex(Ancestor);
        if (Ancestor != Upper) continue;
        const FVector From = (ReferenceComponentPose[Lower].GetLocation() - ReferenceComponentPose[Upper].GetLocation()).GetSafeNormal();
        if (From.IsNearlyZero()) continue;
        const FVector Outward = Side == 0 ? Across : -Across;
        const FVector Direction = Outward * FMath::Sin(Angle) - Up * FMath::Cos(Angle);
        const FQuat Target = (FQuat::FindBetweenNormals(From, Direction) * ReferenceComponentPose[Upper].GetRotation()).GetNormalized();
        const int32 Parent = Ref.GetParentIndex(Upper);
        const FQuat ParentCS = Parent == INDEX_NONE ? FQuat::Identity : ReferenceComponentPose[Parent].GetRotation();
        IdleArmLocalRotations.Add(Channel, (ParentCS.Inverse() * Target).GetNormalized());
    }
}

void UVtuberRigComponent::RefreshHeadReference()
{
    HeadBindCS = ParentBindCS = FQuat::Identity;
    ReferenceComponentPose.Reset();
    if (!Mesh || !Mesh->GetSkeletalMeshAsset()) return;
    const auto& Ref = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
    for (int32 Index = 0; Index < Ref.GetNum(); ++Index)
        ReferenceComponentPose.Add(FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, Index));
    const int32 Head = Ref.FindBoneIndex(HeadBone);
    if (Head == INDEX_NONE) return;
    HeadBindCS = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, Head).GetRotation();
    const int32 Parent = Ref.GetParentIndex(Head);
    if (Parent != INDEX_NONE) ParentBindCS = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, Parent).GetRotation();
    SmoothedHeadLocal = Ref.GetRefBonePose()[Head].GetRotation();
}

void UVtuberRigComponent::SubmitSnapshot(const FVtuberTrackingSnapshot& Snapshot)
{
    check(IsInGameThread());
    if (!FMath::IsFinite(Snapshot.TimestampSeconds) || Snapshot.TimestampSeconds <= 0
        || Snapshot.TimestampSeconds > FPlatformTime::Seconds() + 1 || Snapshot.TimestampSeconds < Latest.TimestampSeconds) return;
    Latest = Snapshot;
    if (Latest.HeadRotation.ContainsNaN() || Latest.HeadRotation.SizeSquared() < .5 || Latest.HeadRotation.SizeSquared() > 1.5)
        Latest.bFaceTracked = false;
    else Latest.HeadRotation.Normalize();
    for (auto It = Latest.BlendShapes.CreateIterator(); It; ++It)
    {
        if (!GetTrackingChannels().Contains(It.Key())) It.RemoveCurrent();
        else It.Value() = FMath::IsFinite(It.Value()) ? FMath::Clamp(It.Value(), 0.f, 1.f) : 0;
    }
}

bool UVtuberRigComponent::HasTracking() const
{
    return Latest.bFaceTracked && Latest.TimestampSeconds > 0
        && FPlatformTime::Seconds() - Latest.TimestampSeconds <= Settings.LostTrackingSeconds;
}

bool UVtuberRigComponent::CalibrateNeutral()
{
    const bool bFace = HasTracking();
    FQuat Body = FQuat::Identity;
    const bool bBody = Latest.bPoseTracked && Latest.TimestampSeconds > 0
        && FPlatformTime::Seconds() - Latest.TimestampSeconds <= Settings.LostTrackingSeconds && ReadBodyFrame(Latest, Body);
    if (!bFace && !bBody) return false;
    if (bFace) { NeutralHead = Latest.HeadRotation; NeutralExpressions = Latest.BlendShapes; }
    if (bBody) NeutralBodyFrame = Body;
    // Do not zero the smoothed output: an existing pose returns continuously to the new neutral.
    return true;
}

void UVtuberRigComponent::ResetNeutral()
{
    NeutralHead = FQuat::Identity;
    NeutralBodyFrame = FQuat::Identity;
    NeutralExpressions.Reset();
}

bool UVtuberRigComponent::SetSettings(const FVtuberRigSettings& Value)
{
    if (!ValidSettings(Value)) return false;
    if (Value.SmoothingSeconds != Settings.SmoothingSeconds)
        for (auto& Response : ChannelResponses) Response.Value.SmoothingSeconds = FMath::Clamp(Value.SmoothingSeconds, 0.f, .5f);
    Settings = Value;
    return true;
}

FVtuberChannelResponse UVtuberRigComponent::GetChannelResponse(FName Channel) const
{
    if (const auto* Response = ChannelResponses.Find(Channel)) return *Response;
    FVtuberChannelResponse Default;
    Default.bBipolar = IsBodyChannel(Channel);
    Default.Lower = Default.bBipolar ? -1.f : 0.f;
    if (Channel == TEXT("eyeBlinkLeft") || Channel == TEXT("eyeBlinkRight"))
    {
        Default.Lower = .3f; Default.Upper = .7f;
    }
    for (const auto& Mapping : Mappings) if (Mapping.Channel == Channel) Default.bEnabled = Mapping.bEnabled;
    return Default;
}

bool UVtuberRigComponent::SetChannelResponse(FName Channel, const FVtuberChannelResponse& Response)
{
    if (!IsKnownChannel(Channel) || Response.bBipolar != IsBodyChannel(Channel)
        || !FiniteRange(Response.SmoothingSeconds, 0.f, .5f) || !FiniteRange(Response.Sensitivity, .1f, 3.f)
        || !FiniteRange(Response.Curvature, 0.f, 1.f) || Response.Precision < 0 || Response.Precision > 200
        || !FiniteRange(Response.Lower, Response.bBipolar ? -1.f : 0.f, 1.f)
        || !FiniteRange(Response.Upper, Response.Lower + .00999f, 1.f)) return false;
    ChannelResponses.Add(Channel, Response);
    for (auto& Mapping : Mappings) if (Mapping.Channel == Channel) Mapping.bEnabled = Response.bEnabled;
    ResponseFiltered.Remove(Channel);
    return true;
}

void UVtuberRigComponent::ResetChannelResponse(FName Channel)
{
    if (!IsKnownChannel(Channel)) return;
    for (auto& Mapping : Mappings)
        if (Mapping.Channel == Channel)
        {
            Mapping.Curve = FVtuberThresholdCurve(); Mapping.Maximum = Mapping.Sensitivity = 1.f;
            Mapping.bInvert = false; Mapping.bEnabled = Channel != TEXT("_neutral");
        }
    if (Channel == TEXT("headYawPositive")) Settings.YawPositive = FVtuberThresholdCurve();
    if (Channel == TEXT("headYawNegative")) Settings.YawNegative = FVtuberThresholdCurve();
    if (Channel == TEXT("headPitchPositive")) Settings.PitchPositive = FVtuberThresholdCurve();
    if (Channel == TEXT("headPitchNegative")) Settings.PitchNegative = FVtuberThresholdCurve();
    if (Channel == TEXT("headRollPositive")) Settings.RollPositive = FVtuberThresholdCurve();
    if (Channel == TEXT("headRollNegative")) Settings.RollNegative = FVtuberThresholdCurve();
    ChannelResponses.Remove(Channel);
    ChannelResponses.Add(Channel, GetChannelResponse(Channel));
    ResponseFiltered.Remove(Channel);
}

void UVtuberRigComponent::ResetAllChannelResponses()
{
    ChannelResponses.Reset(); ResponseFiltered.Reset();
    for (FName Channel : GetTrackingChannels()) ResetChannelResponse(Channel);
    for (FName Channel : HeadChannels) ResetChannelResponse(Channel);
    for (const auto& Definition : BodyTargets) ResetChannelResponse(Definition.Key);
}

float UVtuberRigComponent::EvaluateChannelResponse(FName Channel, float Raw) const
{
    if (!IsKnownChannel(Channel) || !FMath::IsFinite(Raw)) return 0;
    const auto R = GetChannelResponse(Channel);
    if (!R.bEnabled) return 0;
    // Curves and live data use the same complete response, including legacy
    // profile controls. Keeping the old chain here avoids a misleading UI plot.
    for (const auto& Mapping : Mappings)
        if (Mapping.Channel == Channel)
        {
            if (!Mapping.bEnabled) return 0;
            Raw = FMath::Clamp(Mapping.Curve.Evaluate(Mapping.bInvert ? 1.f - Raw : Raw) * Mapping.Maximum * Mapping.Sensitivity, 0.f, 1.f);
            break;
        }
    if (Channel == TEXT("headYawPositive")) Raw = Settings.YawPositive.Evaluate(Raw);
    if (Channel == TEXT("headYawNegative")) Raw = Settings.YawNegative.Evaluate(Raw);
    if (Channel == TEXT("headPitchPositive")) Raw = Settings.PitchPositive.Evaluate(Raw);
    if (Channel == TEXT("headPitchNegative")) Raw = Settings.PitchNegative.Evaluate(Raw);
    if (Channel == TEXT("headRollPositive")) Raw = Settings.RollPositive.Evaluate(Raw);
    if (Channel == TEXT("headRollNegative")) Raw = Settings.RollNegative.Evaluate(Raw);
    float T = FMath::Clamp((Raw - R.Lower) / FMath::Max(R.Upper - R.Lower, .01f), 0.f, 1.f);
    const float K = R.Curvature * 6.f;
    if (K > .0001f)
    {
        const float Low = 1.f / (1.f + FMath::Exp(K));
        T = (1.f / (1.f + FMath::Exp(-K * (2.f * T - 1.f))) - Low) / (1.f - 2.f * Low);
    }
    float Value = R.bBipolar ? 2.f * T - 1.f : T;
    const bool bHead = Channel.ToString().StartsWith(TEXT("head"));
    if (R.bInvert) Value = R.bBipolar || bHead ? -Value : 1.f - Value;
    Value = FMath::Clamp(Value * R.Sensitivity, R.bBipolar || bHead ? -1.f : 0.f, 1.f);
    if (R.Precision > 0) Value = FMath::RoundToFloat(Value * R.Precision) / R.Precision;
    if (IsBodyChannel(Channel)) Value = FMath::Clamp(Value, -LocalLimit(Channel) / 180.f, LocalLimit(Channel) / 180.f);
    return Value;
}

float UVtuberRigComponent::MapResponse(FName Channel, float Input, float DeltaTime, bool bTracked, bool, bool bFilter)
{
    ChannelInputs.Add(Channel, bTracked ? Input : 0.f);
    const auto Response = GetChannelResponse(Channel);
    const float Target = bTracked && Response.bEnabled ? EvaluateChannelResponse(Channel, Input) : 0.f;
    if (!bFilter) return Target;
    const float Seconds = bTracked && Response.bEnabled ? Response.SmoothingSeconds : Settings.ReturnSeconds;
    float& Filtered = ResponseFiltered.FindOrAdd(Channel);
    const float Alpha = Seconds <= SMALL_NUMBER ? 1.f : 1.f - FMath::Exp(-FMath::Min(DeltaTime, .25f) / Seconds);
    Filtered = FMath::Lerp(Filtered, Target, Alpha);
    ChannelOutputs.Add(Channel, Filtered);
    return Filtered;
}

bool UVtuberRigComponent::SetMapping(const FVtuberMorphMapping& Mapping)
{
    if (!ValidMapping(Mapping)) return false;
    // Unknown targets remain visible as missing so profiles can be repaired in the editor.
    for (auto& M : Mappings) if (M.Channel == Mapping.Channel)
    {
        M = Mapping;
        if (auto* Response = ChannelResponses.Find(M.Channel)) Response->bEnabled = M.bEnabled;
        return true;
    }
    return false;
}

bool UVtuberRigComponent::SetHeadBone(FName Bone)
{
    if (!Bone.IsNone() && !GetAvailableBones().Contains(Bone)) return false;
    HeadBone = Bone;
    RefreshHeadReference();
    BuildIdleArmPose();
    return true;
}

FName UVtuberRigComponent::GetBodyBone(FName Channel) const
{
    return BodyBones.FindRef(Channel);
}

bool UVtuberRigComponent::SetBodyBone(FName Channel, FName Bone)
{
    bool bKnown = false;
    for (const FBodyTargetDefinition& Definition : BodyTargets)
        if (Channel == Definition.Key) { bKnown = true; break; }
    if (!bKnown || (!Bone.IsNone() && !GetAvailableBones().Contains(Bone))) return false;
    if (Bone.IsNone()) BodyBones.Remove(Channel);
    else BodyBones.Add(Channel, Bone);
    BuildIdleArmPose();
    return true;
}

bool UVtuberRigComponent::IsBodyChannelTracked(FName Channel) const
{
    if (Latest.TimestampSeconds <= 0 || FPlatformTime::Seconds() - Latest.TimestampSeconds > Settings.LostTrackingSeconds) return false;
    return TrackedBodyChannels.Contains(Channel);
}

float UVtuberRigComponent::GetBodyChannelOutputDegrees(FName Channel) const
{
    if (const FQuat* Rotation = SmoothedBody.Find(Channel))
        return FMath::Clamp(FMath::RadiansToDegrees(Rotation->GetAngle()), 0.f, 180.f);
    return 0.f;
}

TArray<FName> UVtuberRigComponent::GetAvailableBones() const
{
    TArray<FName> Result;
    if (Mesh && Mesh->GetSkeletalMeshAsset())
        for (const auto& Bone : Mesh->GetSkeletalMeshAsset()->GetRefSkeleton().GetRefBoneInfo()) Result.Add(Bone.Name);
    return Result;
}

TArray<FName> UVtuberRigComponent::GetAvailableMorphs() const
{
    TArray<FName> Result;
    if (Mesh && Mesh->GetSkeletalMeshAsset())
        for (const auto& Morph : Mesh->GetSkeletalMeshAsset()->GetMorphTargets()) if (Morph) Result.Add(Morph->GetFName());
    return Result;
}

TArray<FString> UVtuberRigComponent::GetMissingMappings() const
{
    TArray<FString> Result;
    if (HeadBone.IsNone() || !GetAvailableBones().Contains(HeadBone)) Result.Add(TEXT("head"));
    const TArray<FName> Available = GetAvailableMorphs();
    for (const auto& M : Mappings)
        if (M.bEnabled && (M.Morph.IsNone() || !Available.Contains(M.Morph))) Result.Add(M.Channel.ToString());
    return Result;
}

TArray<FString> UVtuberRigComponent::GetMissingBodyMappings() const
{
    TArray<FString> Missing;
    const TArray<FName> Available = GetAvailableBones();
    for (const FBodyTargetDefinition& Definition : BodyTargets)
        if (!Available.Contains(BodyBones.FindRef(Definition.Key))) Missing.Add(Definition.Key);
    return Missing;
}

float UVtuberRigComponent::GetChannelInput(FName Channel) const
{
    if (IsBodyChannel(Channel)) return ChannelInputs.FindRef(Channel);
    if (!HasTracking()) return 0;
    const FString Name = Channel.ToString();
    if (Name.StartsWith(TEXT("head")))
    {
        const FRotator Angles = (Latest.HeadRotation * NeutralHead.Inverse()).Rotator().GetNormalized();
        const float Angle = Name.Contains(TEXT("Yaw")) ? Angles.Yaw / Settings.YawLimitDegrees
            : Name.Contains(TEXT("Pitch")) ? Angles.Pitch / Settings.PitchLimitDegrees : Angles.Roll / Settings.RollLimitDegrees;
        return FMath::Clamp(Name.EndsWith(TEXT("Negative")) ? -Angle : Angle, 0.f, 1.f);
    }
    const float Neutral = FMath::Clamp(NeutralExpressions.FindRef(Channel), 0.f, .999f);
    return FMath::Clamp((Latest.BlendShapes.FindRef(Channel) - Neutral) / (1 - Neutral), 0.f, 1.f);
}

float UVtuberRigComponent::GetChannelOutput(FName Channel) const
{
    return ChannelOutputs.FindRef(Channel);
}

float UVtuberRigComponent::MapSignedAngle(float Degrees, float SafeLimit, const FVtuberThresholdCurve& Positive, const FVtuberThresholdCurve& Negative)
{
    if (!FMath::IsFinite(Degrees) || !FMath::IsFinite(SafeLimit) || SafeLimit <= 0) return 0;
    return Degrees >= 0 ? Positive.Evaluate(Degrees / SafeLimit) * SafeLimit : -Negative.Evaluate(-Degrees / SafeLimit) * SafeLimit;
}

void UVtuberRigComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    UActorComponent::TickComponent(DeltaTime, TickType, TickFunction);
    UpdateTracking(DeltaTime);
}

void UVtuberRigComponent::UpdateTracking(float DeltaTime)
{
    if (!Mesh || !Mesh->GetSkeletalMeshAsset() || !FMath::IsFinite(DeltaTime) || DeltaTime <= 0) return;
    CanonicalToComponent = Mesh->GetComponentQuat().Inverse().GetNormalized();
    const bool Fresh = Latest.TimestampSeconds > 0 && FPlatformTime::Seconds() - Latest.TimestampSeconds <= Settings.LostTrackingSeconds;
    const bool FaceTracking = Fresh && Latest.bFaceTracked && Settings.bHeadEnabled;
    const bool PoseTracking = Fresh && Latest.bPoseTracked && Settings.bBodyEnabled;
    const bool HandTracking = Fresh && Settings.bHandsEnabled;
    const auto& Ref = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
    if (ReferenceComponentPose.Num() != Ref.GetNum()) { RefreshHeadReference(); BuildIdleArmPose(); }
    const auto& Rest = Ref.GetRefBonePose();
    TMap<int32, FQuat> RawTargets;
    TrackedBodyChannels.Reset();
    auto BoneIndex = [&](FName Channel) { return Ref.FindBoneIndex(BodyBones.FindRef(Channel)); };
    auto Position = [&](int32 Index) { return ReferenceComponentPose[Index].GetLocation(); };
    auto SetRaw = [&](FName Channel, const FQuat& Rotation)
    {
        const int32 Index = BoneIndex(Channel);
        if (Index != INDEX_NONE && !Rotation.ContainsNaN() && Rotation.SizeSquared() > .5)
        {
            RawTargets.Add(Index, Rotation.GetNormalized());
            TrackedBodyChannels.Add(Channel);
        }
    };
    auto DeltaTarget = [&](FName Channel, const FQuat& Delta)
    {
        const int32 Index = BoneIndex(Channel);
        if (Index != INDEX_NONE) SetRaw(Channel, Delta * ReferenceComponentPose[Index].GetRotation());
    };
    auto ToComponent = [&](const FVector& Point) { return CanonicalToComponent.RotateVector(Point); };
    auto Aim = [&](FName Channel, int32 Child, const FVector& Measured, const FQuat& ReferenceDelta)
    {
        const int32 Index = BoneIndex(Channel);
        if (Index == INDEX_NONE || Measured.SizeSquared() < 1.e-8) return;
        // Semantic chain endpoints skip PMX twist/dummy helpers. Terminal fingers
        // use the preceding segment until authored PMX tails are exposed by the importer.
        const int32 Previous = Ref.GetParentIndex(Index);
        const FVector Direction = Child != INDEX_NONE ? Position(Child) - Position(Index)
            : Previous != INDEX_NONE ? Position(Index) - Position(Previous) : FVector::ZeroVector;
        if (Direction.SizeSquared() < 1.e-8) return;
        const FQuat Swing = FQuat::FindBetweenNormals(ReferenceDelta.RotateVector(Direction).GetSafeNormal(), Measured.GetSafeNormal());
        SetRaw(Channel, Swing * ReferenceDelta * ReferenceComponentPose[Index].GetRotation());
    };

    FQuat BodyFrame, BodyDelta = FQuat::Identity;
    const bool BodyValid = PoseTracking && ReadBodyFrame(Latest, BodyFrame);
    if (BodyValid)
    {
        const FQuat CanonicalDelta = BoundedTorso((BodyFrame * NeutralBodyFrame.Inverse()).GetNormalized());
        BodyDelta = (CanonicalToComponent * CanonicalDelta * CanonicalToComponent.Inverse()).GetNormalized();
        DeltaTarget(TEXT("spine"), FQuat::Slerp(FQuat::Identity, BodyDelta, BoneIndex(TEXT("chest")) == INDEX_NONE ? 1.f : .35f));
        DeltaTarget(TEXT("chest"), BodyDelta);
        // Shoulder controls carry the torso only. Arm elevation belongs to the
        // upper arm; aiming both shoulder and arm doubles the deformation.
        DeltaTarget(TEXT("shoulder_l"), BodyDelta);
        DeltaTarget(TEXT("shoulder_r"), BodyDelta);
    }
    for (int32 Side = 0; Side < 2; ++Side)
    {
        const TCHAR* UpperKey = Side == 0 ? TEXT("upperarm_l") : TEXT("upperarm_r");
        const TCHAR* LowerKey = Side == 0 ? TEXT("lowerarm_l") : TEXT("lowerarm_r");
        const TCHAR* WristKey = Side == 0 ? TEXT("hand_l") : TEXT("hand_r");
        const int32 UpperIndex = BoneIndex(UpperKey), LowerIndex = BoneIndex(LowerKey), WristIndex = BoneIndex(WristKey);
        FVector Shoulder, Elbow, Wrist;
        if (!PoseTracking || UpperIndex == INDEX_NONE || LowerIndex == INDEX_NONE || WristIndex == INDEX_NONE
            || !ReadPosePoint(Latest, 11 + Side, Shoulder) || !ReadPosePoint(Latest, 13 + Side, Elbow) || !ReadPosePoint(Latest, 15 + Side, Wrist)) continue;
        const FVector Upper = ToComponent(Elbow - Shoulder), Lower = ToComponent(Wrist - Elbow);
        const FVector NeutralUpper = (Position(LowerIndex) - Position(UpperIndex)).GetSafeNormal();
        const FVector NeutralLower = (Position(WristIndex) - Position(LowerIndex)).GetSafeNormal();
        if (Upper.IsNearlyZero() || Lower.IsNearlyZero() || NeutralUpper.IsNearlyZero() || NeutralLower.IsNearlyZero()) continue;
        FQuat ArmDelta = (FQuat::FindBetweenNormals(BodyDelta.RotateVector(NeutralUpper), Upper.GetSafeNormal()) * BodyDelta).GetNormalized();
        const float Reliability = FVector::CrossProduct(Upper.GetSafeNormal(), Lower.GetSafeNormal()).Size();
        const float Weight = FMath::Clamp((Reliability - .05f) / .15f, 0.f, 1.f);
        if (Weight > 0)
        {
            FVector Reference = NeutralLower;
            if (FVector::CrossProduct(NeutralUpper, NeutralLower).Size() < .05)
            {
                Reference = ToComponent(FVector::ForwardVector);
                if (FMath::Abs(FVector::DotProduct(NeutralUpper, Reference)) > .95) Reference = ToComponent(FVector::UpVector);
            }
            FQuat NeutralPlane, MeasuredPlane;
            if (MakeFrame(Reference, NeutralUpper, NeutralPlane) && MakeFrame(Lower, Upper, MeasuredPlane))
                ArmDelta = FQuat::Slerp(ArmDelta, MeasuredPlane * NeutralPlane.Inverse(), Weight).GetNormalized();
        }
        Aim(UpperKey, LowerIndex, Upper, ArmDelta);
        Aim(LowerKey, WristIndex, Lower, ArmDelta);
    }

    // A wrist needs the palm frame, not an arbitrary wrist-to-pinky direction.
    // Fingers share its axial rotation, then add their own measured swing.
    for (int32 Side = 0; Side < 2; ++Side)
    {
        if (!HandTracking) break;
        const FString Suffix = Side == 0 ? TEXT("_l") : TEXT("_r");
        const FName WristKey(*(TEXT("hand") + Suffix));
        const int32 Wrist = BoneIndex(WristKey), Index = BoneIndex(*(TEXT("index1") + Suffix));
        const int32 Middle = BoneIndex(*(TEXT("middle1") + Suffix)), Little = BoneIndex(*(TEXT("pinky1") + Suffix));
        FVector P0, P5, P9, P17;
        const uint8 Domain = Side + 1;
        if (Wrist == INDEX_NONE || Index == INDEX_NONE || Middle == INDEX_NONE || Little == INDEX_NONE
            || !ReadHandPoint(Latest, Domain, 0, P0) || !ReadHandPoint(Latest, Domain, 5, P5)
            || !ReadHandPoint(Latest, Domain, 9, P9) || !ReadHandPoint(Latest, Domain, 17, P17)) continue;
        FQuat NeutralPalm, MeasuredPalm;
        if (!MakeFrame(Position(Index) - Position(Little), Position(Middle) - Position(Wrist), NeutralPalm)
            || !MakeFrame(ToComponent(P5 - P17), ToComponent(P9 - P0), MeasuredPalm)) continue;
        const FQuat PalmDelta = (MeasuredPalm * NeutralPalm.Inverse()).GetNormalized();
        DeltaTarget(WristKey, PalmDelta);
        for (const auto& Definition : BodyTargets)
        {
            if (Definition.Domain != Domain) continue;
            FVector Direction;
            if (!ReadTargetDirection(Latest, Definition, Direction)) continue;
            FString Next(Definition.Key);
            const int32 SegmentPosition = Next.Len() - 3;
            const TCHAR Segment = Next[SegmentPosition];
            int32 Child = INDEX_NONE;
            if (Segment == TEXT('1') || Segment == TEXT('2'))
            {
                Next[SegmentPosition] = Segment + 1;
                Child = BoneIndex(*Next);
            }
            Aim(Definition.Key, Child, ToComponent(Direction), PalmDelta);
        }
    }

    // Face rotation is an absolute component target. Converting against the
    // animated parent below prevents chest/neck rotation being applied twice.
    const FRotator HeadAngles = (Latest.HeadRotation * NeutralHead.Inverse()).Rotator().GetNormalized();
    auto HeadAxis = [&](const TCHAR* Positive, const TCHAR* Negative, float Angle, float Limit)
    {
        const float P = FMath::Clamp(FMath::Max(Angle, 0.f) / Limit, 0.f, 1.f);
        const float N = FMath::Clamp(FMath::Max(-Angle, 0.f) / Limit, 0.f, 1.f);
        return FMath::Clamp(MapResponse(Positive, P, DeltaTime, FaceTracking, true)
            - MapResponse(Negative, N, DeltaTime, FaceTracking, true), -1.f, 1.f) * Limit;
    };
    SmoothedHead = FRotator(
        HeadAxis(TEXT("headPitchPositive"), TEXT("headPitchNegative"), HeadAngles.Pitch, Settings.PitchLimitDegrees),
        HeadAxis(TEXT("headYawPositive"), TEXT("headYawNegative"), HeadAngles.Yaw, Settings.YawLimitDegrees),
        HeadAxis(TEXT("headRollPositive"), TEXT("headRollNegative"), HeadAngles.Roll, Settings.RollLimitDegrees)).Quaternion();
    const int32 HeadIndex = Ref.FindBoneIndex(HeadBone);
    if (FaceTracking)
    {
        const FQuat HeadDelta = (CanonicalToComponent * SmoothedHead * CanonicalToComponent.Inverse()).GetNormalized();
        DeltaTarget(TEXT("neck"), FQuat::Slerp(FQuat::Identity, HeadDelta, .35f));
        if (HeadIndex != INDEX_NONE) RawTargets.Add(HeadIndex, (HeadDelta * HeadBindCS).GetNormalized());
    }

    if (auto* Instance = Cast<UVtuberAnimInstance>(Mesh->GetAnimInstance()))
    {
        Instance->HeadBone = HeadBone;
        Instance->BodyComponentRotations.Reset();
        TMap<int32, FName> ChannelsByBone;
        for (const auto& Definition : BodyTargets)
        {
            const int32 Index = BoneIndex(Definition.Key);
            if (Index != INDEX_NONE) ChannelsByBone.Add(Index, Definition.Key);
        }
        TArray<FQuat> Resolved;
        Resolved.SetNum(Ref.GetNum());
        for (int32 Index = 0; Index < Ref.GetNum(); ++Index)
        {
            const int32 Parent = Ref.GetParentIndex(Index);
            const FQuat ParentCS = Parent == INDEX_NONE ? FQuat::Identity : Resolved[Parent];
            const FQuat BindLocal = Rest[Index].GetRotation().GetNormalized();
            FQuat Local = BindLocal;
            const FName* Channel = ChannelsByBone.Find(Index);
            const FQuat* Raw = RawTargets.Find(Index);
            if (Channel)
            {
                const bool Tracked = TrackedBodyChannels.Contains(*Channel);
                const FQuat DesiredLocal = Raw ? (ParentCS.Inverse() * *Raw).GetNormalized() : BindLocal;
                FVector Axis;
                float Angle;
                ShortAxisAngle(BindLocal.Inverse() * DesiredLocal, Axis, Angle);
                const float Mapped = MapResponse(*Channel, Angle / PI, DeltaTime, Tracked, true, false);
                const float Limit = FMath::DegreesToRadians(LocalLimit(*Channel));
                FQuat TargetDelta(Axis, FMath::Clamp(Mapped * PI, -Limit, Limit));
                if (!Tracked || !GetChannelResponse(*Channel).bEnabled)
                    if (const FQuat* Idle = IdleArmLocalRotations.Find(*Channel))
                        TargetDelta = (BindLocal.Inverse() * *Idle).GetNormalized();
                FQuat& Smooth = SmoothedBody.FindOrAdd(*Channel, FQuat::Identity);
                const float Seconds = Tracked && GetChannelResponse(*Channel).bEnabled ? GetChannelResponse(*Channel).SmoothingSeconds : Settings.ReturnSeconds;
                const float Alpha = Seconds <= SMALL_NUMBER ? 1.f : 1.f - FMath::Exp(-FMath::Min(DeltaTime, .25f) / Seconds);
                Smooth = FQuat::Slerp(Smooth, TargetDelta, Alpha).GetNormalized();
                Local = (BindLocal * Smooth).GetNormalized();
                FVector SmoothAxis; float SmoothAngle;
                ShortAxisAngle(Smooth, SmoothAxis, SmoothAngle);
                ChannelOutputs.Add(*Channel, SmoothAngle / PI * (FVector::DotProduct(SmoothAxis, Axis) < 0 ? -1.f : 1.f));
            }
            if (Index == HeadIndex && (FaceTracking || !Channel))
            {
                const FQuat DesiredLocal = Raw ? (ParentCS.Inverse() * *Raw).GetNormalized() : BindLocal;
                // Head channels already filter live data; on loss return to a
                // local neutral so the neck continues to carry the head.
                const float Alpha = FaceTracking ? 1.f : 1.f - FMath::Exp(-FMath::Min(DeltaTime, .25f) / Settings.ReturnSeconds);
                SmoothedHeadLocal = FQuat::Slerp(SmoothedHeadLocal, DesiredLocal, Alpha).GetNormalized();
                Local = SmoothedHeadLocal;
                Instance->HeadLocalRotation = Local;
            }
            if (Local.ContainsNaN() || Local.SizeSquared() < .5) Local = BindLocal;
            Resolved[Index] = (ParentCS * Local).GetNormalized();
            if (Channel || Index == HeadIndex) Instance->BodyComponentRotations.Add(Ref.GetBoneName(Index), Resolved[Index]);
        }
    }
    TMap<FName, float> MorphWeights;
    const TArray<FName> Available = GetAvailableMorphs();
    for (const auto& M : Mappings)
    {
        const bool Active = Fresh && Latest.bFaceTracked && Settings.bExpressionsEnabled && M.bEnabled;
        const float Input = GetChannelInput(M.Channel);
        const float Output = MapResponse(M.Channel, Input, DeltaTime, Active, false);
        SmoothedExpressions.Add(M.Channel, Output);
        if (!M.Morph.IsNone() && Available.Contains(M.Morph))
        {
            float& Weight = MorphWeights.FindOrAdd(M.Morph);
            Weight = FMath::Max(Weight, Output);
        }
    }
    for (FName Name : AppliedMorphs) if (!MorphWeights.Contains(Name)) Mesh->SetMorphTarget(Name, 0);
    AppliedMorphs.Reset();
    for (const auto& Entry : MorphWeights)
    {
        Mesh->SetMorphTarget(Entry.Key, Entry.Value < .0001f ? 0 : Entry.Value);
        AppliedMorphs.Add(Entry.Key);
    }
}

FVector UVtuberRigComponent::GetFaceLightDirection(const FVector& WorldLightDirection) const
{
    if (!Mesh || HeadBone.IsNone()) return WorldLightDirection.GetSafeNormal();
    const int32 Index = Mesh->GetBoneIndex(HeadBone);
    if (Index == INDEX_NONE || !Mesh->GetComponentSpaceTransforms().IsValidIndex(Index)) return WorldLightDirection.GetSafeNormal();
    const FQuat CurrentCS = Mesh->GetComponentSpaceTransforms()[Index].GetRotation();
    const FQuat SkinRotation = (CurrentCS * HeadBindCS.Inverse()).GetNormalized();
    const FTransform& Component = Mesh->GetComponentTransform();
    const FVector LocalLight = Component.InverseTransformVectorNoScale(WorldLightDirection);
    return Component.TransformVectorNoScale(SkinRotation.UnrotateVector(LocalLight)).GetSafeNormal();
}

bool UVtuberRigComponent::SaveProfile(const FString& Path, FString& Error) const
{
    Error.Reset();
    if (Path.IsEmpty()) { Error = TEXT("未指定模型映射配置路径。"); return false; }
    auto Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("version"), 1);
    Root->SetStringField(TEXT("head_bone"), HeadBone.ToString());
    auto S = MakeShared<FJsonObject>();
    S->SetBoolField(TEXT("head_enabled"), Settings.bHeadEnabled); S->SetBoolField(TEXT("expressions_enabled"), Settings.bExpressionsEnabled);
    S->SetBoolField(TEXT("body_enabled"), Settings.bBodyEnabled); S->SetBoolField(TEXT("hands_enabled"), Settings.bHandsEnabled);
    S->SetNumberField(TEXT("smoothing_seconds"), Settings.SmoothingSeconds); S->SetNumberField(TEXT("lost_seconds"), Settings.LostTrackingSeconds);
    S->SetNumberField(TEXT("return_seconds"), Settings.ReturnSeconds); S->SetNumberField(TEXT("yaw_limit"), Settings.YawLimitDegrees);
    S->SetNumberField(TEXT("pitch_limit"), Settings.PitchLimitDegrees); S->SetNumberField(TEXT("roll_limit"), Settings.RollLimitDegrees);
    S->SetObjectField(TEXT("yaw_positive"), CurveJson(Settings.YawPositive)); S->SetObjectField(TEXT("yaw_negative"), CurveJson(Settings.YawNegative));
    S->SetObjectField(TEXT("pitch_positive"), CurveJson(Settings.PitchPositive)); S->SetObjectField(TEXT("pitch_negative"), CurveJson(Settings.PitchNegative));
    S->SetObjectField(TEXT("roll_positive"), CurveJson(Settings.RollPositive)); S->SetObjectField(TEXT("roll_negative"), CurveJson(Settings.RollNegative));
    Root->SetObjectField(TEXT("settings"), S);
    TArray<TSharedPtr<FJsonValue>> Entries;
    for (const auto& M : Mappings)
    {
        auto O = MakeShared<FJsonObject>();
        O->SetStringField(TEXT("channel"), M.Channel.ToString()); O->SetStringField(TEXT("morph"), M.Morph.ToString());
        O->SetBoolField(TEXT("enabled"), M.bEnabled); O->SetBoolField(TEXT("invert"), M.bInvert);
        O->SetNumberField(TEXT("maximum"), M.Maximum); O->SetNumberField(TEXT("sensitivity"), M.Sensitivity);
        O->SetObjectField(TEXT("curve"), CurveJson(M.Curve)); Entries.Add(MakeShared<FJsonValueObject>(O));
    }
    Root->SetArrayField(TEXT("mappings"), Entries);
    auto Body = MakeShared<FJsonObject>();
    for (const FBodyTargetDefinition& Definition : BodyTargets)
        Body->SetStringField(Definition.Key, BodyBones.FindRef(Definition.Key).ToString());
    Root->SetObjectField(TEXT("body_bones"), Body);
    auto Responses = MakeShared<FJsonObject>();
    for (const auto& Pair : ChannelResponses)
    {
        const auto& R = Pair.Value;
        auto O = MakeShared<FJsonObject>();
        O->SetBoolField(TEXT("enabled"), R.bEnabled); O->SetBoolField(TEXT("invert"), R.bInvert);
        O->SetNumberField(TEXT("smoothing"), R.SmoothingSeconds); O->SetNumberField(TEXT("sensitivity"), R.Sensitivity);
        O->SetNumberField(TEXT("curvature"), R.Curvature); O->SetNumberField(TEXT("precision"), R.Precision);
        O->SetNumberField(TEXT("lower"), R.Lower); O->SetNumberField(TEXT("upper"), R.Upper);
        Responses->SetObjectField(Pair.Key.ToString(), O);
    }
    Root->SetObjectField(TEXT("responses"), Responses);
    auto BodyNeutral = MakeShared<FJsonObject>();
    BodyNeutral->SetNumberField(TEXT("x"), NeutralBodyFrame.X); BodyNeutral->SetNumberField(TEXT("y"), NeutralBodyFrame.Y);
    BodyNeutral->SetNumberField(TEXT("z"), NeutralBodyFrame.Z); BodyNeutral->SetNumberField(TEXT("w"), NeutralBodyFrame.W);
    Root->SetObjectField(TEXT("neutral_body"), BodyNeutral);
    auto Neutral = MakeShared<FJsonObject>();
    Neutral->SetNumberField(TEXT("x"), NeutralHead.X); Neutral->SetNumberField(TEXT("y"), NeutralHead.Y);
    Neutral->SetNumberField(TEXT("z"), NeutralHead.Z); Neutral->SetNumberField(TEXT("w"), NeutralHead.W);
    auto Expressions = MakeShared<FJsonObject>();
    for (const auto& Pair : NeutralExpressions) Expressions->SetNumberField(Pair.Key.ToString(), Pair.Value);
    Neutral->SetObjectField(TEXT("expressions"), Expressions); Root->SetObjectField(TEXT("neutral"), Neutral);
    FString Text;
    if (!FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text))) { Error = TEXT("映射配置序列化失败。"); return false; }
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    const FString Temp = Path + TEXT(".tmp");
    if (!FFileHelper::SaveStringToFile(Text, *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
        || !IFileManager::Get().Move(*Path, *Temp, true, true)) { Error = TEXT("映射配置保存失败。"); return false; }
    return true;
}

bool UVtuberRigComponent::LoadProfile(const FString& Path, FString& Error)
{
    Error.Reset();
    const auto Fail = [&Error] { Error = TEXT("映射配置无效或版本不支持，保留当前映射。"); return false; };
    if (IFileManager::Get().FileSize(*Path) > 1024 * 1024) return Fail();
    FString Text;
    TSharedPtr<FJsonObject> Root;
    if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root) return Fail();
    double Version;
    FString Bone;
    const TSharedPtr<FJsonObject>* S;
    if (!Root->TryGetNumberField(TEXT("version"), Version) || Version != 1 || !Root->TryGetStringField(TEXT("head_bone"), Bone)
        || !Root->TryGetObjectField(TEXT("settings"), S)) return Fail();
    FVtuberRigSettings NewSettings;
    if (!(*S)->TryGetBoolField(TEXT("head_enabled"), NewSettings.bHeadEnabled) || !(*S)->TryGetBoolField(TEXT("expressions_enabled"), NewSettings.bExpressionsEnabled)
        || !ReadNumber(**S, TEXT("smoothing_seconds"), NewSettings.SmoothingSeconds) || !ReadNumber(**S, TEXT("lost_seconds"), NewSettings.LostTrackingSeconds)
        || !ReadNumber(**S, TEXT("return_seconds"), NewSettings.ReturnSeconds) || !ReadNumber(**S, TEXT("yaw_limit"), NewSettings.YawLimitDegrees)
        || !ReadNumber(**S, TEXT("pitch_limit"), NewSettings.PitchLimitDegrees) || !ReadNumber(**S, TEXT("roll_limit"), NewSettings.RollLimitDegrees)
        || !ReadCurve(**S, TEXT("yaw_positive"), NewSettings.YawPositive) || !ReadCurve(**S, TEXT("yaw_negative"), NewSettings.YawNegative)
        || !ReadCurve(**S, TEXT("pitch_positive"), NewSettings.PitchPositive) || !ReadCurve(**S, TEXT("pitch_negative"), NewSettings.PitchNegative)
        || !ReadCurve(**S, TEXT("roll_positive"), NewSettings.RollPositive) || !ReadCurve(**S, TEXT("roll_negative"), NewSettings.RollNegative)
        || !ValidSettings(NewSettings)) return Fail();
    // Profiles from before upper-body tracking did not carry these switches.
    (*S)->TryGetBoolField(TEXT("body_enabled"), NewSettings.bBodyEnabled);
    (*S)->TryGetBoolField(TEXT("hands_enabled"), NewSettings.bHandsEnabled);
    const TArray<TSharedPtr<FJsonValue>>* Entries;
    if (!Root->TryGetArrayField(TEXT("mappings"), Entries) || Entries->Num() > UE_ARRAY_COUNT(Channels)) return Fail();
    TArray<FVtuberMorphMapping> NewMappings = Mappings;
    TSet<FName> Seen;
    for (const auto& Entry : *Entries)
    {
        const TSharedPtr<FJsonObject>* O;
        if (!Entry->TryGetObject(O)) return Fail();
        FVtuberMorphMapping M;
        FString Channel, Morph;
        if (!(*O)->TryGetStringField(TEXT("channel"), Channel) || !(*O)->TryGetStringField(TEXT("morph"), Morph)
            || !(*O)->TryGetBoolField(TEXT("enabled"), M.bEnabled) || !ReadNumber(**O, TEXT("maximum"), M.Maximum)
            || !ReadCurve(**O, TEXT("curve"), M.Curve)) return Fail();
        // Profiles written before inversion/sensitivity support retain the identity defaults.
        (*O)->TryGetBoolField(TEXT("invert"), M.bInvert);
        (*O)->TryGetNumberField(TEXT("sensitivity"), M.Sensitivity);
        M.Channel = *Channel; M.Morph = *Morph;
        if (!ValidMapping(M) || Seen.Contains(M.Channel)) return Fail();
        Seen.Add(M.Channel);
        bool Found = false;
        for (auto& Existing : NewMappings) if (Existing.Channel == M.Channel) { Existing = M; Found = true; break; }
        if (!Found) NewMappings.Add(M);
    }
    TMap<FName, FName> NewBodyBones = BodyBones;
    const TSharedPtr<FJsonObject>* SavedBody;
    if (Root->HasField(TEXT("body_bones")))
    {
        if (!Root->TryGetObjectField(TEXT("body_bones"), SavedBody)) return Fail();
        const TArray<FName> Available = GetAvailableBones();
        for (const auto& Pair : (*SavedBody)->Values) if (!IsBodyChannel(*Pair.Key)) return Fail();
        for (const FBodyTargetDefinition& Definition : BodyTargets)
        {
            FString BoneName;
            if (!(*SavedBody)->HasField(Definition.Key)) continue;
            if (!(*SavedBody)->TryGetStringField(Definition.Key, BoneName)) return Fail();
            FName Mapped(*BoneName);
            // Earlier releases incorrectly aliased the PMX pelvis controls as
            // a spine. Migrate that known defect without losing morph tuning.
            if (FName(Definition.Key) == TEXT("spine"))
            {
                const FString Old = NormalizeName(BoneName);
                if (Old == TEXT("腰") || Old == TEXT("センター") || Old == TEXT("center"))
                    Mapped = FindAlias(Available, Definition.Aliases);
            }
            if (!Mapped.IsNone())
            {
                if (!Available.Contains(Mapped)) return Fail();
                NewBodyBones.Add(Definition.Key, Mapped);
            }
            else NewBodyBones.Remove(Definition.Key);
        }
    }
    TMap<FName, FVtuberChannelResponse> NewResponses;
    // Legacy profiles retain their global smoothing; response defaults are
    // otherwise identity, so existing expression curves remain effective.
    auto AddDefault = [&](FName Channel)
    {
        FVtuberChannelResponse R;
        R.bBipolar = IsBodyChannel(Channel); R.Lower = R.bBipolar ? -1.f : 0.f;
        R.SmoothingSeconds = FMath::Clamp(NewSettings.SmoothingSeconds, 0.f, .5f);
        NewResponses.Add(Channel, R);
    };
    for (FName Channel : GetTrackingChannels()) AddDefault(Channel);
    for (FName Channel : HeadChannels) AddDefault(Channel);
    for (const auto& Definition : BodyTargets) AddDefault(Definition.Key);
    if (Root->HasField(TEXT("responses")))
    {
        const TSharedPtr<FJsonObject>* Responses;
        if (!Root->TryGetObjectField(TEXT("responses"), Responses)) return Fail();
        for (const auto& Pair : (*Responses)->Values)
        {
            const FName Channel(*Pair.Key);
            const TSharedPtr<FJsonObject>* O;
            if (!IsKnownChannel(Channel) || !Pair.Value->TryGetObject(O)) return Fail();
            FVtuberChannelResponse R = NewResponses.FindRef(Channel);
            float Precision;
            if (!(*O)->TryGetBoolField(TEXT("enabled"), R.bEnabled) || !(*O)->TryGetBoolField(TEXT("invert"), R.bInvert)
                || !ReadNumber(**O, TEXT("smoothing"), R.SmoothingSeconds) || !ReadNumber(**O, TEXT("sensitivity"), R.Sensitivity)
                || !ReadNumber(**O, TEXT("curvature"), R.Curvature) || !ReadNumber(**O, TEXT("precision"), Precision)
                || !ReadNumber(**O, TEXT("lower"), R.Lower) || !ReadNumber(**O, TEXT("upper"), R.Upper)
                || !FiniteRange(R.SmoothingSeconds, 0.f, .5f) || !FiniteRange(R.Sensitivity, .1f, 3.f)
                || !FiniteRange(R.Curvature, 0.f, 1.f) || !FiniteRange(Precision, 0.f, 200.f) || Precision != FMath::FloorToFloat(Precision)
                || !FiniteRange(R.Lower, R.bBipolar ? -1.f : 0.f, 1.f) || !FiniteRange(R.Upper, R.Lower + .00999f, 1.f)) return Fail();
            R.Precision = static_cast<int32>(Precision);
            NewResponses.Add(Channel, R);
        }
    }
    for (auto& Mapping : NewMappings)
    {
        auto& Response = NewResponses.FindChecked(Mapping.Channel);
        Response.bEnabled = Mapping.bEnabled && Response.bEnabled;
        Mapping.bEnabled = Response.bEnabled;
    }
    FQuat NewBodyNeutral = FQuat::Identity;
    if (Root->HasField(TEXT("neutral_body")))
    {
        const TSharedPtr<FJsonObject>* O;
        float X, Y, Z, W;
        if (!Root->TryGetObjectField(TEXT("neutral_body"), O) || !ReadNumber(**O, TEXT("x"), X) || !ReadNumber(**O, TEXT("y"), Y)
            || !ReadNumber(**O, TEXT("z"), Z) || !ReadNumber(**O, TEXT("w"), W)) return Fail();
        NewBodyNeutral = FQuat(X, Y, Z, W);
        if (NewBodyNeutral.SizeSquared() < .5 || NewBodyNeutral.SizeSquared() > 1.5) return Fail();
        NewBodyNeutral.Normalize();
    }
    FQuat NewNeutral = FQuat::Identity;
    TMap<FName, float> NewExpressions;
    const TSharedPtr<FJsonObject>* Neutral;
    if (Root->TryGetObjectField(TEXT("neutral"), Neutral))
    {
        float X, Y, Z, W;
        if (!ReadNumber(**Neutral, TEXT("x"), X) || !ReadNumber(**Neutral, TEXT("y"), Y)
            || !ReadNumber(**Neutral, TEXT("z"), Z) || !ReadNumber(**Neutral, TEXT("w"), W)) return Fail();
        NewNeutral = FQuat(X, Y, Z, W);
        if (NewNeutral.SizeSquared() < .5 || NewNeutral.SizeSquared() > 1.5) return Fail();
        NewNeutral.Normalize();
        const TSharedPtr<FJsonObject>* Expressions;
        if (!(*Neutral)->TryGetObjectField(TEXT("expressions"), Expressions)) return Fail();
        for (const auto& Pair : (*Expressions)->Values)
        {
            double Value;
            if (!GetTrackingChannels().Contains(FName(*Pair.Key)) || !Pair.Value->TryGetNumber(Value) || !FMath::IsFinite(Value) || Value < 0 || Value > 1) return Fail();
            NewExpressions.Add(*Pair.Key, static_cast<float>(Value));
        }
    }
    Settings = NewSettings; Mappings = MoveTemp(NewMappings); BodyBones = MoveTemp(NewBodyBones); HeadBone = *Bone;
    NeutralHead = NewNeutral; NeutralExpressions = MoveTemp(NewExpressions);
    NeutralBodyFrame = NewBodyNeutral; ChannelResponses = MoveTemp(NewResponses);
    ResponseFiltered.Reset();
    ProfilePath = Path;
    RefreshHeadReference();
    BuildIdleArmPose();
    return true;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Skeleton.h"
#include "Misc/MemStack.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVtuberProxyLifecycleTest, "HonkaiVTuber.Rig.ProxyLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVtuberProxyLifecycleTest::RunTest(const FString& Parameters)
{
    auto* Mesh = NewObject<USkeletalMeshComponent>();
    auto* Instance = NewObject<UVtuberAnimInstance>(Mesh);
    FAnimInstanceProxy* Proxy = Instance->CreateAnimInstanceProxy();
    const bool bRegistered = TestTrue(TEXT("VRM4U base owns the created VTuber proxy"), Instance->GetProxy() == Proxy);
    if (bRegistered) Instance->NativePostEvaluateAnimation();
    Instance->DestroyAnimInstanceProxy(Proxy);
    TestNull(TEXT("Destroyed proxy is no longer retained"), Instance->GetProxy());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVtuberPoseInitializationTest, "HonkaiVTuber.Rig.PoseInitialization", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVtuberPoseInitializationTest::RunTest(const FString& Parameters)
{
    auto* Skeleton = NewObject<USkeleton>();
    const FTransform UnusedPose(FRotator::ZeroRotator, FVector(13, 0, 0));
    {
        FReferenceSkeletonModifier Modifier(Skeleton);
        Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
        Modifier.Add(FMeshBoneInfo(TEXT("unused"), TEXT("unused"), 0), UnusedPose);
        Modifier.Add(FMeshBoneInfo(TEXT("head"), TEXT("head"), 0), FTransform(FRotator::ZeroRotator, FVector(0, 0, 40)));
    }
    auto* Asset = NewObject<USkeletalMesh>();
    Asset->SetSkeleton(Skeleton);
    Asset->SetRefSkeleton(Skeleton->GetReferenceSkeleton());
    auto* Mesh = NewObject<USkeletalMeshComponent>();
    Mesh->SetSkeletalMesh(Asset);
    auto* Instance = NewObject<UVtuberAnimInstance>(Mesh);
    Mesh->AnimScriptInstance = Instance;
    auto* AssetList = NewObject<UVrmAssetListObject>(Mesh);
    auto* Meta = NewObject<UVrmMetaObject>(AssetList);
    AssetList->SkeletalMesh = Asset;
    AssetList->VrmMetaObject = Meta;
    Meta->SkeletalMesh = Asset;
    Meta->VrmAssetListObject = AssetList;
    Instance->DstVrmAssetList = AssetList;
    Instance->SrcVrmAssetList = AssetList;
    Instance->SrcAsSkinnedMeshComponent = Mesh;

    FVtuberAnimProxy Proxy(Instance);
    Proxy.bIgnoreVRMConstraint = true;
    Proxy.bIgnoreVRMSwingBone = true;
    Proxy.PreUpdate(Instance, 1.f / 60.f);
    FMemMark Mark(FMemStack::Get());
    const TArray<FBoneIndexType> Required{0, 1, 2};
    FBoneContainer Bones;
    Bones.InitializeTo(Required, UE::Anim::FCurveFilterSettings(), *Skeleton);
    FPoseContext BaseProbe(Bones);
    BaseProbe.AnimInstanceProxy = &Proxy;
    BaseProbe.ResetToRefPose();
    if (!TestTrue(TEXT("Fixture reaches VRM4U source-copy true path"), Proxy.FVrmAnimInstanceCopyProxy::Evaluate(BaseProbe))) return false;
    FPoseContext Output(Bones);
    Output.AnimInstanceProxy = &Proxy;
    Output.ResetToRefPose();
    Output.Pose[FCompactPoseBoneIndex(1)].SetTranslation(FVector(777, 0, 0));
    TestTrue(TEXT("VTuber proxy evaluates"), Proxy.Evaluate(Output));
    TestTrue(TEXT("Unused PMX bone is initialized from reference pose"), Output.Pose[FCompactPoseBoneIndex(1)].Equals(UnusedPose));
    Mesh->AnimScriptInstance = nullptr;
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVtuberPmxPhysicsProxyTest, "HonkaiVTuber.Rig.PmxPhysicsProxy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVtuberPmxPhysicsProxyTest::RunTest(const FString& Parameters)
{
    auto* Skeleton = NewObject<USkeleton>();
    {
        FReferenceSkeletonModifier Modifier(Skeleton);
        Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
        Modifier.Add(FMeshBoneInfo(TEXT("unused"), TEXT("unused"), 0), FTransform(FVector(13, 0, 0)));
        Modifier.Add(FMeshBoneInfo(TEXT("pendant"), TEXT("pendant"), 0), FTransform(FVector(0, 0, 100)));
        Modifier.Add(FMeshBoneInfo(TEXT("tip"), TEXT("tip"), 2), FTransform(FVector(0, 0, 10)));
        Modifier.Add(FMeshBoneInfo(TEXT("omitted_tip"), TEXT("omitted_tip"), 3), FTransform(FVector(0, 0, 10)));
    }
    auto* Asset = NewObject<USkeletalMesh>();
    Asset->SetSkeleton(Skeleton); Asset->SetRefSkeleton(Skeleton->GetReferenceSkeleton());
    auto* Mesh = NewObject<USkeletalMeshComponent>();
    Mesh->SetSkeletalMesh(Asset);
    Mesh->SetWorldTransform(FTransform(FRotator(0, -90, 0), FVector(100, 200, 300)));
    auto* Instance = NewObject<UVtuberAnimInstance>(Mesh);
    Mesh->AnimScriptInstance = Instance;
    auto* AssetList = NewObject<UVrmAssetListObject>(Mesh);
    AssetList->SkeletalMesh = Asset;
    AssetList->VrmMetaObject = NewObject<UVrmMetaObject>(AssetList);
    AssetList->VrmMetaObject->SkeletalMesh = Asset;
    AssetList->VrmMetaObject->VrmAssetListObject = AssetList;
    auto* Data = NewObject<UVrmPmxPhysicsData>(AssetList);
    AssetList->PmxPhysicsData = Data; Instance->DstVrmAssetList = AssetList;
    Data->ModelScale = .1f;
    auto& Body = Data->RigidBodies.AddDefaulted_GetRef();
    Body.BoneName = TEXT("pendant"); Body.SourceBoneIndex = 2; Body.Mode = 1; Body.Shape = 0;
    Body.Position = FVector(0, 10, 0); Body.Size = FVector(.5, 0, 0); Body.Mass = 1;
    const FQuat RootRotation = FRotator(0, 12, 0).Quaternion();
    Instance->BodyComponentRotations.Add(TEXT("root"), RootRotation);
    FVtuberAnimProxy Proxy(Instance);
    FMemMark Mark(FMemStack::Get());
    FBoneContainer Bones;
    Bones.InitializeTo(TArray<FBoneIndexType>{0, 2, 3}, UE::Anim::FCurveFilterSettings(), *Skeleton);
    const auto Pendant = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(2));
    const auto Tip = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(3));
    TestEqual(TEXT("Fixture uses sparse compact indices"), Pendant.GetInt(), 1);
    FPoseContext Output(Bones); Output.AnimInstanceProxy = &Proxy;
    auto Step = [&](float Delta)
    {
        Proxy.PreUpdate(Instance, Delta);
        return Proxy.Evaluate(Output);
    };
    TestTrue(TEXT("PMX metadata initializes through the animation proxy"), Step(0));
    TestTrue(TEXT("Initial physical pose matches the current animation"), Output.Pose[Pendant].GetLocation().Equals(FVector(0, 0, 100), .01));
    for (int32 Frame = 0; Frame < 60; ++Frame) TestTrue(TEXT("Real Bullet proxy step evaluates"), Step(1.f / 60.f));
    const FVector FallenPosition = Output.Pose[Pendant].GetLocation();
    TestTrue(TEXT("PMX gravity reaches the compact output pose"), FallenPosition.Z < 0);
    TestTrue(TEXT("An unsimulated child follows its dynamic parent without cancelling the fall"), Output.Pose[Tip].GetLocation().Equals(FVector(0, 0, 10), .01));
    TestTrue(TEXT("Body tracking is applied before secondary physics"), Output.Pose[FCompactPoseBoneIndex(0)].GetRotation().Equals(RootRotation, .0001));

    FVtuberAnimProxy FreshProxy(Instance);
    FreshProxy.PreUpdate(Instance, 0);
    FPoseContext FreshOutput(Bones); FreshOutput.AnimInstanceProxy = &FreshProxy;
    FreshProxy.Evaluate(FreshOutput);
    TestTrue(TEXT("A new proxy owns an independent PMX context"), FreshOutput.Pose[Pendant].GetLocation().Equals(FVector(0, 0, 100), .01));
    Step(0);
    TestTrue(TEXT("Initializing another proxy does not reset this model"), Output.Pose[Pendant].GetLocation().Equals(FallenPosition, .01));
    Step(.5f);
    TestTrue(TEXT("A long raw frame resets physics instead of advancing a clamped step"), Output.Pose[Pendant].GetLocation().Equals(FVector(0, 0, 100), .01));
    Mesh->SetWorldScale3D(FVector(2));
    Step(0);
    TestTrue(TEXT("Scale changes rebuild physics at the animated pose"), Output.Pose[Pendant].GetLocation().Equals(FVector(0, 0, 100), .01));
    for (int32 Frame = 0; Frame < 60; ++Frame) Step(1.f / 60.f);
    TestTrue(TEXT("Physics continues after a scale rebuild"), Output.Pose[Pendant].GetLocation().Z < 0);
    for (const FTransform& Transform : Output.Pose.GetBones())
        TestTrue(TEXT("Every compact transform remains finite and normalized"), !Transform.ContainsNaN() && Transform.GetRotation().IsNormalized());
    Mesh->AnimScriptInstance = nullptr;
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVtuberCurveTest, "HonkaiVTuber.Rig.ThresholdCurve", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVtuberCurveTest::RunTest(const FString& Parameters)
{
    FVtuberThresholdCurve Curve{.2f, .8f};
    TestEqual(TEXT("Before start"), Curve.Evaluate(-.1f), 0.f);
    TestEqual(TEXT("At start"), Curve.Evaluate(.2f), 0.f);
    TestTrue(TEXT("Linear midpoint"), FMath::IsNearlyEqual(Curve.Evaluate(.5f), .5f));
    TestEqual(TEXT("At full"), Curve.Evaluate(.8f), 1.f);
    TestEqual(TEXT("Above full"), Curve.Evaluate(1.5f), 1.f);
    TestTrue(TEXT("Continuous at start"), Curve.Evaluate(.20001f) < .0001f);
    TestTrue(TEXT("Continuous at full"), Curve.Evaluate(.79999f) > .9999f);
    Curve.Full = Curve.Start;
    TestFalse(TEXT("Coincident knots rejected"), Curve.IsValid());
    TestEqual(TEXT("Invalid curve safe"), Curve.Evaluate(.5f), 0.f);
    const FVtuberThresholdCurve Identity;
    TestEqual(TEXT("Negative head direction preserved"), UVtuberRigComponent::MapSignedAngle(-20, 40, Identity, Identity), -20.f);
    TestEqual(TEXT("Head range limited"), UVtuberRigComponent::MapSignedAngle(200, 40, Identity, Identity), 40.f);
    TestEqual(TEXT("52 MediaPipe channels"), UVtuberRigComponent::GetTrackingChannels().Num(), 52);
    auto* Rig = NewObject<UVtuberRigComponent>();
    TestTrue(TEXT("Body response is identity at neutral"), FMath::IsNearlyZero(Rig->EvaluateChannelResponse(TEXT("upperarm_l"), 0.f)));
    TestTrue(TEXT("Eye response retains the original project's open and closed thresholds"),
        FMath::IsNearlyEqual(Rig->GetChannelResponse(TEXT("eyeBlinkLeft")).Lower, .3f)
        && FMath::IsNearlyEqual(Rig->GetChannelResponse(TEXT("eyeBlinkLeft")).Upper, .7f));
    auto Response = Rig->GetChannelResponse(TEXT("upperarm_l"));
    Response.bInvert = true; Response.Curvature = .6f; Response.Precision = 20;
    TestTrue(TEXT("Valid per-channel response accepted"), Rig->SetChannelResponse(TEXT("upperarm_l"), Response));
    TestTrue(TEXT("Bipolar inversion retains neutral"), FMath::IsNearlyZero(Rig->EvaluateChannelResponse(TEXT("upperarm_l"), 0.f)));
    TestTrue(TEXT("Bipolar curve is odd and inverted"), Rig->EvaluateChannelResponse(TEXT("upperarm_l"), .5f) < 0
        && FMath::IsNearlyEqual(Rig->EvaluateChannelResponse(TEXT("upperarm_l"), .5f), -Rig->EvaluateChannelResponse(TEXT("upperarm_l"), -.5f)));
    Response.Lower = Response.Upper;
    TestFalse(TEXT("Coincident response range rejected"), Rig->SetChannelResponse(TEXT("upperarm_l"), Response));
    auto HeadResponse = Rig->GetChannelResponse(TEXT("headYawPositive")); HeadResponse.bInvert = true;
    Rig->SetChannelResponse(TEXT("headYawPositive"), HeadResponse);
    TestEqual(TEXT("Inverted head remains still at neutral"), Rig->EvaluateChannelResponse(TEXT("headYawPositive"), 0.f), 0.f);
    TestTrue(TEXT("Head inversion reverses rotation rather than adding a constant"), Rig->EvaluateChannelResponse(TEXT("headYawPositive"), .5f) < 0);
    auto Legacy = Rig->GetMappings()[1]; Legacy.Curve = FVtuberThresholdCurve{.2f, .8f}; Legacy.Maximum = .6f; Legacy.Sensitivity = .5f;
    Rig->SetMapping(Legacy);
    TestTrue(TEXT("UI response evaluator includes the legacy morph chain"), FMath::IsNearlyEqual(Rig->EvaluateChannelResponse(Legacy.Channel, .5f), .15f));
    Rig->ResetChannelResponse(Legacy.Channel);
    TestTrue(TEXT("Reset clears legacy and new response controls together"), FMath::IsNearlyEqual(Rig->EvaluateChannelResponse(Legacy.Channel, .5f), .5f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVtuberProfileTest, "HonkaiVTuber.Rig.ProfileValidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVtuberProfileTest::RunTest(const FString& Parameters)
{
    auto* Rig = NewObject<UVtuberRigComponent>();
    FVtuberRigSettings Settings = Rig->GetSettings();
    Settings.YawLimitDegrees = 200;
    TestFalse(TEXT("Unsafe head range rejected"), Rig->SetSettings(Settings));
    const FString Path = FPaths::ProjectSavedDir() / TEXT("Automation/VtuberRig.json");
    FString Error;
    auto Response = Rig->GetChannelResponse(TEXT("upperarm_l"));
    Response.Lower = -.7f; Response.Upper = .8f; Response.bInvert = true;
    Response.SmoothingSeconds = .17f; Response.Sensitivity = 1.6f; Response.Curvature = .4f; Response.Precision = 25;
    TestTrue(TEXT("Configure body response"), Rig->SetChannelResponse(TEXT("upperarm_l"), Response));
    TestTrue(TEXT("Write profile"), Rig->SaveProfile(Path, Error));
    auto* Copy = NewObject<UVtuberRigComponent>();
    TestTrue(TEXT("Read profile"), Copy->LoadProfile(Path, Error));
    TestEqual(TEXT("Range preserved"), Copy->GetSettings().YawLimitDegrees, 55.f);
    const auto RestoredResponse = Copy->GetChannelResponse(TEXT("upperarm_l"));
    TestTrue(TEXT("Channel response round trips all fields"), RestoredResponse.bInvert && RestoredResponse.bBipolar
        && FMath::IsNearlyEqual(RestoredResponse.Lower, -.7f) && FMath::IsNearlyEqual(RestoredResponse.Upper, .8f)
        && FMath::IsNearlyEqual(RestoredResponse.SmoothingSeconds, .17f) && FMath::IsNearlyEqual(RestoredResponse.Sensitivity, 1.6f)
        && FMath::IsNearlyEqual(RestoredResponse.Curvature, .4f) && RestoredResponse.Precision == 25);
    FString Saved;
    FFileHelper::LoadFileToString(Saved, *Path);
    TSharedPtr<FJsonObject> Invalid;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Saved), Invalid);
    Invalid->GetObjectField(TEXT("settings"))->SetNumberField(TEXT("yaw_limit"), 200);
    Saved.Reset();
    FJsonSerializer::Serialize(Invalid.ToSharedRef(), TJsonWriterFactory<>::Create(&Saved));
    FFileHelper::SaveStringToFile(Saved, *Path);
    TestFalse(TEXT("Unsafe JSON rejected atomically"), Copy->LoadProfile(Path, Error));
    TestEqual(TEXT("Previous setting retained"), Copy->GetSettings().YawLimitDegrees, 55.f);
    TestTrue(TEXT("Restore valid profile before body validation"), Copy->SaveProfile(Path, Error));
    FFileHelper::LoadFileToString(Saved, *Path);
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Saved), Invalid);
    Invalid->GetObjectField(TEXT("body_bones"))->SetStringField(TEXT("unexpected_joint"), TEXT("head"));
    Saved.Reset();
    FJsonSerializer::Serialize(Invalid.ToSharedRef(), TJsonWriterFactory<>::Create(&Saved));
    FFileHelper::SaveStringToFile(Saved, *Path);
    TestFalse(TEXT("Unknown body channel rejected atomically"), Copy->LoadProfile(Path, Error));
    TestEqual(TEXT("Previous body setting retained"), Copy->GetSettings().YawLimitDegrees, 55.f);
    IFileManager::Get().Delete(*Path);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVtuberRuntimeRigTest, "HonkaiVTuber.Rig.NativePoseAndMorph", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVtuberRuntimeRigTest::RunTest(const FString& Parameters)
{
    // No render data, private models, or registered world components: this checks native CPU pose and weights.
    auto* Skeleton = NewObject<USkeleton>();
    const FTransform RootPose(FRotator(0, 20, 0));
    const FTransform HeadPose(FRotator(12, 0, 0), FVector(0, 0, 40));
    {
        FReferenceSkeletonModifier Modifier(Skeleton);
        Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), RootPose);
        Modifier.Add(FMeshBoneInfo(TEXT("unused"), TEXT("unused"), 0), FTransform::Identity);
        Modifier.Add(FMeshBoneInfo(TEXT("head"), TEXT("head"), 0), HeadPose);
    }
    auto* Asset = NewObject<USkeletalMesh>();
    Asset->SetSkeleton(Skeleton);
    Asset->SetRefSkeleton(Skeleton->GetReferenceSkeleton());
    auto* BlinkA = NewObject<UMorphTarget>(Asset, TEXT("BlinkA"));
    auto* BlinkB = NewObject<UMorphTarget>(Asset, TEXT("BlinkB"));
    FMorphTargetLODModel MorphData;
    FMorphTargetDelta Delta;
    Delta.SourceIdx = 0; Delta.PositionDelta = FVector3f(0, 0, -1); Delta.TangentZDelta = FVector3f::ZeroVector;
    MorphData.Vertices.Add(Delta); MorphData.NumVertices = 1; MorphData.NumBaseMeshVerts = 1;
    BlinkA->GetMorphLODModels().Add(MorphData); BlinkB->GetMorphLODModels().Add(MorphData);
    Asset->GetMorphTargets().Add(BlinkA); Asset->GetMorphTargets().Add(BlinkB);
    auto* Mesh = NewObject<USkeletalMeshComponent>();
    Mesh->SetSkeletalMesh(Asset);
    auto* Rig = NewObject<UVtuberRigComponent>();
    if (!TestTrue(TEXT("Initialize unregistered CPU rig"), Rig->Initialize(Mesh))) return false;
    TestTrue(TEXT("Hand channel accepts a model bone"), Rig->SetBodyBone(TEXT("thumb1_l"), TEXT("head")));
    TestFalse(TEXT("Unknown body channel rejected"), Rig->SetBodyBone(TEXT("unknown_joint"), TEXT("head")));
    TestFalse(TEXT("Unknown target bone rejected"), Rig->SetBodyBone(TEXT("thumb1_l"), TEXT("absent")));
    const FString BodyPath = FPaths::ProjectSavedDir() / TEXT("Automation/VtuberBodyMap.json");
    FString BodyError;
    TestTrue(TEXT("Save selected hand bone"), Rig->SaveProfile(BodyPath, BodyError));
    TestTrue(TEXT("Clear hand bone"), Rig->SetBodyBone(TEXT("thumb1_l"), NAME_None));
    TestTrue(TEXT("Cleared hand bone is reported missing"), Rig->GetMissingBodyMappings().Contains(TEXT("thumb1_l")));
    TestTrue(TEXT("Load selected hand bone"), Rig->LoadProfile(BodyPath, BodyError));
    TestEqual(TEXT("Selected bone round trips"), Rig->GetBodyBone(TEXT("thumb1_l")), FName(TEXT("head")));
    IFileManager::Get().Delete(*BodyPath);
    TestTrue(TEXT("Isolate head pose from hand mapping"), Rig->SetBodyBone(TEXT("thumb1_l"), NAME_None));
    auto* Instance = NewObject<UVtuberAnimInstance>(Mesh);
    Mesh->AnimScriptInstance = Instance;
    auto Settings = Rig->GetSettings(); Settings.SmoothingSeconds = 0;
    Rig->SetSettings(Settings);
    FVtuberMorphMapping Mapping; Mapping.Channel = TEXT("eyeBlinkLeft"); Mapping.Morph = BlinkA->GetFName();
    TestTrue(TEXT("Map actual synthetic morph"), Rig->SetMapping(Mapping));
    auto LinearEye = Rig->GetChannelResponse(Mapping.Channel);
    LinearEye.Lower = 0.f; LinearEye.Upper = 1.f; LinearEye.SmoothingSeconds = 0.f;
    Rig->SetChannelResponse(Mapping.Channel, LinearEye);
    FVtuberTrackingSnapshot Snapshot;
    Snapshot.bFaceTracked = true; Snapshot.TimestampSeconds = FPlatformTime::Seconds();
    Snapshot.HeadRotation = FRotator(0, 30, 0).Quaternion(); Snapshot.BlendShapes.Add(Mapping.Channel, .75f);
    Rig->SubmitSnapshot(Snapshot);
    Rig->UpdateTracking(1.f / 60);
    TestTrue(TEXT("Native rig writes morph weight"), FMath::IsNearlyEqual(Mesh->GetMorphTarget(BlinkA->GetFName()), .75f));
    {
        FMemMark Mark(FMemStack::Get());
        const TArray<FBoneIndexType> Required{0, 2}; // Head mesh index 2 becomes compact index 1.
        FBoneContainer Bones;
        Bones.InitializeTo(Required, UE::Anim::FCurveFilterSettings(), *Skeleton);
        FPoseContext Output(Bones);
        FVtuberAnimProxy Proxy(Instance);
        Proxy.PreUpdate(Instance, 1.f / 60);
        TestTrue(TEXT("Production anim proxy evaluates"), Proxy.Evaluate(Output));
        const FCompactPoseBoneIndex HeadIndex = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(2));
        TestEqual(TEXT("Nonidentity compact mapping"), HeadIndex.GetInt(), 1);
        const FQuat EvaluatedHeadCS = RootPose.GetRotation() * Output.Pose[HeadIndex].GetRotation();
        const FQuat ExpectedHeadCS = Snapshot.HeadRotation * Rig->HeadBindCS;
        TestTrue(TEXT("Head rotates 30 degrees around component axis despite rotated bind pose"), EvaluatedHeadCS.Equals(ExpectedHeadCS, .0001f));
        TestTrue(TEXT("Root pose untouched"), Output.Pose[FCompactPoseBoneIndex(0)].Equals(RootPose));
    }
    Mapping.Morph = BlinkB->GetFName(); Rig->SetMapping(Mapping);
    Rig->UpdateTracking(1.f / 60);
    TestEqual(TEXT("Changing mapping clears old target"), Mesh->GetMorphTarget(BlinkA->GetFName()), 0.f);
    TestTrue(TEXT("Changing mapping drives new target"), FMath::IsNearlyEqual(Mesh->GetMorphTarget(BlinkB->GetFName()), .75f));
    Snapshot.bFaceTracked = false; Snapshot.TimestampSeconds = FPlatformTime::Seconds(); Rig->SubmitSnapshot(Snapshot);
    Rig->UpdateTracking(.1f);
    TestTrue(TEXT("Lost pose returns smoothly, not instantly"), Rig->SmoothedHead.Rotator().Yaw > 0 && Rig->SmoothedHead.Rotator().Yaw < 30);
    TestTrue(TEXT("Lost morph fades continuously"), Mesh->GetMorphTarget(BlinkB->GetFName()) > 0 && Mesh->GetMorphTarget(BlinkB->GetFName()) < .75f);
    for (int32 I = 0; I < 40; ++I) Rig->UpdateTracking(.1f);
    TestTrue(TEXT("Lost head returns to neutral"), Rig->SmoothedHead.AngularDistance(FQuat::Identity) < .001);
    TestEqual(TEXT("Lost morph eventually removed"), Mesh->GetMorphTarget(BlinkB->GetFName()), 0.f);
    Snapshot.bFaceTracked = true; Snapshot.TimestampSeconds = FPlatformTime::Seconds(); Rig->SubmitSnapshot(Snapshot);
    TestTrue(TEXT("Tracked neutral calibration accepted"), Rig->CalibrateNeutral());
    Rig->UpdateTracking(.1f);
    TestTrue(TEXT("Calibrated orientation becomes neutral"), Rig->SmoothedHead.Equals(FQuat::Identity, .0001f));
    TestEqual(TEXT("Calibrated expression becomes zero"), Mesh->GetMorphTarget(BlinkB->GetFName()), 0.f);
    TestTrue(TEXT("Asset root reference remains unchanged"), Asset->GetRefSkeleton().GetRefBonePose()[0].Equals(RootPose));
    TestTrue(TEXT("Asset head reference remains unchanged"), Asset->GetRefSkeleton().GetRefBonePose()[2].Equals(HeadPose));
    TestEqual(TEXT("Asset morph delta remains unchanged"), BlinkA->GetMorphLODModels()[0].Vertices[0].PositionDelta, FVector3f(0, 0, -1));
    TestTrue(TEXT("Map hand channel for hand-only input"), Rig->SetBodyBone(TEXT("thumb1_l"), TEXT("head")));
    FVtuberTrackingSnapshot HandOnly;
    HandOnly.TimestampSeconds = FPlatformTime::Seconds();
    FVtuberHandTracking Hand;
    Hand.Handedness = TEXT("Left"); Hand.Score = 1.f; Hand.WorldLandmarks.SetNum(21);
    Hand.WorldLandmarks[1].Position = FVector(.1f, .1f, .1f);
    Hand.WorldLandmarks[2].Position = FVector(.1f, .2f, .1f);
    HandOnly.Hands.Add(MoveTemp(Hand));
    Rig->SubmitSnapshot(HandOnly); Rig->UpdateTracking(.1f);
    TestFalse(TEXT("Two finger points without a palm frame cannot drive an unrelated head bone"), Rig->IsBodyChannelTracked(TEXT("thumb1_l")));
    TestEqual(TEXT("Incomplete palm safely keeps the mapped joint neutral"), Rig->GetBodyChannelOutputDegrees(TEXT("thumb1_l")), 0.f);
    Mapping.bInvert = true; Rig->SetMapping(Mapping); Rig->UpdateTracking(.1f);
    TestEqual(TEXT("Inverted expression returns to zero when face is lost"), Mesh->GetMorphTarget(BlinkB->GetFName()), 0.f);
    Mesh->AnimScriptInstance = nullptr;

    auto* RotatedMesh = NewObject<USkeletalMeshComponent>();
    RotatedMesh->SetSkeletalMesh(Asset);
    RotatedMesh->SetWorldRotation(FRotator(0, -90, 0));
    auto* RotatedRig = NewObject<UVtuberRigComponent>();
    if (!TestTrue(TEXT("Initialize rotated model basis"), RotatedRig->Initialize(RotatedMesh))) return false;
    auto* RotatedInstance = NewObject<UVtuberAnimInstance>(RotatedMesh);
    RotatedMesh->AnimScriptInstance = RotatedInstance;
    Settings = RotatedRig->GetSettings(); Settings.SmoothingSeconds = 0;
    RotatedRig->SetSettings(Settings);
    Snapshot.HeadRotation = FRotator(30, 0, 0).Quaternion(); Snapshot.TimestampSeconds = FPlatformTime::Seconds();
    RotatedRig->SubmitSnapshot(Snapshot); RotatedRig->UpdateTracking(.1f);
    FQuat ComponentDelta = (RotatedRig->ParentBindCS * RotatedInstance->HeadLocalRotation * RotatedRig->HeadBindCS.Inverse()).GetNormalized();
    TestTrue(TEXT("World yaw -90 maps canonical pitch to component X"), FMath::Abs(ComponentDelta.GetRotationAxis().X) > .999);
    TestTrue(TEXT("Canonical pitch magnitude is preserved"), FMath::IsNearlyEqual(ComponentDelta.GetAngle(), FMath::DegreesToRadians(30.0), .0001));
    Snapshot.HeadRotation = FRotator(0, 30, 0).Quaternion(); Snapshot.TimestampSeconds = FPlatformTime::Seconds();
    RotatedRig->SubmitSnapshot(Snapshot); RotatedRig->UpdateTracking(.1f);
    ComponentDelta = (RotatedRig->ParentBindCS * RotatedInstance->HeadLocalRotation * RotatedRig->HeadBindCS.Inverse()).GetNormalized();
    TestTrue(TEXT("Rotated model preserves canonical yaw around component Z"), ComponentDelta.Equals(Snapshot.HeadRotation, .0001f));
    RotatedMesh->AnimScriptInstance = nullptr;
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVtuberAnatomicalRetargetTest, "HonkaiVTuber.Rig.AnatomicalRetarget", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVtuberAnatomicalRetargetTest::RunTest(const FString& Parameters)
{
    auto* Skeleton = NewObject<USkeleton>();
    TArray<FTransform> GlobalRest;
    TMap<FName, int32> Indices;
    {
        FReferenceSkeletonModifier Modifier(Skeleton);
        auto Add = [&](FName Name, FName Parent, FVector Location, FRotator Rotation = FRotator(7, 20, -4))
        {
            const int32 ParentIndex = Parent.IsNone() ? INDEX_NONE : Indices.FindChecked(Parent);
            const FTransform Global(Rotation, Location);
            const FTransform Local = ParentIndex == INDEX_NONE ? Global : Global.GetRelativeTransform(GlobalRest[ParentIndex]);
            Modifier.Add(FMeshBoneInfo(Name, Name.ToString(), ParentIndex), Local);
            Indices.Add(Name, GlobalRest.Num()); GlobalRest.Add(Global);
        };
        Add(TEXT("root"), NAME_None, FVector::ZeroVector, FRotator(0, 20, 0));
        Add(TEXT("腰"), TEXT("root"), FVector(0, 0, 90), FRotator(12, 20, 5));
        Add(TEXT("下半身"), TEXT("腰"), FVector(0, 0, 80));
        Add(TEXT("上半身"), TEXT("腰"), FVector(0, 0, 100));
        Add(TEXT("上半身2"), TEXT("上半身"), FVector(0, 0, 130));
        Add(TEXT("首"), TEXT("上半身2"), FVector(0, 0, 150));
        Add(TEXT("頭"), TEXT("首"), FVector(0, 0, 165), FRotator(12, 24, -7));
        for (int32 Side = 0; Side < 2; ++Side)
        {
            const FString Prefix = Side == 0 ? TEXT("左") : TEXT("右");
            const float Sign = Side == 0 ? -1.f : 1.f;
            auto B = [&](const TCHAR* Part) { return FName(*(Prefix + Part)); };
            Add(B(TEXT("肩")), TEXT("上半身2"), FVector(0, Sign * 10, 140));
            Add(B(TEXT("肩C")), B(TEXT("肩")), FVector(0, Sign * 15, 140));
            Add(B(TEXT("腕")), B(TEXT("肩C")), FVector(0, Sign * 20, 140));
            // Helpers deliberately precede the true anatomical endpoint.
            Add(B(TEXT("腕捩")), B(TEXT("腕")), FVector(0, Sign * 35, 130));
            Add(B(TEXT("ひじ")), B(TEXT("腕捩")), FVector(0, Sign * 45, 125));
            Add(B(TEXT("手捩")), B(TEXT("ひじ")), FVector(0, Sign * 55, 115));
            Add(B(TEXT("手首")), B(TEXT("手捩")), FVector(0, Sign * 65, 105));
            Add(B(TEXT("ダミー")), B(TEXT("手首")), FVector(12, Sign * 65, 105));
            Add(B(TEXT("人指１")), B(TEXT("手首")), FVector(2, Sign * 72, 102));
            Add(B(TEXT("人指２")), B(TEXT("人指１")), FVector(2, Sign * 76, 99));
            Add(B(TEXT("人指３")), B(TEXT("人指２")), FVector(2, Sign * 79, 96));
            Add(B(TEXT("中指１")), B(TEXT("手首")), FVector(0, Sign * 73, 100));
            Add(B(TEXT("小指１")), B(TEXT("手首")), FVector(-2, Sign * 72, 100));
            Add(B(TEXT("親指０")), B(TEXT("手首")), FVector(3, Sign * 68, 104));
            Add(B(TEXT("親指１")), B(TEXT("親指０")), FVector(5, Sign * 70, 102));
            Add(B(TEXT("親指２")), B(TEXT("親指１")), FVector(6, Sign * 72, 100));
        }
    }
    auto* Asset = NewObject<USkeletalMesh>();
    Asset->SetSkeleton(Skeleton); Asset->SetRefSkeleton(Skeleton->GetReferenceSkeleton());
    auto* Mesh = NewObject<USkeletalMeshComponent>(); Mesh->SetSkeletalMesh(Asset);
    auto* Rig = NewObject<UVtuberRigComponent>();
    if (!TestTrue(TEXT("Initialize anatomical PMX fixture"), Rig->Initialize(Mesh))) return false;
    auto* Instance = NewObject<UVtuberAnimInstance>(Mesh); Mesh->AnimScriptInstance = Instance;
    auto Settings = Rig->GetSettings(); Settings.SmoothingSeconds = 0.f; Rig->SetSettings(Settings);
    TestEqual(TEXT("PMX spine never binds pelvis control"), Rig->GetBodyBone(TEXT("spine")), FName(TEXT("上半身")));
    TestEqual(TEXT("PMX thumb begins at segment zero"), Rig->GetBodyBone(TEXT("thumb1_l")), FName(TEXT("左親指０")));
    // This is the original Godot fixture contract: MP=(godot.x,-godot.y,-godot.z),
    // UE=(godot.z,-godot.x,godot.y). Expected data is encoded independently of
    // TrackingToCanonical, including legal zero-valued wrist coordinates.
    auto MP = [](const FVector& UE) { return FVector(-UE.Y, -UE.Z, -UE.X); };
    auto RestPosition = [&](const TCHAR* Name) { return GlobalRest[Indices.FindChecked(Name)].GetLocation() / 100.; };
    FVtuberTrackingSnapshot Neutral;
    Neutral.bPoseTracked = true; Neutral.bFaceTracked = true;
    Neutral.PoseWorldLandmarks.SetNum(33);
    auto SetPose = [&](FVtuberTrackingSnapshot& Frame, int32 Index, const FVector& P)
    {
        auto& Point = Frame.PoseWorldLandmarks[Index]; Point.Position = MP(P); Point.Visibility = .9f; Point.Presence = .9f;
    };
    SetPose(Neutral, 11, RestPosition(TEXT("左腕"))); SetPose(Neutral, 12, RestPosition(TEXT("右腕")));
    SetPose(Neutral, 13, RestPosition(TEXT("左ひじ"))); SetPose(Neutral, 14, RestPosition(TEXT("右ひじ")));
    SetPose(Neutral, 15, RestPosition(TEXT("左手首"))); SetPose(Neutral, 16, RestPosition(TEXT("右手首")));
    SetPose(Neutral, 23, FVector(0, -.15, .9)); SetPose(Neutral, 24, FVector(0, .15, .9));
    auto Submit = [&](FVtuberTrackingSnapshot Frame)
    {
        Frame.TimestampSeconds = FPlatformTime::Seconds(); Rig->SubmitSnapshot(Frame); Rig->UpdateTracking(1.f / 60.f);
    };
    auto Evaluate = [&](float DeltaSeconds = 1.f / 60.f)
    {
        TArray<FTransform> Result;
        FMemMark Mark(FMemStack::Get());
        TArray<FBoneIndexType> Required; for (int32 I = 0; I < GlobalRest.Num(); ++I) Required.Add(I);
        FBoneContainer Bones; Bones.InitializeTo(Required, UE::Anim::FCurveFilterSettings(), *Skeleton);
        FVtuberAnimProxy Proxy(Instance); Proxy.PreUpdate(Instance, DeltaSeconds);
        FPoseContext Output(Bones); Output.AnimInstanceProxy = &Proxy;
        Proxy.Evaluate(Output);
        for (int32 I = 0; I < GlobalRest.Num(); ++I) Result.Add(Output.Pose[Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(I))]);
        return Result;
    };
    const TArray<FTransform> Rest = Asset->GetRefSkeleton().GetRefBonePose();
    auto ComponentPose = [&](const TArray<FTransform>& Local)
    {
        TArray<FTransform> Result;
        for (int32 I = 0; I < Local.Num(); ++I)
        {
            const int32 Parent = Asset->GetRefSkeleton().GetParentIndex(I);
            Result.Add(Parent == INDEX_NONE ? Local[I] : Local[I] * Result[Parent]);
        }
        return Result;
    };
    auto CheckIdle = [&](const TArray<FTransform>& Local)
    {
        const auto CS = ComponentPose(Local);
        for (int32 Side = 0; Side < 2; ++Side)
        {
            const FString Prefix = Side == 0 ? TEXT("左") : TEXT("右");
            const int32 Upper = Indices.FindChecked(*(Prefix + TEXT("腕")));
            const int32 Elbow = Indices.FindChecked(*(Prefix + TEXT("ひじ")));
            const FVector Direction = (CS[Elbow].GetLocation() - CS[Upper].GetLocation()).GetSafeNormal();
            const double Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Direction, -FVector::UpVector), -1., 1.)));
            TestTrue(TEXT("Idle upper arms rest twenty degrees outward from the torso"), FMath::IsNearlyEqual(Angle, 20., .01));
            TestTrue(TEXT("Idle left and right arms remain on their anatomical sides"), Direction.Y * (Side == 0 ? -1 : 1) > 0);
        }
        for (int32 I = 0; I < Local.Num(); ++I)
        {
            const bool Upper = I == Indices.FindChecked(TEXT("左腕")) || I == Indices.FindChecked(TEXT("右腕"));
            TestTrue(TEXT("Idle pose remains finite and retains authored translations and scales"), !Local[I].ContainsNaN()
                && Local[I].GetLocation().Equals(Rest[I].GetLocation(), .001) && Local[I].GetScale3D().Equals(Rest[I].GetScale3D(), .001));
            if (!Upper) TestTrue(TEXT("Forearms, hands, fingers and other local poses retain their authored rest"), Local[I].Equals(Rest[I], .001));
        }
    };
    Rig->UpdateTracking(1.f / 60.f);
    auto Pose = Evaluate();
    CheckIdle(Pose);
    TestFalse(TEXT("Display idle is not reported as a tracked arm"), Rig->IsBodyChannelTracked(TEXT("upperarm_l")));
    const auto IdleCS = ComponentPose(Pose);
    Mesh->SetWorldRotation(FRotator(0, -90, 0)); Rig->UpdateTracking(1.f / 60.f);
    CheckIdle(Evaluate());
    Mesh->SetWorldRotation(FRotator::ZeroRotator);
#if WITH_VRM4U_PMX_BULLET
    auto* IdleAssetList = NewObject<UVrmAssetListObject>(Mesh);
    IdleAssetList->SkeletalMesh = Asset;
    auto* IdlePhysics = NewObject<UVrmPmxPhysicsData>(IdleAssetList);
    IdlePhysics->ModelScale = .1f; IdleAssetList->PmxPhysicsData = IdlePhysics;
    auto& IdleBody = IdlePhysics->RigidBodies.AddDefaulted_GetRef();
    const int32 Wrist = Indices.FindChecked(TEXT("左手首"));
    const FVector WristRest = GlobalRest[Wrist].GetLocation();
    IdleBody.BoneName = TEXT("左手首"); IdleBody.SourceBoneIndex = Wrist;
    IdleBody.Mode = 1; IdleBody.Shape = 0; IdleBody.Size = FVector(.1, 0, 0); IdleBody.Mass = 1; IdleBody.CollisionMask = 0;
    IdleBody.Position = FVector(WristRest.X, WristRest.Z, -WristRest.Y) / 10.;
    Instance->DstVrmAssetList = IdleAssetList;
    const auto PhysicalIdleCS = ComponentPose(Evaluate(0));
    TestTrue(TEXT("Secondary physics initializes from the relaxed animated wrist, not the source bind pose"),
        PhysicalIdleCS[Wrist].Equals(IdleCS[Wrist], .01));
    Instance->DstVrmAssetList = nullptr;
#endif
    Submit(Neutral);
    Pose = Evaluate();
    for (int32 I = 0; I < Pose.Num(); ++I)
        TestTrue(FString::Printf(TEXT("Neutral retains nonidentity bind pose %d"), I), Pose[I].Equals(Rest[I], .0001f));
    TestFalse(TEXT("Waist control is never targeted"), Instance->BodyComponentRotations.Contains(TEXT("腰")));
    TestFalse(TEXT("Root is never targeted"), Instance->BodyComponentRotations.Contains(TEXT("root")));
    TestFalse(TEXT("Lower body is never targeted"), Instance->BodyComponentRotations.Contains(TEXT("下半身")));

    FVtuberTrackingSnapshot Lean = Neutral;
    const FQuat TorsoLean = FRotator(0, 0, 12).Quaternion();
    for (auto& Point : Lean.PoseWorldLandmarks)
        if (Point.Visibility > .45f) Point.Position = MP(TorsoLean.RotateVector(TrackingToCanonical(Point.Position)));
    Lean.HeadRotation = FRotator(0, 20, 0).Quaternion();
    Submit(Lean);
    const FQuat ExpectedHead = Lean.HeadRotation * GlobalRest[Indices.FindChecked(TEXT("頭"))].GetRotation();
    TestTrue(TEXT("Head component target does not inherit torso/neck twice"), Instance->BodyComponentRotations.FindChecked(TEXT("頭")).Equals(ExpectedHead, .0001f));
    const FQuat ExpectedUpper = TorsoLean * GlobalRest[Indices.FindChecked(TEXT("左腕"))].GetRotation();
    TestTrue(TEXT("Torso roll carries arm axial rotation through PMX helper chain"), Instance->BodyComponentRotations.FindChecked(TEXT("左腕")).Equals(ExpectedUpper, .0001f));
    Pose = Evaluate();
    for (const TCHAR* Name : {TEXT("root"), TEXT("腰"), TEXT("下半身"), TEXT("左肩"), TEXT("左腕捩"), TEXT("左手捩")})
    {
        const int32 I = Indices.FindChecked(Name);
        TestTrue(FString::Printf(TEXT("Untouched control/helper local pose %s"), Name), Pose[I].Equals(Rest[I], .0001f));
    }
    // Same upper/forearm directions as Godot test_mocap_retarget.gd, transformed
    // from its RH basis. The fixture includes twist helpers between endpoints.
    FVtuberTrackingSnapshot Bent = Neutral;
    auto GodotToUE = [](const FVector& V) { return FVector(V.Z, -V.X, V.Y); };
    const FVector Upper = GodotToUE(FVector(.8, .5, .05).GetSafeNormal());
    const FVector Lower = GodotToUE(FVector(-.1, .3, .7).GetSafeNormal());
    const FVector Shoulder = RestPosition(TEXT("左腕"));
    SetPose(Bent, 13, Shoulder + Upper * .24); SetPose(Bent, 15, Shoulder + Upper * .24 + Lower * .22);
    Submit(Bent);
    for (const auto& Pair : TArray<TPair<FName, FName>>{{TEXT("upperarm_l"), TEXT("lowerarm_l")}, {TEXT("lowerarm_l"), TEXT("hand_l")}})
    {
        const int32 A = Indices.FindChecked(Rig->GetBodyBone(Pair.Key)), B = Indices.FindChecked(Rig->GetBodyBone(Pair.Value));
        const FVector Reference = (GlobalRest[B].GetLocation() - GlobalRest[A].GetLocation()).GetSafeNormal();
        const FQuat Delta = Instance->BodyComponentRotations.FindChecked(Rig->GetBodyBone(Pair.Key)) * GlobalRest[A].GetRotation().Inverse();
        const FVector Expected = Pair.Key == TEXT("upperarm_l") ? Upper : Lower;
        TestTrue(TEXT("Anatomical arm aim survives nonidentity bind and helper parents"), FVector::DotProduct(Delta.RotateVector(Reference), Expected) > .995);
    }

    FVtuberTrackingSnapshot Missing = Neutral; Missing.bFaceTracked = false;
    for (int32 I : {11, 12, 13, 14, 15, 16})
        Missing.PoseWorldLandmarks[I].Visibility = Missing.PoseWorldLandmarks[I].Presence = .44f;
    const FQuat BeforeLoss = Rig->SmoothedBody.FindChecked(TEXT("upperarm_l"));
    Submit(Missing);
    TestFalse(TEXT("Low confidence shoulders reject torso"), Rig->IsBodyChannelTracked(TEXT("spine")));
    TestFalse(TEXT("Low confidence chain rejects arm"), Rig->IsBodyChannelTracked(TEXT("upperarm_l")));
    const FQuat FirstReturn = Rig->SmoothedBody.FindChecked(TEXT("upperarm_l"));
    const int32 IdleUpper = Indices.FindChecked(TEXT("左腕"));
    const FQuat IdleDelta = Rest[IdleUpper].GetRotation().Inverse() * Rig->IdleArmLocalRotations.FindChecked(TEXT("upperarm_l"));
    TestTrue(TEXT("Tracking loss returns toward idle continuously rather than snapping"),
        !FirstReturn.Equals(BeforeLoss, .0001) && !FirstReturn.Equals(IdleDelta, .0001));
    for (int32 I = 0; I < 80; ++I) Rig->UpdateTracking(.1f);
    Pose = Evaluate();
    CheckIdle(Pose);

    FVtuberTrackingSnapshot Hands;
    FVtuberHandTracking Hand; Hand.Handedness = TEXT("Left"); Hand.Score = .9f; Hand.WorldLandmarks.SetNum(21);
    const FVector PalmOrigin = RestPosition(TEXT("左手首"));
    for (auto& Point : Hand.WorldLandmarks) Point.Position = MP(PalmOrigin);
    auto HP = [&](int32 Point, const TCHAR* Bone) { Hand.WorldLandmarks[Point].Position = MP(RestPosition(Bone)); };
    HP(0, TEXT("左手首")); HP(1, TEXT("左親指０")); HP(2, TEXT("左親指１")); HP(3, TEXT("左親指２"));
    HP(5, TEXT("左人指１")); HP(6, TEXT("左人指２")); HP(7, TEXT("左人指３")); HP(9, TEXT("左中指１")); HP(17, TEXT("左小指１"));
    Hand.WorldLandmarks[4].Position = MP(RestPosition(TEXT("左親指２")) + FVector(.01, -.02, -.02));
    Hand.WorldLandmarks[8].Position = MP(RestPosition(TEXT("左人指３")) + FVector(0, -.03, -.03));
    const FVector PalmAxis = (RestPosition(TEXT("左中指１")) - PalmOrigin).GetSafeNormal();
    const FQuat PalmRoll(PalmAxis, FMath::DegreesToRadians(25.f));
    for (auto& Point : Hand.WorldLandmarks)
        Point.Position = MP(PalmRoll.RotateVector(TrackingToCanonical(Point.Position) - PalmOrigin));
    Hands.Hands.Add(Hand); Submit(Hands);
    TestTrue(TEXT("Hand-only packet tracks a complete palm"), Rig->IsBodyChannelTracked(TEXT("thumb1_l")));
    TestTrue(TEXT("Wrist follows full palm roll without a pose or face"), Instance->BodyComponentRotations.FindChecked(TEXT("左手首")).Equals(PalmRoll * GlobalRest[Indices.FindChecked(TEXT("左手首"))].GetRotation(), .0001f));
    TestTrue(TEXT("Palm roll carries finger axial rotation"), Instance->BodyComponentRotations.FindChecked(TEXT("左人指１")).Equals(PalmRoll * GlobalRest[Indices.FindChecked(TEXT("左人指１"))].GetRotation(), .0001f));
    for (const auto& Pair : Instance->BodyComponentRotations)
        TestTrue(TEXT("All hand-only output rotations are finite and normalized"), !Pair.Value.ContainsNaN() && Pair.Value.IsNormalized());
    for (FName Channel : Rig->GetBodyTrackingChannels(true))
        TestTrue(TEXT("Finger rotation remains within safety limit"), Rig->GetBodyChannelOutputDegrees(Channel) <= 110.01f);
    Hands.Hands[0].WorldLandmarks[5] = Hands.Hands[0].WorldLandmarks[17]; Submit(Hands);
    TestFalse(TEXT("Collapsed palm cannot drive fingers"), Rig->IsBodyChannelTracked(TEXT("thumb1_l")));

    Lean.bFaceTracked = false; Submit(Lean);
    TestTrue(TEXT("Standing body can calibrate without a face"), Rig->CalibrateNeutral());
    Submit(Lean);
    TestTrue(TEXT("Standing calibration removes captured torso lean"), Rig->SmoothedBody.FindChecked(TEXT("spine")).Equals(FQuat::Identity, .0001f));

    const FString Path = FPaths::ProjectSavedDir() / TEXT("Automation/VtuberLegacyRetarget.json");
    FString Error, Json;
    TestTrue(TEXT("Save retarget profile"), Rig->SaveProfile(Path, Error));
    FFileHelper::LoadFileToString(Json, *Path);
    TSharedPtr<FJsonObject> Legacy; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Legacy);
    Legacy->RemoveField(TEXT("responses")); Legacy->RemoveField(TEXT("neutral_body"));
    Legacy->GetObjectField(TEXT("body_bones"))->SetStringField(TEXT("spine"), TEXT("腰"));
    Legacy->GetObjectField(TEXT("body_bones"))->RemoveField(TEXT("chest"));
    auto Mapping = Rig->GetMappings()[1]; Mapping.Maximum = .42f; Rig->SetMapping(Mapping);
    Json.Reset(); FJsonSerializer::Serialize(Legacy.ToSharedRef(), TJsonWriterFactory<>::Create(&Json));
    // User tuning in an old profile must survive the targeted body migration.
    Legacy->GetArrayField(TEXT("mappings"))[1]->AsObject()->SetNumberField(TEXT("maximum"), .42);
    Json.Reset(); FJsonSerializer::Serialize(Legacy.ToSharedRef(), TJsonWriterFactory<>::Create(&Json));
    FFileHelper::SaveStringToFile(Json, *Path);
    TestTrue(TEXT("Legacy partial body map loads"), Rig->LoadProfile(Path, Error));
    TestEqual(TEXT("Legacy waist binding migrates to upper body"), Rig->GetBodyBone(TEXT("spine")), FName(TEXT("上半身")));
    TestEqual(TEXT("Legacy partial map retains chest default"), Rig->GetBodyBone(TEXT("chest")), FName(TEXT("上半身2")));
    TestTrue(TEXT("Legacy morph tuning survives body migration"), FMath::IsNearlyEqual(Rig->GetMappings()[1].Maximum, .42f));
    IFileManager::Get().Delete(*Path);
    Mesh->AnimScriptInstance = nullptr;
    return true;
}
#endif
