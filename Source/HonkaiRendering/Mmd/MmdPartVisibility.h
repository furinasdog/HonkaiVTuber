#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class USkinnedMeshComponent;

struct FMmdVisiblePart
{
    FString Id;
    int32 SectionIndex = INDEX_NONE;
    TArray<int32> SectionIndices;
    int32 MaterialIndex = INDEX_NONE;
    TArray<int32> VertexIndices;
    int32 TriangleCount = 0;
    FBox Bounds = FBox(ForceInit);
};

// Connected triangle islands from the imported render mesh, with local-only choices.
class FMmdPartVisibility
{
public:
    bool Build(USkeletalMesh* Mesh, FString& Error);
    const TArray<FMmdVisiblePart>& Parts() const { return ModelParts; }
    const FString& TopologySignature() const { return Signature; }

    bool bEnabled = true;
    TSet<FString> HiddenIds;

    bool Apply(USkinnedMeshComponent* Main, USkinnedMeshComponent* Outline, FString& Error,
        const FString& IsolatePart = FString());
    bool Load(const FString& Path, FString& Error);
    bool Save(const FString& Path, FString& Error) const;
    bool WriteReport(const FString& Path, FString& Error) const;

private:
    TWeakObjectPtr<USkeletalMesh> SourceMesh;
    TArray<FMmdVisiblePart> ModelParts;
    TArray<FVector3f> Positions;
    TArray<FVector2f> UVs;
    TArray<FColor> OriginalColors;
    TArray<int32> VertexParts;
    FString Signature;
    TWeakObjectPtr<USkinnedMeshComponent> AppliedMain, AppliedOutline;
    FString AppliedKey;
    bool bHasApplied = false;
};
