#include "MmdReplaySequence.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"

bool FMmdReplaySequence::Load(const FString& Path, float Fps, bool bLoop, bool bMirror, FString& Error)
{
    Frames.Reset();
    const int64 Size = IFileManager::Get().FileSize(*Path);
    FString Text;
    if (Size <= 0 || Size > 1024 * 1024 || !FFileHelper::LoadFileToString(Text, *Path))
    { Error = TEXT("回放文件必须可读、非空，且不超过 1 MiB"); return false; }
    return Parse(Text, Fps, bLoop, bMirror, Error);
}

bool FMmdReplaySequence::Parse(const FString& JsonLines, float Fps, bool bLoop, bool bMirror, FString& Error)
{
    Frames.Reset(); Error.Reset();
    if (!FMath::IsFinite(Fps) || Fps < 1 || Fps > 240)
    { Error = TEXT("回放帧率必须在 1 到 240 之间"); return false; }
    if (JsonLines.IsEmpty() || JsonLines.Len() > 1024 * 1024 || FTCHARToUTF8(*JsonLines).Length() > 1024 * 1024)
    { Error = TEXT("回放内容必须非空，且不超过 1 MiB"); return false; }
    TArray<FString> Lines;
    JsonLines.ParseIntoArrayLines(Lines, false);
    TArray<FVtuberTrackingSnapshot> Parsed;
    for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
    {
        const FString Line = Lines[LineIndex].TrimStartAndEnd();
        if (Line.IsEmpty()) continue;
        FVtuberTrackingSnapshot Frame;
        if (Parsed.Num() >= 600 || !FNativeTracking::ParseReplayFrameJson(Line, bMirror, Frame))
        { Error = FString::Printf(TEXT("回放第 %d 行无效，或帧数超过 600"), LineIndex + 1); return false; }
        Parsed.Add(MoveTemp(Frame));
    }
    if (Parsed.IsEmpty()) { Error = TEXT("回放没有有效帧"); return false; }
    Frames = MoveTemp(Parsed); FramesPerSecond = Fps; bLooping = bLoop;
    return true;
}

int32 FMmdReplaySequence::FrameIndex(double ElapsedWorldSeconds) const
{
    if (Frames.IsEmpty() || !FMath::IsFinite(ElapsedWorldSeconds)) return INDEX_NONE;
    const double Time = FMath::Max(0., ElapsedWorldSeconds);
    if (!bLooping && Time >= (Frames.Num() - 1) / double(FramesPerSecond)) return Frames.Num() - 1;
    const double Position = FMath::FloorToDouble(Time * FramesPerSecond);
    if (!FMath::IsFinite(Position)) return INDEX_NONE;
    return bLooping ? static_cast<int32>(FMath::Fmod(Position, double(Frames.Num()))) : static_cast<int32>(Position);
}

bool FMmdReplaySequence::Sample(double ElapsedWorldSeconds, FVtuberTrackingSnapshot& Out) const
{
    const int32 Index = FrameIndex(ElapsedWorldSeconds);
    if (!Frames.IsValidIndex(Index)) return false;
    Out = Frames[Index];
    return true;
}

