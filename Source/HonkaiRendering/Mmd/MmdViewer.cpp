#include "MmdViewer.h"
#include "HonkaiRenderInstance.h"
#include "MmdUiStyle.h"
#include "MmdNotices.h"
#include "Misc/App.h"
#include "Async/Async.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/EngineVersion.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"
#include "ImageUtils.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "Misc/Base64.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "LoaderBPFunctionLibrary.h"
#include "VrmAssetListObject.h"
#include "VrmPmxPhysicsData.h"
#include "VrmPoseableMeshComponent.h"
#include "Vtuber/VtuberOutput.h"
#include "Vtuber/SVtuberRigPanel.h"
#include "Vtuber/SVtuberCaptureResults.h"
#include "Vtuber/SVtuberCalibrationWizard.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SCanvas.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SOverlay.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Skeleton.h"
#include "ReferenceSkeleton.h"
#include "Animation/MorphTarget.h"
#include "Layout/ArrangedChildren.h"
#endif
#include "Input/DragAndDrop.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Brushes/SlateColorBrush.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"
#include "UObject/ConstructorHelpers.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
THIRD_PARTY_INCLUDES_START
#include <shobjidl.h>
THIRD_PARTY_INCLUDES_END
#endif

#if __has_include("HonkaiBuildStamp.h")
#include "HonkaiBuildStamp.h"
#endif

namespace
{
    const FLinearColor Panel = FMmdUiStyle::Colors().Panel, Text = FMmdUiStyle::Colors().Text,
        Muted = FMmdUiStyle::Colors().Muted;

    FVector2D MmdFitPanelSize(FVector2D Preferred, FVector2D Viewport)
    {
        if (Viewport.X <= 0 || Viewport.Y <= 0) return Preferred;
        return FVector2D(FMath::Min(Preferred.X, FMath::Max(1.0, Viewport.X - 24.0)),
            FMath::Min(Preferred.Y, FMath::Max(1.0, Viewport.Y - 24.0)));
    }

    FVector2D MmdClampPanelPosition(FVector2D Position, FVector2D Size, FVector2D Viewport)
    {
        if (Viewport.X <= 0 || Viewport.Y <= 0) return Position;
        return FVector2D(FMath::Clamp(Position.X, 0.0, FMath::Max(0.0, Viewport.X - Size.X)),
            FMath::Clamp(Position.Y, 0.0, FMath::Max(0.0, Viewport.Y - Size.Y)));
    }

    float OutlineRegion(const FString& MaterialName)
    {
        const FString Name = MaterialName.ToLower();
        for (const TCHAR* Word : {TEXT("hair"), TEXT("髪"), TEXT("髮"), TEXT("头发")})
            if (Name.Contains(Word)) return 2.f;
        // Material vocabulary is shared across models; clothing takes precedence over body-part words.
        for (const TCHAR* Word : {TEXT("glove"), TEXT("sleeve"), TEXT("accessory"), TEXT("sock"), TEXT("shoe"),
            TEXT("衣"), TEXT("袖"), TEXT("饰"), TEXT("飾"), TEXT("套"), TEXT("环"), TEXT("環"), TEXT("袋"), TEXT("靴"), TEXT("袜")})
            if (Name.Contains(Word)) return 0.f;
        for (const TCHAR* Word : {TEXT("skin"), TEXT("face"), TEXT("neck"), TEXT("hand"), TEXT("肌"), TEXT("皮肤"),
            TEXT("顔"), TEXT("脸"), TEXT("脖"), TEXT("首"), TEXT("手"), TEXT("腿"), TEXT("足")})
            if (Name.Contains(Word)) return 1.f;
        return 0.f;
    }

    bool SafeFaceTextureName(const FString& Name)
    {
        return !Name.IsEmpty() && Name == FPaths::GetCleanFilename(Name)
            && !Name.Contains(TEXT(":")) && !Name.Contains(TEXT(".."))
            && FPaths::GetExtension(Name).Equals(TEXT("png"), ESearchCase::IgnoreCase);
    }

#if WITH_DEV_AUTOMATION_TESTS
    bool LoadTrackingPreviewFixture(FNativeTrackingPreview& Out)
    {
        FString ImagePath, DataPath;
        if (!FParse::Value(FCommandLine::Get(), TEXT("VtuberPreviewImage="), ImagePath)
            || !FParse::Value(FCommandLine::Get(), TEXT("VtuberPreviewData="), DataPath)) return false;
        FVtuberTrackingSnapshot Snapshot;
        if (!FNativeTracking::LoadReplayFrame(DataPath, false, Snapshot)) return false;
        TArray<uint8> Bytes;
        if (IFileManager::Get().FileSize(*ImagePath) > 16 * 1024 * 1024 || !FFileHelper::LoadFileToArray(Bytes, *ImagePath)) return false;
        auto& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
        auto Decoder = Module.CreateImageWrapper(Module.DetectImageFormat(Bytes.GetData(), Bytes.Num()));
        if (!Decoder || !Decoder->SetCompressed(Bytes.GetData(), Bytes.Num())) return false;
        const int32 W = Decoder->GetWidth(), H = Decoder->GetHeight();
        if (W <= 0 || H <= 0 || W > 4096 || H > 4096) return false;
        TArray64<uint8> Raw;
        if (!Decoder->GetRaw(ERGBFormat::BGRA, 8, Raw) || Raw.Num() != int64(W) * H * 4) return false;
        const float Scale = FMath::Min(320.f / W, 240.f / H);
        Out.Width = FMath::Max(1, FMath::FloorToInt(W * Scale)); Out.Height = FMath::Max(1, FMath::FloorToInt(H * Scale));
        TArray<FColor> Source, Resized;
        Source.SetNumUninitialized(W * H); FMemory::Memcpy(Source.GetData(), Raw.GetData(), Raw.Num());
        FImageUtils::ImageResize(W, H, Source, Out.Width, Out.Height, Resized, false);
        Out.BGRA.SetNumUninitialized(Resized.Num() * 4); FMemory::Memcpy(Out.BGRA.GetData(), Resized.GetData(), Out.BGRA.Num());
        Out.Sequence = 1; Out.TimestampSeconds = FPlatformTime::Seconds(); Out.Tracking = MoveTemp(Snapshot);
        Out.Status = TEXT("离线验证样本，摄像头未启动");
        return Out.HasImage();
    }
#endif

    TMap<int32, UTexture2D*> LoadFaceMaps(const FString& ModelPath, UVrmAssetListObject* Asset, const FString& ExplicitProfile = FString())
    {
        TMap<int32, UTexture2D*> Result;
        TArray<uint8> ModelBytes;
        if (!FFileHelper::LoadFileToArray(ModelBytes, *ModelPath)) return Result;
        FSHAHash Hash;
        FSHA1::HashBuffer(ModelBytes.GetData(), ModelBytes.Num(), Hash.Hash);
        const FString SourceHash = Hash.ToString().ToLower();
        // Portable sidecars take precedence over the local authoring cache.
        FString ProfilePath = ExplicitProfile.IsEmpty() ? ModelPath + TEXT(".toon.json") : ExplicitProfile;
        if (ExplicitProfile.IsEmpty() && !FPaths::FileExists(ProfilePath))
        {
            const FString Legacy = FPaths::ProjectSavedDir() / TEXT("MmdShading") / SourceHash;
            const FString Migrated = FMmdEnhancement::UserDataDir() / TEXT("LegacyShading") / SourceHash;
            // Preserve earlier authoring results while keeping new runtime data outside the checkout.
            if (!FPaths::FileExists(Migrated / TEXT("profile.json")) && FPaths::FileExists(Legacy / TEXT("profile.json")))
            {
                IFileManager::Get().MakeDirectory(*Migrated, true);
                for (const TCHAR* File : {TEXT("face.png"), TEXT("coordinates.png"), TEXT("profile.json")})
                    IFileManager::Get().Copy(*(Migrated / File), *(Legacy / File), false);
            }
            ProfilePath = Migrated / TEXT("profile.json");
        }
        const int64 ProfileSize = IFileManager::Get().FileSize(*ProfilePath);
        if (ProfileSize <= 0 || ProfileSize > 65536) return Result;
        FString TextValue;
        TSharedPtr<FJsonObject> Profile;
        if (!FFileHelper::LoadFileToString(TextValue, *ProfilePath)
            || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TextValue), Profile) || !Profile) return Result;
        FString DeclaredHash;
        double Version = 0;
        const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
        if (!Profile->TryGetNumberField(TEXT("version"), Version) || Version != 1
            || !Profile->TryGetStringField(TEXT("source_sha1"), DeclaredHash) || DeclaredHash != SourceHash
            || !Profile->TryGetArrayField(TEXT("materials"), Entries) || Entries->Num() > Asset->SkeletalMesh->GetMaterials().Num())
        {
            UE_LOG(LogTemp, Warning, TEXT("MMD_FACE_PROFILE rejected: incompatible profile %s"), *ProfilePath);
            return Result;
        }
        for (const auto& Entry : *Entries)
        {
            const TSharedPtr<FJsonObject>* Object = nullptr;
            if (!Entry->TryGetObject(Object) || !Object || !Object->IsValid()) continue;
            double Index = -1;
            FString Name, TextureName;
            if (!(*Object)->TryGetNumberField(TEXT("index"), Index) || Index < 0
                || Index >= Asset->SkeletalMesh->GetMaterials().Num() || Index != FMath::FloorToDouble(Index)
                || !(*Object)->TryGetStringField(TEXT("name"), Name)
                || Name != Asset->SkeletalMesh->GetMaterials()[int32(Index)].MaterialSlotName.ToString()
                || !(*Object)->TryGetStringField(TEXT("texture"), TextureName) || !SafeFaceTextureName(TextureName)) continue;
            const FString TexturePath = FPaths::GetPath(ProfilePath) / TextureName;
            const int64 Bytes = IFileManager::Get().FileSize(*TexturePath);
            if (Bytes <= 0 || Bytes > 4 * 1024 * 1024) continue;
            TArray<uint8> Buffer;
            if (!FFileHelper::LoadFileToArray(Buffer, *TexturePath)) continue;
            auto Decoder = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper")).CreateImageWrapper(EImageFormat::PNG);
            if (!Decoder->SetCompressed(Buffer.GetData(), Buffer.Num()) || Decoder->GetWidth() > 2048 || Decoder->GetHeight() > 2048) continue;
            if (auto* Texture = FImageUtils::ImportBufferAsTexture2D(Buffer))
            {
                Texture->SRGB = false;
                Texture->AddressX = Texture->AddressY = TA_Clamp;
                Texture->UpdateResource();
                if (ExplicitProfile.IsEmpty()) Asset->Textures.Add(Texture);
                Result.Add(int32(Index), Texture);
            }
        }
        UE_LOG(LogTemp, Display, TEXT("MMD_FACE_PROFILE materials=%d source=%s"), Result.Num(), *SourceHash);
        return Result;
    }
	TSharedRef<STextBlock> Label(const FString& Value, int32 Size = 12, FLinearColor Color = Text)
	{
		return SNew(STextBlock).Text(FText::FromString(Value)).Font(FCoreStyle::GetDefaultFontStyle("Regular", Size)).ColorAndOpacity(Color).AutoWrapText(true);
	}

}

struct FMmdMenuSetup
{
    FString Title;
    TFunction<void()> Escape, ToggleHud;
    TFunction<void(FVector2D)> Move;
    TFunction<void()> Collapse;
    TSharedRef<SWidget> Content;
};

class SMmdMenu : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SMmdMenu) {} SLATE_END_ARGS()
    TFunction<void()> Escape;
    TFunction<void()> ToggleHud;
    TFunction<void(FVector2D)> Move;
    TFunction<void()> Collapse;
    FButtonStyle HeaderButtonStyle;
    bool bCollapsed = false, bMoving = false;
    void Construct(const FArguments&, FMmdMenuSetup In)
    {
        Escape = MoveTemp(In.Escape);
        ToggleHud = MoveTemp(In.ToggleHud);
        Move = MoveTemp(In.Move);
        Collapse = MoveTemp(In.Collapse);
        HeaderButtonStyle = FMmdUiStyle::Get().Button;
        HeaderButtonStyle.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
        TSharedRef<SHorizontalBox> Header = SNew(SHorizontalBox);
        Header->AddSlot().FillWidth(1).VAlign(VAlign_Center).Padding(0, 0, 10, 0)
            [SNew(STextBlock).Text(FText::FromString(In.Title)).ColorAndOpacity(Text)
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 13)).AutoWrapText(false).OverflowPolicy(ETextOverflowPolicy::Ellipsis)];
        if (Collapse)
            Header->AddSlot().AutoWidth().VAlign(VAlign_Center)
                [SNew(SBox).WidthOverride(56).HeightOverride(30)
                    [SNew(SButton).ButtonStyle(&HeaderButtonStyle).ContentPadding(0).HAlign(HAlign_Center).VAlign(VAlign_Center)
                        .OnClicked_Lambda([this] { bCollapsed = !bCollapsed; Collapse(); return FReply::Handled(); })
                        [SNew(STextBlock).Text_Lambda([this] { return FText::FromString(bCollapsed ? TEXT("展开") : TEXT("收起")); })
                            .ColorAndOpacity(FSlateColor::UseForeground()).Font(FCoreStyle::GetDefaultFontStyle("Regular", 12)).AutoWrapText(false)]]];
        Header->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
            [SNew(SBox).WidthOverride(56).HeightOverride(30)
                [SNew(SButton).ButtonStyle(&HeaderButtonStyle).ContentPadding(0).HAlign(HAlign_Center).VAlign(VAlign_Center)
                    .OnClicked_Lambda([this] { Escape(); return FReply::Handled(); })
                    [SNew(STextBlock).Text(FText::FromString(TEXT("关闭"))).ColorAndOpacity(FSlateColor::UseForeground())
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12)).AutoWrapText(false)]]];
        ChildSlot[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Panel).Padding(0)
            .Clipping(EWidgetClipping::ClipToBounds)
            [SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                    [SNew(SBox).HeightOverride(44)
                        [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                            .BorderBackgroundColor(FMmdUiStyle::Colors().Header).Padding(FMargin(12, 0))[Header]]]
                + SVerticalBox::Slot().FillHeight(1)
                    [SNew(SBox).Visibility_Lambda([this] { return bCollapsed ? EVisibility::Collapsed : EVisibility::Visible; })[In.Content]]]];
    }
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent& Event) override
    {
        if (Event.GetKey() == EKeys::Escape) { Escape(); return FReply::Handled(); }
        if (Event.GetKey() == EKeys::H) { ToggleHud(); return FReply::Handled(); }
        return FReply::Unhandled();
    }
    virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        if (Event.GetEffectingButton() == EKeys::LeftMouseButton && Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()).Y < 44)
        { bMoving = true; return FReply::Handled().CaptureMouse(SharedThis(this)); }
        return FReply::Handled();
    }
    virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        if (bMoving)
        {
            Move(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition())
                - Geometry.AbsoluteToLocal(Event.GetLastScreenSpacePosition()));
            return FReply::Handled();
        }
        return FReply::Unhandled();
    }
    virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent& Event) override
    {
        if (bMoving && Event.GetEffectingButton() == EKeys::LeftMouseButton)
        { bMoving = false; return FReply::Handled().ReleaseMouseCapture(); }
        return FReply::Unhandled();
    }
    virtual void OnMouseCaptureLost(const FCaptureLostEvent&) override { bMoving = false; }
};

class SMmdViewer : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SMmdViewer) {} SLATE_END_ARGS()
    TWeakObjectPtr<AMmdViewerController> Controller;
    TSharedPtr<SBorder> MenuLayer;
    TSharedPtr<SBox> ReviewHost;
    TSharedPtr<SMmdNoticePanel> ReviewPanel;
    FMmdFirstRunState FirstRun;
    TArray<FMmdNotice> SoftwareNotices;
    FString NoticeHash, DeferredArchive, ApprovedModel, ReviewError;
    int32 NoticeStep = 0;
    bool bNoticesAccepted = true, bReviewOpen = false, bGuideOpen = false, bQaSkipNotices = false;
    FString FirstRunPath() const { return FMmdEnhancement::UserDataDir() / TEXT("Onboarding.json"); }
    void CloseReview()
    {
        bReviewOpen = false; ReviewPanel.Reset();
        if (ReviewHost) ReviewHost->SetContent(SNullWidget::NullWidget);
    }
    void ShowNotice(const FMmdNotice& Notice, bool bTimed, const FString& Folder, TFunction<void()> Accepted, TFunction<void()> Declined)
    {
        bReviewOpen = true; bDragging = false; PlacementKeys.Reset();
        if (Controller.IsValid()) Controller->StopCharacterMovement();
        ReviewHost->SetContent(SAssignNew(ReviewPanel, SMmdNoticePanel).Notice(Notice).RequireFiveSeconds(bTimed)
            .Folder(Folder).Accepted(MoveTemp(Accepted)).Declined(MoveTemp(Declined)));
        if (FSlateApplication::IsInitialized()) FSlateApplication::Get().SetKeyboardFocus(ReviewPanel);
    }
    void ReviewSoftware()
    {
        const FString Folder = FPaths::DirectoryExists(FPaths::ProjectDir() / TEXT("../Licenses"))
            ? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../Licenses")) : FPaths::ProjectDir();
        TWeakPtr<SMmdViewer> Weak = SharedThis(this);
        ShowNotice(SoftwareNotices[NoticeStep], false, Folder, [Weak]
        {
            auto Self = Weak.Pin(); if (!Self) return;
            if (Self->NoticeStep + 1 < Self->SoftwareNotices.Num()) { ++Self->NoticeStep; Self->ReviewSoftware(); return; }
            auto Proposed = Self->FirstRun; Proposed.AcceptedHash = Self->NoticeHash;
            FString Error;
            if (!Proposed.Save(Self->FirstRunPath(), Error)) { Self->ReviewPanel->SetError(Error); return; }
            Self->FirstRun = Proposed; Self->bNoticesAccepted = true;
            if (!Proposed.bGuideSeen) Self->ShowGuide();
            else
            {
                Self->CloseReview();
                if (!Self->DeferredArchive.IsEmpty())
                { const FString Path=MoveTemp(Self->DeferredArchive); Self->Controller->LoadArchive(Path); }
            }
        }, [] { FPlatformMisc::RequestExit(false); });
    }
    bool FinishGuide()
    {
        auto Proposed = FirstRun; Proposed.bGuideSeen = true;
        FString Error;
        if (!Proposed.Save(FirstRunPath(), Error))
        { if (ReviewPanel) ReviewPanel->SetError(Error); else ReviewError = Error; return false; }
        FirstRun = Proposed; bGuideOpen = false; CloseReview(); return true;
    }
    void ShowGuide()
    {
        CloseReview(); bGuideOpen = bReviewOpen = true;
        TWeakPtr<SMmdViewer> Weak = SharedThis(this);
        ReviewHost->SetContent(SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Panel).Padding(28)
            [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,20)[Label(TEXT("导入你的第一个模型"),18)]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,18)[Label(TEXT("将包含 PMX 与贴图的 ZIP 压缩包拖入窗口。\n保留压缩包内的相对目录结构，无需提前解压。"),13)]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,18)[Label(TEXT("选择 PMX 并阅读模型使用守则后，即可查看角色。\n中键旋转，Shift + 中键平移，滚轮缩放。\nWASD 移动角色，E 上移，Q 下移。\nEsc 打开菜单，H 隐藏或恢复菜单。"),12,Muted)]
            + SVerticalBox::Slot().FillHeight(1)[SNullWidget::NullWidget]
            + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text_Lambda([this] { return FText::FromString(ReviewError); }).ColorAndOpacity(FMmdUiStyle::Colors().Warning).AutoWrapText(true)]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)[Button(TEXT("选择 ZIP 压缩包"),[Weak]
                { if (auto Self=Weak.Pin(); Self && Self->FinishGuide()) { Self->DeferredArchive.Reset(); Self->Controller->ImportArchive(); } })]
            + SVerticalBox::Slot().AutoHeight()[Button(TEXT("我知道了"),[Weak]
                { if (auto Self=Weak.Pin(); Self && Self->FinishGuide() && !Self->DeferredArchive.IsEmpty())
                    { const FString Path=MoveTemp(Self->DeferredArchive); Self->Controller->LoadArchive(Path); } })]]);
    }
    void BeginFirstRun()
    {
#if WITH_DEV_AUTOMATION_TESTS
        bQaSkipNotices = FParse::Param(FCommandLine::Get(),TEXT("MmdQaSkipNotices"));
        if (bQaSkipNotices) return;
#endif
        SoftwareNotices = MmdNotices::Software(); NoticeHash = MmdNotices::Fingerprint(SoftwareNotices);
        FirstRun.Load(FirstRunPath()); bNoticesAccepted = FirstRun.AcceptedHash == NoticeHash;
        if (!bNoticesAccepted) ReviewSoftware(); else if (!FirstRun.bGuideSeen) ShowGuide();
    }
    bool RequestModelReview(const FString& Path, const FString& Root)
    {
        if (bQaSkipNotices || ApprovedModel == Path) { ApprovedModel.Reset(); return false; }
        TWeakPtr<SMmdViewer> Weak = SharedThis(this);
        FMmdNotice Notice = MmdNotices::Model(Root);
        Notice.Title += TEXT(" · ") + FPaths::GetCleanFilename(Path);
        ShowNotice(Notice, true, Root, [Weak,Path]
        { if (auto Self=Weak.Pin()) { Self->ApprovedModel=Path; Self->CloseReview(); Self->Controller->LoadModel(Path); } },
        [Weak] { if (auto Self=Weak.Pin()) { Self->CloseReview(); Self->Controller->bChoosing=true; Self->RefreshChoices(); } });
        return true;
    }
    TSharedPtr<SBox> PageHost;
    TSharedPtr<SVtuberRigPanel> MappingPanel;
    TSharedPtr<SVtuberCalibrationWizard> CalibrationPanel;
    TSharedPtr<SBox> ResultsHost;
    TSharedPtr<SVtuberCaptureResults> ResultsPanel;
    FVector2D ResultsPosition{960, 40};
    bool bResultsOpen = false;
