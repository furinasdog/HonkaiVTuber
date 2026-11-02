#include "MmdPartVisibility.h"

#include "Algo/Unique.h"
#include "Components/SkinnedMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#else
#include <cstdio>
#endif

namespace
{
    uint32 PartFloatBits(float Value)
    {
        uint32 Bits = 0;
        if (Value != 0.f) FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
        return Bits;
    }

    struct FPartWeldKey
    {
        uint32 X, Y, Z;
        int32 Material;
        bool operator==(const FPartWeldKey& B) const { return X == B.X && Y == B.Y && Z == B.Z && Material == B.Material; }
        friend uint32 GetTypeHash(const FPartWeldKey& K)
        { return HashCombine(HashCombine(K.X, K.Y), HashCombine(K.Z, ::GetTypeHash(K.Material))); }
    };

    struct FPartVertexKey
    {
        uint32 Values[5];
        bool operator<(const FPartVertexKey& B) const
        {
            for (int32 I = 0; I < 5; ++I) if (Values[I] != B.Values[I]) return Values[I] < B.Values[I];
            return false;
        }
    };

    struct FPartTriangle
    {
        int32 Vertices[3];
        int32 Section, Material;
        FPartVertexKey Keys[3];
        bool operator<(const FPartTriangle& B) const
        {
            for (int32 I = 0; I < 3; ++I)
            {
                if (Keys[I] < B.Keys[I]) return true;
                if (B.Keys[I] < Keys[I]) return false;
            }
            return false;
        }
    };

    FString PartHash(FMD5& Hash)
    {
        uint8 Digest[16]; Hash.Final(Digest);
        return BytesToHex(Digest, UE_ARRAY_COUNT(Digest)).ToLower();
    }

    void PartHashString(FMD5& Hash, const FString& Text)
    {
        FTCHARToUTF8 Bytes(*Text);
        Hash.Update(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length());
    }

