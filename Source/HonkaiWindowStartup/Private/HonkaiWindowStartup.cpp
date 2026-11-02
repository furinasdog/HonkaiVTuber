// SPDX-License-Identifier: MIT
#include "CoreMinimal.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreMisc.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogHonkaiWindowStartup, Log, All);

namespace HonkaiWindowStartup
{
    const TCHAR* SettingsSection = TEXT("/Script/Engine.GameUserSettings");
    const TCHAR* MigrationSection = TEXT("HonkaiRendering.WindowDefaults");
    constexpr int32 WindowDefaultsVersion = 1;

    bool MigrateWindowDefaults(FConfigCacheIni& Config, const FString& Filename)
    {
        int32 Version = 0;
        Config.GetInt(MigrationSection, TEXT("MigrationVersion"), Version, Filename);
        if (Version >= WindowDefaultsVersion) return false;

        int32 Mode = 2;
        Config.GetInt(SettingsSection, TEXT("FullscreenMode"), Mode, Filename);
        if (Mode == 0 || Mode == 1)
        {
            Config.SetInt(SettingsSection, TEXT("FullscreenMode"), 2, Filename);
            Config.SetInt(SettingsSection, TEXT("LastConfirmedFullscreenMode"), 2, Filename);
            Config.SetInt(SettingsSection, TEXT("ResolutionSizeX"), 1280, Filename);
            Config.SetInt(SettingsSection, TEXT("ResolutionSizeY"), 800, Filename);
            Config.SetInt(SettingsSection, TEXT("LastUserConfirmedResolutionSizeX"), 1280, Filename);
            Config.SetInt(SettingsSection, TEXT("LastUserConfirmedResolutionSizeY"), 800, Filename);
        }
        Config.SetInt(MigrationSection, TEXT("MigrationVersion"), WindowDefaultsVersion, Filename);
        return true;
    }
}

class FHonkaiWindowStartupModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
#if PLATFORM_WINDOWS
        if (GIsEditor || IsRunningCommandlet() || IsRunningDedicatedServer()) return;
        // PostConfigInit follows KnownIni loading and precedes PreloadResolutionSettings/RHI/window creation.
        if (!GConfig || GGameUserSettingsIni.IsEmpty() || !GConfig->FindConfigFile(GGameUserSettingsIni))
        {
            UE_LOG(LogHonkaiWindowStartup, Warning, TEXT("WINDOW_DEFAULTS settings are unavailable before window creation; migration skipped."));
            return;
        }
        if (HonkaiWindowStartup::MigrateWindowDefaults(*GConfig, GGameUserSettingsIni))
        {
            if (GConfig->Flush(false, GGameUserSettingsIni))
            {
                UE_LOG(LogHonkaiWindowStartup, Display, TEXT("WINDOW_DEFAULTS migration=1 saved before resolution preload."));
            }
            else
            {
                UE_LOG(LogHonkaiWindowStartup, Warning, TEXT("WINDOW_DEFAULTS migration could not be saved; check user settings permissions."));
            }
        }
#endif
    }
};

IMPLEMENT_MODULE(FHonkaiWindowStartupModule, HonkaiWindowStartup)