#if WITH_DEV_AUTOMATION_TESTS
    FNativeTrackingPreview PreviewFixture;
    bool bPreviewFixture = false;
#endif
    FVector2D NavigationPosition{20, 20}, PagePosition{360, 40}, PageSize{560, 520};
    FVector2D NavigationSize{320, 440}, PreferredPageSize{560, 520}, ResultsSize{420, 610};
    bool bMenuOpen = false, bHudVisible = false, bNavigationCollapsed = false;
    bool bDragging = false;
    TSet<FKey> PlacementKeys;
    FName CurrentPage = "Main";
    TArray<TSharedPtr<int32>> PartMaterialOptions;
    int32 PartMaterialFilter = INDEX_NONE;
    FButtonStyle ButtonStyle;
    FButtonStyle PrimaryButtonStyle;
    TArray<TSharedPtr<FIntPoint>> TrackingResolutions;

    void Construct(const FArguments&, AMmdViewerController* InController)
    {
        Controller = InController;
        ButtonStyle = FMmdUiStyle::Get().Button;
        PrimaryButtonStyle = FMmdUiStyle::Get().Primary;
        TrackingResolutions = {MakeShared<FIntPoint>(640, 480), MakeShared<FIntPoint>(1280, 720)};
        TSharedRef<SVerticalBox> Navigation = SNew(SVerticalBox);
        const TCHAR* NavigationNames[] = {TEXT("模型"), TEXT("捕捉"), TEXT("映射"), TEXT("画面"), TEXT("场景"), TEXT("应用")};
        const FName NavigationPages[] = {"Model", "Tracking", "Mapping", "Graphics", "Scene", "Application"};
        for (int32 Row = 0; Row < 2; ++Row)
        {
            TSharedRef<SHorizontalBox> Buttons = SNew(SHorizontalBox);
            for (int32 Column = 0; Column < 3; ++Column)
            {
                const int32 Index = Row * 3 + Column;
                Buttons->AddSlot().FillWidth(1).Padding(3)
                    [Button(NavigationNames[Index], [this, Page = NavigationPages[Index]] { OpenPage(Page); })];
            }
            Navigation->AddSlot().AutoHeight()[Buttons];
        }
        Navigation->AddSlot().AutoHeight().Padding(3, 8, 3, 4)
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 4, 0)
                    [SNew(SButton).ButtonStyle(&PrimaryButtonStyle).ContentPadding(FMargin(8, 7))
                        .IsEnabled_Lambda([this] { return Controller.IsValid() && Controller->Rig && !Controller->bBusy
                            && !Controller->Tracking.IsRunning() && !Controller->Tracking.IsStopping(); })
                        .OnClicked_Lambda([this] { Controller->ToggleTracking(); return FReply::Handled(); })
                        [SNew(STextBlock).Text_Lambda([this] { return FText::FromString(Controller.IsValid() ? Controller->TrackingActionLabel() : FString(TEXT("开启动捕"))); }).ColorAndOpacity(FSlateColor::UseForeground())
                            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))]]
                + SHorizontalBox::Slot().FillWidth(1)
                    [SNew(SButton).ButtonStyle(&ButtonStyle).ContentPadding(FMargin(8, 7))
                        .IsEnabled_Lambda([this] { return Controller.IsValid() && (Controller->IsTrackingReplay() || Controller->Tracking.IsRunning()) && !Controller->Tracking.IsStopping(); })
                        .OnClicked_Lambda([this] { if (Controller->IsTrackingReplay()) Controller->StopTrackingReplay(); else Controller->ToggleTracking(); return FReply::Handled(); })
                        [SNew(STextBlock).Text_Lambda([this] { return FText::FromString(Controller.IsValid() && Controller->Tracking.IsStopping()
                            ? TEXT("正在停止…") : TEXT("停止")); }).ColorAndOpacity(FSlateColor::UseForeground())
                            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))]]];
        Navigation->AddSlot().AutoHeight().Padding(3, 8, 3, 0)
            [SNew(STextBlock).Text_Lambda([this] { return FText::FromString(Controller.IsValid() ? Controller->ModelName : FString()); })
                .ColorAndOpacity(Text).AutoWrapText(true)];
        Navigation->AddSlot().AutoHeight().Padding(3, 4)
            [SNew(STextBlock).Text_Lambda([this]
                { return FText::FromString(Controller.IsValid() ? Controller->Tracking.Status + TEXT("\n") + Controller->TrackingSummary() : FString()); })
                .ColorAndOpacity(Muted).AutoWrapText(true)];
        Navigation->AddSlot().AutoHeight().Padding(3, 0, 3, 4)
            [SNew(STextBlock).Text_Lambda([this] { return FText::FromString(Controller.IsValid()
                ? FString::Printf(TEXT("动捕 %.1f FPS"), Controller->Tracking.InferenceFps) : FString()); })
                .ColorAndOpacity(Muted)];
        Navigation->AddSlot().AutoHeight().Padding(3, 3)
            [Button(TEXT("识别结果"), [this] { OpenResults(); })];
        TWeakPtr<SMmdViewer> Weak = SharedThis(this);
        FMmdMenuSetup NavigationSetup{FString(TEXT("Honkai Render")),
            [Weak] { if (auto Self = Weak.Pin()) Self->ToggleHud(); },
            [Weak] { if (auto Self = Weak.Pin()) Self->ToggleHud(); },
            [Weak](FVector2D Delta) { if (auto Self = Weak.Pin()) Self->MovePanel(Self->NavigationPosition, Delta, Self->NavigationSize); },
            [Weak] { if (auto Self = Weak.Pin()) { Self->bNavigationCollapsed = !Self->bNavigationCollapsed; Self->UpdatePanelLayout(Self->GetCachedGeometry().GetLocalSize()); } },
            SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Panel).Padding(12)
                [SNew(SScrollBox).ScrollBarThickness(FVector2D(6, 6)) + SScrollBox::Slot()[Navigation]]};
        TSharedRef<SMmdMenu> NavigationWindow = SNew(SMmdMenu, NavigationSetup);
        ChildSlot
        [
            SNew(SOverlay)
            + SOverlay::Slot()
            [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor(FLinearColor::Transparent)]
            + SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(24)
            [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Panel).Padding(16)
                .Visibility_Lambda([this] { return bHudVisible && Controller.IsValid() && Controller->IsGenerating() && !bMenuOpen ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
                [SNew(SBox).WidthOverride_Lambda([this] { const float Width = GetCachedGeometry().GetLocalSize().X;
                    return Width > 0 ? FMath::Min(320.f, FMath::Max(1.f, Width - 48.f)) : 320.f; })
                    [SNew(STextBlock).ColorAndOpacity(Text).AutoWrapText(true)
                    .Text_Lambda([this] { return FText::FromString(Controller.IsValid() ? Controller->GenerationText() + TEXT("\nEsc 查看进度或取消") : FString()); })]]]
            + SOverlay::Slot()
            [SAssignNew(MenuLayer, SBorder)
                .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor(FLinearColor::Transparent).Padding(0).Clipping(EWidgetClipping::ClipToBounds)
                .Visibility(EVisibility::Collapsed).IsEnabled_Lambda([this] { return !bReviewOpen; })
                [SNew(SCanvas)
                    + SCanvas::Slot().Position_Lambda([this] { return NavigationPosition; })
                        .Size_Lambda([this] { return NavigationSize; })[NavigationWindow]
                    + SCanvas::Slot().Position_Lambda([this] { return PagePosition; })
                        .Size_Lambda([this] { return PageSize; })
                        [SAssignNew(PageHost, SBox).Visibility_Lambda([this]
                            { return bHudVisible && bMenuOpen ? EVisibility::Visible : EVisibility::Collapsed; })
                            .IsEnabled_Lambda([this] { return bMenuOpen; })]
                    + SCanvas::Slot().Position_Lambda([this] { return ResultsPosition; }).Size_Lambda([this] { return ResultsSize; })
                        [SAssignNew(ResultsHost, SBox).Visibility_Lambda([this]
                            { return bResultsOpen ? EVisibility::Visible : EVisibility::Collapsed; })]]]
            + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(16)
            [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Panel.CopyWithNewOpacity(.85f)).Padding(10,6)
                .Visibility(EVisibility::HitTestInvisible)
                [SNew(STextBlock).ColorAndOpacity(Text).Font(FCoreStyle::GetDefaultFontStyle("Regular",12))
                    .Text_Lambda([this] {
                        const bool Fresh = Controller.IsValid() && Controller->Tracking.IsRunning()
                            && FPlatformTime::Seconds() - Controller->LastTrackingFrame < .75;
                        return FText::FromString(FString::Printf(TEXT("渲染 %.1f FPS\n识别 %.1f FPS"),
                            Controller.IsValid() ? Controller->DisplayRenderFps : 0.f, Fresh ? Controller->Tracking.InferenceFps : 0.f));
                    })]]
            + SOverlay::Slot()
            [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0,0,0,.55f)).Padding(16)
                .Visibility_Lambda([this] { return bReviewOpen ? EVisibility::Visible : EVisibility::Collapsed; })
                .HAlign(HAlign_Center).VAlign(VAlign_Center)
                [SAssignNew(ReviewHost,SBox)
                    .WidthOverride_Lambda([this] { const float W=GetCachedGeometry().GetLocalSize().X; const float Preferred=bGuideOpen?620.f:720.f; return W>0?FMath::Clamp(W-32.f,1.f,Preferred):Preferred; })
                    .HeightOverride_Lambda([this] { const float H=GetCachedGeometry().GetLocalSize().Y; const float Preferred=bGuideOpen?390.f:620.f; return H>0?FMath::Clamp(H-32.f,1.f,Preferred):Preferred; })]]
        ];
    }
    virtual void Tick(const FGeometry& Geometry, double Time, float Delta) override
    {
        SCompoundWidget::Tick(Geometry, Time, Delta);
        UpdatePanelLayout(Geometry.GetLocalSize());
        MenuLayer->SetVisibility(bHudVisible ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed);
        FVector Movement = FVector::ZeroVector;
        if (!IsMenuBlocking() && !bDragging)
        {
            Movement.X = float(PlacementKeys.Contains(EKeys::W)) - float(PlacementKeys.Contains(EKeys::S));
            Movement.Y = float(PlacementKeys.Contains(EKeys::D)) - float(PlacementKeys.Contains(EKeys::A));
            Movement.Z = float(PlacementKeys.Contains(EKeys::E)) - float(PlacementKeys.Contains(EKeys::Q));
        }
        if (Controller.IsValid()) Controller->MoveCharacter(Movement, Delta);
    }
    bool IsMenuBlocking() const
    {
        return bReviewOpen || (bHudVisible && (CurrentPage == "Choose" || CurrentPage == "Status")
            && bMenuOpen);
    }
    void ToggleHud()
    {
        if (bReviewOpen) return;
        if (bHudVisible && MappingPanel) MappingPanel->FlushSave();
        bHudVisible = !bHudVisible;
        if (!bHudVisible && Controller.IsValid()) Controller->PreviewPart();
        if (Controller.IsValid()) Controller->Tracking.SetPreviewEnabled(bHudVisible && bResultsOpen);
        PlacementKeys.Reset();
        if (Controller.IsValid()) Controller->StopCharacterMovement();
        if (FSlateApplication::IsInitialized()) FSlateApplication::Get().SetKeyboardFocus(SharedThis(this));
    }
    void UpdatePanelLayout(FVector2D Viewport)
    {
        NavigationSize = MmdFitPanelSize(FVector2D(320, bNavigationCollapsed ? 44 : 440), Viewport);
        PageSize = MmdFitPanelSize(PreferredPageSize, Viewport);
        ResultsSize = MmdFitPanelSize(FVector2D(420, 610), Viewport);
        NavigationPosition = MmdClampPanelPosition(NavigationPosition, NavigationSize, Viewport);
        PagePosition = MmdClampPanelPosition(PagePosition, PageSize, Viewport);
        ResultsPosition = MmdClampPanelPosition(ResultsPosition, ResultsSize, Viewport);
    }
    void MovePanel(FVector2D& Position, FVector2D Delta, FVector2D Size)
    {
        Position = MmdClampPanelPosition(Position + Delta, Size, GetCachedGeometry().GetLocalSize());
    }
    bool PointerOnPanel(const FGeometry& Geometry, const FPointerEvent& Event) const
    {
        if (!bHudVisible) return false;
        const FVector2D Point = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
        const FSlateRect Navigation(NavigationPosition.X, NavigationPosition.Y,
            NavigationPosition.X + NavigationSize.X, NavigationPosition.Y + NavigationSize.Y);
        const FSlateRect Page(PagePosition.X, PagePosition.Y,
            PagePosition.X + PageSize.X, PagePosition.Y + PageSize.Y);
        const FSlateRect Results(ResultsPosition.X, ResultsPosition.Y, ResultsPosition.X + ResultsSize.X, ResultsPosition.Y + ResultsSize.Y);
        return Navigation.ContainsPoint(Point) || (bMenuOpen && Page.ContainsPoint(Point))
            || (bResultsOpen && Results.ContainsPoint(Point));
    }
    void CloseResults()
    {
        bResultsOpen = false;
        if (Controller.IsValid()) Controller->Tracking.SetPreviewEnabled(false);
        if (ResultsHost) ResultsHost->SetContent(SNullWidget::NullWidget);
        ResultsPanel.Reset();
    }
    void OpenResults()
    {
        bResultsOpen = bHudVisible = true;
        if (Controller.IsValid()) Controller->Tracking.SetPreviewEnabled(true);
#if WITH_DEV_AUTOMATION_TESTS
        bPreviewFixture = LoadTrackingPreviewFixture(PreviewFixture);
#endif
        ResultsPosition = FVector2D(FMath::Max(20.f, GetCachedGeometry().GetLocalSize().X - 440.f), 40);
        TWeakPtr<SMmdViewer> Weak = SharedThis(this);
        auto Content = SAssignNew(ResultsPanel, SVtuberCaptureResults).GetPreview_Lambda([this]
        {
#if WITH_DEV_AUTOMATION_TESTS
            if (bPreviewFixture) { PreviewFixture.TimestampSeconds = FPlatformTime::Seconds(); return PreviewFixture; }
#endif
            return Controller.IsValid() ? Controller->Tracking.GetPreview() : FNativeTrackingPreview();
        });
        FMmdMenuSetup Setup{TEXT("识别结果"),
            [Weak] { if (auto Self = Weak.Pin()) Self->CloseResults(); },
            [Weak] { if (auto Self = Weak.Pin()) Self->ToggleHud(); },
            [Weak](FVector2D Delta) { if (auto Self = Weak.Pin()) Self->MovePanel(Self->ResultsPosition, Delta, Self->ResultsSize); },
            {}, Content};
        ResultsHost->SetContent(SNew(SMmdMenu, Setup));
    }
    void CloseWindow()
    {
        if (MappingPanel) MappingPanel->FlushSave();
        MappingPanel.Reset();
        CalibrationPanel.Reset();
        bMenuOpen = false;
        CurrentPage = "Main";
        if (PageHost) PageHost->SetContent(SNullWidget::NullWidget);
        if (Controller.IsValid()) { Controller->bChoosing = false; Controller->ClearMaterialPreview(); Controller->PreviewPart(); }
        if (FSlateApplication::IsInitialized()) FSlateApplication::Get().SetKeyboardFocus(SharedThis(this));
    }
    TSharedRef<SButton> Button(const FString& Caption, TFunction<void()> Action)
    {
        return SNew(SButton).ButtonStyle(&ButtonStyle).ContentPadding(FMargin(16, 11))
            .IsEnabled_Lambda([this] { return Controller.IsValid() && !Controller->bBusy; })
            .OnClicked_Lambda([Action] { Action(); return FReply::Handled(); })
            [SNew(STextBlock).Text(FText::FromString(Caption)).Font(FCoreStyle::GetDefaultFontStyle("Regular", 13))
                .ColorAndOpacity(FSlateColor::UseForeground()).AutoWrapText(true).Justification(ETextJustify::Center)];
    }
    void OpenPage(FName Page = "Main")
    {
        if (bReviewOpen) return;
        if (Page == "Results") { OpenResults(); return; }
        if (Page == "Main") { CloseWindow(); bHudVisible = true; return; }
        if (MappingPanel) MappingPanel->FlushSave();
        MappingPanel.Reset();
        CalibrationPanel.Reset();
        if (Controller.IsValid() && Page != "Parts") Controller->PreviewPart();
        CurrentPage = Page;
        PlacementKeys.Reset();
        if (Controller.IsValid()) Controller->StopCharacterMovement();
        TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
        auto Add = [&](TSharedRef<SWidget> Widget) { Body->AddSlot().AutoHeight().Padding(0, 0, 0, 10)[Widget]; };
        FString Title = TEXT("菜单");
        if (Page == "Graphics")
        {
            Title = TEXT("画面");
            Add(Button(TEXT("相机与抗锯齿"), [this] { OpenPage("Settings"); }));
            Add(Button(TEXT("OBS 视频输出"), [this] { OpenPage("Output"); }));
            Add(Button(TEXT("视角"), [this] { OpenPage("View"); }));
            Add(Button(TEXT("渲染"), [this] { OpenPage("Rendering"); }));
        }
        else if (Page == "Scene")
        {
            Title = TEXT("场景");
            Add(Button(TEXT("背景颜色"), [this] { OpenPage("Background"); }));
        }
        else if (Page == "Application")
        {
            Title = TEXT("应用");
            Add(Button(TEXT("关于"), [this] { OpenPage("About"); }));
            Add(Label(TEXT("NVIDIA DLSS\nPowered by NVIDIA. DLSS 为 NVIDIA 商标。\n第三方许可见安装目录 Licenses。"), 11, Muted));
            Add(Button(TEXT("打开用户数据目录"), [] { FPlatformProcess::ExploreFolder(*FMmdEnhancement::UserDataDir()); }));
            Add(Button(TEXT("清理生成缓存"), [this] { OpenPage("ClearCache"); }));
            Add(Label(TEXT("中键旋转 · Shift + 中键平移 · 滚轮缩放\n小键盘切换视角 · Home 重新取景\nH 隐藏或恢复界面"), 12, Muted));
        }
        else if (Page == "About")
        {
            Title = TEXT("关于");
            const FString Product(TEXT("HonkaiRendering"));
#if defined(HONKAI_BUILD_STAMP)
            const FString Version(HONKAI_BUILD_STAMP);
            const FString BuildTime(HONKAI_BUILD_TIME);
            const FString Contributors(HONKAI_BUILD_CONTRIBUTORS);
#else
            const FString Version(TEXT("开发构建"));
            const FString BuildTime(TEXT("未记录"));
            const FString Contributors(TEXT("由 Build-Release.ps1 生成"));
#endif
            Add(Label(FString::Printf(TEXT("%s · %s\n构建时间：%s\nUnreal Engine %s"),
                *Product, *Version, *BuildTime, *FEngineVersion::Current().ToString(EVersionComponent::Patch)), 13));
            Add(Label(TEXT("插件版本"), 13));
            FString Plugins;
            for (const TCHAR* Name : {TEXT("HonkaiToonRenderer"), TEXT("HonkaiMotionCapture"), TEXT("VRM4U"), TEXT("Spout2_DX12"), TEXT("DLSS")})
            {
                const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(Name);
                if (Plugin && Plugin->IsEnabled())
                    Plugins += FString::Printf(TEXT("%s  %s\n"), Name, *Plugin->GetDescriptor().VersionName);
            }
            Add(Label(Plugins.TrimEnd(), 12, Muted));
            Add(Label(TEXT("贡献者"), 13));
            Add(Label(Contributors, 12, Muted));
            Add(Label(TEXT("开源许可"), 13));
            Add(Label(TEXT("项目、Honkai Toon Renderer、Honkai Motion Capture、VRM4U 和 UMT：MIT\nAssimp：BSD-3-Clause · Spout2 SDK：BSD-2-Clause\nMediaPipe 和 OpenCV：Apache-2.0 · Bullet：Zlib"), 12, Muted));
            const FString LicenseDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../Licenses"));
            if (FPaths::DirectoryExists(LicenseDir))
                Add(Button(TEXT("打开完整许可"), [LicenseDir] { FPlatformProcess::ExploreFolder(*LicenseDir); }));
            Add(Label(FString::Printf(TEXT("%s uses Unreal® Engine. Unreal® is a trademark or registered trademark of Epic Games, Inc. in the United States of America and elsewhere.\nUnreal® Engine, Copyright 1998 – %d, Epic Games, Inc. All rights reserved."),
                *Product, FDateTime::UtcNow().GetYear()), 11, Muted));
        }
        else if (Page == "Tracking")
        {
            Title = TEXT("动作捕捉");
            const auto CanConfigure = [this]
            {
                return Controller.IsValid() && !Controller->bBusy
                    && !Controller->Tracking.IsRunning() && !Controller->Tracking.IsStopping();
            };
            const auto Section = [&](const TCHAR* Caption, float Top)
            {
                Body->AddSlot().AutoHeight().Padding(0, Top, 0, 8)
                    [SNew(STextBlock).Text(FText::FromString(Caption)).ColorAndOpacity(Text)
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))];
            };
            const auto Setting = [&](const TCHAR* Caption, const TSharedRef<SWidget>& Value)
            {
                Body->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
                    [SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
                            [SNew(SBox).WidthOverride(124)[Label(Caption, 12, Muted)]]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Value]];
            };
            Section(TEXT("状态"), 0);
            Add(Button(TEXT("打开识别结果"), [this] { OpenResults(); }));
            Add(SNew(STextBlock).Text_Lambda([this] { return FText::FromString(Controller->Tracking.Status); })
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 13)).AutoWrapText(true).ColorAndOpacity(Text));
            Add(SNew(STextBlock).Text_Lambda([this] { return FText::FromString(Controller->TrackingSummary()); })
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12)).AutoWrapText(true).ColorAndOpacity(Muted));
            Setting(TEXT("处理速度"), SNew(STextBlock).Text_Lambda([this] { return FText::FromString(FString::Printf(TEXT("%.1f 帧/秒，%.1f ms"),
                Controller->Tracking.InferenceFps, Controller->Tracking.InferenceMs)); }).ColorAndOpacity(Text)
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12)));
            Setting(TEXT("摄像头"), SNew(SBox).WidthOverride(180).HeightOverride(34)
                [SNew(SButton).ButtonStyle(&PrimaryButtonStyle).HAlign(HAlign_Center)
                    .IsEnabled_Lambda([this] { return Controller.IsValid() && !Controller->Tracking.IsStopping()
                        && (Controller->Tracking.IsRunning() || (Controller->Rig && !Controller->bBusy)); })
                    .OnClicked_Lambda([this] { Controller->ToggleTracking(); return FReply::Handled(); })
                    [SNew(STextBlock).Text_Lambda([this] { return FText::FromString(Controller->TrackingActionLabel()); })
                        .ColorAndOpacity(FSlateColor::UseForeground()).Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))]]);

            Section(TEXT("摄像头"), 12);
            Setting(TEXT("设备编号"), SNew(SBox).WidthOverride(140).HeightOverride(34)
                [SNew(SNumericEntryBox<int32>).EditableTextBoxStyle(&FMmdUiStyle::Get().Editable).SpinBoxStyle(&FMmdUiStyle::Get().Spin)
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12)).AllowSpin(true).MinDesiredValueWidth(116).MinValue(0).MaxValue(32)
                    .IsEnabled_Lambda(CanConfigure).Value_Lambda([this] { return Controller->Tracking.CameraIndex; })
                    .OnValueCommitted_Lambda([this, CanConfigure](int32 Value, ETextCommit::Type)
                        { if (CanConfigure()) { Controller->Tracking.CameraIndex = FMath::Clamp(Value, 0, 32); Controller->Tracking.SaveSettings(); } })]);
            TSharedPtr<FIntPoint> SelectedResolution;
            for (const auto& Resolution : TrackingResolutions)
                if (Resolution->X == Controller->Tracking.Width && Resolution->Y == Controller->Tracking.Height) SelectedResolution = Resolution;
            Setting(TEXT("分辨率"), SNew(SBox).WidthOverride(220).HeightOverride(34)
                [SNew(SComboBox<TSharedPtr<FIntPoint>>).ComboBoxStyle(&FMmdUiStyle::Get().Combo).ItemStyle(&FMmdUiStyle::Get().Row)
                    .OptionsSource(&TrackingResolutions).InitiallySelectedItem(SelectedResolution).IsEnabled_Lambda(CanConfigure)
                    .OnGenerateWidget_Lambda([](TSharedPtr<FIntPoint> Resolution) -> TSharedRef<SWidget>
                        { return Label(Resolution.IsValid() ? FString::Printf(TEXT("%d × %d"), Resolution->X, Resolution->Y) : FString(), 12); })
                    .OnSelectionChanged_Lambda([this, CanConfigure](TSharedPtr<FIntPoint> Resolution, ESelectInfo::Type SelectInfo)
                    {
                        if (!Resolution.IsValid() || !CanConfigure() || SelectInfo == ESelectInfo::Direct) return;
                        Controller->Tracking.Width = Resolution->X; Controller->Tracking.Height = Resolution->Y;
                        Controller->Tracking.FramesPerSecond = 30; Controller->Tracking.SaveSettings();
                    })
                    [SNew(STextBlock).Text_Lambda([this] { return FText::FromString(FString::Printf(TEXT("%d × %d"),
                        Controller->Tracking.Width, Controller->Tracking.Height)); }).ColorAndOpacity(Text)
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))]]);
            Add(Label(TEXT("停止摄像头后可调整配置；新配置在下次启动时生效。"), 11, Muted));

            Section(TEXT("追踪"), 12);
            Setting(TEXT("帧率上限"), SNew(SBox).WidthOverride(140).HeightOverride(34)
                [SNew(SNumericEntryBox<int32>).EditableTextBoxStyle(&FMmdUiStyle::Get().Editable).SpinBoxStyle(&FMmdUiStyle::Get().Spin)
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12)).AllowSpin(true).MinDesiredValueWidth(116).MinValue(5).MaxValue(60)
                    .IsEnabled_Lambda(CanConfigure).Value_Lambda([this] { return Controller->Tracking.FramesPerSecond; })
                    .OnValueCommitted_Lambda([this, CanConfigure](int32 Value, ETextCommit::Type)
                        { if (CanConfigure()) { Controller->Tracking.FramesPerSecond = FMath::Clamp(Value, 5, 60); Controller->Tracking.SaveSettings(); } })]);
            Setting(TEXT("置信度阈值"), SNew(SBox).WidthOverride(140).HeightOverride(34)
                [SNew(SNumericEntryBox<float>).EditableTextBoxStyle(&FMmdUiStyle::Get().Editable).SpinBoxStyle(&FMmdUiStyle::Get().Spin)
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12)).AllowSpin(true).MinDesiredValueWidth(116).MinValue(.1f).MaxValue(.99f)
                    .MinFractionalDigits(2).MaxFractionalDigits(2).IsEnabled_Lambda(CanConfigure).Value_Lambda([this] { return Controller->Tracking.Confidence; })
                    .OnValueCommitted_Lambda([this, CanConfigure](float Value, ETextCommit::Type)
                        { if (CanConfigure() && FMath::IsFinite(Value)) { Controller->Tracking.Confidence = FMath::Clamp(Value, .1f, .99f); Controller->Tracking.SaveSettings(); } })]);
            Setting(TEXT("镜像"), SNew(SCheckBox).Style(&FMmdUiStyle::Get().Check).IsEnabled_Lambda(CanConfigure)
                .IsChecked_Lambda([this] { return Controller->Tracking.bMirror ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this, CanConfigure](ECheckBoxState State)
                    { if (CanConfigure()) { Controller->Tracking.bMirror = State == ECheckBoxState::Checked; Controller->Tracking.SaveSettings(); } })
                [Label(TEXT("左右镜像"), 12)]);
            Setting(TEXT("中立姿态"), SNew(SBox).WidthOverride(180).HeightOverride(34)
                [SNew(SButton).ButtonStyle(&ButtonStyle).HAlign(HAlign_Center)
                    .IsEnabled_Lambda([this] { return Controller.IsValid() && Controller->Rig && !Controller->bBusy && !Controller->Tracking.IsStopping()
                        && (Controller->Rig->HasTracking() || (Controller->bPoseSeen && FPlatformTime::Seconds() - Controller->LastTrackingFrame < .5)); })
                    .OnClicked_Lambda([this]
                    {
                        if (Controller->Rig && Controller->Rig->CalibrateNeutral())
                        {
                            FString Error;
                            if (!Controller->Rig->SaveProfile(Error)) Controller->Tracking.Status = TEXT("中立姿态保存失败：") + Error;
                        }
                        return FReply::Handled();
                    })[Label(TEXT("校准当前姿态"), 12)]]);
            Add(Label(TEXT("正对镜头、保持自然姿态后校准。摄像头画面只在本机处理。"), 11, Muted));
            Add(Button(TEXT("映射配置向导"), [this] { OpenPage("Calibration"); }));
        }
        else if (Page == "Mapping" || Page == "MappingOptions")
        {
            Title = TEXT("动作映射");
            Add(Button(TEXT("映射配置向导"), [this] { OpenPage("Calibration"); }));
            if (Controller->Rig) Add(SAssignNew(MappingPanel, SVtuberRigPanel, Controller->Rig.Get()).InitialTab(Page == "MappingOptions" ? 2 : 0));
            else Add(Label(TEXT("请先导入角色。")));
            Add(Button(TEXT("返回"), [this] { OpenPage(); }));
        }
        else if (Page == "Calibration")
        {
            Title = TEXT("映射配置向导");
            if (Controller.IsValid() && Controller->Rig)
            {
                const TWeakObjectPtr<UVtuberRigComponent> CalibratingRig = Controller->Rig.Get();
                const TWeakPtr<SMmdViewer> Weak = SharedThis(this);
                Add(SAssignNew(CalibrationPanel, SVtuberCalibrationWizard, CalibratingRig.Get())
                    .IsMirrored_Lambda([this] { return Controller.IsValid() && Controller->Tracking.bMirror; })
                    .GetSnapshot_Lambda([this] { return Controller.IsValid() ? Controller->LastTrackingSnapshot : FVtuberTrackingSnapshot(); })
                    .IsTracking_Lambda([this, CalibratingRig]
                    {
                        if (!Controller.IsValid() || !CalibratingRig.IsValid() || Controller->Rig != CalibratingRig.Get()
                            || Controller->bBusy || !bHudVisible || !bMenuOpen || CurrentPage != "Calibration") return false;
                        bool bRunning = Controller->Tracking.IsRunning() && !Controller->Tracking.IsStopping();
#if WITH_DEV_AUTOMATION_TESTS
                        bRunning |= Controller->bTrackingReplay;
#endif
                        return bRunning && Controller->LastTrackingFrame > 0
                            && FPlatformTime::Seconds() - Controller->LastTrackingFrame < .5;
                    })
                    .OnFinished_Lambda([Weak] { if (auto Self = Weak.Pin()) Self->OpenPage("Mapping"); }));
            }
            else Add(Label(TEXT("请先导入角色，再启动捕捉。")));
        }
        else if (Page == "Output")
        {
            Title = TEXT("Spout 输出");
            Add(SNew(STextBlock).Text_Lambda([this] { return FText::FromString(Controller->Output ? Controller->Output->GetStatus() : TEXT("尚未初始化")); }).ColorAndOpacity(Text).AutoWrapText(true));
            Add(Button(TEXT("启动 / 停止 HonkaiVTuber 输出"), [this] { Controller->ToggleSpout(); }));
            Add(Label(TEXT("输出 1280 × 720，30 FPS。视角跟随当前摄像机，菜单不会进入输出。\n透明输出需通过接收端检查后启用。"), 12, Muted));
            Add(Button(TEXT("返回"), [this] { OpenPage(); }));
        }
        else if (Page == "Model")
        {
            Title = TEXT("模型");
            Add(Button(TEXT("导入 ZIP 压缩包"), [this] { Controller->ImportArchive(); }));
            Add(Label(TEXT("也可以直接将 ZIP 拖入角色窗口。"), 12, Muted));
            if (!Controller->ModelName.IsEmpty())
            {
                Add(Label(Controller->ModelName, 14));
                Add(Label(Controller->ModelInfo, 12, Muted));
                Add(Button(TEXT("生成增强效果"), [this] { Controller->GenerateEnhancement(); OpenPage("Generation"); }));
                Add(Button(TEXT("选择面部材质"), [this] { OpenPage("FaceMaterial"); }));
                Add(Button(TEXT("部件显隐"), [this] { OpenPage("Parts"); }));
                Add(Button(TEXT("导出增强数据"), [this] { Controller->ExportEnhancement(); OpenPage("Generation"); }));
            }
            Add(Button(TEXT("返回"), [this] { OpenPage(); }));
        }
        else if (Page == "Parts")
        {
            Title = TEXT("部件显隐");
            Add(Label(TEXT("选择隐藏独立部件。设置仅保存在本机；关闭功能可恢复原始显示。"), 12, Muted));
            Add(SNew(SCheckBox).Style(&FMmdUiStyle::Get().Check)
                .IsEnabled_Lambda([this] { return Controller->HasParts(); })
                .IsChecked_Lambda([this] { return Controller->GetPartVisibility().bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { Controller->SetPartVisibilityEnabled(State == ECheckBoxState::Checked); })
                [Label(TEXT("启用部件隐藏"), 13)]);
            Add(SNew(STextBlock).Text_Lambda([this] { return FText::FromString(Controller->PartVisibilityNotice); }).ColorAndOpacity(Muted).AutoWrapText(true));
            PartMaterialOptions.Reset();
            PartMaterialOptions.Add(MakeShared<int32>(INDEX_NONE));
            for (int32 I = 0; I < Controller->MaterialNames.Num(); ++I) PartMaterialOptions.Add(MakeShared<int32>(I));
            if (!Controller->MaterialNames.IsValidIndex(PartMaterialFilter)) PartMaterialFilter = INDEX_NONE;
            const auto MaterialLabel = [this](int32 Index) { return Index == INDEX_NONE ? FString(TEXT("全部材质"))
                : Controller->MaterialNames.IsValidIndex(Index) ? Controller->MaterialNames[Index] : FString::Printf(TEXT("材质 %d"), Index + 1); };
            Add(SNew(SComboBox<TSharedPtr<int32>>).ComboBoxStyle(&FMmdUiStyle::Get().Combo)
                .OptionsSource(&PartMaterialOptions).InitiallySelectedItem(PartMaterialOptions[PartMaterialFilter + 1])
                .OnGenerateWidget_Lambda([this, MaterialLabel](TSharedPtr<int32> Item) { return Label(MaterialLabel(*Item), 13); })
                .OnSelectionChanged_Lambda([this](TSharedPtr<int32> Item, ESelectInfo::Type)
                    { if (Item) { PartMaterialFilter = *Item; Controller->PreviewPart(); OpenPage("Parts"); } })
                [SNew(STextBlock).Text(FText::FromString(MaterialLabel(PartMaterialFilter))).ColorAndOpacity(Text)]);
            TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
            const auto& Parts = Controller->GetPartVisibility().Parts();
            for (int32 I = 0; I < Parts.Num(); ++I)
            {
                const auto& Part = Parts[I];
                if (PartMaterialFilter != INDEX_NONE && Part.MaterialIndex != PartMaterialFilter) continue;
                const FString Id = Part.Id;
                const FString Caption = FString::Printf(TEXT("部件 %d · %d 面"), I + 1, Part.TriangleCount);
                Rows->AddSlot().AutoHeight().Padding(0, 3)
                [SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                    [SNew(SCheckBox).Style(&FMmdUiStyle::Get().Check)
                        .IsEnabled_Lambda([this] { return Controller->GetPartVisibility().bEnabled; })
                        .IsChecked_Lambda([this, Id] { return Controller->GetPartVisibility().HiddenIds.Contains(Id) ? ECheckBoxState::Unchecked : ECheckBoxState::Checked; })
                        .OnCheckStateChanged_Lambda([this, Id](ECheckBoxState State) { Controller->SetPartHidden(Id, State != ECheckBoxState::Checked); })
                        [Label(Caption, 12)]]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [SNew(SButton).ButtonStyle(&ButtonStyle).ContentPadding(FMargin(10, 6))
                        .IsEnabled_Lambda([this] { return Controller->GetPartVisibility().bEnabled; })
                        .OnClicked_Lambda([this, Id] { Controller->PreviewPart(Controller->GetPreviewPart() == Id ? FString() : Id); return FReply::Handled(); })
                        [Label(TEXT("单独显示"), 12)]]];
            }
            Add(SNew(SBox).MaxDesiredHeight(280)[SNew(SScrollBox) + SScrollBox::Slot()[Rows]]);
            Add(Button(TEXT("结束预览"), [this] { Controller->PreviewPart(); }));
            Add(Button(TEXT("恢复所有部件"), [this] { Controller->RestoreParts(); }));
            Add(Button(TEXT("返回模型菜单"), [this] { OpenPage("Model"); }));
        }
        else if (Page == "Rendering")
        {
            Title = TEXT("渲染");
            Add(SNew(STextBlock).Text_Lambda([this] { return FText::FromString(Controller->IsEnhanced() ? TEXT("当前：增强效果") : TEXT("当前：原效果")); }).ColorAndOpacity(Text));
            Add(Label(TEXT("原效果保留增强前的渲染。切换不会改变摄像机和灯光。"), 12, Muted));
            Add(SNew(STextBlock).Text_Lambda([this] { return FText::FromString(Controller->HasEnhancement() ? TEXT("增强数据已就绪，可直接切换。") : TEXT("尚无增强数据，请先生成增强效果。")); }).AutoWrapText(true).ColorAndOpacity(Muted));
            Add(Button(TEXT("原效果"), [this] { Controller->SetEnhanced(false); }));
            Add(Button(TEXT("增强效果"), [this] { Controller->SetEnhanced(true); }));
            Add(Button(TEXT("生成 / 查看进度"), [this] { if (!Controller->IsGenerating()) Controller->GenerateEnhancement(); OpenPage("Generation"); }));
            Add(Button(TEXT("返回"), [this] { OpenPage(); }));
        }
        else if (Page == "Generation")
        {
            Title = TEXT("增强效果");
            Add(SNew(STextBlock).Text_Lambda([this] { return FText::FromString(Controller->GenerationText()); }).AutoWrapText(true).ColorAndOpacity(Text));
            Add(SNew(SProgressBar).Percent_Lambda([this]() -> TOptional<float> { return FMath::Clamp(Controller->GenerationProgress(), 0.f, 1.f); })
                .Visibility_Lambda([this] { return Controller->IsGenerating() ? EVisibility::Visible : EVisibility::Collapsed; }));
            Add(Button(TEXT("取消生成"), [this] { Controller->CancelEnhancement(); }));
            Add(Button(TEXT("生成 / 重试"), [this] { Controller->GenerateEnhancement(); }));
            Add(Button(TEXT("重新生成（不复用缓存）"), [this] { Controller->GenerateEnhancement(true); }));
            Add(Button(TEXT("选择面部材质"), [this] { OpenPage("FaceMaterial"); }));
            Add(Button(TEXT("返回角色，后台继续"), [this] { CloseWindow(); }));
            Add(Button(TEXT("返回模型菜单"), [this] { OpenPage("Model"); }));
        }
        else if (Page == "FaceMaterial")
        {
            Title = TEXT("选择面部材质");
            Add(Label(TEXT("点选材质，角色上的对应部位会变为青色。\n确认它仅覆盖面部，再开始生成。"), 12, Muted));
            TSharedRef<SVerticalBox> Choices = SNew(SVerticalBox);
            for (int32 I = 0; I < Controller->MaterialNames.Num(); ++I)
                Choices->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[Button(FString::Printf(TEXT("%d  %s"), I + 1, *Controller->MaterialNames[I]), [this, I] { Controller->PreviewMaterial(I); })];
            Add(SNew(SBox).HeightOverride(230)[SNew(SScrollBox) + SScrollBox::Slot()[Choices]]);
            Add(SNew(STextBlock).Text_Lambda([this] { const int32 Index = Controller->GetPreviewMaterial(); return FText::FromString(Controller->MaterialNames.IsValidIndex(Index)
                ? TEXT("已选择：") + Controller->MaterialNames[Index] : TEXT("尚未选择材质")); }).ColorAndOpacity(Text));
            Add(Button(TEXT("使用此材质生成"), [this] { const int32 Index = Controller->GetPreviewMaterial(); if (Index != INDEX_NONE) { Controller->GenerateEnhancement(false, Index); OpenPage("Generation"); } }));
            Add(Button(TEXT("返回"), [this] { Controller->ClearMaterialPreview(); OpenPage("Model"); }));
        }
        else if (Page == "Background")
        {
            Title = TEXT("背景");
            Add(Button(TEXT("Blender 中性灰"), [this] { Controller->SetBackground(0); }));
            Add(Button(TEXT("浅灰"), [this] { Controller->SetBackground(1); }));
            Add(Button(TEXT("深灰"), [this] { Controller->SetBackground(2); }));
            Add(Button(TEXT("返回"), [this] { OpenPage(); }));
        }
        else if (Page == "Settings")
        {
            Title = TEXT("设置");
            Add(Button(TEXT("默认手臂角度"), [this] { OpenPage("MappingOptions"); }));
            Add(SNew(STextBlock).Text_Lambda([this] { return FText::FromString(Controller.IsValid() ? Controller->UpscalingStatus() : FString()); }).AutoWrapText(true).ColorAndOpacity(Text));
            Add(Button(TEXT("TSR · 关闭 DLSS"), [this] { Controller->SetUpscaling(EHonkaiUpscalingMode::TSR); }));
            FString DlssReason;
            const bool bDlssSupported = FHonkaiUpscaling::IsSupported(DlssReason);
            auto DlaaButton = Button(TEXT("NVIDIA DLAA · 原生分辨率抗锯齿"), [this] { Controller->SetUpscaling(EHonkaiUpscalingMode::DLAA); });
            DlaaButton->SetEnabled(bDlssSupported); Add(DlaaButton);
            auto DlssButton = Button(TEXT("NVIDIA DLSS 4.5 · 质量"), [this] { Controller->SetUpscaling(EHonkaiUpscalingMode::DLSSQuality); });
            DlssButton->SetEnabled(bDlssSupported); Add(DlssButton);
            Add(Label(TEXT("DLSS 仅作用于主视口；界面按窗口分辨率绘制。Spout 使用独立捕获。"), 11, Muted));
            Add(Button(TEXT("标准质量 · 100% 内部分辨率"), [this] { Controller->SetRenderScale(100); }));
            Add(Button(TEXT("高质量 · 150% 内部分辨率"), [this] { Controller->SetRenderScale(150); }));
            Add(SNew(STextBlock).Text_Lambda([this] { return FText::FromString(FString::Printf(TEXT("增强贴图尺寸：%d"), Controller->EnhancementResolution)); }).ColorAndOpacity(Text));
            Add(Button(TEXT("增强贴图 · 512"), [this] { Controller->SetEnhancementResolution(512); }));
            Add(Button(TEXT("增强贴图 · 1024"), [this] { Controller->SetEnhancementResolution(1024); }));
            Add(Button(TEXT("打开用户数据目录"), [] { FPlatformProcess::ExploreFolder(*FMmdEnhancement::UserDataDir()); }));
            Add(Button(TEXT("清理生成缓存"), [this] { OpenPage("ClearCache"); }));
            Add(Button(TEXT("返回"), [this] { OpenPage(); }));
        }
        else if (Page == "ClearCache")
        {
            Title = TEXT("清理生成缓存");
            Add(Label(TEXT("删除生成的缓存；保留导入的模型、原始资源和工具。\n当前模型恢复原效果，之后可重新生成。"), 12, Muted));
            Add(Button(TEXT("确认清理缓存"), [this] { Controller->ClearEnhancementCache(); OpenPage("Generation"); }));
            Add(Button(TEXT("返回"), [this] { OpenPage("Settings"); }));
        }
        else if (Page == "View")
        {
            Title = TEXT("视角");
            Add(Button(TEXT("聚焦模型"), [this] { Controller->FocusModel(); CloseWindow(); }));
            Add(Button(TEXT("正交 / 透视"), [this] { Controller->ToggleProjection(); CloseWindow(); }));
            Add(Label(TEXT("中键拖动：旋转（向右拖动时视角向左转）\nShift + 中键：平移\nCtrl + 中键或滚轮：缩放\nWASD：水平移动角色；E：上移；Q：下移\n\n小键盘 1 / 3 / 7：正 / 右 / 顶视图\nCtrl + 小键盘：反向视图\n小键盘 5：正交 / 透视\n小键盘 . 或 Home：聚焦"), 12, Muted));
            Add(Button(TEXT("返回"), [this] { OpenPage(); }));
        }
        else if (Page == "Choose")
        {
            Title = TEXT("选择 PMX 文件");
            Add(Label(TEXT("选择压缩包中的角色模型。"), 12, Muted));
            TSharedRef<SVerticalBox> Choices = SNew(SVerticalBox);
            for (const FString& Path : Controller->ModelChoices)
                Choices->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Button(Path, [this, Path] { Controller->LoadModel(Path); })];
            Add(SNew(SBox).HeightOverride(280)[SNew(SScrollBox) + SScrollBox::Slot()[Choices]]);
            Add(Button(TEXT("取消"), [this] { Controller->bChoosing = false; CloseWindow(); }));
        }
        else
        {
            Title = Controller->bBusy ? TEXT("正在加载") : TEXT("导入结果");
            Add(SNew(STextBlock).Text_Lambda([this] { return FText::FromString(Controller->Status); }).AutoWrapText(true).ColorAndOpacity(Text));
            if (!Controller->bBusy) Add(Button(TEXT("关闭"), [this] { CloseWindow(); }));
        }
        TWeakPtr<SMmdViewer> Weak = SharedThis(this);
        const bool bWidePage = Page == "Mapping" || Page == "MappingOptions" || Page == "Calibration";
        PreferredPageSize = bWidePage ? FVector2D(880, 680) : Page == "Tracking" ? FVector2D(560, 700) : Page == "Parts" ? FVector2D(560, 680) : FVector2D(560, 520);
        const FVector2D Viewport = GetCachedGeometry().GetLocalSize();
        UpdatePanelLayout(Viewport);
        PagePosition = FVector2D(FMath::Max(0.f, (Viewport.X - PageSize.X) * .5f),
            FMath::Max(0.f, (Viewport.Y - PageSize.Y) * .5f));
        FMmdMenuSetup PageSetup{Title,
            [Weak] { if (auto Self = Weak.Pin()) Self->CloseWindow(); },
            [Weak] { if (auto Self = Weak.Pin()) Self->ToggleHud(); },
            [Weak](FVector2D Delta) { if (auto Self = Weak.Pin()) Self->MovePanel(Self->PagePosition, Delta, Self->PageSize); },
            TFunction<void()>(),
            SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor(Panel).Padding(bWidePage ? 10.f : 20.f)
                [SNew(SScrollBox).ScrollBarThickness(FVector2D(6, 6)) + SScrollBox::Slot()
                    [SNew(SScrollBox).Orientation(Orient_Horizontal).ScrollBarThickness(FVector2D(6, 6)) + SScrollBox::Slot()
                        [SNew(SBox).WidthOverride_Lambda([this, bWidePage]
                            { return FMath::Max(360.f, float(PageSize.X) - (bWidePage ? 20.f : 40.f) - 12.f); })[Body]]]]};
        TSharedRef<SMmdMenu> Content = SNew(SMmdMenu, PageSetup);
        PageHost->SetContent(Content);
        bDragging = false;
        bHudVisible = true;
        bMenuOpen = true;
        FSlateApplication::Get().SetKeyboardFocus(Content);
    }
    void RefreshChoices() { OpenPage("Choose"); }
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FReply OnPreviewKeyDown(const FGeometry&, const FKeyEvent& Event) override
    {
        if (bReviewOpen && (Event.GetKey() == EKeys::H || Event.GetKey() == EKeys::Escape)) return FReply::Handled();
        if (Event.GetKey() == EKeys::H) { ToggleHud(); return FReply::Handled(); }
        if (Event.GetKey() == EKeys::Escape) { if (!bHudVisible) ToggleHud(); else if (bMenuOpen) CloseWindow(); else ToggleHud(); return FReply::Handled(); }
        return FReply::Unhandled();
    }
    virtual void OnFocusLost(const FFocusEvent& Event) override
    {
        PlacementKeys.Reset();
        if (Controller.IsValid()) Controller->StopCharacterMovement();
        SCompoundWidget::OnFocusLost(Event);
    }
    virtual FReply OnKeyUp(const FGeometry&, const FKeyEvent& Event) override
    {
        if (PlacementKeys.Remove(Event.GetKey())) return FReply::Handled();
        return FReply::Unhandled();
    }
    virtual FReply OnDragOver(const FGeometry&, const FDragDropEvent& Event) override
    {
        auto Files = Event.GetOperationAs<FExternalDragOperation>();
        return Files && Files->HasFiles() ? FReply::Handled() : FReply::Unhandled();
    }
    virtual FReply OnDrop(const FGeometry&, const FDragDropEvent& Event) override
    {
        auto Files = Event.GetOperationAs<FExternalDragOperation>();
        if (!Files || !Files->HasFiles()) return FReply::Unhandled();
        if (!Controller->bBusy)
        {
            if (Files->GetFiles().Num() == 1 && FPaths::GetExtension(Files->GetFiles()[0]).Equals(TEXT("zip"), ESearchCase::IgnoreCase))
                Controller->LoadArchive(Files->GetFiles()[0]);
            else { Controller->Status = TEXT("请一次拖入一个包含 PMX 和贴图的 ZIP 压缩包。"); OpenPage("Status"); }
        }
        return FReply::Handled();
    }
    virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        if (bReviewOpen) return FReply::Handled();
        if (Event.GetEffectingButton() == EKeys::MiddleMouseButton && !PointerOnPanel(Geometry, Event))
        {
            bDragging = true;
            return FReply::Handled().CaptureMouse(SharedThis(this)).UseHighPrecisionMouseMovement(SharedThis(this)).SetUserFocus(SharedThis(this));
        }
        return FReply::Handled().SetUserFocus(SharedThis(this));
    }
    virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent& Event) override
    {
        if (Event.GetEffectingButton() == EKeys::MiddleMouseButton) { bDragging = false; return FReply::Handled().ReleaseMouseCapture(); }
        return FReply::Unhandled();
    }
    virtual void OnMouseCaptureLost(const FCaptureLostEvent&) override { bDragging = false; }
    virtual FReply OnMouseMove(const FGeometry&, const FPointerEvent& Event) override
    {
        if (bDragging) { Controller->Orbit(Event.GetCursorDelta(), Event.IsShiftDown(), Event.IsControlDown()); return FReply::Handled(); }
        return FReply::Unhandled();
    }
    virtual FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        if (bReviewOpen) return FReply::Handled();
        if (!PointerOnPanel(Geometry, Event)) Controller->Zoom(Event.GetWheelDelta());
        return FReply::Handled();
    }
    virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent& Event) override
    {
        if (bReviewOpen) return FReply::Unhandled();
        if (Event.GetKey() == EKeys::H) { ToggleHud(); return FReply::Handled(); }
        if (Event.GetKey() == EKeys::Escape) { if (!bHudVisible) ToggleHud(); else if (bMenuOpen) CloseWindow(); else ToggleHud(); return FReply::Handled(); }
        if (!IsMenuBlocking() && (Event.GetKey() == EKeys::W || Event.GetKey() == EKeys::A || Event.GetKey() == EKeys::S || Event.GetKey() == EKeys::D
            || Event.GetKey() == EKeys::E || Event.GetKey() == EKeys::Q))
        { PlacementKeys.Add(Event.GetKey()); return FReply::Handled(); }
        if (IsMenuBlocking()) return FReply::Handled();
        if (Event.GetKey() == EKeys::NumPadOne) Controller->SetView(1, Event.IsControlDown());
        else if (Event.GetKey() == EKeys::NumPadThree) Controller->SetView(3, Event.IsControlDown());
        else if (Event.GetKey() == EKeys::NumPadSeven) Controller->SetView(7, Event.IsControlDown());
        else if (Event.GetKey() == EKeys::NumPadFive) Controller->ToggleProjection();
        else if (Event.GetKey() == EKeys::Decimal || Event.GetKey() == EKeys::Home) Controller->FocusModel();
        else return FReply::Unhandled();
        return FReply::Handled();
    }
};

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdFaceProfileTest, "HonkaiRendering.Rendering.FaceProfile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMmdFaceProfileTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("Local PNG accepted"), SafeFaceTextureName(TEXT("face.png")));
    for (const TCHAR* Name : {TEXT("../face.png"), TEXT("..\\face.png"), TEXT("/face.png"), TEXT("C:face.png"), TEXT("face.jpg"), TEXT("")})
        TestFalse(TEXT("Unsafe or unsupported texture name rejected"), SafeFaceTextureName(Name));
    const FString Root = FPaths::ProjectSavedDir() / TEXT("Tests") / FGuid::NewGuid().ToString(EGuidFormats::Digits);
    IFileManager::Get().MakeDirectory(*Root, true);
    const FString ModelPath = Root / TEXT("fixture.pmx");
    TArray<uint8> ModelBytes{1, 2, 3};
    FFileHelper::SaveArrayToFile(ModelBytes, *ModelPath);
    FSHAHash Hash;
    FSHA1::HashBuffer(ModelBytes.GetData(), ModelBytes.Num(), Hash.Hash);
    auto* Asset = NewObject<UVrmAssetListObject>();
    Asset->SkeletalMesh = NewObject<USkeletalMesh>();
    Asset->SkeletalMesh->GetMaterials().Add(FSkeletalMaterial(nullptr, true, false, TEXT("Face")));
    TArray<FColor> Pixels{FColor(128, 128, 255, 255)};
    TArray<uint8> Png;
    FImageUtils::ThumbnailCompressImageArray(1, 1, Pixels, Png);
    FFileHelper::SaveArrayToFile(Png, *(Root / TEXT("face.png")));
    const FString Valid = FString::Printf(TEXT("{\"version\":1,\"source_sha1\":\"%s\",\"materials\":[{\"index\":0,\"name\":\"Face\",\"texture\":\"face.png\"}]}"), *Hash.ToString().ToLower());
    FFileHelper::SaveStringToFile(Valid, *(ModelPath + TEXT(".toon.json")));
    TestEqual(TEXT("Matching profile loads"), LoadFaceMaps(ModelPath, Asset).Num(), 1);
    FFileHelper::SaveStringToFile(Valid.Replace(TEXT("\"Face\""), TEXT("\"Other\"")), *(ModelPath + TEXT(".toon.json")));
    TestEqual(TEXT("Wrong slot name falls back"), LoadFaceMaps(ModelPath, Asset).Num(), 0);
    FFileHelper::SaveStringToFile(Valid.Replace(TEXT("face.png"), TEXT("../face.png")), *(ModelPath + TEXT(".toon.json")));
    TestEqual(TEXT("Traversal falls back"), LoadFaceMaps(ModelPath, Asset).Num(), 0);
    FFileHelper::SaveStringToFile(Valid.Replace(*Hash.ToString().ToLower(), TEXT("wrong")), *(ModelPath + TEXT(".toon.json")));
    TestEqual(TEXT("Different source model falls back"), LoadFaceMaps(ModelPath, Asset).Num(), 0);
    for (const FString& Path : {ModelPath, ModelPath + TEXT(".toon.json"), Root / TEXT("face.png")}) IFileManager::Get().Delete(*Path);
    IFileManager::Get().DeleteDirectory(*Root);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdMaterialsTest, "HonkaiRendering.Rendering.SharedMaterials",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMmdMaterialsTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("Skin uses the shared skin outline"), OutlineRegion(TEXT("skin_hand")), 1.f);
    TestEqual(TEXT("Glove does not inherit hand skin styling"), OutlineRegion(TEXT("hand_glove")), 0.f);
    TestEqual(TEXT("Japanese hair uses hair styling"), OutlineRegion(TEXT("髪_01")), 2.f);
    TestEqual(TEXT("Unclassified mixed materials keep the default outline"), OutlineRegion(TEXT("body")), 0.f);

    for (const TCHAR* Path : {TEXT("/HonkaiToonRenderer/Materials/M_Toon"), TEXT("/HonkaiToonRenderer/Materials/M_ToonTwoSided")})
    {
        auto* Material = LoadObject<UMaterial>(nullptr, Path);
        if (!TestNotNull(TEXT("Shared surface material exists"), Material)) return false;
        TestTrue(TEXT("Surface receives real lighting and shadows"), Material->GetShadingModels().HasShadingModel(MSM_DefaultLit));
        TestTrue(TEXT("Source cutout alpha is retained"), Material->GetBlendMode() == BLEND_Masked);
    }
    auto* Cel = LoadObject<UMaterial>(nullptr, TEXT("/HonkaiToonRenderer/Materials/M_CelLighting"));
    if (!TestNotNull(TEXT("Cel pass exists"), Cel)) return false;
    TestTrue(TEXT("Cel pass uses scene lighting"), Cel->MaterialDomain == MD_PostProcess);
    TestTrue(TEXT("GBuffer sampling precedes temporal upscaling"), Cel->BlendableLocation == BL_SceneColorBeforeDOF);
    for (const TCHAR* Path : {TEXT("/HonkaiToonRenderer/Materials/M_Outline"), TEXT("/HonkaiToonRenderer/Materials/M_FaceOutline")})
    {
        auto* Material = LoadObject<UMaterial>(nullptr, Path);
        if (!TestNotNull(TEXT("Outline material exists"), Material)) return false;
        TestTrue(TEXT("Outline retains backface masking"), Material->IsTwoSided() && Material->GetBlendMode() == BLEND_Masked);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdEffectToggleTest, "HonkaiRendering.UI.EnhancementToggle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMmdEffectToggleTest::RunTest(const FString& Parameters)
{
    auto* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/HonkaiToonRenderer/Materials/M_Toon.M_Toon"));
    if (!TestNotNull(TEXT("Shared material available"), Parent)) return false;
    const UWorld::InitializationValues Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    if (!TestNotNull(TEXT("Test world created"), World)) return false;
    auto* Controller = World->SpawnActor<AMmdViewerController>();
    auto* Original = UTexture2D::CreateTransient(1, 1);
    auto* Enhanced = UTexture2D::CreateTransient(1, 1);
    for (UTexture2D* Map : { Original, Enhanced }) { Map->SRGB = false; Map->AddressX = Map->AddressY = TA_Clamp; }
    Controller->SurfaceTints.Add(FLinearColor(.8f, .7f, .6f, .4f));
    Controller->MaterialNames.Add(TEXT("Face"));
    Controller->OriginalFaceMaps.Add(0, Original);
    Controller->EnhancedFaceMaps.Add(0, Enhanced);
    Controller->ViewerCamera = World->SpawnActor<ACameraActor>();
    Controller->ViewerCamera->SetActorLocationAndRotation(FVector(50, 60, 70), FRotator(5, 15, 0));
    auto* Skeleton = NewObject<USkeleton>(Controller);
    {
        FReferenceSkeletonModifier Modifier(Skeleton);
        Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
    }
    auto* Asset = NewObject<USkeletalMesh>(Controller);
    Asset->SetSkeleton(Skeleton); Asset->SetRefSkeleton(Skeleton->GetReferenceSkeleton());
    Asset->GetMaterials().Add(FSkeletalMaterial(Parent));
    auto* Mesh = NewObject<USkeletalMeshComponent>(Controller); Mesh->SetSkeletalMesh(Asset);
    Controller->Renderer = NewObject<UHonkaiRenderInstance>(Controller);
    TArray<FHonkaiSurfaceInput> Inputs; Inputs.SetNum(1); Inputs[0].Tint = Controller->SurfaceTints[0];
    FString BindError;
    if (!TestTrue(TEXT("Host uses the plugin bind interface"), Controller->Renderer->Bind({Mesh, nullptr, Controller->ViewerCamera->GetCameraComponent()},
        Inputs, Controller->OriginalFaceMaps, {}, BindError))) { AddError(BindError); World->DestroyWorld(false); return false; }
    auto* Surface = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
    if (!TestNotNull(TEXT("Plugin surface material"), Surface)) { World->DestroyWorld(false); return false; }
    const FTransform Camera = Controller->ViewerCamera->GetActorTransform();
    const FVector Light = Controller->KeyDirection;
    Controller->SetEnhanced(true);
    UTexture* Actual = nullptr;
    Surface->GetTextureParameterValue(TEXT("FaceMap"), Actual);
    TestTrue(TEXT("Enhanced map applied without reload"), Actual == Enhanced);
    Controller->SetEnhanced(false);
    Surface->GetTextureParameterValue(TEXT("FaceMap"), Actual);
    TestTrue(TEXT("Original map restored"), Actual == Original);
    TestTrue(TEXT("Camera unchanged by A/B"), Controller->ViewerCamera->GetActorTransform().Equals(Camera));
    TestTrue(TEXT("Light unchanged by A/B"), Controller->KeyDirection.Equals(Light));
    Controller->PreviewMaterial(0);
    FLinearColor Tint;
    Surface->GetVectorParameterValue(TEXT("Tint"), Tint);
    TestTrue(TEXT("Material selection visibly highlights"), !Tint.Equals(Controller->SurfaceTints[0]));
    TestEqual(TEXT("Preview retains authored alpha"), Tint.A, Controller->SurfaceTints[0].A);
    Controller->ClearMaterialPreview();
    Surface->GetVectorParameterValue(TEXT("Tint"), Tint);
    TestTrue(TEXT("Closing selection restores exact tint"), Tint.Equals(Controller->SurfaceTints[0]));

    Controller->SetEnhanced(true);
    Controller->SelectedFaceMaterial = INDEX_NONE;
    Controller->ModelState.MaterialIndex = 0;
    Controller->ModelState.ParametersJson = TEXT("{\"resolution\":256}");
    Controller->ModelState.ProfileDirectory = TEXT("previous-valid-profile");
    Controller->PreviewMaterial(0);
    TestEqual(TEXT("Preview does not replace the pending generation material"), Controller->SelectedFaceMaterial, INDEX_NONE);
    Controller->ClearMaterialPreview();
    TestEqual(TEXT("Cancelled preview preserves the applied material selection"), Controller->ModelState.MaterialIndex, 0);
    Controller->SetEnhancementResolution(1024);
    TestTrue(TEXT("Changing pending resolution preserves the visible enhanced effect"), Controller->IsEnhanced());
    Surface->GetTextureParameterValue(TEXT("FaceMap"), Actual);
    TestTrue(TEXT("Changing pending resolution keeps the applied texture"), Actual == Enhanced);
    TestEqual(TEXT("Applied parameters remain tied to the existing profile"), Controller->ModelState.ParametersJson, FString(TEXT("{\"resolution\":256}")));
    TestEqual(TEXT("Pending resolution is independent"), Controller->EnhancementResolution, 1024);

    // A/B saves the applied metadata, even while different generation settings are pending.
    const FString StateFixture = FPaths::ProjectSavedDir() / TEXT("Tests") / (FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".pmx"));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(StateFixture), true);
    const FString FixtureContent = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    if (!TestTrue(TEXT("State fixture created"), FFileHelper::SaveStringToFile(FixtureContent, *StateFixture))) { World->DestroyWorld(false); return false; }
    Controller->LoadedModelPath = StateFixture;
    Controller->SelectedFaceMaterial = INDEX_NONE;
    Controller->SetEnhanced(false);
    Controller->SetEnhanced(true);
    const auto SavedState = FMmdEnhancement::ReadModelState(StateFixture);
    TestEqual(TEXT("A/B persists applied material rather than pending selection"), SavedState.MaterialIndex, 0);
    TestEqual(TEXT("A/B persists applied parameters rather than pending resolution"), SavedState.ParametersJson, FString(TEXT("{\"resolution\":256}")));

    Controller->GenerationRequest.MaterialIndex = INDEX_NONE;
    Controller->GenerationRequest.ParametersJson = TEXT("{\"resolution\":1024}");
    for (const auto State : {EMmdEnhancementState::Failed, EMmdEnhancementState::Cancelled, EMmdEnhancementState::Succeeded})
    {
        FMmdEnhancementStatus Result;
        Result.State = State;
        Result.ResultDirectory = TEXT("missing-result-must-not-apply");
        Controller->bGenerationPending = true;
        Controller->HandleGenerationStatus(Result);
        TestTrue(TEXT("Failed, cancelled or invalid generation retains the current enhanced effect"), Controller->IsEnhanced());
        Surface->GetTextureParameterValue(TEXT("FaceMap"), Actual);
        TestTrue(TEXT("Unaccepted generation retains the applied texture"), Actual == Enhanced);
        TestEqual(TEXT("Unaccepted generation retains the applied material"), Controller->ModelState.MaterialIndex, 0);
        TestEqual(TEXT("Unaccepted generation retains the applied parameters"), Controller->ModelState.ParametersJson, FString(TEXT("{\"resolution\":256}")));
        TestEqual(TEXT("Unaccepted generation retains the previous profile"), Controller->ModelState.ProfileDirectory, FString(TEXT("previous-valid-profile")));
        TestFalse(TEXT("Terminal result is consumed once"), Controller->bGenerationPending);
    }
    Controller->SetEnhanced(false);
    Controller->EnhancementNotice = TEXT("restored-original-choice");
    FMmdEnhancementStatus PreviousResult;
    PreviousResult.State = EMmdEnhancementState::Succeeded;
    Controller->HandleGenerationStatus(PreviousResult);
    TestFalse(TEXT("A previous model's completed job cannot override restored original mode"), Controller->IsEnhanced());
    TestEqual(TEXT("An unowned terminal result is not processed"), Controller->EnhancementNotice, FString(TEXT("restored-original-choice")));
    TestTrue(TEXT("Pending settings and terminal results preserve the camera"), Controller->ViewerCamera->GetActorTransform().Equals(Camera));
    TestTrue(TEXT("Pending settings and terminal results preserve lighting"), Controller->KeyDirection.Equals(Light));

    TArray<uint8> FixtureBytes;
    FFileHelper::LoadFileToArray(FixtureBytes, *StateFixture);
    FSHAHash FixtureHash;
    FSHA1::HashBuffer(FixtureBytes.GetData(), FixtureBytes.Num(), FixtureHash.Hash);
    IFileManager::Get().Delete(*(FMmdEnhancement::UserDataDir() / TEXT("Models") / (FixtureHash.ToString().ToLower() + TEXT(".json"))));
    IFileManager::Get().Delete(*StateFixture);
    Controller->LoadedModelPath.Empty();
    // Exercise profile-state commit with synthetic textures; no user profile path is set.
    Controller->Renderer->Detach();
    Asset->GetMaterials().Add(FSkeletalMaterial(Parent));
    Controller->SurfaceTints.Add(FLinearColor(.7f, .6f, .5f, .8f));
    Controller->MaterialNames.Add(TEXT("FaceSecond"));
    Inputs.SetNum(2); Inputs[1].Tint = Controller->SurfaceTints[1];
    auto* SecondOriginal = UTexture2D::CreateTransient(1, 1);
    auto* SecondEnhanced = UTexture2D::CreateTransient(1, 1);
    for (UTexture2D* Map : { SecondOriginal, SecondEnhanced }) { Map->SRGB = false; Map->AddressX = Map->AddressY = TA_Clamp; }
    Controller->OriginalFaceMaps.Add(1, SecondOriginal);
    if (!TestTrue(TEXT("Two-slot profile fixture binds"), Controller->Renderer->Bind({Mesh, nullptr, Controller->ViewerCamera->GetCameraComponent()},
        Inputs, Controller->OriginalFaceMaps, {}, BindError))) { AddError(BindError); World->DestroyWorld(false); return false; }
    FHonkaiFaceMaps GoodMaps; GoodMaps.Add(0, Enhanced); GoodMaps.Add(1, SecondEnhanced);
    FMmdModelState GoodState = Controller->ModelState;
    GoodState.MaterialIndex = 1; GoodState.ParametersJson = TEXT("{\"resolution\":512}"); GoodState.ProfileDirectory = TEXT("synthetic-valid-profile");
    Controller->PreviewMaterial(0);
    TestTrue(TEXT("Valid two-slot profile commits atomically"), Controller->CommitGeneratedProfile(GoodMaps, GoodState, true));
    TestEqual(TEXT("Successful profile commit restores the source tint preview"), Controller->GetPreviewMaterial(), INDEX_NONE);
    TestEqual(TEXT("Committed cache retains both slots"), Controller->EnhancedFaceMaps.Num(), 2);
    for (int32 I = 0; I < 2; ++I)
    {
        auto* ActiveSurface = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(I));
        UTexture* Visible = nullptr; ActiveSurface->GetTextureParameterValue(TEXT("FaceMap"), Visible);
        TestTrue(TEXT("Each successful slot uses its candidate texture"), Visible == GoodMaps[I]);
    }
    const FMmdModelState BeforeFailure = Controller->ModelState;
    auto* NoMips = NewObject<UTexture2D>();
    auto* NoMipData = new FTexturePlatformData(); NoMipData->SizeX = NoMipData->SizeY = 1; NoMipData->PixelFormat = PF_B8G8R8A8;
    NoMips->SetPlatformData(NoMipData); NoMips->SRGB = false; NoMips->AddressX = NoMips->AddressY = TA_Clamp;
    FHonkaiFaceMaps BadMaps = GoodMaps; BadMaps[0] = NoMips;
    FMmdModelState RejectedState = GoodState;
    RejectedState.MaterialIndex = 0; RejectedState.ParametersJson = TEXT("{\"resolution\":1024}"); RejectedState.ProfileDirectory = TEXT("must-not-commit");
    Controller->PreviewMaterial(1);
    for (bool bEnableCandidate : {false, true})
    {
        TestFalse(TEXT("An unusable candidate is rejected even when restoring disabled enhancement"),
            Controller->CommitGeneratedProfile(BadMaps, RejectedState, bEnableCandidate));
        TestEqual(TEXT("Failed preparation retains prior material selection"), Controller->ModelState.MaterialIndex, BeforeFailure.MaterialIndex);
        TestEqual(TEXT("Failed preparation retains prior parameters"), Controller->ModelState.ParametersJson, BeforeFailure.ParametersJson);
        TestEqual(TEXT("Failed preparation retains prior profile path"), Controller->ModelState.ProfileDirectory, BeforeFailure.ProfileDirectory);
        TestEqual(TEXT("Failed preparation retains prior saved enable flag"), Controller->ModelState.bEnhanced, BeforeFailure.bEnhanced);
        TestTrue(TEXT("Failed preparation retains prior enabled choice"), Controller->IsEnhanced());
        TestEqual(TEXT("Failed preparation retains the active temporary preview"), Controller->GetPreviewMaterial(), 1);
        TestTrue(TEXT("Failed preparation retains both cached maps"), Controller->EnhancedFaceMaps[0] == GoodMaps[0] && Controller->EnhancedFaceMaps[1] == GoodMaps[1]);
        for (int32 I = 0; I < 2; ++I)
        {
            UTexture* Visible = nullptr;
            Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(I))->GetTextureParameterValue(TEXT("FaceMap"), Visible);
            TestTrue(TEXT("Failed preparation retains the prior rendered map"), Visible == GoodMaps[I]);
        }
    }
    auto Menu = SNew(SMmdViewer, Controller);
    for (const FName Page : {FName("Model"), FName("Rendering"), FName("Background"), FName("Settings"), FName("Generation"), FName("FaceMaterial")})
    {
        Menu->OpenPage(Page);
        TestEqual(TEXT("Required page is reachable"), Menu->CurrentPage, Page);
    }
    Menu->CloseWindow();
    TestTrue(TEXT("Viewer uses the shared dark panel and text palette"),
        Panel.Equals(FMmdUiStyle::Colors().Panel) && Text.Equals(FMmdUiStyle::Colors().Text));
    Controller->ModelActor = World->SpawnActor<AActor>();
    auto* ModelRoot = NewObject<USceneComponent>(Controller->ModelActor);
    Controller->ModelActor->SetRootComponent(ModelRoot);
    ModelRoot->RegisterComponent();
    Controller->Rotation = FRotator::ZeroRotator;
    auto Controls = SNew(SMmdViewer, Controller);
    Controls->PlacementKeys.Add(EKeys::E);
    Controls->Tick(FGeometry(), 0, .1f);
    const float RaisedZ = Controller->ModelActor->GetActorLocation().Z;
    TestTrue(TEXT("E raises the character"), RaisedZ > 0.f);
    Controls->PlacementKeys.Reset();
    Controls->PlacementKeys.Add(EKeys::Q);
    Controls->Tick(FGeometry(), 0, .1f);
    Controls->Tick(FGeometry(), .1, .1f);
    TestTrue(TEXT("Q lowers the character after smooth reversal"), Controller->ModelActor->GetActorLocation().Z < RaisedZ);
    Controller->StopCharacterMovement();
    Controller->ModelActor->SetActorLocation(FVector::ZeroVector);
    Controller->MoveCharacter(FVector::ForwardVector, .02f);
    const double FirstTravel = Controller->ModelActor->GetActorLocation().Size();
    TestTrue(TEXT("Movement starts below full speed"), FirstTravel > 0 && FirstTravel < .5);
    const double FirstSpeed = Controller->MovementVelocity.Size();
    Controller->MoveCharacter(FVector::ZeroVector, .02f);
    TestTrue(TEXT("Key release decelerates smoothly"), Controller->MovementVelocity.Size() > 0
        && Controller->MovementVelocity.Size() < FirstSpeed);
    Controls->OnFocusLost(FFocusEvent(EFocusCause::SetDirectly, 0));
    TestTrue(TEXT("Focus loss clears residual movement"), Controller->MovementVelocity.IsZero());
    auto Travel = [&](int32 Fps, FVector Input)
    {
        Controller->StopCharacterMovement();
        Controller->ModelActor->SetActorLocation(FVector::ZeroVector);
        for (int32 Frame = 0; Frame < Fps; ++Frame) Controller->MoveCharacter(Input, 1.f / Fps);
        for (int32 Frame = 0; Frame < Fps; ++Frame) Controller->MoveCharacter(FVector::ZeroVector, 1.f / Fps);
        TestTrue(TEXT("Released movement settles to rest"), Controller->MovementVelocity.IsZero());
        return Controller->ModelActor->GetActorLocation();
    };
    const FVector At30 = Travel(30, FVector::ForwardVector);
    TestTrue(TEXT("Movement distance is consistent at 30 and 120 FPS"), At30.Equals(Travel(120, FVector::ForwardVector), .002));
    TestTrue(TEXT("Diagonal movement keeps the same speed"), FMath::IsNearlyEqual(At30.Size(), Travel(30, FVector(1, 1, 1)).Size(), .002));
    Controller->Rig = NewObject<UVtuberRigComponent>(Controller);
    Controller->bTrackingReplay = Controller->bReplaySequenceRequested = true;
    Controller->bFaceSeen = true; Controller->HandsSeen = 2;
    TestEqual(TEXT("Replay uses the same start capture label"), Controller->TrackingActionLabel(), FString(TEXT("开启动捕")));
    Controller->StopTrackingReplay();
    TestFalse(TEXT("Stopping replay releases both replay gates"), Controller->bTrackingReplay || Controller->bReplaySequenceRequested);
    TestFalse(TEXT("Stopping replay clears stale detection state"), Controller->bFaceSeen || Controller->HandsSeen != 0);
    TestFalse(TEXT("Stopping replay alone never opens the camera"), Controller->Tracking.IsRunning());
    Controls->OpenPage("Calibration");
    TestTrue(TEXT("Calibration wizard opens inside the viewer"), Controls->CalibrationPanel.IsValid());
    Controls->CloseWindow();
    TestFalse(TEXT("Closing calibration releases its pending session"), Controls->CalibrationPanel.IsValid());
    Controller->Orbit(FVector2D(10.f, 0.f), false, false);
    TestTrue(TEXT("Horizontal orbit follows the corrected drag direction"), Controller->Rotation.Yaw > 0.f);
    const float BeforeVerticalOrbit = Controller->Rotation.Pitch;
    Controller->Orbit(FVector2D(0.f, 10.f), false, false);
    TestTrue(TEXT("Vertical orbit follows the corrected drag direction"), Controller->Rotation.Pitch < BeforeVerticalOrbit);
    Controller->ShowcaseLight = World->SpawnActor<ADirectionalLight>();
    Controller->Rotation.Yaw = 180.f; Controller->UpdateCamera();
    const FVector FirstLight = Controller->KeyDirection;
    Controller->Orbit(FVector2D(100.f, 0.f), false, false);
    const FVector NextLight = Controller->KeyDirection;
    const double YawChange = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
        FVector::DotProduct(FirstLight.GetSafeNormal2D(), NextLight.GetSafeNormal2D()), -1.0, 1.0)));
    TestTrue(TEXT("Preview key follows the camera yaw"), FMath::IsNearlyEqual(YawChange, 30.0, .001));
    TestTrue(TEXT("Orbit preserves preview key elevation"), FMath::IsNearlyEqual(FirstLight.Z, NextLight.Z, .00001));
    World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdOverlayTest, "HonkaiRendering.UI.InViewportMenu",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMmdOverlayTest::RunTest(const FString& Parameters)
{
    const int32 Windows = FSlateApplication::Get().GetTopLevelWindows().Num();
    auto Menu = SNew(SMmdViewer, nullptr);
    TestFalse(TEXT("Navigation does not block camera"), Menu->IsMenuBlocking());
    TestTrue(TEXT("Viewer starts with UI hidden"), Menu->MenuLayer->GetVisibility() == EVisibility::Collapsed);
    Menu->OpenResults();
    TestTrue(TEXT("Results open independently of mapping pages"), Menu->bResultsOpen && Menu->ResultsPanel.IsValid());
    Menu->OpenPage("Application");
    TestTrue(TEXT("Opening a settings page retains recognition results"), Menu->bResultsOpen);
    Menu->CloseResults();
    TestFalse(TEXT("Closing recognition results releases its preview widget"), Menu->bResultsOpen || Menu->ResultsPanel.IsValid());
    Menu->bHudVisible = false;
    Menu->ToggleHud();
    Menu->Tick(FGeometry(), 0, 0);
    TestTrue(TEXT("H restores floating navigation"), Menu->MenuLayer->GetVisibility().IsVisible());
    Menu->OpenPage("Application");
    Menu->Tick(FGeometry(), 0, 0);
    TestFalse(TEXT("Floating page leaves background controls available"), Menu->IsMenuBlocking());
    auto* Rig = NewObject<UVtuberRigComponent>();
    auto Mapping = SNew(SVtuberRigPanel, Rig);
    Mapping->bSavePending = true;
    Menu->MappingPanel = Mapping;
    Menu->ToggleHud();
    Menu->Tick(FGeometry(), 0, 0);
    TestFalse(TEXT("H hides all pages and releases input"), Menu->IsMenuBlocking());
    TestTrue(TEXT("Hidden overlay collapses"), Menu->MenuLayer->GetVisibility() == EVisibility::Collapsed);
    TestFalse(TEXT("H flushes pending mapping changes"), Mapping->bSavePending);
    const FKeyEvent EscapeEvent(EKeys::Escape, FModifierKeysState(), uint32(0), false, 0, 0);
    Menu->OnPreviewKeyDown(FGeometry(), EscapeEvent);
    Menu->Tick(FGeometry(), 0, 0);
    TestTrue(TEXT("Esc restores hidden page directly"), Menu->bHudVisible && Menu->bMenuOpen && Menu->MenuLayer->GetVisibility().IsVisible());
    Menu->ToggleHud();
    Menu->ToggleHud();
    Menu->Tick(FGeometry(), 0, 0);
    TestTrue(TEXT("H restores the open page"), Menu->bMenuOpen && Menu->MenuLayer->GetVisibility().IsVisible());
    Menu->CloseWindow();
    TestFalse(TEXT("Closing the floating page immediately releases camera input"), Menu->IsMenuBlocking());
    Menu->PageHost->UpdateAllAttributes();
    TestTrue(TEXT("Closing immediately collapses the page host"), Menu->PageHost->GetVisibility() == EVisibility::Collapsed);
    Menu->OpenPage("Scene");
    TestTrue(TEXT("The floating page can reopen immediately"), Menu->bMenuOpen);
    Menu->CloseWindow();
    Menu->Tick(FGeometry(), 0, 0);
    TestFalse(TEXT("Closed menu releases camera input"), Menu->IsMenuBlocking());
    TestTrue(TEXT("Navigation remains visible after page closes"), Menu->MenuLayer->GetVisibility().IsVisible());
    TestEqual(TEXT("No native window was created"), FSlateApplication::Get().GetTopLevelWindows().Num(), Windows);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdFloatingLayoutTest, "HonkaiRendering.UI.ResponsiveFloatingPanels",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMmdFloatingLayoutTest::RunTest(const FString& Parameters)
{
    auto Menu = SNew(SMmdViewer, nullptr);
    Menu->PreferredPageSize = FVector2D(880, 680);
    for (const FVector2D Viewport : {FVector2D(640, 480), FVector2D(1280, 800), FVector2D(1920, 1080)})
    {
        Menu->NavigationPosition = Menu->PagePosition = Menu->ResultsPosition = FVector2D(1800, 1000);
        Menu->UpdatePanelLayout(Viewport);
        auto Fits = [&](FVector2D Position, FVector2D Size)
        { return Position.X >= 0 && Position.Y >= 0 && Position.X + Size.X <= Viewport.X && Position.Y + Size.Y <= Viewport.Y; };
        TestTrue(TEXT("Navigation stays inside the resized viewport"), Fits(Menu->NavigationPosition, Menu->NavigationSize));
        TestTrue(TEXT("Settings stay inside the resized viewport"), Fits(Menu->PagePosition, Menu->PageSize));
        TestTrue(TEXT("Results stay inside the resized viewport"), Fits(Menu->ResultsPosition, Menu->ResultsSize));
        TestTrue(TEXT("Expanded navigation reserves space for recognition controls"), Menu->NavigationSize.Y >= 440);
    }
    Menu->bNavigationCollapsed = true;
    Menu->UpdatePanelLayout(FVector2D(1280, 800));
    TestEqual(TEXT("Collapsed navigation contains only its one-line header"), Menu->NavigationSize.Y, 44.0);
    FMmdMenuSetup Setup{TEXT("较长的窗口标题应省略而不是挤压按钮"), [] {}, [] {}, [](FVector2D) {}, [] {}, SNullWidget::NullWidget};
    auto Header = SNew(SMmdMenu, Setup);
    Header->bCollapsed = true;
    Header->SlatePrepass();
    TestTrue(TEXT("Header stays a single fixed-height row"), FMath::IsNearlyEqual(double(Header->GetDesiredSize().Y), 44.0));
    for (float Width : {180.f, 320.f})
    {
        int32 Actions = 0;
        TFunction<void(TSharedRef<SWidget>, const FGeometry&)> CheckButtons;
        CheckButtons = [&](TSharedRef<SWidget> Widget, const FGeometry& Geometry)
        {
            if (Widget->GetTypeAsString() == TEXT("SButton"))
            {
                ++Actions;
                const FVector2D Position = Geometry.GetAbsolutePosition(), Size = Geometry.GetLocalSize();
                TestTrue(TEXT("Header action remains inside its row horizontally"), Position.X >= -.1 && Position.X + Size.X <= Width + .1);
                TestTrue(TEXT("Header action remains vertically centered inside its row"), Position.Y >= -.1 && Position.Y + Size.Y <= 44.1);
                TestTrue(TEXT("Header action keeps its usable click target"), Size.X >= 47.9 && Size.Y >= 27.9);
            }
            FArrangedChildren Children(EVisibility::Visible);
            Widget->ArrangeChildren(Geometry, Children);
            for (int32 Index = 0; Index < Children.Num(); ++Index) CheckButtons(Children[Index].Widget, Children[Index].Geometry);
        };
        CheckButtons(Header, FGeometry::MakeRoot(FVector2f(Width, 44), FSlateLayoutTransform()));
        TestEqual(TEXT("Both header actions remain available at narrow widths"), Actions, 2);
    }
    return true;
}
#endif

AMmdViewerGameMode::AMmdViewerGameMode()
{
	PlayerControllerClass = AMmdViewerController::StaticClass(); DefaultPawnClass = nullptr;
}

void AMmdViewerController::BeginPlay()
{
	Super::BeginPlay(); bShowMouseCursor = true; PrimaryActorTick.bCanEverTick = true; SetActorTickEnabled(true);
    Tracking.LoadSettings();
#if WITH_DEV_AUTOMATION_TESTS
    FString ReplayPath;
    bool bReplayMirror = Tracking.bMirror;
    int32 ReplayMirrorOption = bReplayMirror ? 1 : 0;
    if (FParse::Value(FCommandLine::Get(), TEXT("VtuberReplayMirror="), ReplayMirrorOption)) bReplayMirror = ReplayMirrorOption != 0;
    if (FParse::Value(FCommandLine::Get(), TEXT("VtuberReplaySequence="), ReplayPath))
    {
        // An explicitly requested invalid fixture still blocks camera startup.
        bTrackingReplay = true; bReplaySequenceRequested = true;
        float ReplayFps = 60;
        FParse::Value(FCommandLine::Get(), TEXT("VtuberReplayFPS="), ReplayFps);
        const bool bLoop = FParse::Param(FCommandLine::Get(), TEXT("VtuberReplayLoop"));
        FString Error;
        const bool bLoaded = ReplaySequence.Load(ReplayPath, ReplayFps, bLoop, bReplayMirror, Error);
        Tracking.Status = bLoaded ? TEXT("离线序列回放，不占用摄像头") : TEXT("离线回放被拒绝：") + Error;
        UE_LOG(LogTemp, Display, TEXT("VTUBER_REPLAY_SEQUENCE offline=1 loaded=%d frames=%d fps=%.3f loop=%d mirror=%d error=%s"),
            bLoaded, ReplaySequence.Num(), ReplayFps, bLoop, bReplayMirror, *Error);
    }
    else if (FParse::Value(FCommandLine::Get(), TEXT("VtuberReplay="), ReplayPath))
    {
        bTrackingReplay = FNativeTracking::LoadReplayFrame(ReplayPath, bReplayMirror, ReplaySnapshot);
        if (bTrackingReplay) { UE_LOG(LogTemp, Display, TEXT("VTUBER_REPLAY offline=1 loaded=1 path=%s"), *ReplayPath); }
        else { UE_LOG(LogTemp, Warning, TEXT("VTUBER_REPLAY offline=1 loaded=0 path=%s"), *ReplayPath); }
        if (bTrackingReplay) Tracking.Status = TEXT("离线姿态回放，不占用摄像头");
    }
#endif
    ImportTemplate = NewObject<UVrmAssetListObject>(this);
    ImportTemplate->MtoonUnlitSet = LoadObject<UVrmImportMaterialSet>(nullptr, TEXT("/VRM4U/ImportDataSet/UE5/DS_VRM_UE5_MToonUnLit.DS_VRM_UE5_MToonUnLit"));
	ViewerCamera = GetWorld()->SpawnActor<ACameraActor>(); SetViewTarget(ViewerCamera);
	UCameraComponent* C = ViewerCamera->GetCameraComponent(); C->FieldOfView = 35; C->bConstrainAspectRatio = false;
    UHonkaiRenderInstance::ConfigureCamera(C);
    auto* Key = GetWorld()->SpawnActor<ADirectionalLight>();
    ShowcaseLight = Key;
    auto* Light = CastChecked<UDirectionalLightComponent>(Key->GetLightComponent());
    Light->SetMobility(EComponentMobility::Movable);
    Light->SetIntensity(3.141593f);
    Light->SetLightColor(FLinearColor::White);
    Light->SetIndirectLightingIntensity(0);
    Light->SetVolumetricScatteringIntensity(0);
    Light->SetLightSourceAngle(2.f);
    Light->SetShadowBias(0.3f);
    Light->SetShadowSlopeBias(0.3f);
    float LightYaw = -5.f;
    FParse::Value(FCommandLine::Get(), TEXT("MmdLightYaw="), LightYaw);
    float LightPitch = -15.f;
#if WITH_DEV_AUTOMATION_TESTS
    FParse::Value(FCommandLine::Get(), TEXT("MmdLightPitch="), LightPitch);
#endif
    ShowcaseLightPitch = LightPitch;
    ShowcaseLightYawOffset = LightYaw;
    Key->SetActorRotation(FRotator(LightPitch, LightYaw + Rotation.Yaw, 0));
    KeyDirection = -Key->GetActorForwardVector();
    Renderer = NewObject<UHonkaiRenderInstance>(this);
    UpdateCamera();
    Output = NewObject<UVtuberOutput>(this);
    Output->Initialize(ViewerCamera, C, {});
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
        if (auto* Component = It->FindComponentByClass<UStaticMeshComponent>())
            if (auto* Material = Component->GetMaterial(0); Material && Material->GetName() == TEXT("M_Backdrop"))
                BackdropMaterial = Component->CreateDynamicMaterialInstance(0);
	SetBackground(0);
	Widget = SNew(SMmdViewer, this); GEngine->GameViewport->AddViewportWidgetContent(Widget.ToSharedRef());
	FInputModeUIOnly Input; Input.SetWidgetToFocus(Widget); Input.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock); SetInputMode(Input);
    Widget->BeginFirstRun();
#if WITH_DEV_AUTOMATION_TESTS
    FString UiCapture;
    if (FParse::Value(FCommandLine::Get(),TEXT("MmdUiCapture="),UiCapture))
    {
        FString Page;
        if (FParse::Value(FCommandLine::Get(),TEXT("MmdUiCapturePage="),Page) && Page == TEXT("Guide"))
        { Widget->bNoticesAccepted = true; Widget->ShowGuide(); }
        else if (Page == TEXT("Rules"))
        {
            FString Root;
            if (FParse::Value(FCommandLine::Get(),TEXT("MmdUiRulesRoot="),Root) && FPaths::DirectoryExists(Root))
                Widget->ShowNotice(MmdNotices::Model(Root),true,Root,[] {},[] {});
        }
        FTimerHandle Shot, Exit;
        GetWorldTimerManager().SetTimer(Shot,[UiCapture] { FScreenshotRequest::RequestScreenshot(UiCapture,true,false); },4.f,false);
        GetWorldTimerManager().SetTimer(Exit,[] { FPlatformMisc::RequestExit(false); },8.f,false);
    }
#endif
	FString AutoImport;
	if (FParse::Value(FCommandLine::Get(), TEXT("MmdArchive="), AutoImport)) LoadArchive(AutoImport);
}

