#include "MmdEnhancement.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Misc/Base64.h"
#include "Serialization/JsonSerializer.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace
{
    int32 ActiveJobs = 0;

    FString FullPath(FString Path)
    {
        Path = FPaths::ConvertRelativePathToFull(Path);
        FPaths::NormalizeFilename(Path);
        FPaths::CollapseRelativeDirectories(Path);
        return Path;
    }

    bool Inside(const FString& Path, const FString& Root)
    {
        return FullPath(Path).StartsWith(FullPath(Root) + TEXT("/"), ESearchCase::IgnoreCase);
    }

    bool UnlinkedTree(const FString& Root)
    {
#if PLATFORM_WINDOWS
        TArray<FString> Pending{Root};
        int32 Count = 0;
        while (!Pending.IsEmpty())
        {
            const FString Path = Pending.Pop(EAllowShrinking::No);
            const DWORD Attributes = ::GetFileAttributesW(*Path);
            if (Attributes == INVALID_FILE_ATTRIBUTES) continue;
            if ((Attributes & FILE_ATTRIBUTE_REPARSE_POINT) || ++Count > 100000) return false;
            if (Attributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                TArray<FString> Children;
                IFileManager::Get().FindFiles(Children, *(Path / TEXT("*")), true, true);
                for (const FString& Child : Children) Pending.Add(Path / Child);
            }
        }
#endif
        return true;
    }

    FString FileHash(const FString& Path)
    {
        TUniquePtr<FArchive> File(IFileManager::Get().CreateFileReader(*Path));
        if (!File) return {};
        FSHA1 Hash;
        TArray<uint8> Buffer;
        Buffer.SetNumUninitialized(1024 * 1024);
        int64 Remaining = File->TotalSize();
        while (Remaining > 0)
        {
            const int32 Count = int32(FMath::Min<int64>(Remaining, Buffer.Num()));
            File->Serialize(Buffer.GetData(), Count);
            if (File->IsError()) return {};
            Hash.Update(Buffer.GetData(), Count);
            Remaining -= Count;
        }
        Hash.Final();
        FSHAHash Value;
        Hash.GetHash(Value.Hash);
        return Value.ToString().ToLower();
    }

    TSharedPtr<FJsonObject> ReadJson(const FString& Path, int64 Limit = 1024 * 1024)
    {
        const int64 Size = IFileManager::Get().FileSize(*Path);
        if (Size <= 0 || Size > Limit) return nullptr;
        FString Text;
        TSharedPtr<FJsonObject> Object;
#if PLATFORM_WINDOWS
        HANDLE File = ::CreateFileW(*Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (File == INVALID_HANDLE_VALUE) return nullptr;
        TArray<uint8> Buffer;
        Buffer.SetNumUninitialized(int32(Size));
        DWORD Read = 0;
        const bool Success = ::ReadFile(File, Buffer.GetData(), DWORD(Size), &Read, nullptr) && Read == Size;
        ::CloseHandle(File);
        if (!Success) return nullptr;
        FFileHelper::BufferToString(Text, Buffer.GetData(), Buffer.Num());
#else
        if (!FFileHelper::LoadFileToString(Text, *Path)) return nullptr;
#endif
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object)) return nullptr;
        return Object;
    }

    bool WriteJson(const FString& Path, const TSharedRef<FJsonObject>& Object)
    {
        FString Text;
        FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Text));
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
        const FString Temporary = Path + TEXT(".") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".tmp");
        if (!FFileHelper::SaveStringToFile(Text, *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) return false;
#if PLATFORM_WINDOWS
        if (::MoveFileExW(*Temporary, *Path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
#else
        if (IFileManager::Get().Move(*Path, *Temporary, true, false, false, true)) return true;
#endif
        IFileManager::Get().Delete(*Temporary);
        return false;
    }

    FString StatePath(const FString& ModelPath)
    {
        const FString Hash = FileHash(ModelPath);
        return Hash.IsEmpty() ? FString() : FMmdEnhancement::UserDataDir() / TEXT("Models") / (Hash + TEXT(".json"));
    }

    bool ValidCache(const FString& Directory, const FString& ModelPath, const FMmdModelState& State)
    {
        if (!Inside(Directory, FMmdEnhancement::UserDataDir() / TEXT("Enhancements")) || !UnlinkedTree(Directory)) return false;
        const auto Manifest = ReadJson(Directory / TEXT("manifest.json"), 4 * 1024 * 1024);
        if (!Manifest) return false;
        FString RelativeModel, Algorithm;
        const TArray<TSharedPtr<FJsonValue>>* Files = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Scripts = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Outputs = nullptr;
        if (!Manifest->TryGetStringField(TEXT("model_relative"), RelativeModel)
            || !Manifest->TryGetStringField(TEXT("algorithm"), Algorithm) || Algorithm != TEXT("face-v2")
            || !Manifest->TryGetArrayField(TEXT("files"), Files) || Files->Num() > 20000
            || !Manifest->TryGetArrayField(TEXT("scripts"), Scripts) || Scripts->Num() != 3
            || !Manifest->TryGetArrayField(TEXT("outputs"), Outputs) || Outputs->Num() != 3) return false;
        int32 MaterialIndex = INDEX_NONE;
        TSharedPtr<FJsonObject> Parameters;
        const TSharedPtr<FJsonObject>* CachedParameters = nullptr;
        if (!Manifest->TryGetNumberField(TEXT("material_index"), MaterialIndex) || MaterialIndex != State.MaterialIndex
            || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(State.ParametersJson), Parameters) || !Parameters
            || !Manifest->TryGetObjectField(TEXT("parameters"), CachedParameters) || !CachedParameters) return false;
        if (Parameters->Values.Num() > 1 || (Parameters->Values.Num() == 1 && !Parameters->HasField(TEXT("resolution")))) return false;
        double Resolution = 512, CachedResolution = 0;
        Parameters->TryGetNumberField(TEXT("resolution"), Resolution);
        if (!(*CachedParameters)->TryGetNumberField(TEXT("resolution"), CachedResolution) || Resolution != CachedResolution) return false;
        FString NormalizedRelative = RelativeModel.Replace(TEXT("\\"), TEXT("/"));
        const FString Absolute = FullPath(ModelPath);
        if (!Absolute.EndsWith(TEXT("/") + NormalizedRelative, ESearchCase::IgnoreCase)) return false;
        const FString ResourceRoot = Absolute.LeftChop(NormalizedRelative.Len() + 1);
        if (!UnlinkedTree(ResourceRoot)) return false;
        TArray<FString> CurrentFiles;
        IFileManager::Get().FindFilesRecursive(CurrentFiles, *ResourceRoot, TEXT("*"), true, false);
        if (CurrentFiles.Num() != Files->Num()) return false;
        TSet<FString> ListedFiles;
        for (const auto& Entry : *Files)
        {
            const TSharedPtr<FJsonObject>* File = nullptr;
            FString Name;
            if (!Entry->TryGetObject(File) || !File || !(*File)->TryGetStringField(TEXT("path"), Name)) return false;
            ListedFiles.Add(FullPath(ResourceRoot / Name).ToLower());
        }
        if (ListedFiles.Num() != CurrentFiles.Num()) return false;
        for (const FString& File : CurrentFiles) if (!ListedFiles.Contains(FullPath(File).ToLower())) return false;
        auto CheckFiles = [](const TArray<TSharedPtr<FJsonValue>>& Entries, const FString& Root)
        {
            for (const auto& Entry : Entries)
            {
                const TSharedPtr<FJsonObject>* Object = nullptr;
                FString Name, Hash;
                if (!Entry->TryGetObject(Object) || !Object || !(*Object)->TryGetStringField(TEXT("path"), Name)
                    || !(*Object)->TryGetStringField(TEXT("sha1"), Hash) || !Inside(Root / Name, Root)
                    || FileHash(Root / Name) != Hash) return false;
            }
            return true;
        };
        return CheckFiles(*Files, ResourceRoot) && CheckFiles(*Scripts, FPaths::ProjectDir() / TEXT("Scripts")) && CheckFiles(*Outputs, Directory)
            && FPaths::FileExists(Directory / TEXT("profile.json"));
    }
}