    bool SavePartJson(const FString& Path, const TSharedRef<FJsonObject>& Root, FString& Error)
    {
        FString Text;
        if (!FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text)))
        { Error = TEXT("部件配置序列化失败。"); return false; }
        const FString Full = FPaths::ConvertRelativePathToFull(Path);
        if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Full), true))
        { Error = TEXT("无法创建部件配置目录，原文件未更改。"); return false; }
        const FString Temp = Full + TEXT(".") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".tmp");
        if (!FFileHelper::SaveStringToFile(Text, *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        {
            IFileManager::Get().Delete(*Temp, false, true, true);
            Error = TEXT("无法写入部件配置临时文件，原文件未更改。"); return false;
        }
#if PLATFORM_WINDOWS
        const bool bSaved = ::MoveFileExW(*Temp, *Full, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        const bool bSaved = std::rename(TCHAR_TO_UTF8(*Temp), TCHAR_TO_UTF8(*Full)) == 0;
#endif
        if (!bSaved)
        {
            IFileManager::Get().Delete(*Temp, false, true, true);
            Error = TEXT("替换部件配置失败，原文件未更改。"); return false;
        }
        return true;
    }

    TArray<TSharedPtr<FJsonValue>> PartVector(FVector V)
    { return { MakeShared<FJsonValueNumber>(V.X), MakeShared<FJsonValueNumber>(V.Y), MakeShared<FJsonValueNumber>(V.Z) }; }
}

bool FMmdPartVisibility::Build(USkeletalMesh* Mesh, FString& Error)
{
    Error.Reset();
    const auto Fail = [&Error](const TCHAR* Message) { Error = Message; return false; };
    const FSkeletalMeshRenderData* Render = Mesh ? Mesh->GetResourceForRendering() : nullptr;
    if (!Render || Render->LODRenderData.IsEmpty()) return Fail(TEXT("模型没有可读的 LOD0 渲染数据。"));
    const auto& LOD = Render->LODRenderData[0];
    const auto& P = LOD.StaticVertexBuffers.PositionVertexBuffer;
    const auto& T = LOD.StaticVertexBuffers.StaticMeshVertexBuffer;
    const auto& C = LOD.StaticVertexBuffers.ColorVertexBuffer;
    const int32 Count = P.GetNumVertices();
    const auto* IndexBuffer = LOD.MultiSizeIndexContainer.IsIndexBufferValid() ? LOD.MultiSizeIndexContainer.GetIndexBuffer() : nullptr;
    if (Count <= 0 || Count > 4000000 || !P.GetVertexData() || !IndexBuffer || !IndexBuffer->Num()
        || T.GetNumVertices() != uint32(Count) || !T.GetNumTexCoords() || !T.GetTexCoordData())
        return Fail(TEXT("模型顶点、UV 或索引数据不可读。"));
    if (uint64(IndexBuffer->GetResourceDataSize()) < uint64(IndexBuffer->Num()) * LOD.MultiSizeIndexContainer.GetDataTypeSize())
        return Fail(TEXT("模型的 CPU 索引数据已释放，无法建立部件。"));
    if (C.GetNumVertices() && (C.GetNumVertices() != uint32(Count) || !C.GetVertexData()))
        return Fail(TEXT("模型原顶点颜色数据不可读。"));
    FMmdPartVisibility Next;
    Next.SourceMesh = Mesh;
    Next.Positions.Reserve(Count); Next.UVs.Reserve(Count); Next.OriginalColors.Reserve(Count);
    for (int32 I = 0; I < Count; ++I)
    {
        const FVector3f Position = P.VertexPosition(I);
        const FVector2f UV = T.GetVertexUV(I, 0);
        if (Position.ContainsNaN() || !FMath::IsFinite(UV.X) || !FMath::IsFinite(UV.Y))
            return Fail(TEXT("模型含非有限顶点或 UV。"));
        Next.Positions.Add(Position); Next.UVs.Add(UV);
        Next.OriginalColors.Add(C.GetNumVertices() ? C.VertexColor(I) : FColor::White);
    }
    TArray<int32> Parent, Materials;
    Parent.SetNumUninitialized(Count); Materials.Init(INDEX_NONE, Count);
    for (int32 I = 0; I < Count; ++I) Parent[I] = I;
    const auto Find = [&Parent](int32 I) { while (Parent[I] != I) { Parent[I] = Parent[Parent[I]]; I = Parent[I]; } return I; };
    const auto Union = [&Parent, &Find](int32 A, int32 B) { A = Find(A); B = Find(B); if (A != B) Parent[FMath::Max(A, B)] = FMath::Min(A, B); };
    TMap<FPartWeldKey, int32> Welds;
    TArray<FPartTriangle> Triangles;
    for (int32 S = 0; S < LOD.RenderSections.Num(); ++S)
    {
        const auto& Section = LOD.RenderSections[S];
        const uint64 End = uint64(Section.BaseIndex) + uint64(Section.NumTriangles) * 3;
        if (End > uint64(IndexBuffer->Num()) || End > 12000000)
            return Fail(TEXT("模型部件的三角形范围无效。"));
        for (uint32 I = 0; I < Section.NumTriangles; ++I)
        {
            FPartTriangle Triangle; Triangle.Section = S; Triangle.Material = Section.MaterialIndex;
            for (int32 J = 0; J < 3; ++J)
            {
                const uint32 Vertex = IndexBuffer->Get(Section.BaseIndex + I * 3 + J);
                if (Vertex >= uint32(Count)) return Fail(TEXT("模型部件引用了不存在的顶点。"));
                if (Materials[Vertex] != INDEX_NONE && Materials[Vertex] != Triangle.Material)
                    return Fail(TEXT("不同材质共享同一渲染顶点，无法安全独立遮罩。"));
                Materials[Vertex] = Triangle.Material;
                Triangle.Vertices[J] = int32(Vertex);
                const auto& Position = Next.Positions[Vertex]; const auto& UV = Next.UVs[Vertex];
                FPartVertexKey& Key = Triangle.Keys[J];
                Key = {{ PartFloatBits(Position.X), PartFloatBits(Position.Y), PartFloatBits(Position.Z), PartFloatBits(UV.X), PartFloatBits(UV.Y) }};
                const FPartWeldKey Weld{ Key.Values[0], Key.Values[1], Key.Values[2], Triangle.Material };
                if (const int32* Existing = Welds.Find(Weld)) Union(int32(Vertex), *Existing);
                else Welds.Add(Weld, int32(Vertex));
            }
            Union(Triangle.Vertices[0], Triangle.Vertices[1]); Union(Triangle.Vertices[0], Triangle.Vertices[2]);
            // Attribute ordering makes identity independent of triangle/index-buffer order.
            for (int32 A = 0; A < 2; ++A) for (int32 B = A + 1; B < 3; ++B)
                if (Triangle.Keys[B] < Triangle.Keys[A]) Swap(Triangle.Keys[A], Triangle.Keys[B]);
            Triangles.Add(Triangle);
        }
    }
    if (Triangles.IsEmpty()) return Fail(TEXT("模型没有可显示的三角形部件。"));
    TMap<int32, TArray<FPartTriangle>> Islands;
    for (const auto& Triangle : Triangles) Islands.FindOrAdd(Find(Triangle.Vertices[0])).Add(Triangle);
    for (auto& Entry : Islands)
    {
        Entry.Value.Sort();
        FMmdVisiblePart Part;
        Part.MaterialIndex = Entry.Value[0].Material; Part.TriangleCount = Entry.Value.Num();
        FMD5 Hash;
        PartHashString(Hash, TEXT("mmd-part-v1:"));
        Hash.Update(reinterpret_cast<const uint8*>(&Part.MaterialIndex), sizeof(Part.MaterialIndex));
        for (const auto& Triangle : Entry.Value)
        {
            Hash.Update(reinterpret_cast<const uint8*>(Triangle.Keys), sizeof(Triangle.Keys));
            Part.SectionIndices.AddUnique(Triangle.Section);
            for (int32 Vertex : Triangle.Vertices) Part.VertexIndices.Add(Vertex);
        }
        Part.VertexIndices.Sort();
        Part.VertexIndices.SetNum(Algo::Unique(Part.VertexIndices), EAllowShrinking::No);
        Part.SectionIndices.Sort(); Part.SectionIndex = Part.SectionIndices[0];
        for (int32 Vertex : Part.VertexIndices) Part.Bounds += FVector(Next.Positions[Vertex]);
        Part.Id = TEXT("part-") + PartHash(Hash);
        Next.ModelParts.Add(MoveTemp(Part));
    }
    Next.ModelParts.Sort([](const FMmdVisiblePart& A, const FMmdVisiblePart& B) { return A.Id < B.Id; });
    Next.VertexParts.Init(INDEX_NONE, Count);
    FMD5 Topology;
    PartHashString(Topology, TEXT("mmd-topology-v1:"));
    for (int32 I = 0; I < Next.ModelParts.Num(); ++I)
    {
        PartHashString(Topology, Next.ModelParts[I].Id);
        for (int32 Vertex : Next.ModelParts[I].VertexIndices) Next.VertexParts[Vertex] = I;
    }
    Next.Signature = PartHash(Topology);
    *this = MoveTemp(Next);
    return true;
}

bool FMmdPartVisibility::Apply(USkinnedMeshComponent* Main, USkinnedMeshComponent* Outline, FString& Error, const FString& IsolatePart)
{
    Error.Reset();
    if (!SourceMesh.IsValid() || !Main || Main->GetSkinnedAsset() != SourceMesh.Get()
        || (Outline && Outline->GetSkinnedAsset() != SourceMesh.Get()))
    { Error = TEXT("部件遮罩的组件与模型不匹配。"); return false; }
    TSet<FString> Known;
    for (const auto& Part : ModelParts) Known.Add(Part.Id);
    for (const FString& Id : HiddenIds) if (bEnabled && !Known.Contains(Id))
    { Error = TEXT("隐藏列表包含未知部件，未更新遮罩。"); return false; }
    if (bEnabled && !IsolatePart.IsEmpty() && !Known.Contains(IsolatePart))
    { Error = TEXT("预览部件不存在，未更新遮罩。"); return false; }
    TArray<FString> Ordered = HiddenIds.Array(); Ordered.Sort();
    FString Key = bEnabled ? TEXT("1:") : TEXT("0:");
    for (const auto& Id : Ordered) Key += Id + TEXT(";");
    Key += TEXT("/") + IsolatePart;
    if (bHasApplied && AppliedMain.Get() == Main && AppliedOutline.Get() == Outline && AppliedKey == Key) return true;
    if (AppliedMain.IsValid() && AppliedMain.Get() != Main) AppliedMain->ClearVertexColorOverride(0);
    if (AppliedOutline.IsValid() && AppliedOutline.Get() != Outline) AppliedOutline->ClearVertexColorOverride(0);
    const bool bMask = bEnabled && (!HiddenIds.IsEmpty() || !IsolatePart.IsEmpty());
    if (!bMask)
    {
        Main->ClearVertexColorOverride(0);
        if (Outline && Outline != Main) Outline->ClearVertexColorOverride(0);
    }
    else
    {
        TArray<FLinearColor> Colors; Colors.Reserve(OriginalColors.Num());
        for (int32 I = 0; I < OriginalColors.Num(); ++I)
        {
            const int32 Part = VertexParts[I];
            const bool bVisible = Part == INDEX_NONE || (!IsolatePart.IsEmpty()
                ? ModelParts[Part].Id == IsolatePart : !HiddenIds.Contains(ModelParts[Part].Id));
            FColor Color = OriginalColors[I]; Color.A = bVisible ? 255 : 0;
            // The engine packs override floats without sRGB conversion.
            Colors.Add(Color.ReinterpretAsLinear());
        }
        Main->SetVertexColorOverride_LinearColor(0, Colors);
        if (Outline && Outline != Main) Outline->SetVertexColorOverride_LinearColor(0, Colors);
    }
    AppliedMain = Main; AppliedOutline = Outline; AppliedKey = MoveTemp(Key); bHasApplied = true;
    return true;
}

bool FMmdPartVisibility::Load(const FString& Path, FString& Error)
{
    Error.Reset();
    const auto Fail = [&Error] { Error = TEXT("部件配置无效或不匹配当前拓扑，保留当前设置。"); return false; };
    if (Signature.IsEmpty() || IFileManager::Get().FileSize(*Path) < 0 || IFileManager::Get().FileSize(*Path) > 1024 * 1024) return Fail();
    FString Text, LoadedSignature; TSharedPtr<FJsonObject> Root;
    if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root) return Fail();
    double Version; bool Enabled;
    const TArray<TSharedPtr<FJsonValue>>* Hidden;
    if (!Root->TryGetNumberField(TEXT("version"), Version) || Version != 1
        || !Root->TryGetStringField(TEXT("topology"), LoadedSignature) || LoadedSignature != Signature
        || !Root->TryGetBoolField(TEXT("enabled"), Enabled) || !Root->TryGetArrayField(TEXT("hidden"), Hidden)
        || Hidden->Num() > ModelParts.Num()) return Fail();
    TSet<FString> Known, NewHidden;
    for (const auto& Part : ModelParts) Known.Add(Part.Id);
    for (const auto& Item : *Hidden)
    {
        FString Id;
        if (!Item.IsValid() || !Item->TryGetString(Id) || !Known.Contains(Id) || NewHidden.Contains(Id)) return Fail();
        NewHidden.Add(MoveTemp(Id));
    }
    bEnabled = Enabled; HiddenIds = MoveTemp(NewHidden);
    return true;
}