void AMmdViewerController::EndPlay(const EEndPlayReason::Type Reason)
{
    Upscaling.Restore();
    Tracking.Stop();
    if (Output) Output->Shutdown();
    if (Renderer) Renderer->Detach();
	Enhancement.Cancel();
	if (Widget) Widget->CloseWindow();
	if (Widget && GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(Widget.ToSharedRef());
	Widget.Reset(); Super::EndPlay(Reason);
}

void AMmdViewerController::ImportArchive()
{
    if (!Widget || !Widget->bNoticesAccepted || (Widget->bReviewOpen && !Widget->bGuideOpen)) return;
    if (Widget->bGuideOpen && !Widget->FinishGuide()) return;
    Widget->CloseWindow();
#if PLATFORM_WINDOWS
	IFileOpenDialog* Dialog = nullptr;
	HRESULT Init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&Dialog))))
	{
		COMDLG_FILTERSPEC Filter{L"MMD ZIP 压缩包", L"*.zip"}; Dialog->SetFileTypes(1, &Filter); Dialog->SetTitle(L"导入 MMD 模型压缩包");
		Dialog->SetOptions(FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);
		const void* Parent = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(Widget);
		if (SUCCEEDED(Dialog->Show(static_cast<HWND>(const_cast<void*>(Parent)))))
		{
			IShellItem* Item = nullptr;
			if (SUCCEEDED(Dialog->GetResult(&Item)))
			{
				PWSTR File = nullptr;
				if (SUCCEEDED(Item->GetDisplayName(SIGDN_FILESYSPATH, &File))) { FString Path(File); CoTaskMemFree(File); LoadArchive(Path); }
				Item->Release();
			}
		}
		Dialog->Release();
	}
	else Status = TEXT("无法打开系统文件选择器。");
	if (SUCCEEDED(Init)) CoUninitialize();
