#include <Windows.h>
#include <wrl/client.h>
#include "SpoutDX.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;
namespace fs = std::filesystem;

struct FrameCounterStats
{
    long First = 0, Last = 0;
    uint64_t Advances = 0, Delta = 0, Resets = 0;
    double FirstTime = 0., LastTime = 0.;
    void Observe(long Counter, double Time)
    {
        if (Counter <= 0) return;
        if (Last == 0) { First = Last = Counter; FirstTime = LastTime = Time; return; }
        if (Counter == Last) return;
        if (Counter < Last) { ++Resets; Last = Counter; return; }
        ++Advances;
        Delta += static_cast<uint64_t>(Counter - Last);
        Last = Counter;
        LastTime = Time;
    }
    bool HasRate() const { return Advances > 0 && Resets == 0 && LastTime > FirstTime; }
    double Rate() const { return Delta / (LastTime - FirstTime); }
};

static int CounterSelfTest()
{
    FrameCounterStats Stats;
    Stats.Observe(0, 0.); Stats.Observe(0, 1.);
    if (Stats.HasRate() || Stats.Advances != 0) return 1;
    Stats.Observe(100, 1.); Stats.Observe(100, 1.1);
    if (Stats.HasRate() || Stats.Advances != 0) return 1;
    Stats.Observe(103, 1.1);
    if (!Stats.HasRate() || Stats.Advances != 1 || Stats.Delta != 3 || std::abs(Stats.Rate() - 30.) > 0.001) return 1;
    Stats.Observe(101, 1.2);
    if (Stats.HasRate() || Stats.Resets != 1) return 1;
    std::cout << "Counter self-test passed; no graphics device was opened.\n";
    return 0;
}

static std::string Quote(const std::string& Text)
{
    std::string Result = "\"";
    for (char C : Text)
    {
        if (C == '\\' || C == '"') Result += '\\';
        if (C == '\n') Result += "\\n";
        else if (C == '\r') Result += "\\r";
        else if (C == '\t') Result += "\\t";
        else Result += C;
    }
    return Result + "\"";
}

static bool WriteBitmap(const fs::path& Path, unsigned Width, unsigned Height, const std::vector<uint8_t>& Bgra)
{
    BITMAPFILEHEADER File{};
    BITMAPV4HEADER Info{};
    File.bfType = 0x4d42;
    File.bfOffBits = sizeof(File) + sizeof(Info);
    File.bfSize = File.bfOffBits + static_cast<DWORD>(Bgra.size());
    Info.bV4Size = sizeof(Info);
    Info.bV4Width = static_cast<LONG>(Width);
    Info.bV4Height = -static_cast<LONG>(Height);
    Info.bV4Planes = 1;
    Info.bV4BitCount = 32;
    Info.bV4V4Compression = BI_BITFIELDS;
    Info.bV4SizeImage = static_cast<DWORD>(Bgra.size());
    Info.bV4RedMask = 0x00ff0000;
    Info.bV4GreenMask = 0x0000ff00;
    Info.bV4BlueMask = 0x000000ff;
    Info.bV4AlphaMask = 0xff000000;
    Info.bV4CSType = LCS_sRGB;
    std::ofstream Stream(Path, std::ios::binary);
    Stream.write(reinterpret_cast<const char*>(&File), sizeof(File));
    Stream.write(reinterpret_cast<const char*>(&Info), sizeof(Info));
    Stream.write(reinterpret_cast<const char*>(Bgra.data()), static_cast<std::streamsize>(Bgra.size()));
    return Stream.good();
}

