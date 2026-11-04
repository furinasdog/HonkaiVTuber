#include "MmdNotices.h"
#include "MmdUiStyle.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace
{
    bool ReadNotice(const FString& Path, FString& Text)
    {
        const int64 Size = IFileManager::Get().FileSize(*Path);
        if (Size < 0 || Size > 1024 * 1024) return false;
        TArray<uint8> Bytes;
        if (!FFileHelper::LoadFileToArray(Bytes, *Path)) return false;
#if PLATFORM_WINDOWS
        // Legacy Chinese MMD readmes often use GBK without a BOM.
        if (Bytes.Num() > 0 && Bytes[0] != 0xff && Bytes[0] != 0xfe)
        {
            int32 Count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, reinterpret_cast<const char*>(Bytes.GetData()), Bytes.Num(), nullptr, 0);
            const uint32 Encoding = Count ? CP_UTF8 : 936;
            if (!Count) Count = MultiByteToWideChar(Encoding, 0, reinterpret_cast<const char*>(Bytes.GetData()), Bytes.Num(), nullptr, 0);
            if (Count > 0)
            {
                Text.GetCharArray().SetNumUninitialized(Count + 1);
                MultiByteToWideChar(Encoding, 0, reinterpret_cast<const char*>(Bytes.GetData()), Bytes.Num(), Text.GetCharArray().GetData(), Count);
                Text.GetCharArray()[Count] = 0;
                Text.RemoveFromStart(FString::Chr(0xfeff));
                return true;
            }
        }
#endif
        FFileHelper::BufferToString(Text, Bytes.GetData(), Bytes.Num());
        return true;
    }
}

bool FMmdFirstRunState::Load(const FString& Path)
{
    AcceptedHash.Reset(); bGuideSeen = false;
    FString Text;
    if (!ReadNotice(Path, Text)) return false;
    TSharedPtr<FJsonObject> Object;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object) || !Object) return false;
    Object->TryGetStringField(TEXT("accepted_hash"), AcceptedHash);
    Object->TryGetBoolField(TEXT("guide_seen"), bGuideSeen);
    return AcceptedHash.Len() == 40;
}