#else
	Status = TEXT("当前导入文件选择器仅支持 Windows。");
#endif
}

void AMmdViewerController::LoadArchive(const FString& Path)
{
    if (bBusy) return;
    if (!Widget->bNoticesAccepted) { Widget->DeferredArchive = Path; return; }
    if (Widget->bGuideOpen) { if (!Widget->FinishGuide()) return; }
    if (Widget->bReviewOpen) return;
    if (Enhancement.IsRunning()) { EnhancementNotice = TEXT("请先取消或等待增强任务，再导入其他模型。"); Widget->OpenPage("Generation"); return; }
    bHudVisibleBeforeImport = Widget.IsValid() && Widget->bHudVisible;
    bBusy = true; Status = TEXT("正在展开压缩包…"); Widget->OpenPage("Status");
    const FString Storage = FMmdEnhancement::ImportsDir() / FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Destination = Storage / TEXT("Resources");
    const FString ArchiveCopy = Storage / TEXT("package.zip");
    const FString Script = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Scripts/Expand-MmdArchive.ps1"));
    TWeakObjectPtr<AMmdViewerController> Weak(this);
    Async(EAsyncExecution::ThreadPool, [Weak, Path, Destination, Script, Storage, ArchiveCopy]
    {
        IFileManager::Get().MakeDirectory(*Storage, true);
        if (IFileManager::Get().FileSize(*Path) <= 0 || IFileManager::Get().FileSize(*Path) > 512ll * 1024 * 1024
            || IFileManager::Get().Copy(*ArchiveCopy, *Path, false) != COPY_OK)
        {
            AsyncTask(ENamedThreads::GameThread, [Weak] { if (Weak.IsValid() && Weak->Widget.IsValid()) { Weak->bBusy = false; Weak->Status = TEXT("无法复制 ZIP 到用户数据目录，或文件超过 512 MiB。"); Weak->Widget->OpenPage("Status"); } });
            return;
        }
        auto Quote = [](FString S) { return TEXT("'") + S.Replace(TEXT("'"), TEXT("''")) + TEXT("'"); };
        FString Command = TEXT("& ([ScriptBlock]::Create([IO.File]::ReadAllText(") + Quote(Script) + TEXT("))) -ArchivePath ") + Quote(ArchiveCopy) + TEXT(" -Destination ") + Quote(Destination);
        auto Utf16 = StringCast<UTF16CHAR>(*Command);
        FString Args = TEXT("-NoProfile -NonInteractive -EncodedCommand ") + FBase64::Encode(reinterpret_cast<const uint8*>(Utf16.Get()), Utf16.Length() * 2);
        FString StdOut, StdErr; int32 Code = -1;
        FPlatformProcess::ExecProcess(TEXT("powershell.exe"), *Args, &Code, &StdOut, &StdErr);
        TArray<FString> Choices;
        if (Code == 0) IFileManager::Get().FindFilesRecursive(Choices, *Destination, TEXT("*.pmx"), true, false);
        AsyncTask(ENamedThreads::GameThread, [Weak, Destination, Choices = MoveTemp(Choices), Code, StdErr]() mutable
        {
            if (!Weak.IsValid() || !Weak->Widget.IsValid()) return;
            auto* Self = Weak.Get(); Self->bBusy = false;
            if (Code != 0) { Self->Status = TEXT("无法展开 ZIP：文件损坏、加密、路径不安全或大小超限。详情见日志。"); UE_LOG(LogTemp, Warning, TEXT("MMD ZIP: %s"), *StdErr); Self->Widget->OpenPage("Status"); return; }
            if (Choices.IsEmpty()) { Self->Status = TEXT("压缩包中没有 PMX 文件。"); Self->Widget->OpenPage("Status"); return; }
            Self->ImportDirectory = Destination; Self->ModelChoices.Reset();
            for (FString& Choice : Choices) { FPaths::MakePathRelativeTo(Choice, *(Destination + TEXT("/"))); Self->ModelChoices.Add(Choice); }
            Self->ModelChoices.Sort(); Self->bChoosing = true; Self->Widget->RefreshChoices(); Self->Status = TEXT("请选择要加载的 PMX 文件。");
            FString AutoModel;
            if (FParse::Value(FCommandLine::Get(), TEXT("MmdModel="), AutoModel)) Self->LoadModel(AutoModel);
        });
    });
}

