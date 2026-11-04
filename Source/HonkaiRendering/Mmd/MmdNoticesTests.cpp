#include "MmdNotices.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdNoticeStateTest,"HonkaiRendering.UI.NoticeState",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMmdNoticeStateTest::RunTest(const FString&)
{
    const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/Notices") / FGuid::NewGuid().ToString(EGuidFormats::Digits));
    const FString File = Directory / TEXT("Onboarding.json");
    FMmdFirstRunState State;
    TestFalse(TEXT("A fresh user requires review"),State.Load(File));
    const TArray<FMmdNotice> Docs{{TEXT("MIT"),TEXT("Original permission")},{TEXT("Models"),TEXT("Follow the model author's rules")}};
    State.AcceptedHash = MmdNotices::Fingerprint(Docs); State.bGuideSeen = true;
    FString Error;
    TestTrue(TEXT("Acceptances save to the isolated real filesystem"),State.Save(File,Error));
    FMmdFirstRunState Restored;
    TestTrue(TEXT("Accepted state loads"),Restored.Load(File));
    TestEqual(TEXT("Acceptance is bound to the actual notice text"),Restored.AcceptedHash,State.AcceptedHash);
    TestTrue(TEXT("Guide state survives restart"),Restored.bGuideSeen);
    auto Changed = Docs; Changed[1].Text += TEXT(" Revision");
    TestNotEqual(TEXT("Changed terms need review again"),State.AcceptedHash,MmdNotices::Fingerprint(Changed));
#if PLATFORM_WINDOWS
    HANDLE Locked = CreateFileW(*File,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (TestTrue(TEXT("Lock the old configuration against replacement"),Locked != INVALID_HANDLE_VALUE))
    {
        State.bGuideSeen = false;
        TestFalse(TEXT("A blocked atomic replacement reports failure"),State.Save(File,Error));
        CloseHandle(Locked);
        TestTrue(TEXT("The previous acceptance is preserved"),Restored.Load(File) && Restored.bGuideSeen);
    }
#endif
    const FString Readme = Directory / TEXT("使用规则.txt");
    TestTrue(TEXT("Write a model rule fixture"),FFileHelper::SaveStringToFile(TEXT("Synthetic model rule: do not redistribute."),*Readme,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    TestTrue(TEXT("Rules retain their original contents"),MmdNotices::Model(Directory).Text.Contains(TEXT("Synthetic model rule: do not redistribute.")));
    const TArray<uint8> Gbk{0xca,0xb9,0xd3,0xc3};
    TestTrue(TEXT("Write a legacy GBK readme"),FFileHelper::SaveArrayToFile(Gbk,*Readme));
    TestTrue(TEXT("Legacy Chinese model rules decode without losing their text"),MmdNotices::Model(Directory).Text.Contains(TEXT("使用")));
    TestFalse(TEXT("A fixture with no rules is not described as unrestricted"),MmdNotices::Model(Directory / TEXT("Absent")).Text.IsEmpty());
    IFileManager::Get().Delete(*Readme); IFileManager::Get().Delete(*File); IFileManager::Get().DeleteDirectory(*Directory);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdNoticeReadTimeTest,"HonkaiRendering.UI.NoticeReadTime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMmdNoticeReadTimeTest::RunTest(const FString&)
{
    FMmdNoticeReadTime Timer;
    for (int32 I=0;I<80;++I) Timer.Advance(.25f,false);
    TestFalse(TEXT("Hidden or unfocused review does not unlock the confirmation"),Timer.Ready());
    for (int32 I=0;I<19;++I) Timer.Advance(.25f,true);
    TestFalse(TEXT("Four and three-quarter visible seconds are insufficient"),Timer.Ready());
    Timer.Advance(.25f,true);
    TestTrue(TEXT("Five visible seconds unlock timing only"),Timer.Ready());
    FMmdNoticeReadTime Stalled;
    Stalled.Advance(30.f,true);
    TestFalse(TEXT("A single delayed frame cannot bypass the reading period"),Stalled.Ready());
    return true;
}
#endif