float MmdQaCaptureDelay(const TCHAR* CommandLine)
{
    float Delay = 8;
    FParse::Value(CommandLine, TEXT("MmdCaptureDelay="), Delay);
    return FMath::IsFinite(Delay) && Delay >= .05f && Delay <= 120 ? Delay : 8.f;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdReplaySequenceTest, "HonkaiVTuber.Tracking.ReplaySequence",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMmdReplaySequenceTest::RunTest(const FString& Parameters)
{
    const FString Left = TEXT("{\"schema\":1,\"timestamp\":1,\"tracked\":true,\"head\":{\"yaw\":15,\"pitch\":0,\"roll\":0},\"blendshapes\":{\"eyeBlinkLeft\":0.75,\"eyeBlinkRight\":0.1}}");
    const FString Right = TEXT("{\"schema\":1,\"timestamp\":2,\"tracked\":true,\"head\":{\"yaw\":-15,\"pitch\":0,\"roll\":0},\"blendshapes\":{\"eyeBlinkLeft\":0.2,\"eyeBlinkRight\":0.8}}");
    FString Error;
    FMmdReplaySequence Sequence;
    if (!TestTrue(TEXT("Actual native parser loads a JSONL sequence"), Sequence.Parse(Left + TEXT("\r\n\n") + Right, 60, false, false, Error))) return false;
    TestEqual(TEXT("Blank lines are not fabricated frames"), Sequence.Num(), 2);
    TestEqual(TEXT("Negative time starts at the first frame"), Sequence.FrameIndex(-1), 0);
    TestEqual(TEXT("Subframe times hold their frame"), Sequence.FrameIndex(.5 / 60), 0);
    TestEqual(TEXT("Explicit FPS advances from elapsed world time"), Sequence.FrameIndex(1. / 60), 1);
    TestEqual(TEXT("Nonlooping playback holds its tail"), Sequence.FrameIndex(1.e100), 1);
    FVtuberTrackingSnapshot Plain, Mirrored;
    TestTrue(TEXT("First source frame samples"), Sequence.Sample(0, Plain));
    TestEqual(TEXT("Unmirrored input retains its anatomical side"), Plain.BlendShapes.FindRef(TEXT("eyeBlinkLeft")), .75f);
    TestTrue(TEXT("The same raw frame parses with exactly one mirror"), FNativeTracking::ParseReplayFrameJson(Left, true, Mirrored));
    TestEqual(TEXT("Raw left moves to canonical right exactly once"), Mirrored.BlendShapes.FindRef(TEXT("eyeBlinkRight")), .75f);
    TestTrue(TEXT("Mirror is applied to the parsed head once"), Mirrored.HeadRotation.Equals(FQuat(-Plain.HeadRotation.X, Plain.HeadRotation.Y, -Plain.HeadRotation.Z, Plain.HeadRotation.W), 1.e-6));
    TestTrue(TEXT("Looping sequence loads"), Sequence.Parse(Left + TEXT("\n") + Right, 60, true, true, Error));
    TestEqual(TEXT("Loop boundary returns to the first frame"), Sequence.FrameIndex(2. / 60), 0);
    TestEqual(TEXT("Next loop advances normally"), Sequence.FrameIndex(3.5 / 60), 1);
    TestTrue(TEXT("Sequence mirrors through the same entry"), Sequence.Sample(0, Mirrored));
    TestEqual(TEXT("Sequence input is not mirrored twice"), Mirrored.BlendShapes.FindRef(TEXT("eyeBlinkRight")), .75f);
    const TArray<FString> BadCases = { FString(), FString(TEXT("\n \r\n")), Left.LeftChop(1), Left.Replace(TEXT("\"schema\":1"), TEXT("\"schema\":2")),
        Left.Replace(TEXT("\"tracked\":true"), TEXT("\"tracked\":\"true\"")), Left.Replace(TEXT("\"yaw\":15"), TEXT("\"yaw\":\"bad\"")),
        Left.Replace(TEXT("\"eyeBlinkLeft\":0.75"), TEXT("\"eyeBlinkLeft\":\"bad\"")), Left.Replace(TEXT("\"timestamp\":1"), TEXT("\"timestamp\":0")),
        FString(TEXT("{\"schema\":1,\"timestamp\":1,\"tracked\":false,\"pose_landmarks\":[{}]}")),
        FString(TEXT("{\"schema\":1,\"timestamp\":1,\"tracked\":false,\"hands\":[{},{},{}]}")), Left + TEXT("\n{}"),
        Left.Replace(TEXT("\"schema\":1"), TEXT("\"schema\":\"1\"")), Left.Replace(TEXT("\"schema\":1"), TEXT("\"schema\":true")),
        Left.Replace(TEXT("\"timestamp\":1"), TEXT("\"timestamp\":\"1\"")), Left.Replace(TEXT("\"tracked\":true"), TEXT("\"tracked\":1")),
        Left.Replace(TEXT("\"yaw\":15"), TEXT("\"yaw\":\"15\"")), Left.Replace(TEXT("\"eyeBlinkLeft\":0.75"), TEXT("\"eyeBlinkLeft\":\"0.75\"")),
        Left.Replace(TEXT("\"eyeBlinkLeft\":0.75"), TEXT("\"eyeBlinkLeft\":true")) };
    for (int32 CaseIndex = 0; CaseIndex < BadCases.Num(); ++CaseIndex)
    {
        const FString Label = FString::Printf(TEXT("Invalid case %d: "), CaseIndex);
        TestFalse(Label + TEXT("sequence is rejected"), Sequence.Parse(BadCases[CaseIndex], 60, false, false, Error));
        TestEqual(Label + TEXT("rejected sequence cannot retain an earlier valid sample"), Sequence.Num(), 0);
    }
    FString Limit;
    for (int32 Index = 0; Index < 600; ++Index) Limit += Left + TEXT("\n");
    TestTrue(TEXT("Exactly 600 actual frames are allowed"), Sequence.Parse(Limit, 30, false, false, Error));
    TestFalse(TEXT("Frame 601 is rejected"), Sequence.Parse(Limit + Left, 30, false, false, Error));
    TestFalse(TEXT("Content larger than one MiB is rejected"), Sequence.Parse(FString::ChrN(1024 * 1024 + 1, TCHAR(' ')), 60, false, false, Error));
    TestFalse(TEXT("A single frame cannot bypass the native 64 KiB bound"), FNativeTracking::ParseReplayFrameJson(FString::ChrN(65537, TCHAR(' ')), false, Plain));
    FString Loop56;
    for (int32 Index = 0; Index < 56; ++Index) Loop56 += Left + TEXT("\n");
    TestTrue(TEXT("The actual 56-frame QA cadence loads"), Sequence.Parse(Loop56, 60, true, true, Error));
    TestEqual(TEXT("Frame-domain modulo does not lose one frame at whole seconds"), Sequence.FrameIndex(2), 8);
    for (const float Fps : { 0.f, -1.f, 241.f })
        TestFalse(TEXT("Invalid explicit frame rates are rejected"), Sequence.Parse(Left, Fps, false, false, Error));
    TestEqual(TEXT("Default screenshot timing is unchanged"), MmdQaCaptureDelay(TEXT("")), 8.f);
    TestEqual(TEXT("A QA screenshot can wait 4.1 world seconds"), MmdQaCaptureDelay(TEXT("-MmdCaptureDelay=4.1")), 4.1f);
    TestTrue(TEXT("QA exit follows screenshot"), MmdQaCaptureDelay(TEXT("-MmdCaptureDelay=4.1")) + 4 > 4.1f);
    TestEqual(TEXT("An invalid screenshot delay retains its default"), MmdQaCaptureDelay(TEXT("-MmdCaptureDelay=-1")), 8.f);
    return true;
}
#endif