void AMmdViewerController::LoadModel(const FString& Path)
{
    if (bBusy || !ModelChoices.Contains(Path) || !Widget->bNoticesAccepted || Widget->bReviewOpen) return;
    if (Widget->RequestModelReview(Path, ImportDirectory)) return;
    if (!ImportTemplate || !ImportTemplate->MtoonUnlitSet) { Status = TEXT("缺少 VRM4U 默认导入资源。"); return; }
    Enhancement.Cancel(); bGenerationPending = false; ClearMaterialPreview();
    PendingPath = Path; bBusy = true; bChoosing = false; Status = TEXT("正在加载模型与贴图…"); Widget->OpenPage("Status");
    StopCharacterMovement();
    FImportOptionData Options;
    Options.ModelScale = 0.1f;
    Options.MaterialType = EVRMImportMaterialType::VRMIMT_MToonUnlit;
    Options.bSkipPhysics = false; Options.bSkipRetargeter = true; Options.bSkipMorphTarget = false;
    Options.bForceOriginalMorphTargetName = true;
    Options.bGenerateOutlineMaterial = false;
    // VRM4U's async prepass assumes VRM; its generic loader handles PMX.
    FTimerHandle Handle;
    GetWorldTimerManager().SetTimer(Handle, [this, Path, Options]
    {
        ULoaderBPFunctionLibrary::LoadVRMFile(ImportTemplate, PendingAsset, ImportDirectory / Path, Options);
        OnModelLoaded();
    }, 0.1f, false);
}