FMmdEnhancement::~FMmdEnhancement() { Cancel(); }

FString FMmdEnhancement::UserDataDir()
{
    FString Override;
    if (FParse::Value(FCommandLine::Get(), TEXT("MmdUserData="), Override) && !Override.IsEmpty()) return FullPath(Override);
    return FullPath(FString(FPlatformProcess::UserSettingsDir()) / TEXT("HonkaiVTuber"));
}

FString FMmdEnhancement::ImportsDir() { return UserDataDir() / TEXT("Imports"); }

FMmdModelState FMmdEnhancement::ReadModelState(const FString& ModelPath)
{
    FMmdModelState State;
    const FString Path = StatePath(ModelPath);
    const auto Object = Path.IsEmpty() ? nullptr : ReadJson(Path);
    if (!Object) return State;
    Object->TryGetBoolField(TEXT("enhanced"), State.bEnhanced);
    Object->TryGetNumberField(TEXT("material_index"), State.MaterialIndex);
    Object->TryGetStringField(TEXT("parameters"), State.ParametersJson);
    Object->TryGetStringField(TEXT("profile_directory"), State.ProfileDirectory);
    if (!ValidCache(State.ProfileDirectory, ModelPath, State)) { State.ProfileDirectory.Empty(); State.bEnhanced = false; }
    return State;
}

