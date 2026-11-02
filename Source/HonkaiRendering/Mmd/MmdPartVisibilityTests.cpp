// SPDX-License-Identifier: MIT
#include "MmdPartVisibility.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ReferenceSkeleton.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include <limits>
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace HonkaiPartVisibilityTests
{
USkeletalMesh* Mesh(bool bReordered = false)
{
    const TArray<FVector3f> Positions{
        {0, 0, 0}, {1, 0, 0}, {0, 1, 0},
        {0, 1, 0}, {1, 0, 0}, {1, 1, 0},
        {10, 0, 0}, {11, 0, 0}, {10, 1, 0},
        {0, 0, 0}, {1, 0, 0}, {0, 1, 0}
    };
    auto* Skeleton = NewObject<USkeleton>();
    {
        FReferenceSkeletonModifier Modifier(Skeleton);
        Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
    }
    auto* Asset = NewObject<USkeletalMesh>();
    Asset->SetSkeleton(Skeleton);
    Asset->SetRefSkeleton(Skeleton->GetReferenceSkeleton());
    Asset->AddLODInfo();
    Asset->GetMaterials().SetNum(2);
    Asset->AllocateResourceForRendering();
    auto* LOD = new FSkeletalMeshLODRenderData;
    Asset->GetResourceForRendering()->LODRenderData.Add(LOD);
    LOD->StaticVertexBuffers.PositionVertexBuffer.Init(Positions.Num(), true);
    LOD->StaticVertexBuffers.StaticMeshVertexBuffer.Init(Positions.Num(), 1, true);
    TArray<FColor> Colors;
    Colors.SetNum(Positions.Num());
    for (int32 Old = 0; Old < Positions.Num(); ++Old)
    {
        const int32 Index = bReordered ? Positions.Num() - 1 - Old : Old;
        LOD->StaticVertexBuffers.PositionVertexBuffer.VertexPosition(Index) = Positions[Old];
        // The duplicate corners deliberately have different UVs, as imported seams do.
        LOD->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexUV(Index, 0, FVector2f(Old / 16.f, (Old % 3) / 4.f));
        Colors[Index] = FColor(10 + Old * 7, 40 + Old * 4, 90 + Old * 3, 120 + Old);
    }
    LOD->StaticVertexBuffers.ColorVertexBuffer.InitFromColorArray(Colors);
    TArray<uint32> Indices = bReordered
        ? TArray<uint32>{6, 7, 8, 4, 5, 3, 1, 2, 0, 11, 9, 10}
        : TArray<uint32>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    if (bReordered) for (uint32& Index : Indices) Index = Positions.Num() - 1 - Index;
    LOD->MultiSizeIndexContainer.RebuildIndexBuffer(sizeof(uint32), Indices);
    const uint32 Bases[]{0, bReordered ? 6u : 3u, 9};
    const uint32 Counts[]{bReordered ? 2u : 1u, bReordered ? 1u : 2u, 1};
    for (int32 Index = 0; Index < 3; ++Index)
    {
        auto& Section = LOD->RenderSections.AddDefaulted_GetRef();
        Section.MaterialIndex = Index == 2 ? 1 : 0;
        Section.BaseIndex = Bases[Index];
        Section.NumTriangles = Counts[Index];
        Section.BaseVertexIndex = 0;
        Section.NumVertices = Positions.Num();
    }
    return Asset;
}

TArray<FString> Ids(const FMmdPartVisibility& Visibility)
{
    TArray<FString> Result;
    for (const auto& Part : Visibility.Parts()) Result.Add(Part.Id);
    Result.Sort();
    return Result;
}

bool HasOverride(const USkeletalMeshComponent* Component)
{
    return Component->LODInfo.IsValidIndex(0) && Component->LODInfo[0].OverrideVertexColors != nullptr;
}

bool SameSet(const TSet<FString>& A, const TSet<FString>& B)
{
    if (A.Num() != B.Num()) return false;
    for (const FString& Id : A) if (!B.Contains(Id)) return false;
    return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdPartTopologyTest, "Honkai.Mmd.PartVisibility.Topology",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMmdPartTopologyTest::RunTest(const FString& Parameters)
{
    using namespace HonkaiPartVisibilityTests;
    FString Error;
    FMmdPartVisibility Visibility;
    if (!TestTrue(TEXT("Actual runtime render buffers build"), Visibility.Build(Mesh(), Error))) return false;
    TestEqual(TEXT("Seam corners weld within one material, while islands and other materials remain separate"), Visibility.Parts().Num(), 3);
    const FMmdVisiblePart* Joined = nullptr;
    for (const auto& Part : Visibility.Parts()) if (Part.TriangleCount == 2) Joined = &Part;
    if (!TestNotNull(TEXT("A connected two-triangle part exists"), Joined)) return false;
    TestEqual(TEXT("Both duplicated seam corners remain in the visibility mask"), Joined->VertexIndices.Num(), 6);
    TestEqual(TEXT("One same-material island can span multiple imported sections"), Joined->SectionIndices.Num(), 2);
    TestEqual(TEXT("The joined island preserves its material"), Joined->MaterialIndex, 0);
    TestTrue(TEXT("Part bounds describe the whole island"), Joined->Bounds.Min.Equals(FVector(0, 0, 0)) && Joined->Bounds.Max.Equals(FVector(1, 1, 0)));
    FMmdPartVisibility Reordered;
    if (!TestTrue(TEXT("Reordered render buffers build"), Reordered.Build(Mesh(true), Error))) return false;
    TestTrue(TEXT("Vertex, triangle and same-material section reordering preserve stable part IDs"), Ids(Visibility) == Ids(Reordered));
    TestEqual(TEXT("Topology signature survives harmless render ordering changes"), Visibility.TopologySignature(), Reordered.TopologySignature());
    auto* ChangedUV = Mesh();
    ChangedUV->GetResourceForRendering()->LODRenderData[0].StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexUV(0, 0, FVector2f(.9f, .8f));
    FMmdPartVisibility Changed;
    TestTrue(TEXT("A valid UV edit remains a valid mesh"), Changed.Build(ChangedUV, Error));
    TestNotEqual(TEXT("A real UV edit invalidates old component identities"), Changed.TopologySignature(), Visibility.TopologySignature());

    auto* Invalid = Mesh();
    auto& LOD = Invalid->GetResourceForRendering()->LODRenderData[0];
    TArray<uint32> Indices; LOD.MultiSizeIndexContainer.GetIndexBuffer(Indices);
    Indices[0] = 999;
    LOD.MultiSizeIndexContainer.RebuildIndexBuffer(sizeof(uint32), Indices);
    TestFalse(TEXT("An out-of-range vertex reference is rejected before indexing"), Reordered.Build(Invalid, Error));
    TestFalse(TEXT("Rejected geometry provides an actionable error"), Error.IsEmpty());
    Invalid = Mesh();
    Invalid->GetResourceForRendering()->LODRenderData[0].RenderSections[0].BaseIndex = 999;
    TestFalse(TEXT("An out-of-range section index span is rejected"), Reordered.Build(Invalid, Error));
    Invalid = Mesh();
    auto& SharedLOD = Invalid->GetResourceForRendering()->LODRenderData[0];
    SharedLOD.MultiSizeIndexContainer.GetIndexBuffer(Indices); Indices[9] = 0;
    SharedLOD.MultiSizeIndexContainer.RebuildIndexBuffer(sizeof(uint32), Indices);
    TestFalse(TEXT("A vertex index shared by different materials cannot silently share alpha"), Reordered.Build(Invalid, Error));
    Invalid = Mesh();
    Invalid->GetResourceForRendering()->LODRenderData[0].StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexUV(
        0, 0, FVector2f(std::numeric_limits<float>::quiet_NaN(), 0));
    TestFalse(TEXT("Non-finite UVs cannot enter stable IDs or reports"), Reordered.Build(Invalid, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdPartMaskTest, "Honkai.Mmd.PartVisibility.Mask",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMmdPartMaskTest::RunTest(const FString& Parameters)
{
    using namespace HonkaiPartVisibilityTests;
    auto* Asset = Mesh();
    auto* Main = NewObject<USkeletalMeshComponent>(); Main->SetSkeletalMeshAsset(Asset);
    auto* Outline = NewObject<USkeletalMeshComponent>(); Outline->SetSkeletalMeshAsset(Asset);
    FMmdPartVisibility Visibility; FString Error;
    if (!TestTrue(TEXT("Mask fixture builds"), Visibility.Build(Asset, Error))) return false;
    TestTrue(TEXT("Default has no hidden parts"), Visibility.HiddenIds.IsEmpty());
    TestTrue(TEXT("Default apply succeeds"), Visibility.Apply(Main, Outline, Error));
    TestFalse(TEXT("No selection does not allocate an unnecessary color override"), HasOverride(Main) || HasOverride(Outline));
    const FMmdVisiblePart* Hidden = nullptr;
    const FMmdVisiblePart* Solo = nullptr;
    for (const auto& Part : Visibility.Parts())
        if (Part.TriangleCount == 2) Hidden = &Part;
        else if (Part.MaterialIndex == 1) Solo = &Part;
    if (!TestNotNull(TEXT("Hidden island exists"), Hidden) || !TestNotNull(TEXT("Isolate island exists"), Solo)) return false;
    Visibility.HiddenIds.Add(Hidden->Id);
    if (!TestTrue(TEXT("One hidden island applies to both components"), Visibility.Apply(Main, Outline, Error))) return false;
    if (!TestTrue(TEXT("Main and outline receive real UE color buffers"), HasOverride(Main) && HasOverride(Outline))) return false;
    const auto CheckColors = [&](const USkeletalMeshComponent* Component, const TArray<int32>& VisibleOnly)
    {
        const auto& Buffer = *Component->LODInfo[0].OverrideVertexColors;
        const auto& Original = Asset->GetResourceForRendering()->LODRenderData[0].StaticVertexBuffers.ColorVertexBuffer;
        TestEqual(TEXT("Color mask covers every render vertex"), Buffer.GetNumVertices(), Original.GetNumVertices());
        for (uint32 Index = 0; Index < Buffer.GetNumVertices(); ++Index)
        {
            const FColor Color = Buffer.VertexColor(Index), Before = Original.VertexColor(Index);
            TestTrue(TEXT("Raw RGB survives the standard linear-color override API exactly"), Color.R == Before.R && Color.G == Before.G && Color.B == Before.B);
            const bool bVisible = VisibleOnly.IsEmpty() ? !Hidden->VertexIndices.Contains(Index) : VisibleOnly.Contains(Index);
            TestEqual(TEXT("Only the selected topology vertices change visibility"), int32(Color.A), bVisible ? 255 : 0);
        }
    };
    CheckColors(Main, {}); CheckColors(Outline, {});
    const auto SavedHidden = Visibility.HiddenIds;
    TestTrue(TEXT("Temporary isolate applies"), Visibility.Apply(Main, Outline, Error, Solo->Id));
    CheckColors(Main, Solo->VertexIndices); CheckColors(Outline, Solo->VertexIndices);
    TestTrue(TEXT("Isolate never rewrites the saved hidden selection"), SameSet(Visibility.HiddenIds, SavedHidden));
    Visibility.bEnabled = false;
    TestTrue(TEXT("The feature-wide switch disables all masking even during isolate"), Visibility.Apply(Main, Outline, Error, Solo->Id));
    TestFalse(TEXT("Feature off clears main and outline overrides"), HasOverride(Main) || HasOverride(Outline));
    Visibility.HiddenIds.Add(TEXT("unknown-stale-id"));
    TestTrue(TEXT("The recovery switch still restores when IDs have become invalid"), Visibility.Apply(Main, Outline, Error, TEXT("unknown-preview")));
    TestFalse(TEXT("Unknown stale selections cannot defeat the recovery switch"), HasOverride(Main) || HasOverride(Outline));
    Visibility.HiddenIds.Remove(TEXT("unknown-stale-id"));
    const auto& Original = Asset->GetResourceForRendering()->LODRenderData[0].StaticVertexBuffers.ColorVertexBuffer;
    TestEqual(TEXT("Restoring does not mutate the source alpha"), int32(Original.VertexColor(0).A), 120);
    TestTrue(TEXT("Disabling preserves choices for a later re-enable"), SameSet(Visibility.HiddenIds, SavedHidden));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdPartConfigTest, "Honkai.Mmd.PartVisibility.Config",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMmdPartConfigTest::RunTest(const FString& Parameters)
{
    using namespace HonkaiPartVisibilityTests;
    FMmdPartVisibility Visibility; FString Error;
    if (!TestTrue(TEXT("Config fixture builds"), Visibility.Build(Mesh(), Error))) return false;
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("Tests/PartVisibility") / FGuid::NewGuid().ToString(EGuidFormats::Digits);
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Path = Directory / TEXT("model.json");
    Visibility.HiddenIds.Add(Visibility.Parts()[0].Id); Visibility.bEnabled = false;
    if (!TestTrue(TEXT("Local model configuration saves"), Visibility.Save(Path, Error))) return false;
    FString Saved; FFileHelper::LoadFileToString(Saved, *Path);
    FMmdPartVisibility Loaded;
    if (!TestTrue(TEXT("A fresh matching model builds"), Loaded.Build(Mesh(true), Error))) return false;
    if (!TestTrue(TEXT("Matching topology loads despite render-order changes"), Loaded.Load(Path, Error))) return false;
    TestFalse(TEXT("User feature-wide disabled preference survives"), Loaded.bEnabled);
    TestTrue(TEXT("Hidden parts survive local save/load"), SameSet(Loaded.HiddenIds, Visibility.HiddenIds));
    const auto PreviousHidden = Loaded.HiddenIds;
    const FString WrongSignature = FString::ChrN(Loaded.TopologySignature().Len(), TEXT('z'));
    FFileHelper::SaveStringToFile(Saved.Replace(*Loaded.TopologySignature(), *WrongSignature), *Path);
    TestFalse(TEXT("Configuration for other topology is rejected"), Loaded.Load(Path, Error));
    TestTrue(TEXT("Topology mismatch preserves the existing selection"), SameSet(Loaded.HiddenIds, PreviousHidden));
    TestFalse(TEXT("Topology mismatch preserves the feature switch"), Loaded.bEnabled);
    FFileHelper::SaveStringToFile(TEXT("{broken"), *Path);
    TestFalse(TEXT("Malformed configuration is rejected"), Loaded.Load(Path, Error));
    TestTrue(TEXT("Parse failure preserves all old selections"), SameSet(Loaded.HiddenIds, PreviousHidden));
    TestFalse(TEXT("Parse failure preserves the feature switch"), Loaded.bEnabled);
    FFileHelper::SaveStringToFile(Saved, *Path);
#if PLATFORM_WINDOWS
    HANDLE Locked = ::CreateFileW(*FPaths::ConvertRelativePathToFull(Path), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (TestTrue(TEXT("The old configuration is opened without delete sharing"), Locked != INVALID_HANDLE_VALUE))
    {
        Visibility.bEnabled = true;
        TestFalse(TEXT("An atomic replace failure is reported"), Visibility.Save(Path, Error));
        FString StillSaved; FFileHelper::LoadFileToString(StillSaved, *Path);
        TestEqual(TEXT("A failed replace leaves the old configuration byte-for-byte intact"), StillSaved, Saved);
        ::CloseHandle(Locked);
    }
#endif
    TestTrue(TEXT("A source-only diagnostic report writes"), Visibility.WriteReport(Directory / TEXT("parts.json"), Error));
    IFileManager::Get().DeleteDirectory(*Directory, false, true);
    return true;
}
#endif