bool FMmdPartVisibility::Save(const FString& Path, FString& Error) const
{
    Error.Reset();
    if (Signature.IsEmpty()) { Error = TEXT("尚未建立部件拓扑，无法保存。"); return false; }
    TSet<FString> Known; for (const auto& Part : ModelParts) Known.Add(Part.Id);
    TArray<FString> Ordered = HiddenIds.Array(); Ordered.Sort();
    TArray<TSharedPtr<FJsonValue>> Hidden;
    for (const auto& Id : Ordered)
    {
        if (!Known.Contains(Id)) { Error = TEXT("隐藏列表含未知部件，原文件未更改。"); return false; }
        Hidden.Add(MakeShared<FJsonValueString>(Id));
    }
    const auto Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("version"), 1); Root->SetStringField(TEXT("topology"), Signature);
    Root->SetBoolField(TEXT("enabled"), bEnabled); Root->SetArrayField(TEXT("hidden"), Hidden);
    return SavePartJson(Path, Root, Error);
}

bool FMmdPartVisibility::WriteReport(const FString& Path, FString& Error) const
{
    Error.Reset();
    if (Signature.IsEmpty()) { Error = TEXT("尚未建立部件拓扑。"); return false; }
    const auto Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("version"), 1); Root->SetStringField(TEXT("topology"), Signature);
    Root->SetNumberField(TEXT("vertex_count"), Positions.Num());
    int32 NonWhiteAlpha = 0;
    for (const FColor& Color : OriginalColors) NonWhiteAlpha += Color.A != 255;
    Root->SetNumberField(TEXT("original_nonwhite_alpha_vertices"), NonWhiteAlpha);
    TArray<TSharedPtr<FJsonValue>> PartsJson;
    for (const auto& Part : ModelParts)
    {
        const auto P = MakeShared<FJsonObject>();
        P->SetStringField(TEXT("id"), Part.Id); P->SetNumberField(TEXT("section"), Part.SectionIndex);
        P->SetNumberField(TEXT("material"), Part.MaterialIndex); P->SetNumberField(TEXT("triangles"), Part.TriangleCount);
        P->SetArrayField(TEXT("bounds_min"), PartVector(Part.Bounds.Min)); P->SetArrayField(TEXT("bounds_max"), PartVector(Part.Bounds.Max));
        TArray<TSharedPtr<FJsonValue>> Sections, Vertices;
        for (int32 S : Part.SectionIndices) Sections.Add(MakeShared<FJsonValueNumber>(S));
        for (int32 I : Part.VertexIndices)
        {
            const auto V = MakeShared<FJsonObject>(); V->SetNumberField(TEXT("index"), I);
            V->SetArrayField(TEXT("position"), PartVector(FVector(Positions[I])));
            V->SetArrayField(TEXT("uv"), { MakeShared<FJsonValueNumber>(UVs[I].X), MakeShared<FJsonValueNumber>(UVs[I].Y) });
            Vertices.Add(MakeShared<FJsonValueObject>(V));
        }
        P->SetArrayField(TEXT("sections"), Sections); P->SetArrayField(TEXT("vertices"), Vertices);
        PartsJson.Add(MakeShared<FJsonValueObject>(P));
    }
    Root->SetArrayField(TEXT("parts"), PartsJson);
    return SavePartJson(Path, Root, Error);
}