bool FMmdEnhancement::SaveModelState(const FString& ModelPath, const FMmdModelState& State, FString& Error)
{
    const FString Path = StatePath(ModelPath);
    if (Path.IsEmpty()) { Error = TEXT("无法读取模型，配置未保存。"); return false; }
    auto Object = MakeShared<FJsonObject>();
    Object->SetNumberField(TEXT("version"), 1);
    Object->SetBoolField(TEXT("enhanced"), State.bEnhanced);
    Object->SetNumberField(TEXT("material_index"), State.MaterialIndex);
    Object->SetStringField(TEXT("parameters"), State.ParametersJson);
    Object->SetStringField(TEXT("profile_directory"), State.ProfileDirectory);
    if (WriteJson(Path, Object)) return true;
    Error = TEXT("无法写入用户模型配置。"); return false;
}

bool FMmdEnhancement::Start(const FMmdEnhancementRequest& Request)
{
    if (IsRunning()) return false;
    Status = FMmdEnhancementStatus();
    if (!FPaths::FileExists(Request.ModelPath) || !Inside(Request.ModelPath, Request.ResourceRoot))
    {
        Status.State = EMmdEnhancementState::Failed; Status.Message = TEXT("模型路径必须位于导入资源目录中。"); return false;
    }
    JobDirectory = UserDataDir() / TEXT("Jobs") / FGuid::NewGuid().ToString(EGuidFormats::Digits);
    auto Object = MakeShared<FJsonObject>();
    Object->SetStringField(TEXT("model"), FullPath(Request.ModelPath));
    Object->SetStringField(TEXT("resource_root"), FullPath(Request.ResourceRoot));
    Object->SetStringField(TEXT("data_root"), UserDataDir());
    Object->SetStringField(TEXT("scripts"), FullPath(FPaths::ProjectDir() / TEXT("Scripts")));
    Object->SetNumberField(TEXT("material_index"), Request.MaterialIndex);
    Object->SetStringField(TEXT("parameters"), Request.ParametersJson);
    Object->SetBoolField(TEXT("force"), Request.bForce);
    const FString RequestPath = JobDirectory / TEXT("request.json");
    if (!WriteJson(RequestPath, Object))
    {
        Status.State = EMmdEnhancementState::Failed; Status.Message = TEXT("无法创建增强任务目录。"); return false;
    }
    const FString Script = FullPath(FPaths::ProjectDir() / TEXT("Scripts/Prepare-MmdEnhancement.ps1"));
    auto Quote = [](const FString& Value) { return TEXT("'") + Value.Replace(TEXT("'"), TEXT("''")) + TEXT("'"); };
    const FString Command = TEXT("& ([ScriptBlock]::Create([IO.File]::ReadAllText(") + Quote(Script) + TEXT("))) -RequestPath ") + Quote(RequestPath);
    const auto Utf16 = StringCast<UTF16CHAR>(*Command);
    const FString Arguments = TEXT("-NoProfile -NonInteractive -EncodedCommand ") + FBase64::Encode(reinterpret_cast<const uint8*>(Utf16.Get()), Utf16.Length() * 2);
    Process = FPlatformProcess::CreateProc(TEXT("powershell.exe"), *Arguments, false, true, true, nullptr, 0, nullptr, nullptr);
    if (!Process.IsValid())
    {
        Status.State = EMmdEnhancementState::Failed; Status.Message = TEXT("无法启动后台生成任务。"); return false;
    }
    ++ActiveJobs;
    Status.State = EMmdEnhancementState::Running; Status.Stage = TEXT("准备"); Status.Message = TEXT("检查本机 Blender 与生成缓存…");
    NextPoll = 0;
    return true;
}