void AMmdViewerController::OnModelLoaded()
{
    bBusy = false;
    if (!PendingAsset || !PendingAsset->SkeletalMesh) { Status = TEXT("VRM4U 无法读取此 PMX，详见输出日志。"); UE_LOG(LogTemp, Error, TEXT("MMD_LOAD_FAILED %s"), *PendingPath); Widget->OpenPage("Status"); return; }
    PendingAsset->SkeletalMesh->CalculateExtendedBounds();
    auto* NewActor = GetWorld()->SpawnActor<AActor>();
    auto* Mesh = NewObject<USkeletalMeshComponent>(NewActor); NewActor->SetRootComponent(Mesh); Mesh->RegisterComponent();
    Mesh->SetSkeletalMesh(PendingAsset->SkeletalMesh); Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Mesh->SetSimulatePhysics(false); Mesh->SetCastShadow(true);
    Mesh->bPauseAnims = false;
    auto* Outline = NewObject<UVrmPoseableMeshComponent>(NewActor);
    Outline->SetupAttachment(Mesh); Outline->RegisterComponent(); Outline->SetSkinnedAssetAndUpdate(PendingAsset->SkeletalMesh);
    Outline->SetCollisionEnabled(ECollisionEnabled::NoCollision); Outline->SetCastShadow(false);
    Outline->SetLeaderPoseComponent(Mesh); Outline->AddTickPrerequisiteComponent(Mesh);
    const auto FaceMaps = LoadFaceMaps(ImportDirectory / PendingPath, PendingAsset);
    FHonkaiFaceMaps NewOriginalFaceMaps;
    for (const auto& Entry : FaceMaps) NewOriginalFaceMaps.Add(Entry.Key, Entry.Value);
    TArray<FHonkaiSurfaceInput> SurfaceInputs;
    TArray<FLinearColor> NewSurfaceTints;
    TArray<FString> NewMaterialNames;
    int32 Missing = 0;
    for (int32 I = 0; I < Mesh->GetNumMaterials(); ++I)
    {
        UMaterialInterface* Source = Mesh->GetMaterial(I);
        FHonkaiSurfaceInput Input;
        UTexture* Texture = nullptr;
        UTexture* ToonTexture = nullptr;
        UTexture* SphereTexture = nullptr;
        float EdgeEnabled = 0.f, EdgeSize = 0.f;
        if (Source)
        {
            if (!Source->GetTextureParameterValue(TEXT("mtoon_tex_MainTex"), Texture)) Source->GetTextureParameterValue(TEXT("gltf_tex_diffuse"), Texture);
            if (!Source->GetVectorParameterValue(TEXT("mtoon_Color"), Input.Tint)) Source->GetVectorParameterValue(TEXT("gltf_basecolor"), Input.Tint);
            Source->GetScalarParameterValue(TEXT("mmd_ToonEnabled"), Input.ToonStrength);
            Source->GetVectorParameterValue(TEXT("mmd_SpecularColor"), Input.SpecularColor);
            Source->GetScalarParameterValue(TEXT("mmd_Shininess"), Input.Shininess);
            Source->GetTextureParameterValue(TEXT("mmd_ToonTexture"), ToonTexture);
            Source->GetTextureParameterValue(TEXT("mmd_SphereTexture"), SphereTexture);
            Source->GetScalarParameterValue(TEXT("mmd_SphereMode"), Input.SphereMode);
            Source->GetScalarParameterValue(TEXT("mmd_EdgeEnabled"), EdgeEnabled);
            Source->GetScalarParameterValue(TEXT("mmd_EdgeSize"), EdgeSize);
            Source->GetVectorParameterValue(TEXT("mmd_EdgeColor"), Input.OutlineColor);
        }
        Input.BaseTexture = Texture; Input.ToonRamp = Cast<UTexture2D>(ToonTexture); Input.SphereTexture = SphereTexture;
        Input.bTranslucent = Source && IsTranslucentBlendMode(Source->GetBlendMode());
        Input.bTwoSided = Source && Source->IsTwoSided();
        Input.OutlineOpacity = Input.bTranslucent || EdgeEnabled < .5f ? 0.f : Input.Tint.A * Input.OutlineColor.A;
        Input.OutlineWidth = EdgeSize * .16f;
        const FString SlotName = PendingAsset->SkeletalMesh->GetMaterials()[I].MaterialSlotName.ToString();
        Input.OutlineRegion = static_cast<EHonkaiOutlineRegion>(uint8(OutlineRegion(SlotName)));
        SurfaceInputs.Add(Input); NewSurfaceTints.Add(Input.Tint); NewMaterialNames.Add(SlotName);
        Missing += Texture == nullptr;
        UE_LOG(LogTemp, Display, TEXT("MMD_MATERIAL slot=%s texture=%s tint=%s translucent=%d two_sided=%d edge=%.0f edge_size=%.3f sphere=%.0f toon=%.0f"),
            *SlotName, *GetNameSafe(Texture), *Input.Tint.ToString(), Input.bTranslucent, Input.bTwoSided, EdgeEnabled, EdgeSize, Input.SphereMode, Input.ToonStrength);
    }
    FHonkaiRenderOptions RenderOptions;
    RenderOptions.bBaseColorOnly = FParse::Param(FCommandLine::Get(), TEXT("MmdBaseColor"));
    RenderOptions.bApplyViewPreset = true;
    FString RenderError;
    if (!Renderer || !Renderer->Bind({Mesh, Outline, ViewerCamera->GetCameraComponent()}, SurfaceInputs, NewOriginalFaceMaps, RenderOptions, RenderError))
    {
        NewActor->Destroy(); PendingAsset = nullptr;
        Status = TEXT("卡通渲染绑定失败：") + RenderError;
        Widget->OpenPage("Status"); return;
    }
    Renderer->SetLighting(KeyDirection, KeyDirection);
    SurfaceTints = MoveTemp(NewSurfaceTints); MaterialNames = MoveTemp(NewMaterialNames);
    OriginalFaceMaps = MoveTemp(NewOriginalFaceMaps); EnhancedFaceMaps.Reset();
    // The plugin owns geometry, skinning and texture decoding; this layer only sets the display material.
    if (ModelActor) ModelActor->Destroy(); ModelActor = NewActor; LoadedAsset = PendingAsset; PendingAsset = nullptr;
    ModelMesh = Mesh; OutlineMesh = Outline;
    LoadedModelPath = ImportDirectory / PendingPath; LoadedResourceRoot = ImportDirectory;
    EnhancementNotice.Empty(); bGenerationPending = false;
    ModelState = FMmdEnhancement::ReadModelState(LoadedModelPath);
    SelectedFaceMaterial = ModelState.MaterialIndex;
    if (SelectedFaceMaterial == INDEX_NONE && ModelState.ProfileDirectory.IsEmpty() && FaceMaps.Num() == 1)
        for (const auto& Entry : FaceMaps) SelectedFaceMaterial = Entry.Key;
    TSharedPtr<FJsonObject> Parameters;
    if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ModelState.ParametersJson), Parameters) && Parameters)
    {
        int32 Resolution = 512;
        if (Parameters->TryGetNumberField(TEXT("resolution"), Resolution) && (Resolution == 256 || Resolution == 512 || Resolution == 1024)) EnhancementResolution = Resolution;
    }
    bEnhanced = false;
    if (!ModelState.ProfileDirectory.IsEmpty())
    {
        const bool RestoreEnhanced = ModelState.bEnhanced;
        ApplyGeneratedProfile(ModelState.ProfileDirectory, RestoreEnhanced, ModelState);
    }
    FBoxSphereBounds Bounds = LoadedAsset->SkeletalMesh->GetBounds();
    if (auto* Render = LoadedAsset->SkeletalMesh->GetResourceForRendering(); Render && Render->LODRenderData.Num())
    {
        const auto& Positions = Render->LODRenderData[0].StaticVertexBuffers.PositionVertexBuffer;
        if (Positions.GetVertexData() && Positions.GetNumVertices())
        {
            FBox VertexBounds(ForceInit);
            for (uint32 I = 0; I < Positions.GetNumVertices(); ++I) VertexBounds += FVector(Positions.VertexPosition(I));
            Bounds = FBoxSphereBounds(VertexBounds);
            LoadedAsset->SkeletalMesh->SetImportedBounds(Bounds);
            LoadedAsset->SkeletalMesh->CalculateExtendedBounds();
            Mesh->UpdateBounds(); Outline->UpdateBounds();
        }
    }
    FRotator ModelRotation(0, -90, 0);
    Mesh->SetWorldRotation(ModelRotation);
    Mesh->SetRelativeLocation(ModelRotation.RotateVector(FVector(-Bounds.Origin.X, -Bounds.Origin.Y, Bounds.BoxExtent.Z - Bounds.Origin.Z)));
    Rig = NewObject<UVtuberRigComponent>(NewActor); Rig->RegisterComponent();
    FSHAHash RigHash; TArray<uint8> RigSource;
    FFileHelper::LoadFileToArray(RigSource, *LoadedModelPath); FSHA1::HashBuffer(RigSource.GetData(), RigSource.Num(), RigHash.Hash);
    Rig->Initialize(Mesh, FPaths::ProjectSavedDir() / TEXT("Mappings") / (RigHash.ToString().ToLower() + TEXT(".json")));
    PartVisibility = FMmdPartVisibility(); PreviewPartId.Reset(); PartVisibilityNotice.Reset();
    PartVisibilityPath = FPaths::ProjectSavedDir() / TEXT("PartVisibility") / (RigHash.ToString().ToLower() + TEXT(".json"));
    FString PartError;
    if (PartVisibility.Build(LoadedAsset->SkeletalMesh, PartError))
    {
#if WITH_DEV_AUTOMATION_TESTS
        FParse::Value(FCommandLine::Get(), TEXT("MmdPartsConfig="), PartVisibilityPath);
        FString Report;
        if (FParse::Value(FCommandLine::Get(), TEXT("MmdPartsReport="), Report) && !PartVisibility.WriteReport(Report, PartError))
            UE_LOG(LogTemp, Warning, TEXT("MMD_PARTS report failed: %s"), *PartError);
#endif
        if (FPaths::FileExists(PartVisibilityPath) && !PartVisibility.Load(PartVisibilityPath, PartError)) PartVisibilityNotice = PartError;
        if (!PartVisibility.Apply(Mesh, Outline, PartError)) PartVisibilityNotice = PartError;
        UE_LOG(LogTemp, Display, TEXT("MMD_PARTS count=%d enabled=%d hidden=%d topology=%s"), PartVisibility.Parts().Num(),
            PartVisibility.bEnabled, PartVisibility.HiddenIds.Num(), *PartVisibility.TopologySignature());
    }
    else PartVisibilityNotice = PartError;
    UE_LOG(LogTemp, Display, TEXT("VTUBER_RIG head=%s bones=%d morphs=%d missing_mappings=%d"), *Rig->GetHeadBone().ToString(), Rig->GetAvailableBones().Num(), Rig->GetAvailableMorphs().Num(), Rig->GetMissingMappings().Num());
    Mesh->RegisterOnBoneTransformsFinalizedDelegate(FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateUObject(this, &AMmdViewerController::UpdateFaceLight));
    if (Output) Output->SetSubjectComponents({Mesh, Outline});
    ModelCenter = FVector(0, 0, Bounds.BoxExtent.Z); ModelRadius = Bounds.SphereRadius;
    UE_LOG(LogTemp, Display, TEXT("MMD_BOUNDS origin=%s extent=%s radius=%.2f"), *Bounds.Origin.ToString(), *Bounds.BoxExtent.ToString(), ModelRadius);
    ModelName = FPaths::GetBaseFilename(PendingPath);
    int32 Vertices = 0, Triangles = 0;
    if (auto* Render = LoadedAsset->SkeletalMesh->GetResourceForRendering(); Render && Render->LODRenderData.Num())
    {
        Vertices = Render->LODRenderData[0].GetNumVertices();
        for (const auto& Section : Render->LODRenderData[0].RenderSections) Triangles += Section.NumTriangles;
    }
    ModelInfo = FString::Printf(TEXT("%s 个顶点\n%s 个三角形\n%d 个材质\n\nVRM4U"), *FText::AsNumber(Vertices).ToString(), *FText::AsNumber(Triangles).ToString(), Mesh->GetNumMaterials());
    Rotation = FRotator(-5, 180, 0);
    FParse::Value(FCommandLine::Get(), TEXT("MmdViewYaw="), Rotation.Yaw);