#if WITH_DEV_AUTOMATION_TESTS
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHonkaiWindowMigrationTest, "HonkaiRendering.UI.LegacyWindowDefaultsMigration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHonkaiWindowMigrationTest::RunTest(const FString& Parameters)
{
    using namespace HonkaiWindowStartup;
    const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/WindowDefaults"));
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Fixture = Directory / (FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".ini"));
    auto ReadInt = [&](FConfigCacheIni& Config, const TCHAR* Key)
    { int32 Value = -1; Config.GetInt(SettingsSection, Key, Value, Fixture); return Value; };
    for (int32 LegacyMode : {0, 1})
    {
        const FString Old = FString::Printf(TEXT("[/Script/Engine.GameUserSettings]\nVersion=5\nFullscreenMode=%d\nLastConfirmedFullscreenMode=%d\nResolutionSizeX=1920\nResolutionSizeY=1080\nbUseVSync=True\n[Unrelated]\nCameraIndex=4\n"), LegacyMode, LegacyMode);
        if (!TestTrue(TEXT("Create a real legacy settings file"), FFileHelper::SaveStringToFile(Old, *Fixture))) return false;
        {
            FConfigCacheIni Config(EConfigCacheType::DiskBacked);
            Config.LoadFile(Fixture);
            TestTrue(TEXT("Unmarked legacy fullscreen migrates on its first run"), MigrateWindowDefaults(Config, Fixture));
            TestEqual(TEXT("First upgraded launch uses windowed mode"), ReadInt(Config, TEXT("FullscreenMode")), 2);
            TestEqual(TEXT("First upgraded launch uses 1280 width"), ReadInt(Config, TEXT("ResolutionSizeX")), 1280);
            TestEqual(TEXT("First upgraded launch uses 800 height"), ReadInt(Config, TEXT("ResolutionSizeY")), 800);
            TestEqual(TEXT("The confirmed mode agrees before initial preload"), ReadInt(Config, TEXT("LastConfirmedFullscreenMode")), 2);
            TestEqual(TEXT("Confirmed width agrees before initial preload"), ReadInt(Config, TEXT("LastUserConfirmedResolutionSizeX")), 1280);
            TestEqual(TEXT("Confirmed height agrees before initial preload"), ReadInt(Config, TEXT("LastUserConfirmedResolutionSizeY")), 800);
            bool VSync = false;
            Config.GetBool(SettingsSection, TEXT("bUseVSync"), VSync, Fixture);
            TestTrue(TEXT("Migration preserves unrelated display preferences"), VSync);
            int32 Camera = -1;
            Config.GetInt(TEXT("Unrelated"), TEXT("CameraIndex"), Camera, Fixture);
            TestEqual(TEXT("Migration preserves unrelated sections"), Camera, 4);
            TestTrue(TEXT("The actual config backend saves the migration"), Config.Flush(false, Fixture));
        }
        {
            FConfigCacheIni UserChoice(EConfigCacheType::DiskBacked);
            UserChoice.LoadFile(Fixture);
            int32 Marker = 0;
            UserChoice.GetInt(MigrationSection, TEXT("MigrationVersion"), Marker, Fixture);
            TestEqual(TEXT("The migration marker survives a real disk reload"), Marker, WindowDefaultsVersion);
            UserChoice.SetInt(SettingsSection, TEXT("FullscreenMode"), LegacyMode, Fixture);
            UserChoice.SetInt(SettingsSection, TEXT("ResolutionSizeX"), 1600, Fixture);
            UserChoice.SetInt(SettingsSection, TEXT("ResolutionSizeY"), 900, Fixture);
            TestTrue(TEXT("Save a later user fullscreen preference"), UserChoice.Flush(false, Fixture));
        }
        {
            FConfigCacheIni SecondRun(EConfigCacheType::DiskBacked);
            SecondRun.LoadFile(Fixture);
            TestFalse(TEXT("Second launch does not rerun migration"), MigrateWindowDefaults(SecondRun, Fixture));
            TestEqual(TEXT("Later fullscreen preference is respected"), ReadInt(SecondRun, TEXT("FullscreenMode")), LegacyMode);
            TestEqual(TEXT("Later window width is respected"), ReadInt(SecondRun, TEXT("ResolutionSizeX")), 1600);
            TestEqual(TEXT("Later window height is respected"), ReadInt(SecondRun, TEXT("ResolutionSizeY")), 900);
        }
    }

    FString Defaults;
    if (!TestTrue(TEXT("Load the real new-user defaults"), FFileHelper::LoadFileToString(Defaults, *(FPaths::ProjectConfigDir() / TEXT("DefaultGameUserSettings.ini"))))) return false;
    if (!TestTrue(TEXT("Create a fresh-user config fixture"), FFileHelper::SaveStringToFile(Defaults, *Fixture))) return false;
    {
        FConfigCacheIni NewUser(EConfigCacheType::DiskBacked);
        NewUser.LoadFile(Fixture);
        TestTrue(TEXT("Fresh users receive a migration marker"), MigrateWindowDefaults(NewUser, Fixture));
        TestEqual(TEXT("Fresh users remain windowed"), ReadInt(NewUser, TEXT("FullscreenMode")), 2);
        TestEqual(TEXT("Fresh-user width remains unchanged"), ReadInt(NewUser, TEXT("ResolutionSizeX")), 1280);
        TestTrue(TEXT("Fresh-user marker is persisted"), NewUser.Flush(false, Fixture));
    }
    if (!TestTrue(TEXT("Create a pre-existing custom window fixture"), FFileHelper::SaveStringToFile(TEXT("[/Script/Engine.GameUserSettings]\nFullscreenMode=2\nResolutionSizeX=1440\nResolutionSizeY=810\n"), *Fixture))) return false;
    {
        FConfigCacheIni CustomWindow(EConfigCacheType::DiskBacked);
        CustomWindow.LoadFile(Fixture);
        MigrateWindowDefaults(CustomWindow, Fixture);
        TestEqual(TEXT("An existing windowed user's custom width is retained"), ReadInt(CustomWindow, TEXT("ResolutionSizeX")), 1440);
        TestEqual(TEXT("An existing windowed user's custom height is retained"), ReadInt(CustomWindow, TEXT("ResolutionSizeY")), 810);
        CustomWindow.Flush(false, Fixture);
    }
    IFileManager::Get().Delete(*Fixture);
    return true;
}
#endif