void FMmdEnhancement::ReadStatus()
{
    const auto Object = ReadJson(JobDirectory / TEXT("status.json"));
    if (!Object) return;
    Object->TryGetStringField(TEXT("stage"), Status.Stage);
    Object->TryGetStringField(TEXT("message"), Status.Message);
    double Progress = Status.Progress;
    Object->TryGetNumberField(TEXT("progress"), Progress);
    Status.Progress = FMath::Clamp(float(Progress), 0.f, 1.f);
    Object->TryGetBoolField(TEXT("cache_hit"), Status.bCacheHit);
    Object->TryGetStringField(TEXT("result_directory"), Status.ResultDirectory);
    if (!Status.ResultDirectory.IsEmpty() && !Inside(Status.ResultDirectory, UserDataDir() / TEXT("Enhancements"))) Status.ResultDirectory.Empty();
    const TArray<TSharedPtr<FJsonValue>>* Candidates = nullptr;
    if (Object->TryGetArrayField(TEXT("candidates"), Candidates))
    {
        Status.Candidates.Reset();
        for (const auto& Value : *Candidates)
        {
            const TSharedPtr<FJsonObject>* Candidate = nullptr;
            FMmdMaterialCandidate Item;
            if (Value->TryGetObject(Candidate) && Candidate && (*Candidate)->TryGetNumberField(TEXT("index"), Item.Index)
                && (*Candidate)->TryGetStringField(TEXT("name"), Item.Name) && Item.Index >= 0) Status.Candidates.Add(Item);
        }
    }
}

void FMmdEnhancement::Tick()
{
    if (!Process.IsValid() || FPlatformTime::Seconds() < NextPoll) return;
    NextPoll = FPlatformTime::Seconds() + 0.15;
    ReadStatus();
    if (FPlatformProcess::IsProcRunning(Process)) return;
    int32 Code = -1;
    FPlatformProcess::GetProcReturnCode(Process, &Code);
    FPlatformProcess::CloseProc(Process);
    --ActiveJobs;
    const auto Object = ReadJson(JobDirectory / TEXT("status.json"));
    FString State;
    if (Object) Object->TryGetStringField(TEXT("state"), State);
    if (State == TEXT("needs_material")) Status.State = EMmdEnhancementState::NeedsMaterial;
    else if (Code == 0 && State == TEXT("succeeded") && !Status.ResultDirectory.IsEmpty()) Status.State = EMmdEnhancementState::Succeeded;
    else { Status.State = EMmdEnhancementState::Failed; if (Status.Message.IsEmpty()) Status.Message = TEXT("后台生成失败，请重试或查看任务日志。"); }
}