#if WITH_DEV_AUTOMATION_TESTS
    float ViewPitch = Rotation.Pitch;
    if (FParse::Value(FCommandLine::Get(), TEXT("MmdViewPitch="), ViewPitch) && FMath::IsFinite(ViewPitch))
        Rotation.Pitch = FMath::Clamp(ViewPitch, -80.f, 80.f);
#endif
    FocusModel();
    float FrameScale = 1.f, TargetHeight = 1.f;
    FParse::Value(FCommandLine::Get(), TEXT("MmdFrameScale="), FrameScale);
    FParse::Value(FCommandLine::Get(), TEXT("MmdTargetHeight="), TargetHeight);
    Distance *= FMath::Clamp(FrameScale, 0.1f, 2.f);
    Target.Z = ModelCenter.Z * FMath::Clamp(TargetHeight, 0.f, 2.f);
    UpdateCamera();
    Status = Missing ? FString::Printf(TEXT("已加载。%d 个材质未匹配到基础贴图，使用材质颜色。"), Missing) : TEXT("已加载。中键旋转；WASD 水平移动，E 上移，Q 下移。");
    UE_LOG(LogTemp, Display, TEXT("MMD_LOADED plugin=VRM4U name=%s vertices=%d triangles=%d missing=%d textures=%d"), *ModelName, Vertices, Triangles, Missing, LoadedAsset->Textures.Num());
    Widget->CloseWindow();
    Widget->bHudVisible = bHudVisibleBeforeImport;
    bool bAllowCamera = true;
#if WITH_DEV_AUTOMATION_TESTS
    bAllowCamera = !bTrackingReplay;
    ReplayStartWorldSeconds = GetWorld()->GetTimeSeconds();
    ReplayFrameIndex = INDEX_NONE;
#endif
    if (bAllowCamera && FParse::Param(FCommandLine::Get(), TEXT("VtuberCamera")) && !Tracking.IsRunning()) Tracking.Start();
    if (Output && FParse::Param(FCommandLine::Get(), TEXT("VtuberSpout"))) Output->SetEnabled(true);
    FString Capture;
    if (FParse::Value(FCommandLine::Get(), TEXT("MmdCapture="), Capture))
    {
        float CaptureDelay = 8.f, ExitDelay = 12.f;
#if WITH_DEV_AUTOMATION_TESTS
        CaptureDelay = MmdQaCaptureDelay(FCommandLine::Get());
        ExitDelay = CaptureDelay + 4.f;
        FString CapturePage;
        if (FParse::Value(FCommandLine::Get(), TEXT("MmdCapturePage="), CapturePage))
        {
            const FName Page = CapturePage.Equals(TEXT("Mapping"), ESearchCase::IgnoreCase) ? FName(TEXT("Mapping"))
                : CapturePage.Equals(TEXT("Capture"), ESearchCase::IgnoreCase) ? FName(TEXT("Tracking"))
                : CapturePage.Equals(TEXT("Nav"), ESearchCase::IgnoreCase) ? FName(TEXT("Main"))
                : CapturePage.Equals(TEXT("Results"), ESearchCase::IgnoreCase) ? FName(TEXT("Results"))
                : CapturePage.Equals(TEXT("Calibration"), ESearchCase::IgnoreCase) ? FName(TEXT("Calibration"))
                : CapturePage.Equals(TEXT("MappingOptions"), ESearchCase::IgnoreCase) ? FName(TEXT("MappingOptions"))
                : CapturePage.Equals(TEXT("Settings"), ESearchCase::IgnoreCase) ? FName(TEXT("Settings"))
                : CapturePage.Equals(TEXT("Output"), ESearchCase::IgnoreCase) ? FName(TEXT("Output"))
                : CapturePage.Equals(TEXT("About"), ESearchCase::IgnoreCase) ? FName(TEXT("About"))
                : CapturePage.Equals(TEXT("Parts"), ESearchCase::IgnoreCase) ? FName(TEXT("Parts")) : NAME_None;
            if (!Page.IsNone())
            {
                TWeakObjectPtr<AMmdViewerController> Weak(this);
                FTimerHandle PageHandle;
                GetWorldTimerManager().SetTimer(PageHandle, [Weak, Page]
                {
                    if (auto* Self = Weak.Get(); Self && Self->Widget.IsValid())
                    {
                        Self->Widget->OpenPage(Page);
                        UE_LOG(LogTemp, Display, TEXT("MMD_CAPTURE_PAGE page=%s"), *Page.ToString());
                    }
                }, FMath::Max(.01f, CaptureDelay - 2.f), false);
            }
            else { UE_LOG(LogTemp, Warning, TEXT("MMD_CAPTURE_PAGE rejected unsupported page=%s"), *CapturePage); }
        }
#endif
        FTimerHandle Handle;
#if WITH_DEV_AUTOMATION_TESTS
        TWeakObjectPtr<AMmdViewerController> Weak(this);
        GetWorldTimerManager().SetTimer(Handle, [Capture, Weak]
        {
            if (auto* Self = Weak.Get(); Self && Self->bReplaySequenceRequested)
            {
                UE_LOG(LogTemp, Display, TEXT("VTUBER_REPLAY_CAPTURE frame=%d blink_left=%.5f blink_right=%.5f"), Self->ReplayFrameIndex,
                    Self->ReplaySnapshot.BlendShapes.FindRef(TEXT("eyeBlinkLeft")), Self->ReplaySnapshot.BlendShapes.FindRef(TEXT("eyeBlinkRight")));
                if (Self->ModelMesh && Self->Rig)
                {
                    auto* Asset = Self->ModelMesh->GetSkeletalMeshAsset();
                    const auto& ImagePoints = Self->ReplaySnapshot.PoseLandmarks;
                    UE_LOG(LogTemp, Display, TEXT("VTUBER_QA_SOURCE image_points=%d elbow14_y=%.6f wrist16_y=%.6f"), ImagePoints.Num(),
                        ImagePoints.IsValidIndex(14) ? ImagePoints[14].Position.Y : -1., ImagePoints.IsValidIndex(16) ? ImagePoints[16].Position.Y : -1.);
                    for (const TCHAR* Channel : {TEXT("spine"), TEXT("chest"), TEXT("upperarm_l"), TEXT("upperarm_r"), TEXT("lowerarm_l"), TEXT("lowerarm_r")})
                        UE_LOG(LogTemp, Display, TEXT("VTUBER_QA_BODY channel=%s tracked=%d input=%.6f output_deg=%.6f"), Channel,
                            Self->Rig->IsBodyChannelTracked(Channel), Self->Rig->GetChannelInput(Channel), Self->Rig->GetBodyChannelOutputDegrees(Channel));
                    for (const auto& Mapping : Self->Rig->GetMappings())
                    {
                        if (Mapping.Channel != TEXT("eyeBlinkLeft") && Mapping.Channel != TEXT("eyeBlinkRight")
                            && Mapping.Channel != TEXT("jawOpen") && Mapping.Channel != TEXT("mouthPucker")
                            && Mapping.Channel != TEXT("mouthFunnel") && Mapping.Channel != TEXT("mouthSmileLeft")
                            && Mapping.Channel != TEXT("mouthSmileRight")) continue;
                        const auto* Morph = Asset ? Asset->FindMorphTarget(Mapping.Morph) : nullptr;
                        FString Sections;
                        if (Morph && !Morph->GetMorphLODModels().IsEmpty())
                            for (int32 Section : Morph->GetMorphLODModels()[0].SectionIndices)
                                Sections += FString::Printf(TEXT("%s%d"), Sections.IsEmpty() ? TEXT("") : TEXT(","), Section);
                        UE_LOG(LogTemp, Display, TEXT("VTUBER_RUNTIME_MORPH with_editor=%d channel=%s target=%s output=%.6f mesh_curve=%.6f sections=[%s]"),
                            WITH_EDITOR, *Mapping.Channel.ToString(), *Mapping.Morph.ToString(), Self->Rig->GetChannelOutput(Mapping.Channel),
                            Self->ModelMesh->GetMorphTarget(Mapping.Morph), *Sections);
                    }
                }
            }
            FScreenshotRequest::RequestScreenshot(Capture, true, false);
        }, CaptureDelay, false);
#else
        GetWorldTimerManager().SetTimer(Handle, [Capture] { FScreenshotRequest::RequestScreenshot(Capture, true, false); }, CaptureDelay, false);
#endif
        if (FParse::Param(FCommandLine::Get(), TEXT("MmdTestExit")))
        {
            FTimerHandle ExitHandle;
            GetWorldTimerManager().SetTimer(ExitHandle, [] { FGenericPlatformMisc::RequestExit(false); }, ExitDelay, false);
        }
    }
}

void AMmdViewerController::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const float FrameSeconds = float(FApp::GetDeltaTime());
    if (FMath::IsFinite(FrameSeconds) && FrameSeconds > 0)
    {
        RenderFrameSeconds = RenderFrameSeconds > 0 ? FMath::Lerp(RenderFrameSeconds, FrameSeconds, 1.f - FMath::Exp(-FrameSeconds / .3f)) : FrameSeconds;
        DisplayRenderFps = 1.f / RenderFrameSeconds;
    }
#if WITH_DEV_AUTOMATION_TESTS
    int32 SingleUpscalingMode = INDEX_NONE;
    if (ModelMesh && UpscalingQaStep == 0 && FParse::Value(FCommandLine::Get(), TEXT("HonkaiUpscalingMode="), SingleUpscalingMode))
    {
        SetUpscaling(static_cast<EHonkaiUpscalingMode>(SingleUpscalingMode));
        UpscalingQaStep = 100;
    }
    if (ModelMesh && FParse::Param(FCommandLine::Get(), TEXT("HonkaiUpscalingQa")))
    {
        if (UpscalingQaStart == 0) UpscalingQaStart = FPlatformTime::Seconds();
        if (UpscalingQaStep < 3 && FPlatformTime::Seconds() - UpscalingQaStart > 4.0 + 5.0 * UpscalingQaStep)
        {
            const EHonkaiUpscalingMode Modes[] = { EHonkaiUpscalingMode::DLAA, EHonkaiUpscalingMode::DLSSQuality, EHonkaiUpscalingMode::TSR };
            auto Signature = []
            {
                FString Value;
                for (const TCHAR* Name : { TEXT("r.AntiAliasingMethod"), TEXT("r.ScreenPercentage"), TEXT("r.TemporalAA.Upscaler"),
                    TEXT("r.TemporalAA.Upsampling"), TEXT("r.DynamicRes.OperationMode"), TEXT("r.NGX.DLSS.Preset"), TEXT("r.NGX.DLSS.Enable") })
                {
                    const auto* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
                    Value += FString::Printf(TEXT("%s=%s@%x;"), Name, Variable ? *Variable->GetString() : TEXT("missing"),
                        Variable ? uint32(Variable->GetFlags() & ECVF_SetByMask) : 0u);
                }
                return Value;
            };
            if (UpscalingQaStep == 0) UpscalingQaBaseline = Signature();
            SetUpscaling(Modes[UpscalingQaStep++]);
            if (UpscalingQaStep == 3)
                UE_LOG(LogTemp, Display, TEXT("HONKAI_UPSCALING_QA_RESTORE ok=%d before=%s after=%s"),
                    UpscalingQaBaseline == Signature(), *UpscalingQaBaseline, *Signature());
        }
    }
#endif
    Enhancement.Tick();
    HandleGenerationStatus(Enhancement.GetStatus());
    FVtuberTrackingSnapshot Snapshot;
#if WITH_DEV_AUTOMATION_TESTS
    if (bTrackingReplay)
    {
        const bool bHasFrame = !bReplaySequenceRequested
            || ReplaySequence.Sample(GetWorld()->GetTimeSeconds() - ReplayStartWorldSeconds, ReplaySnapshot);
        if (bHasFrame && Rig)
        {
            if (bReplaySequenceRequested) ReplayFrameIndex = ReplaySequence.FrameIndex(GetWorld()->GetTimeSeconds() - ReplayStartWorldSeconds);
            ReplaySnapshot.TimestampSeconds = FPlatformTime::Seconds();
            RecordTrackingFrame(ReplaySnapshot);
            Rig->SubmitSnapshot(ReplaySnapshot);
        }
    }
    else
#endif
    if (Tracking.Poll(Snapshot) && Rig) { RecordTrackingFrame(Snapshot); Rig->SubmitSnapshot(Snapshot); }
#if WITH_DEV_AUTOMATION_TESTS
    if (!bTrackingReplay && Rig && FParse::Param(FCommandLine::Get(), TEXT("VtuberPhysicsProbe")))
    {
        const double Time = GetWorld()->GetTimeSeconds();
        Snapshot.TimestampSeconds = FPlatformTime::Seconds(); Snapshot.bFaceTracked = true;
        Snapshot.HeadRotation = FRotator(8 * FMath::Sin(Time), 25 * FMath::Sin(Time * 1.7), 6 * FMath::Sin(Time * 1.3)).Quaternion();
        Rig->SubmitSnapshot(Snapshot);
    }
    if (!bTrackingReplay && Rig && FParse::Param(FCommandLine::Get(), TEXT("VtuberRigProbe")))
    {
        // Explicit development probe, never used by the interactive camera mode.
        Snapshot.TimestampSeconds = FPlatformTime::Seconds(); Snapshot.bFaceTracked = true;
        Snapshot.HeadRotation = FRotator(8, 25, 0).Quaternion();
        Snapshot.BlendShapes.Add(TEXT("jawOpen"), .65f);
        Snapshot.BlendShapes.Add(TEXT("eyeBlinkLeft"), 1.f); Snapshot.BlendShapes.Add(TEXT("eyeBlinkRight"), 1.f);
        Rig->SubmitSnapshot(Snapshot);
    }
#endif
    if (Output) Output->Tick(DeltaSeconds);
}

bool AMmdViewerController::SavePartVisibility()
{
    FString Error;
    if (!HasParts() || PartVisibilityPath.IsEmpty() || !PartVisibility.Save(PartVisibilityPath, Error))
    { PartVisibilityNotice = Error.IsEmpty() ? TEXT("尚无可配置的部件。") : Error; return false; }
    PreviewPartId.Reset();
    if (!PartVisibility.Apply(ModelMesh, OutlineMesh, Error)) { PartVisibilityNotice = Error; return false; }
    PartVisibilityNotice = TEXT("已保存本机设置。关闭“启用部件隐藏”可恢复原始显示。");
    return true;
}

void AMmdViewerController::SetPartVisibilityEnabled(bool bEnabled)
{
    const bool Previous = PartVisibility.bEnabled;
    PartVisibility.bEnabled = bEnabled;
    if (!SavePartVisibility()) PartVisibility.bEnabled = Previous;
}