int wmain(int Argc, wchar_t** Argv)
{
    if (Argc == 2 && std::wstring(Argv[1]) == L"--self-test") return CounterSelfTest();
    fs::path Directory = fs::path(L"Saved") / L"Tests" / L"SpoutProbe";
    double Seconds = 10.;
    for (int Index = 1; Index < Argc; ++Index)
    {
        const std::wstring Arg = Argv[Index];
        if (Arg == L"--out" && Index + 1 < Argc) Directory = Argv[++Index];
        else if (Arg == L"--seconds" && Index + 1 < Argc)
        {
            try { Seconds = std::stod(Argv[++Index]); }
            catch (...) { Seconds = 0.; }
        }
        else
        {
            std::cerr << "Usage: HonkaiSpoutProbe [--out directory] [--seconds 1..10]\n";
            return 2;
        }
    }
    if (!(Seconds >= 1. && Seconds <= 10.))
    {
        std::cerr << "Duration must be between 1 and 10 seconds.\n";
        return 2;
    }
    std::error_code FileError;
    fs::create_directories(Directory, FileError);
    if (FileError)
    {
        std::cerr << "Cannot create output directory: " << FileError.message() << '\n';
        return 2;
    }

    const auto Start = Clock::now();
    const auto Deadline = Start + std::chrono::duration<double>(Seconds);
    spoutDX Receiver;
    // An explicit receiver name does not change the global active sender or any OBS scene.
    Receiver.SetReceiverName("HonkaiVTuber");
    Receiver.SetAdapterAuto(true);
    std::string Error;
    if (!Receiver.OpenDirectX11()) Error = "Cannot create the Spout D3D11 receiver device.";

    uint64_t ReceivePolls = 0, NewFrameFlags = 0;
    FrameCounterStats Counters;
    const bool FrameCountEnabled = Receiver.IsFrameCountEnabled();
    unsigned Width = 0, Height = 0;
    DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN;
    double SenderFps = 0.;
    ComPtr<ID3D11Texture2D> Staging;
    bool CopyPending = false;
    std::vector<uint8_t> Snapshot;
    while (Error.empty() && Clock::now() < Deadline)
    {
        const bool Received = Receiver.ReceiveTexture();
        const bool Updated = Receiver.IsUpdated();
        if (Received && Receiver.IsConnected())
        {
            if (Updated && !Snapshot.empty())
            {
                Error = "The sender changed after the snapshot; rerun with a fixed output.";
                break;
            }
            if (std::string(Receiver.GetSenderName()) != "HonkaiVTuber")
            {
                Error = "The receiver selected an unexpected sender.";
                break;
            }
            Width = Receiver.GetSenderWidth();
            Height = Receiver.GetSenderHeight();
            Format = Receiver.GetSenderFormat();
            SenderFps = Receiver.GetSenderFps();
            if (!Updated)
            {
                ++ReceivePolls;
                if (Receiver.IsFrameNew()) ++NewFrameFlags;
                Counters.Observe(Receiver.GetSenderFrame(), std::chrono::duration<double>(Clock::now() - Start).count());
            }
            if (Updated && CopyPending)
            {
                Staging.Reset();
                CopyPending = false;
            }
            if (ReceivePolls >= 3 && Snapshot.empty() && !CopyPending &&
                std::chrono::duration<double>(Clock::now() - Start).count() >= 0.5)
            {
                ID3D11Texture2D* Texture = Receiver.GetSenderTexture();
                D3D11_TEXTURE2D_DESC Desc{};
                if (Texture) Texture->GetDesc(&Desc);
                const bool Bgra = Format == DXGI_FORMAT_B8G8R8A8_UNORM || Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
                const bool Rgba = Format == DXGI_FORMAT_R8G8B8A8_UNORM || Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
                if (!Texture || !Width || !Height || Width > 8192 || Height > 8192 || (!Bgra && !Rgba) || Desc.SampleDesc.Count != 1)
                {
                    Error = "Unsupported or invalid sender texture; expected single-sample RGBA8/BGRA8.";
                    break;
                }
                Desc.Usage = D3D11_USAGE_STAGING;
                Desc.BindFlags = 0;
                Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                Desc.MiscFlags = 0;
                const HRESULT Created = Receiver.GetDX11Device()->CreateTexture2D(&Desc, nullptr, &Staging);
                if (FAILED(Created))
                {
                    Error = "Cannot allocate the CPU readback staging texture.";
                    break;
                }
                Receiver.GetDX11Context()->CopyResource(Staging.Get(), Texture);
                Receiver.GetDX11Context()->Flush();
                CopyPending = true;
            }
        }
        if (CopyPending)
        {
            D3D11_MAPPED_SUBRESOURCE Mapped{};
            const HRESULT Result = Receiver.GetDX11Context()->Map(Staging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &Mapped);
            if (SUCCEEDED(Result))
            {
                Snapshot.resize(static_cast<size_t>(Width) * Height * 4);
                for (unsigned Row = 0; Row < Height; ++Row)
                    memcpy(Snapshot.data() + static_cast<size_t>(Row) * Width * 4,
                        static_cast<const uint8_t*>(Mapped.pData) + static_cast<size_t>(Row) * Mapped.RowPitch, static_cast<size_t>(Width) * 4);
                Receiver.GetDX11Context()->Unmap(Staging.Get(), 0);
                if (Format == DXGI_FORMAT_R8G8B8A8_UNORM || Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
                    for (size_t Pixel = 0; Pixel < Snapshot.size(); Pixel += 4) std::swap(Snapshot[Pixel], Snapshot[Pixel + 2]);
                CopyPending = false;
                Staging.Reset();
            }
            else if (Result != DXGI_ERROR_WAS_STILL_DRAWING)
            {
                Error = "GPU texture readback failed.";
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const double Elapsed = std::chrono::duration<double>(Clock::now() - Start).count();
    Staging.Reset();
    Receiver.ReleaseReceiver();
    Receiver.CloseDirectX11();
    if (Error.empty() && Snapshot.empty()) Error = "No complete frame received before the deadline.";

    std::array<uint64_t, 256> Histogram{};
    for (size_t Index = 3; Index < Snapshot.size(); Index += 4) ++Histogram[Snapshot[Index]];
    int Minimum = 255, Maximum = 0;
    for (int Alpha = 0; Alpha < 256; ++Alpha)
        if (Histogram[Alpha]) { Minimum = (std::min)(Minimum, Alpha); Maximum = Alpha; }
    const uint64_t PixelCount = Snapshot.size() / 4;
    const fs::path BitmapPath = Directory / L"received.bmp";
    if (!Snapshot.empty() && !WriteBitmap(BitmapPath, Width, Height, Snapshot)) Error = "Cannot save received.bmp.";
    std::ostringstream Json;
    Json << "{\n  \"sender\": \"HonkaiVTuber\",\n  \"ok\": " << (Error.empty() ? "true" : "false")
         << ",\n  \"error\": " << Quote(Error) << ",\n  \"width\": " << Width << ",\n  \"height\": " << Height
         << ",\n  \"dxgi_format\": " << static_cast<unsigned>(Format)
         << ",\n  \"successful_receive_polls\": " << ReceivePolls
         << ",\n  \"sdk_new_frame_flags\": " << NewFrameFlags
         << ",\n  \"sdk_frame_count_enabled\": " << (FrameCountEnabled ? "true" : "false")
         << ",\n  \"sender_frame_counter_first\": " << Counters.First
         << ",\n  \"sender_frame_counter_last\": " << Counters.Last
         << ",\n  \"sender_frame_counter_resets\": " << Counters.Resets
         << ",\n  \"received_new_frames\": " << (Counters.HasRate() ? std::to_string(Counters.Advances + 1) : "null")
         << ",\n  \"sender_frame_counter_delta\": " << (Counters.HasRate() ? std::to_string(Counters.Delta) : "null")
         << ",\n  \"elapsed_seconds\": " << Elapsed
         << ",\n  \"receive_poll_hz\": " << ReceivePolls / (std::max)(Elapsed, 0.001)
         << ",\n  \"observed_sender_counter_hz\": " << (Counters.HasRate() ? std::to_string(Counters.Rate()) : "null")
         << ",\n  \"reported_sender_fps\": " << (Counters.HasRate() ? std::to_string(SenderFps) : "null")
         << ",\n  \"frame_rate_note\": " << Quote(Counters.HasRate() ? "Rate derived from positive advancing sender frame counters." : "Unavailable: SDK flags may identify receive polls rather than new sender frames; no advancing frame counter was observed.")
         << ",\n  \"alpha_min\": " << (PixelCount ? Minimum : -1) << ",\n  \"alpha_max\": " << (PixelCount ? Maximum : -1)
         << ",\n  \"alpha_zero\": " << Histogram[0] << ",\n  \"alpha_full\": " << Histogram[255]
         << ",\n  \"alpha_partial\": " << PixelCount - Histogram[0] - Histogram[255]
         << ",\n  \"opaque_verified\": " << (Error.empty() && PixelCount && Histogram[255] == PixelCount ? "true" : "false")
         << ",\n  \"snapshot\": " << Quote(Snapshot.empty() ? "" : BitmapPath.u8string()) << "\n}\n";
    std::ofstream Report(Directory / L"receiver.json");
    Report << Json.str();
    if (!Report.good()) { std::cerr << "Cannot write receiver.json.\n"; return 2; }
    std::cout << Json.str();
    return Error.empty() ? 0 : 1;
}