void FMmdEnhancement::Cancel()
{
    if (!Process.IsValid()) return;
    FPlatformProcess::TerminateProc(Process, true);
    FPlatformProcess::CloseProc(Process);
    --ActiveJobs;
    Status.State = EMmdEnhancementState::Cancelled;
    Status.Message = TEXT("已取消，原有效果和缓存保持不变。");
    auto Object = MakeShared<FJsonObject>();
    Object->SetStringField(TEXT("state"), TEXT("cancelled"));
    Object->SetStringField(TEXT("message"), Status.Message);
    WriteJson(JobDirectory / TEXT("status.json"), Object);
}

bool FMmdEnhancement::Export(const FString& ModelPath, const FString& ProfileDirectory, const FString& Destination, FString& Error)
{
    const auto Profile = ReadJson(ProfileDirectory / TEXT("profile.json"));
    const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
    FString Hash;
    if (!Profile || !Profile->TryGetStringField(TEXT("source_sha1"), Hash) || Hash != FileHash(ModelPath)
        || !Profile->TryGetArrayField(TEXT("materials"), Entries)) { Error = TEXT("增强数据与当前模型不匹配。"); return false; }
    if (FPaths::DirectoryExists(Destination) || FPaths::FileExists(Destination)) { Error = TEXT("导出目录已存在，请使用新目录。"); return false; }
    TSet<FString> Textures;
    for (const auto& Entry : *Entries)
    {
        const TSharedPtr<FJsonObject>* Object = nullptr;
        FString Name;
        if (!Entry->TryGetObject(Object) || !Object || !(*Object)->TryGetStringField(TEXT("texture"), Name)
            || Name != FPaths::GetCleanFilename(Name) || Name.Contains(TEXT(":")) || Name.Contains(TEXT(".."))
            || !Name.EndsWith(TEXT(".png"), ESearchCase::IgnoreCase) || !FPaths::FileExists(ProfileDirectory / Name))
        { Error = TEXT("增强贴图不存在或路径无效。"); return false; }
        Textures.Add(Name);
    }
    const FString Staging = FullPath(Destination) + TEXT(".tmp-") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    IFileManager::Get().MakeDirectory(*Staging, true);
    bool Success = true;
    for (const FString& Name : Textures) Success &= IFileManager::Get().Copy(*(Staging / Name), *(ProfileDirectory / Name), false) == COPY_OK;
    Success &= WriteJson(Staging / (FPaths::GetCleanFilename(ModelPath) + TEXT(".toon.json")), Profile.ToSharedRef());
    if (Success)
    {
#if PLATFORM_WINDOWS
        Success = ::MoveFileExW(*Staging, *FullPath(Destination), MOVEFILE_WRITE_THROUGH);
#else
        Success = IFileManager::Get().Move(*FullPath(Destination), *Staging, false, false, false, true);
#endif
    }
    if (!Success) { IFileManager::Get().DeleteDirectory(*Staging, false, true); Error = TEXT("导出失败，原增强数据未修改。"); }
    return Success;
}