void AMmdViewerController::SetPartHidden(const FString& Id, bool bPartHidden)
{
    if (!PartVisibility.Parts().ContainsByPredicate([&](const FMmdVisiblePart& Part) { return Part.Id == Id; })) return;
    const TSet<FString> Previous = PartVisibility.HiddenIds;
    if (bPartHidden) PartVisibility.HiddenIds.Add(Id); else PartVisibility.HiddenIds.Remove(Id);
    if (!SavePartVisibility()) PartVisibility.HiddenIds = Previous;
}

void AMmdViewerController::RestoreParts()
{
    const TSet<FString> Previous = PartVisibility.HiddenIds;
    PartVisibility.HiddenIds.Reset();
    if (!SavePartVisibility()) PartVisibility.HiddenIds = Previous;
}

void AMmdViewerController::PreviewPart(const FString& Id)
{
    if (!HasParts() || !ModelMesh) return;
    FString Error;
    if (PartVisibility.Apply(ModelMesh, OutlineMesh, Error, Id)) PreviewPartId = Id;
    else PartVisibilityNotice = Error;
}

bool AMmdViewerController::IsTrackingReplay() const
{
#if WITH_DEV_AUTOMATION_TESTS
    return bTrackingReplay;
#else
    return false;
#endif
}
FString AMmdViewerController::TrackingActionLabel()
{
    return Tracking.IsStopping() ? TEXT("正在停止…") : Tracking.IsRunning() ? TEXT("停止捕捉") : TEXT("开启动捕");
}
void AMmdViewerController::StopTrackingReplay()
{
#if WITH_DEV_AUTOMATION_TESTS
    if (!bTrackingReplay) return;
    bTrackingReplay = bReplaySequenceRequested = false;
    ReplaySequence = {}; ReplaySnapshot = {}; ReplayFrameIndex = INDEX_NONE;
    LastTrackingFrame = 0; LastTrackingSnapshot = {};
    bFaceSeen = bPoseSeen = false; HandsSeen = 0;
    if (Rig) Rig->SubmitSnapshot({});
    Tracking.Status = TEXT("离线回放已停止");
#endif
}
void AMmdViewerController::ToggleTracking()
{
    StopTrackingReplay();
    if (Tracking.IsRunning()) { Tracking.Stop(); LastTrackingFrame = 0; LastTrackingSnapshot = {}; bFaceSeen = bPoseSeen = false; HandsSeen = 0; }
    else if (Rig) Tracking.Start();
    else Tracking.Status = TEXT("请先导入角色");
}
void AMmdViewerController::RecordTrackingFrame(const FVtuberTrackingSnapshot& Snapshot)
{
    LastTrackingFrame = FPlatformTime::Seconds();
    LastTrackingSnapshot = Snapshot;
    bFaceSeen = Snapshot.bFaceTracked;
    bPoseSeen = Snapshot.bPoseTracked && Snapshot.PoseLandmarks.Num() >= 33;
    HandsSeen = 0;
    for (const FVtuberHandTracking& Hand : Snapshot.Hands)
        if (Hand.Score > .05f && (Hand.Landmarks.Num() >= 21 || Hand.WorldLandmarks.Num() >= 21)) ++HandsSeen;
    HandsSeen = FMath::Min(HandsSeen, 2);
}
FString AMmdViewerController::TrackingSummary()
{
    bool bActive = Tracking.IsRunning();
#if WITH_DEV_AUTOMATION_TESTS
    bActive |= bTrackingReplay;
#endif
    const bool bFresh = bActive && LastTrackingFrame > 0 && FPlatformTime::Seconds() - LastTrackingFrame < .5;
    return FString::Printf(TEXT("面部 %s · 上身 %s · 双手 %d/2"),
        bFresh && bFaceSeen ? TEXT("已识别") : TEXT("未识别"),
        bFresh && bPoseSeen ? TEXT("已识别") : TEXT("未识别"), bFresh ? HandsSeen : 0);
}
void AMmdViewerController::ToggleSpout()
{
    if (Output && ModelMesh) Output->SetEnabled(!Output->IsEnabled());
}
void AMmdViewerController::UpdateFaceLight()
{
#if WITH_DEV_AUTOMATION_TESTS
    if (ModelMesh && LoadedAsset && LoadedAsset->PmxPhysicsData && FParse::Param(FCommandLine::Get(), TEXT("VtuberPhysicsProbe")))
    {
        static double LastReport = 0;
        const double Now = FPlatformTime::Seconds();
        if (Now - LastReport >= 1.)
        {
            LastReport = Now;
            const auto& ComponentPose = ModelMesh->GetComponentSpaceTransforms();
            const auto& Ref = ModelMesh->GetSkeletalMeshAsset()->GetRefSkeleton();
            const auto& Rest = Ref.GetRefBonePose();
            int32 Moved = 0, Sampled = 0;
            float MaxAngle = 0, MaxDistance = 0;
            for (const auto& Body : LoadedAsset->PmxPhysicsData->RigidBodies)
            {
                const int32 Bone = Body.Mode ? Ref.FindBoneIndex(Body.BoneName) : INDEX_NONE;
                if (!ComponentPose.IsValidIndex(Bone) || !Rest.IsValidIndex(Bone)) continue;
                const int32 Parent = Ref.GetParentIndex(Bone);
                const FTransform Local = Parent == INDEX_NONE ? ComponentPose[Bone] : ComponentPose[Bone].GetRelativeTransform(ComponentPose[Parent]);
                ++Sampled;
                const float Angle = FMath::RadiansToDegrees(Local.GetRotation().AngularDistance(Rest[Bone].GetRotation()));
                const float LocalShiftCm = FVector::Distance(Local.GetLocation(), Rest[Bone].GetLocation());
                Moved += Angle > .01f || LocalShiftCm > .001f;
                MaxAngle = FMath::Max(MaxAngle, Angle); MaxDistance = FMath::Max(MaxDistance, LocalShiftCm);
            }
            UE_LOG(LogTemp, Display, TEXT("PMX_PHYSICS_PROBE sampled=%d moved_locals=%d max_angle=%.3f max_local_cm=%.4f"), Sampled, Moved, MaxAngle, MaxDistance);
        }
    }
#endif

    if (!Rig) return;
    const FVector Direction = Rig->GetFaceLightDirection(KeyDirection);
    if (Renderer) Renderer->SetLighting(KeyDirection, Direction);
}
void AMmdViewerController::MoveCharacter(FVector Direction, float DeltaSeconds)
{
    if (!ModelActor || !ViewerCamera || bBusy || Direction.ContainsNaN() || !FMath::IsFinite(DeltaSeconds))
    { StopCharacterMovement(); return; }
    const float Delta = FMath::Clamp(DeltaSeconds, 0.f, .1f);
    if (Delta <= 0.f) return;
    const FRotator Yaw(0, ViewerCamera->GetActorRotation().Yaw, 0);
    const FVector DesiredVelocity = Yaw.RotateVector(Direction.GetClampedToMaxSize(1.f)) * 50.f;
    const float Seconds = Direction.IsNearlyZero() ? .08f : .10f;
    const float Keep = FMath::Exp(-Delta / Seconds);
    // Integrate the exponential ramp so travel stays consistent across frame rates.
    const FVector Offset = DesiredVelocity * Delta + (MovementVelocity - DesiredVelocity) * Seconds * (1.f - Keep);
    MovementVelocity = DesiredVelocity + (MovementVelocity - DesiredVelocity) * Keep;
    if (MovementVelocity.IsNearlyZero(.001f)) MovementVelocity = FVector::ZeroVector;
    ModelActor->AddActorWorldOffset(Offset); ModelCenter += Offset;
}

void AMmdViewerController::HandleGenerationStatus(const FMmdEnhancementStatus& Progress)
{
    // Only the request started for this loaded model may commit a terminal result.
    if (!bGenerationPending || Progress.State == EMmdEnhancementState::Running || Progress.State == EMmdEnhancementState::Idle) return;
    bGenerationPending = false;
    if (Progress.State == EMmdEnhancementState::NeedsMaterial)
    {
        EnhancementNotice = TEXT("无法可靠确定面部材质，请在预览中选择。");
        if (Widget) Widget->OpenPage("FaceMaterial");
    }
    else if (Progress.State == EMmdEnhancementState::Succeeded)
    {
        EnhancementNotice = TEXT("正在检查并应用增强数据…");
        FMmdModelState CandidateState = ModelState;
        CandidateState.MaterialIndex = GenerationRequest.MaterialIndex;
        CandidateState.ParametersJson = GenerationRequest.ParametersJson;
        EnhancementNotice.Empty();
        if (ApplyGeneratedProfile(Progress.ResultDirectory, true, CandidateState))
        {
            if (EnhancementNotice.IsEmpty()) EnhancementNotice = Progress.bCacheHit ? TEXT("已复用有效缓存并应用增强效果。") : TEXT("增强效果已生成并应用。");
            UE_LOG(LogTemp, Display, TEXT("MMD_ENHANCEMENT_APPLIED cache_hit=%d profile=%s"), Progress.bCacheHit, *Progress.ResultDirectory);
        }
        else if (EnhancementNotice.IsEmpty()) EnhancementNotice = TEXT("增强数据校验失败，原有效果保持不变。");
    }
}

FString AMmdViewerController::GenerationText() const
{
    if (!EnhancementNotice.IsEmpty()) return EnhancementNotice;
    const auto& Progress = Enhancement.GetStatus();
    if (GenerationRequest.ModelPath != LoadedModelPath || Progress.State == EMmdEnhancementState::Idle) return TEXT("生成面部卡通阴影与鼻部控制数据。模型保持可查看，完成后再应用。");
    return Progress.Stage.IsEmpty() ? Progress.Message : Progress.Stage + TEXT("\n") + Progress.Message;
}

void AMmdViewerController::GenerateEnhancement(bool bForce, int32 Material)
{
    if (Enhancement.IsRunning()) return;
    if (!LoadedAsset || LoadedModelPath.IsEmpty()) { EnhancementNotice = TEXT("请先导入模型。"); return; }
    ClearMaterialPreview();
    if (Material != INDEX_NONE) SelectedFaceMaterial = Material;
    FMmdEnhancementRequest Request;
    Request.ModelPath = LoadedModelPath;
    Request.ResourceRoot = LoadedResourceRoot;
    Request.MaterialIndex = SelectedFaceMaterial;
    Request.ParametersJson = FString::Printf(TEXT("{\"resolution\":%d}"), EnhancementResolution);
    Request.bForce = bForce;
    EnhancementNotice.Empty();
    GenerationRequest = Request;
    bGenerationPending = Enhancement.Start(Request);
}

void AMmdViewerController::CancelEnhancement()
{
    EnhancementNotice.Empty();
    Enhancement.Cancel();
}

bool AMmdViewerController::ApplyGeneratedProfile(const FString& Directory, bool bEnabled, const FMmdModelState& CandidateState)
{
    if (!LoadedAsset || !Renderer || !Renderer->IsBound()) return false;
    const auto Maps = LoadFaceMaps(LoadedModelPath, LoadedAsset, Directory / TEXT("profile.json"));
    if (Maps.IsEmpty()) return false;
    FHonkaiFaceMaps NewMaps;
    for (const auto& Entry : Maps)
    {
        if (!SurfaceTints.IsValidIndex(Entry.Key)) return false;
        NewMaps.Add(Entry.Key, Entry.Value);
    }
    FMmdModelState NextState = CandidateState;
    NextState.ProfileDirectory = Directory;
    return CommitGeneratedProfile(NewMaps, NextState, bEnabled);
}

bool AMmdViewerController::CommitGeneratedProfile(const TMap<int32, TObjectPtr<UTexture2D>>& Maps,
    const FMmdModelState& CandidateState, bool bEnabled)
{
    if (Maps.IsEmpty() || !Renderer || !Renderer->IsBound()) return false;
    FString Error;
    if (!Renderer->PrepareFaceMaps(Maps, Error) || !Renderer->SetFaceMaps(bEnabled ? Maps : OriginalFaceMaps, Error))
    { EnhancementNotice = Error; return false; }
    ClearMaterialPreview();
    EnhancedFaceMaps = Maps;
    ModelState = CandidateState;
    bEnhanced = bEnabled;
    ModelState.bEnhanced = bEnabled;
    SaveEffectChoice();
    return true;
}

void AMmdViewerController::SetEnhanced(bool bEnabled)
{
    ClearMaterialPreview();
    if (bEnabled && EnhancedFaceMaps.IsEmpty()) { EnhancementNotice = TEXT("尚无增强数据，请先生成增强效果。"); return; }
    FString Error;
    if (!Renderer || !Renderer->SetFaceMaps(bEnabled ? EnhancedFaceMaps : OriginalFaceMaps, Error))
    { EnhancementNotice = Error.IsEmpty() ? TEXT("卡通渲染尚未就绪。") : Error; return; }
    bEnhanced = bEnabled;
    SaveEffectChoice();
}

void AMmdViewerController::SaveEffectChoice()
{
    if (LoadedModelPath.IsEmpty()) return;
    ModelState.bEnhanced = bEnhanced;
    // Pending UI choices must not relabel the profile that is currently applied.
    FString Error;
    if (!FMmdEnhancement::SaveModelState(LoadedModelPath, ModelState, Error)) EnhancementNotice = Error;
}

void AMmdViewerController::PreviewMaterial(int32 Index)
{
    if (Enhancement.IsRunning() || !Renderer || !Renderer->IsBound() || !SurfaceTints.IsValidIndex(Index)) return;
    ClearMaterialPreview();
    if (Renderer->SetTint(Index, FLinearColor(.16f, .85f, .58f, SurfaceTints[Index].A))) PreviewMaterialIndex = Index;
}

void AMmdViewerController::ClearMaterialPreview()
{
    if (Renderer && SurfaceTints.IsValidIndex(PreviewMaterialIndex)) Renderer->SetTint(PreviewMaterialIndex, SurfaceTints[PreviewMaterialIndex]);
    PreviewMaterialIndex = INDEX_NONE;
}

void AMmdViewerController::SetEnhancementResolution(int32 Resolution)
{
    if (Enhancement.IsRunning() || (Resolution != 256 && Resolution != 512 && Resolution != 1024) || Resolution == EnhancementResolution) return;
    EnhancementResolution = Resolution;
    EnhancementNotice = TEXT("下次生成将使用新尺寸；当前效果与已保存配置保持不变。");
}

void AMmdViewerController::SetBackground(int32 Preset)
{
    const FLinearColor Colors[] = {FLinearColor(.046665f, .046665f, .046665f), FLinearColor(.72f, .73f, .74f), FLinearColor(.025f, .03f, .04f)};
    if (BackdropMaterial && Preset >= 0 && Preset < UE_ARRAY_COUNT(Colors)) BackdropMaterial->SetVectorParameterValue(TEXT("Tint"), Colors[Preset]);
}

void AMmdViewerController::SetUpscaling(EHonkaiUpscalingMode Mode)
{
    const FIntPoint Size = GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport
        ? GEngine->GameViewport->Viewport->GetSizeXY() : FIntPoint::ZeroValue;
    const bool Accepted = Upscaling.SetMode(Mode, FVector2D(Size.X, Size.Y), UpscalingNotice);
    UE_LOG(LogTemp, Display, TEXT("HONKAI_UPSCALING requested=%d accepted=%d active=%d size=%dx%d reason=%s"),
        int32(Mode), Accepted, int32(Upscaling.GetMode()), Size.X, Size.Y, *UpscalingNotice);
}

FString AMmdViewerController::UpscalingStatus() const
{
    const TCHAR* Name = Upscaling.GetMode() == EHonkaiUpscalingMode::DLAA ? TEXT("NVIDIA DLAA")
        : Upscaling.GetMode() == EHonkaiUpscalingMode::DLSSQuality ? TEXT("NVIDIA DLSS 4.5 · 质量") : TEXT("TSR · DLSS 已关闭");
    FString Reason;
    FHonkaiUpscaling::IsSupported(Reason);
    return FString(Name) + (UpscalingNotice.IsEmpty() ? (Reason.IsEmpty() ? FString() : TEXT("\n") + Reason) : TEXT("\n") + UpscalingNotice);
}

void AMmdViewerController::SetRenderScale(int32 Percent)
{
    if (Percent != 100 && Percent != 150) return;
    Upscaling.Restore(); UpscalingNotice.Empty();
    if (auto* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"))) Variable->Set(Percent, ECVF_SetByCode);
}

void AMmdViewerController::ClearEnhancementCache()
{
    FString Error;
    if (!FMmdEnhancement::ClearCache(Error)) { EnhancementNotice = Error; return; }
    SetEnhanced(false);
    EnhancedFaceMaps.Reset(); ModelState.ProfileDirectory.Empty();
    EnhancementNotice.Empty();
    SaveEffectChoice();
    if (EnhancementNotice.IsEmpty()) EnhancementNotice = TEXT("生成缓存已清理。导入的完整资源和原始文件均已保留。");
}

void AMmdViewerController::ExportEnhancement()
{
    if (ModelState.ProfileDirectory.IsEmpty()) { EnhancementNotice = TEXT("请先生成增强数据，再导出。"); return; }
#if PLATFORM_WINDOWS
    IFileOpenDialog* Dialog = nullptr;
    const HRESULT Init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&Dialog))))
    {
        Dialog->SetTitle(L"选择增强数据导出的父目录");
        Dialog->SetOptions(FOS_PICKFOLDERS | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);
        const void* Parent = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(Widget);
        if (SUCCEEDED(Dialog->Show(static_cast<HWND>(const_cast<void*>(Parent)))))
        {
            IShellItem* Item = nullptr;
            if (SUCCEEDED(Dialog->GetResult(&Item)))
            {
                PWSTR Folder = nullptr;
                if (SUCCEEDED(Item->GetDisplayName(SIGDN_FILESYSPATH, &Folder)))
                {
                    const FString Destination = FString(Folder) / (FPaths::GetBaseFilename(LoadedModelPath) + TEXT("-增强-") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
                    CoTaskMemFree(Folder);
                    FString Error;
                    EnhancementNotice = FMmdEnhancement::Export(LoadedModelPath, ModelState.ProfileDirectory, Destination, Error)
                        ? TEXT("已导出到：\n") + Destination + TEXT("\n将目录内的配置和 PNG 放到相同 PMX 旁，再一起打包即可。") : Error;
                }
                Item->Release();
            }
        }
        Dialog->Release();
    }
    else EnhancementNotice = TEXT("无法打开导出目录选择器。");
    if (SUCCEEDED(Init)) CoUninitialize();
#endif
}

void AMmdViewerController::UpdateCamera()
{
	if (!ViewerCamera) return;
	ViewerCamera->SetActorLocationAndRotation(Target - Rotation.Vector() * Distance, Rotation);
    if (ShowcaseLight)
    {
        float ReferenceYaw = Rotation.Yaw;
#if WITH_DEV_AUTOMATION_TESTS
        if (FParse::Param(FCommandLine::Get(), TEXT("MmdWorldLight"))) ReferenceYaw = 180.f;
#endif
        ShowcaseLight->SetActorRotation(FRotator(ShowcaseLightPitch, ReferenceYaw + ShowcaseLightYawOffset, 0));
        KeyDirection = -ShowcaseLight->GetActorForwardVector();
    }
	ViewerCamera->GetCameraComponent()->ProjectionMode = bOrthographic ? ECameraProjectionMode::Orthographic : ECameraProjectionMode::Perspective;
	ViewerCamera->GetCameraComponent()->OrthoWidth = Distance * 0.65f;
}
void AMmdViewerController::Orbit(FVector2D Delta, bool Pan, bool Dolly)
{
	if (Dolly) { Zoom(-Delta.Y * 0.03f); return; }
	if (Pan) { Target += (FRotationMatrix(Rotation).GetUnitAxis(EAxis::Y) * -Delta.X + FRotationMatrix(Rotation).GetUnitAxis(EAxis::Z) * Delta.Y) * Distance * 0.0008f; }
	else { Rotation.Yaw += Delta.X * 0.3f; Rotation.Pitch = FMath::Clamp(Rotation.Pitch - Delta.Y * 0.3f, -89.9f, 89.9f); }
	UpdateCamera();
}
void AMmdViewerController::Zoom(float Delta) { Distance = FMath::Clamp(Distance * FMath::Exp(-Delta * 0.12f), FMath::Max(1.f, ModelRadius * 0.08f), FMath::Max(1000.f, ModelRadius * 30)); UpdateCamera(); }
void AMmdViewerController::FocusModel() { Target = ModelCenter; Distance = FMath::Max(10.f, ModelRadius * 5.4f); UpdateCamera(); }
void AMmdViewerController::SetView(int32 View, bool Reverse)
{
	if (View == 1) Rotation = FRotator(0, Reverse ? 0 : 180, 0);
	if (View == 3) Rotation = FRotator(0, Reverse ? 90 : -90, 0);
	if (View == 7) Rotation = FRotator(Reverse ? 90 : -90, 180, 0);
	bOrthographic = true; UpdateCamera();
}
void AMmdViewerController::ToggleProjection() { bOrthographic = !bOrthographic; UpdateCamera(); }