bool FMmdFirstRunState::Save(const FString& Path, FString& Error) const
{
    Error.Reset();
    auto Object = MakeShared<FJsonObject>();
    Object->SetStringField(TEXT("accepted_hash"), AcceptedHash);
    Object->SetBoolField(TEXT("guide_seen"), bGuideSeen);
    FString Text;
    FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Text));
    const FString Full = FPaths::ConvertRelativePathToFull(Path);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Full), true);
    const FString Temp = Full + TEXT(".tmp-") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    if (FFileHelper::SaveStringToFile(Text, *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
#if PLATFORM_WINDOWS
        if (::MoveFileExW(*Temp, *Full, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
#endif
    }
    IFileManager::Get().Delete(*Temp);
    Error = TEXT("无法保存协议确认，请检查用户数据目录的写入权限。");
    return false;
}

TArray<FMmdNotice> MmdNotices::Software()
{
    const FString Root = FPaths::ProjectDir();
    const FString Licenses = FPaths::ConvertRelativePathToFull(Root / TEXT("../Licenses"));
    FString License, ThirdParty;
    if (!ReadNotice(Root / TEXT("LICENSE"), License)) ReadNotice(Licenses / TEXT("Project-MIT.txt"), License);
    const bool bHasLicense = !License.IsEmpty();
    if (!bHasLicense) License = TEXT("软件许可文件缺失，请使用包含 Licenses 目录的完整发行版本。");
    if (FPaths::DirectoryExists(Licenses))
    {
        TArray<FString> Files;
        IFileManager::Get().FindFiles(Files, *(Licenses / TEXT("*")), true, false); Files.Sort();
        for (const FString& File : Files)
        {
            FString Text;
            if ((File.EndsWith(TEXT(".txt")) || File.EndsWith(TEXT(".md"))) && ReadNotice(Licenses / File, Text))
                ThirdParty += TEXT("\n\n──────── ") + File + TEXT(" ────────\n") + Text;
        }
    }
    else
    {
        ReadNotice(Root / TEXT("THIRD_PARTY_NOTICES.md"), ThirdParty);
        for (const FString& Relative : {
            FString(TEXT("Plugins/VRM4U/LICENSE")), FString(TEXT("Plugins/VRM4U/LICENSE_assimp")), FString(TEXT("Plugins/VRM4U/LICENSE_UMT")),
            FString(TEXT("Plugins/HonkaiToonRenderer/LICENSE")), FString(TEXT("Plugins/HonkaiMotionCapture/LICENSE")),
            FString(TEXT("Plugins/HonkaiMotionCapture/Source/ThirdParty/MediaPipe/LICENSE")),
            FString(TEXT("Plugins/HonkaiMotionCapture/Source/ThirdParty/OpenCV/LICENSE")),
            FString(TEXT("Plugins/Spout2_DX12/LICENSE")), FString(TEXT("doc/Spout2-SDK-LICENSE.txt")),
            FString(TEXT("Tools/UMTNativePlugin/LICENSE")), FString(TEXT("Tools/UMTNativePlugin/ThirdParty/Bullet-LICENSE.txt")),
            FString(TEXT("Plugins/NVIDIA/DLSS/Source/ThirdParty/NGX/LICENSE.txt")) })
        {
            FString Text;
            if (ReadNotice(Root / Relative, Text)) ThirdParty += TEXT("\n\n──────── ") + Relative + TEXT(" ────────\n") + Text;
        }
    }
    ThirdParty += TEXT("\n\nPDF 格式的附加许可请在完整许可目录查看。第三方组件按各自原始许可提供；本软件不替其版权人作出额外保证。Unreal Engine 随软件提供的技术不获得任何额外保证、条件或责任承诺。");
    return {
        {TEXT("软件开源许可 · 1 / 3"), TEXT("本项目原创代码采用 MIT。下面是完整许可；此确认不缩减 MIT 已授予的权利。\n\n") + License, bHasLicense},
        {TEXT("第三方软件许可 · 2 / 3"), ThirdParty},
        {TEXT("模型与生成内容说明 · 3 / 3"), TEXT("本软件是独立的模型展示与动捕工具，不附带角色素材，也不授予模型、贴图、音视频或其他导入内容的使用权。\n\n导入前请确认你已获得所需授权，并阅读模型作者或权利人的使用守则。渲染图片、视频、直播、生成的增强贴图以及二次分发，仍须遵守源素材的许可、使用守则及适用法律。软件的 MIT 许可不覆盖第三方模型和素材。\n\n软件无法替你判断某个模型是否允许商用、改造、公开展示或分发。由你确认预期用途被允许；存在疑问时应先向权利人核实。\n\n本软件按原始开源许可中的“现状”条件提供，不保证输出适合特定用途，也不保证其不涉及第三方权利；适用法律规定不得排除的责任不受影响。")}
    };
}

FString MmdNotices::Fingerprint(const TArray<FMmdNotice>& Notices)
{
    FString Combined;
    for (const auto& Notice : Notices) Combined += Notice.Title + TEXT("\n") + Notice.Text;
    const FTCHARToUTF8 Bytes(*Combined);
    FSHAHash Hash; FSHA1::HashBuffer(Bytes.Get(), Bytes.Length(), Hash.Hash);
    return Hash.ToString();
}

FMmdNotice MmdNotices::Model(const FString& Root)
{
    TArray<FString> Files;
    IFileManager::Get().FindFilesRecursive(Files, *Root, TEXT("*"), true, false); Files.Sort();
    FMmdNotice Result{TEXT("模型使用守则"), TEXT("以下内容来自导入压缩包，按原文展示，不代表软件重新授予授权。请按模型作者的规定使用。\n")};
    int32 Count = 0;
    for (const FString& File : Files)
    {
        const FString Name = FPaths::GetCleanFilename(File).ToLower();
        const bool bReadme = Name.Contains(TEXT("readme")) || Name.Contains(TEXT("read_me")) || Name.Contains(TEXT("license"))
            || Name.Contains(TEXT("licence")) || Name.Contains(TEXT("使用")) || Name.Contains(TEXT("守则")) || Name.Contains(TEXT("規約"))
            || Name.Contains(TEXT("规约")) || Name.Contains(TEXT("利用")) || Name.Contains(TEXT("说明")) || Name.Contains(TEXT("规则")) || Name.Contains(TEXT("規則"));
        if (!bReadme) continue;
        FString Relative = File; FPaths::MakePathRelativeTo(Relative, *(Root + TEXT("/")));
        FString Text;
        if ((Name.EndsWith(TEXT(".txt")) || Name.EndsWith(TEXT(".md"))) && ReadNotice(File, Text))
        {
            if (Count++ < 16 && Result.Text.Len() + Text.Len() < 512 * 1024)
                Result.Text += TEXT("\n\n──────── ") + Relative + TEXT(" ────────\n") + Text;
            else Result.Text += TEXT("\n另有较长说明，请打开说明目录阅读：") + Relative;
        }
        else Result.Text += TEXT("\n其他说明文件（请在说明目录查看原文件）：") + Relative;
    }
    if (!Count) Result.Text += TEXT("\n\n没有识别到可直接显示的文本守则。请打开说明目录检查原文件，并自行确认模型来源与使用授权。没有说明文件不代表可以自由使用。");
    return Result;
}

void SMmdNoticePanel::Construct(const FArguments& Args)
{
    bRequireTime = Args._RequireFiveSeconds;
    bAvailable = Args._Notice.bAvailable;
    const auto& Style = FMmdUiStyle::Get(); const auto& Colors = FMmdUiStyle::Colors();
    const TFunction<void()> Accepted = Args._Accepted, Declined = Args._Declined;
    const FString Folder = Args._Folder;
    ChildSlot[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Colors.Panel).Padding(20)
        [SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)[SNew(STextBlock).Text(FText::FromString(Args._Notice.Title)).ColorAndOpacity(Colors.Text).Font(FCoreStyle::GetDefaultFontStyle("Bold",16))]
        + SVerticalBox::Slot().FillHeight(1)[SNew(SMultiLineEditableTextBox).Style(&Style.Editable).IsReadOnly(true).AutoWrapText(true)
            .Font(FCoreStyle::GetDefaultFontStyle("Regular",12)).Text(FText::FromString(Args._Notice.Text))]
        + SVerticalBox::Slot().AutoHeight().Padding(0,12,0,6)[SNew(SCheckBox).Style(&Style.Check)
            .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bChecked = State == ECheckBoxState::Checked; })
            [SNew(STextBlock).Text(FText::FromString(bRequireTime ? TEXT("我已阅读并同意遵守模型使用守则") : TEXT("我已阅读并同意上述许可与说明"))).ColorAndOpacity(Colors.Text).Font(FCoreStyle::GetDefaultFontStyle("Regular",12))]]
        + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text_Lambda([this] { return FText::FromString(Error); }).AutoWrapText(true).ColorAndOpacity(Colors.Warning)]
        + SVerticalBox::Slot().AutoHeight().Padding(0,10,0,0)[SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)[SNew(SButton).ButtonStyle(&Style.Button).ContentPadding(FMargin(12,8))
                .Visibility(Folder.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
                .OnClicked_Lambda([Folder] { FPlatformProcess::ExploreFolder(*Folder); return FReply::Handled(); })
                [SNew(STextBlock).Text(FText::FromString(TEXT("打开说明目录"))).ColorAndOpacity(Colors.Text).Font(FCoreStyle::GetDefaultFontStyle("Regular",12))]]
            + SHorizontalBox::Slot().FillWidth(1)[SNullWidget::NullWidget]
            + SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)[SNew(SButton).ButtonStyle(&Style.Button).ContentPadding(FMargin(12,8))
                .OnClicked_Lambda([Declined] { if (Declined) Declined(); return FReply::Handled(); })
                [SNew(STextBlock).Text(FText::FromString(TEXT("不同意"))).ColorAndOpacity(Colors.Text).Font(FCoreStyle::GetDefaultFontStyle("Regular",12))]]
            + SHorizontalBox::Slot().AutoWidth()[SNew(SButton).ButtonStyle(&Style.Primary).ContentPadding(FMargin(12,8))
                .IsEnabled_Lambda([this] { return bAvailable && bChecked && (!bRequireTime || ReadTime.Ready()); })
                .OnClicked_Lambda([this, Accepted] { if (bAvailable && bChecked && (!bRequireTime || ReadTime.Ready()) && Accepted) Accepted(); return FReply::Handled(); })
                [SNew(STextBlock).Text_Lambda([this] { return FText::FromString(bRequireTime && !ReadTime.Ready()
                    ? FString::Printf(TEXT("请阅读（%d 秒）"), FMath::CeilToInt(5.f - ReadTime.Seconds)) : TEXT("同意并继续")); }).ColorAndOpacity(Colors.Text).Font(FCoreStyle::GetDefaultFontStyle("Regular",12))]]]]];
}

void SMmdNoticePanel::Tick(const FGeometry& Geometry, double Time, float Delta)
{
    SCompoundWidget::Tick(Geometry, Time, Delta);
    ReadTime.Advance(Delta, GetVisibility().IsVisible() && FPlatformApplicationMisc::IsThisApplicationForeground());
}