bool FMmdEnhancement::ClearCache(FString& Error)
{
    if (ActiveJobs > 0) { Error = TEXT("请先取消或等待当前生成任务。"); return false; }
    const FString Root = UserDataDir();
    for (const TCHAR* Name : {TEXT("Enhancements"), TEXT("Jobs")})
    {
        const FString Directory = FullPath(Root / Name);
        if (!Inside(Directory, Root) || !UnlinkedTree(Directory)) { Error = TEXT("缓存目录包含链接，已停止清理以保护外部文件。"); return false; }
        if (!IFileManager::Get().DeleteDirectory(*Directory, false, true))
        { if (FPaths::DirectoryExists(Directory)) { Error = TEXT("部分缓存无法删除，请稍后重试。"); return false; } }
    }
    return true;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdEnhancementStorageTest, "HonkaiRendering.Enhancement.Storage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMmdEnhancementStorageTest::RunTest(const FString& Parameters)
{
    const FString Root = FMmdEnhancement::UserDataDir() / TEXT("Jobs") / FGuid::NewGuid().ToString(EGuidFormats::Digits);
    TestTrue(TEXT("Owned job is inside user data"), Inside(Root, FMmdEnhancement::UserDataDir()));
    TestFalse(TEXT("Parent traversal cannot be cleared"), Inside(Root / TEXT("../../outside"), Root));
    TestFalse(TEXT("Root itself cannot be cleared"), Inside(Root, Root));
    IFileManager::Get().MakeDirectory(*Root, true);
    const FString Model = Root / TEXT("fixture.pmx");
    FFileHelper::SaveStringToFile(FGuid::NewGuid().ToString(), *Model);
    const FString Hash = FileHash(Model);
    TestEqual(TEXT("Stable source content hash"), FileHash(Model), Hash);
    const FString JsonPath = Root / TEXT("settings.json");
    auto Object = MakeShared<FJsonObject>();
    Object->SetBoolField(TEXT("enhanced"), false);
    TestTrue(TEXT("Initial state written"), WriteJson(JsonPath, Object));
    Object->SetBoolField(TEXT("enhanced"), true);
    TestTrue(TEXT("State replaced without deleting destination first"), WriteJson(JsonPath, Object));
    const auto Restored = ReadJson(JsonPath);
    TestTrue(TEXT("New state restores"), Restored && Restored->GetBoolField(TEXT("enhanced")));
    FMmdModelState State;
    State.MaterialIndex = 7;
    State.ParametersJson = TEXT("{\"resolution\":256}");
    FString Error;
    TestTrue(TEXT("Model choice persists"), FMmdEnhancement::SaveModelState(Model, State, Error));
    const auto Loaded = FMmdEnhancement::ReadModelState(Model);
    TestEqual(TEXT("Material choice restores"), Loaded.MaterialIndex, State.MaterialIndex);
    TestEqual(TEXT("Generation parameters restore"), Loaded.ParametersJson, State.ParametersJson);
    TestFalse(TEXT("Absent generated data cannot be applied"), Loaded.bEnhanced);
    auto Profile = MakeShared<FJsonObject>();
    Profile->SetNumberField(TEXT("version"), 1);
    Profile->SetStringField(TEXT("source_sha1"), Hash);
    auto Material = MakeShared<FJsonObject>();
    Material->SetNumberField(TEXT("index"), 7);
    Material->SetStringField(TEXT("name"), TEXT("Face"));
    Material->SetStringField(TEXT("texture"), TEXT("face.png"));
    TArray<TSharedPtr<FJsonValue>> Entries{MakeShared<FJsonValueObject>(Material)};
    Profile->SetArrayField(TEXT("materials"), Entries);
    WriteJson(Root / TEXT("profile.json"), Profile);
    FFileHelper::SaveStringToFile(TEXT("fixture"), *(Root / TEXT("face.png")));
    TestTrue(TEXT("Enhancement export publishes a new folder"), FMmdEnhancement::Export(Model, Root, Root / TEXT("export"), Error));
    TestTrue(TEXT("Export uses the portable PMX sidecar name"), FPaths::FileExists(Root / TEXT("export/fixture.pmx.toon.json")));
    TestFalse(TEXT("Export never overwrites an existing directory"), FMmdEnhancement::Export(Model, Root, Root / TEXT("export"), Error));
    IFileManager::Get().Delete(*StatePath(Model));
    if (Inside(Root, FMmdEnhancement::UserDataDir() / TEXT("Jobs"))) IFileManager::Get().DeleteDirectory(*Root, false, true);
    return true;
}
#endif
