// SPDX-License-Identifier: MIT
// Native MediaPipe inference stays outside Unreal so a driver/SDK failure cannot crash the editor.
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "AppContainer.h"
#include <opencv2/opencv.hpp>
#define MP_EXPORT
#include "mediapipe/tasks/c/vision/face_landmarker/face_landmarker.h"
#include "mediapipe/tasks/c/vision/hand_landmarker/hand_landmarker.h"
#include "mediapipe/tasks/c/vision/pose_landmarker/pose_landmarker.h"

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
static std::atomic<bool> Stop{false};
static BOOL WINAPI ConsoleSignal(DWORD) { Stop = true; return TRUE; }
static int64_t UnixMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
static std::string Utf8(const std::wstring& text) {
    if (text.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) throw std::runtime_error("Invalid Unicode path/name");
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}
static std::string JsonString(const std::string& value) {
    std::ostringstream out; out << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c) << std::dec;
        else out << c;
    }
    return out.str() + '"';
}
static void CheckHr(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        std::ostringstream error; error << operation << " failed (HRESULT 0x" << std::hex << static_cast<unsigned long>(hr) << ')';
        throw std::runtime_error(error.str());
    }
}
struct Options {
    fs::path output, image, model, handModel, poseModel, dll, stopFile;
    int camera = 0, width = 640, height = 480, fps = 30;
    float confidence = 0.5f;
    DWORD parent = 0;
    double maxSeconds = 0;
    uintptr_t sharedMap = 0, frameEvent = 0, frameMutex = 0;
    bool list = false, test = false, contained = false, probe = false, hands = true, pose = true;
};
static Options ParseOptions(int argc, wchar_t** argv) {
    wchar_t executable[32768];
    if (!GetModuleFileNameW(nullptr, executable, 32768)) throw std::runtime_error("Cannot locate helper executable");
    fs::path base = fs::path(executable).parent_path();
    Options o; o.dll = base / L"libmediapipe.dll"; o.model = base / L"Models/face_landmarker.task";
    o.handModel = base / L"Models/hand_landmarker.task"; o.poseModel = base / L"Models/pose_landmarker_lite.task";
    for (int i = 1; i < argc; ++i) {
        std::wstring flag = argv[i];
        if (flag == L"--list-cameras") { o.list = true; continue; }
        if (flag == L"--self-test") { o.test = true; continue; }
        if (flag == L"--contained-worker") { o.contained = true; continue; }
        if (flag == L"--probe-camera") { o.probe = true; continue; }
        if (flag == L"--no-hands") { o.hands = false; continue; }
        if (flag == L"--no-pose") { o.pose = false; continue; }
        if (++i == argc) throw std::runtime_error("Missing argument value");
        std::wstring value = argv[i];
        if (flag == L"--output") o.output = fs::absolute(value);
        else if (flag == L"--image") o.image = fs::absolute(value);
        else if (flag == L"--model") o.model = fs::absolute(value);
        else if (flag == L"--hand-model") o.handModel = fs::absolute(value);
        else if (flag == L"--pose-model") o.poseModel = fs::absolute(value);
        else if (flag == L"--dll") o.dll = fs::absolute(value);
        else if (flag == L"--stop-file") o.stopFile = fs::absolute(value);
        else if (flag == L"--camera") o.camera = std::stoi(value);
        else if (flag == L"--width") o.width = std::stoi(value);
        else if (flag == L"--height") o.height = std::stoi(value);
        else if (flag == L"--fps") o.fps = std::stoi(value);
        else if (flag == L"--confidence") o.confidence = std::stof(value);
        else if (flag == L"--parent-pid") o.parent = std::stoul(value);
        else if (flag == L"--max-seconds") o.maxSeconds = std::stod(value);
        else if (flag == L"--shared-map") o.sharedMap = std::stoull(value);
        else if (flag == L"--frame-event") o.frameEvent = std::stoull(value);
        else if (flag == L"--frame-mutex") o.frameMutex = std::stoull(value);
        else throw std::runtime_error("Unknown option: " + Utf8(flag));
    }
    if (o.width < 160 || o.width > 3840 || o.height < 120 || o.height > 2160 || o.fps < 1 || o.fps > 120 || o.camera < 0)
        throw std::runtime_error("Camera options outside supported bounds");
    if (!std::isfinite(o.confidence) || o.confidence < 0 || o.confidence > 1 || !std::isfinite(o.maxSeconds) || o.maxSeconds < 0)
        throw std::runtime_error("Invalid confidence or duration");
    if (!o.list && !o.test && o.output.empty()) throw std::runtime_error("--output is required");
    if (!o.list && !o.test && !o.contained && o.image.empty() && !o.parent && o.maxSeconds <= 0)
        throw std::runtime_error("Camera mode requires --parent-pid or bounded --max-seconds");
    return o;
}
static void WriteAtomic(const fs::path& target, const std::string& json) {
    if (target.empty()) return;
    fs::create_directories(target.parent_path());
    fs::path temp = target; temp += L".tmp." + std::to_wstring(GetCurrentProcessId());
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        file << json << '\n';
        if (!file) throw std::runtime_error("Cannot write output JSON");
    }
    for (int attempt = 0; attempt < 20; ++attempt) {
        if (MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) return;
        Sleep(2);
    }
    DeleteFileW(temp.c_str());
    throw std::runtime_error("Cannot atomically replace output JSON; reader must close its file handle");
}
struct HeadAngles { double yaw = 0, pitch = 0, roll = 0; };
static HeadAngles ExtractHead(const float* matrix) {
    // Official C Matrix is column-major. R = Rz(roll) Ry(yaw) Rx(pitch), all right-handed degrees.
    double r[3][3];
    for (int c = 0; c < 3; ++c) {
        double length = std::sqrt(matrix[c*4]*matrix[c*4] + matrix[c*4+1]*matrix[c*4+1] + matrix[c*4+2]*matrix[c*4+2]);
        if (!std::isfinite(length) || length < 1e-6) throw std::runtime_error("Invalid face rotation matrix");
        for (int row = 0; row < 3; ++row) r[row][c] = matrix[c*4+row] / length;
    }
    constexpr double degrees = 180.0 / 3.14159265358979323846;
    HeadAngles result;
    result.yaw = std::asin(std::clamp(-r[2][0], -1.0, 1.0)) * degrees;
    if (std::abs(r[2][0]) < 0.9999) {
        result.pitch = std::atan2(r[2][1], r[2][2]) * degrees;
        result.roll = std::atan2(r[1][0], r[0][0]) * degrees;
    } else {
        result.pitch = std::atan2(-r[1][2], r[1][1]) * degrees;
        result.roll = 0;
    }
    return result;
}
struct Snapshot {
    struct Point { float x = 0, y = 0, z = 0, visibility = 0, presence = 0; };
    struct Hand { std::string handedness; float score = 0; std::vector<Point> landmarks, worldLandmarks; };
    int64_t timestamp = UnixMs();
    bool tracked = false;
    HeadAngles head;
    std::map<std::string, float> blendshapes;
    std::vector<Point> poseLandmarks, poseWorldLandmarks;
    std::vector<Hand> hands;
    double latency = 0, fps = 0;
    int width = 0, height = 0;
    std::string status = "starting", error;
    std::string Json() const {
        std::ostringstream s; s.imbue(std::locale::classic()); s << std::fixed << std::setprecision(5);
        s << "{\"schema\":1,\"timestamp\":" << timestamp << ",\"tracked\":" << (tracked ? "true" : "false")
          << ",\"head\":{\"yaw\":" << head.yaw << ",\"pitch\":" << head.pitch << ",\"roll\":" << head.roll << "},\"blendshapes\":{";
        bool first = true;
        for (const auto& item : blendshapes) { if (!first) s << ','; first = false; s << JsonString(item.first) << ':' << item.second; }
        auto writePoint = [&s](const Point& p) {
            s << "{\"x\":" << p.x << ",\"y\":" << p.y << ",\"z\":" << p.z
              << ",\"visibility\":" << p.visibility << ",\"presence\":" << p.presence << '}';
        };
        s << "},\"pose_landmarks\":[";
        for (size_t i = 0; i < poseLandmarks.size(); ++i) { if (i) s << ','; writePoint(poseLandmarks[i]); }
        s << "],\"pose_world_landmarks\":[";
        for (size_t i = 0; i < poseWorldLandmarks.size(); ++i) { if (i) s << ','; writePoint(poseWorldLandmarks[i]); }
        s << "],\"hands\":[";
        for (size_t i = 0; i < hands.size(); ++i) {
            if (i) s << ','; s << "{\"handedness\":" << JsonString(hands[i].handedness) << ",\"score\":" << hands[i].score << ",\"landmarks\":[";
            for (size_t j = 0; j < hands[i].landmarks.size(); ++j) { if (j) s << ','; writePoint(hands[i].landmarks[j]); }
            s << "],\"world_landmarks\":[";
            for (size_t j = 0; j < hands[i].worldLandmarks.size(); ++j) { if (j) s << ','; writePoint(hands[i].worldLandmarks[j]); }
            s << "]}";
        }
        s << "],\"latency_ms\":" << latency << ",\"fps\":" << fps << ",\"width\":" << width << ",\"height\":" << height
          << ",\"status\":" << JsonString(status) << ",\"error\":" << JsonString(error) << '}';
        return s.str();
    }
};
class MediaPipe {
    HMODULE library = nullptr;
    MpFaceLandmarkerPtr task = nullptr;
    MpHandLandmarkerPtr handTask = nullptr;
    MpPoseLandmarkerPtr poseTask = nullptr;
    std::string modelPath;
    template<class T> T Load(const char* name) {
        auto p = GetProcAddress(library, name);
        if (!p) throw std::runtime_error(std::string("Missing MediaPipe 0.10.35 export: ") + name);
        return reinterpret_cast<T>(p);
    }
    void Check(MpStatus status, char* error) {
        std::string message = error ? error : "MediaPipe call failed without details";
        if (error) ErrorFree(error);
        if (status != kMpOk) throw std::runtime_error(message);
    }
    decltype(&MpFaceLandmarkerCreate) Create = nullptr;
    decltype(&MpFaceLandmarkerClose) Close = nullptr;
    decltype(&MpFaceLandmarkerDetectImage) DetectImage = nullptr;
    decltype(&MpFaceLandmarkerDetectForVideo) DetectVideo = nullptr;
    decltype(&MpFaceLandmarkerCloseResult) CloseResult = nullptr;
    decltype(&MpImageCreateFromFile) ImageFile = nullptr;
    decltype(&MpImageCreateFromUint8Data) ImageData = nullptr;
    decltype(&MpImageFree) ImageFree = nullptr;
    decltype(&MpErrorFree) ErrorFree = nullptr;
    decltype(&MpHandLandmarkerCreate) HandCreate = nullptr;
    decltype(&MpHandLandmarkerClose) HandClose = nullptr;
    decltype(&MpHandLandmarkerDetectImage) HandDetectImage = nullptr;
    decltype(&MpHandLandmarkerDetectForVideo) HandDetectVideo = nullptr;
    decltype(&MpHandLandmarkerCloseResult) HandCloseResult = nullptr;
    decltype(&MpPoseLandmarkerCreate) PoseCreate = nullptr;
    decltype(&MpPoseLandmarkerClose) PoseClose = nullptr;
    decltype(&MpPoseLandmarkerDetectImage) PoseDetectImage = nullptr;
    decltype(&MpPoseLandmarkerDetectForVideo) PoseDetectVideo = nullptr;
    decltype(&MpPoseLandmarkerCloseResult) PoseCloseResult = nullptr;
public:
    MediaPipe(const Options& o) {
        library = LoadLibraryExW(o.dll.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!library) throw std::runtime_error("Cannot load libmediapipe.dll; run Prepare-NativeTracking.ps1");
        try {
#define BIND(name, member) member = Load<decltype(member)>(#name)
            BIND(MpFaceLandmarkerCreate, Create); BIND(MpFaceLandmarkerClose, Close);
            BIND(MpFaceLandmarkerDetectImage, DetectImage); BIND(MpFaceLandmarkerDetectForVideo, DetectVideo);
            BIND(MpFaceLandmarkerCloseResult, CloseResult); BIND(MpImageCreateFromFile, ImageFile);
            BIND(MpImageCreateFromUint8Data, ImageData); BIND(MpImageFree, ImageFree); BIND(MpErrorFree, ErrorFree);
            if (o.hands) {
                BIND(MpHandLandmarkerCreate, HandCreate); BIND(MpHandLandmarkerClose, HandClose);
                BIND(MpHandLandmarkerDetectImage, HandDetectImage); BIND(MpHandLandmarkerDetectForVideo, HandDetectVideo);
                BIND(MpHandLandmarkerCloseResult, HandCloseResult);
            }
            if (o.pose) {
                BIND(MpPoseLandmarkerCreate, PoseCreate); BIND(MpPoseLandmarkerClose, PoseClose);
                BIND(MpPoseLandmarkerDetectImage, PoseDetectImage); BIND(MpPoseLandmarkerDetectForVideo, PoseDetectVideo);
                BIND(MpPoseLandmarkerCloseResult, PoseCloseResult);
            }
#undef BIND
            modelPath = Utf8(o.model.wstring());
            FaceLandmarkerOptions options{};
            options.base_options.model_asset_path = modelPath.c_str();
            options.base_options.delegate = CPU;
            options.base_options.host_environment = HOST_ENVIRONMENT_UNKNOWN;
            options.base_options.host_system = HOST_SYSTEM_WINDOWS;
            options.running_mode = o.image.empty() ? VIDEO : IMAGE;
            options.num_faces = 1;
            options.min_face_detection_confidence = o.confidence;
            options.min_face_presence_confidence = o.confidence;
            options.min_tracking_confidence = o.confidence;
            options.output_face_blendshapes = true;
            options.output_facial_transformation_matrixes = true;
            char* error = nullptr; MpStatus status = Create(&options, &task, &error); Check(status, error);
            if (o.hands) {
                HandLandmarkerOptions handOptions{}; std::string handPath = Utf8(o.handModel.wstring());
                handOptions.base_options.model_asset_path = handPath.c_str(); handOptions.base_options.delegate = CPU;
                handOptions.base_options.host_environment = HOST_ENVIRONMENT_UNKNOWN; handOptions.base_options.host_system = HOST_SYSTEM_WINDOWS;
                handOptions.running_mode = options.running_mode; handOptions.num_hands = 2;
                handOptions.min_hand_detection_confidence = o.confidence; handOptions.min_hand_presence_confidence = o.confidence; handOptions.min_tracking_confidence = o.confidence;
                status = HandCreate(&handOptions, &handTask, &error); Check(status, error);
            }
            if (o.pose) {
                PoseLandmarkerOptions poseOptions{}; std::string posePath = Utf8(o.poseModel.wstring());
                poseOptions.base_options.model_asset_path = posePath.c_str(); poseOptions.base_options.delegate = CPU;
                poseOptions.base_options.host_environment = HOST_ENVIRONMENT_UNKNOWN; poseOptions.base_options.host_system = HOST_SYSTEM_WINDOWS;
                poseOptions.running_mode = options.running_mode; poseOptions.num_poses = 1;
                poseOptions.min_pose_detection_confidence = o.confidence; poseOptions.min_pose_presence_confidence = o.confidence; poseOptions.min_tracking_confidence = o.confidence;
                status = PoseCreate(&poseOptions, &poseTask, &error); Check(status, error);
            }
        } catch (...) { if (poseTask) { char* e=nullptr; PoseClose(poseTask, &e); if(e) ErrorFree(e); } if (handTask) { char* e=nullptr; HandClose(handTask, &e); if(e) ErrorFree(e); } if(task) { char* e=nullptr; Close(task, &e); if(e) ErrorFree(e); } FreeLibrary(library); library = nullptr; throw; }
    }
    ~MediaPipe() {
        if (poseTask) { char* error = nullptr; PoseClose(poseTask, &error); if (error) ErrorFree(error); }
        if (handTask) { char* error = nullptr; HandClose(handTask, &error); if (error) ErrorFree(error); }
        if (task) { char* error = nullptr; Close(task, &error); if (error) ErrorFree(error); }
        if (library) FreeLibrary(library);
    }
    Snapshot Infer(const std::vector<uint8_t>* rgb, int width, int height, const fs::path& imagePath, int64_t videoMs) {
        const auto begin = Clock::now();
        Snapshot snapshot; snapshot.width = width; snapshot.height = height;
        MpImagePtr image = nullptr; char* error = nullptr;
        MpStatus status;
        if (rgb) status = ImageData(kMpImageFormatSrgb, width, height, rgb->data(), static_cast<int>(rgb->size()), &image, &error);
        else { const auto path = Utf8(imagePath.wstring()); status = ImageFile(path.c_str(), &image, &error); }
        Check(status, error);
        FaceLandmarkerResult result{};
        try {
            error = nullptr;
            status = rgb ? DetectVideo(task, image, nullptr, videoMs, &result, &error) : DetectImage(task, image, nullptr, &result, &error);
            Check(status, error);
            snapshot.tracked = result.face_landmarks_count > 0 && result.facial_transformation_matrixes_count > 0;
            snapshot.status = snapshot.tracked ? "tracking" : "face_not_found";
            if (snapshot.tracked) {
                const auto& matrix = result.facial_transformation_matrixes[0];
                if (matrix.rows != 4 || matrix.cols != 4 || !matrix.data) throw std::runtime_error("Unexpected face matrix shape");
                snapshot.head = ExtractHead(matrix.data);
                if (result.face_blendshapes_count) {
                    const auto& categories = result.face_blendshapes[0];
                    for (uint32_t i = 0; i < categories.categories_count; ++i) {
                        const auto& category = categories.categories[i];
                        if (category.category_name && std::isfinite(category.score)) snapshot.blendshapes[category.category_name] = std::clamp(category.score, 0.f, 1.f);
                    }
                }
            }
            if (poseTask) {
                PoseLandmarkerResult poseResult{}; error = nullptr;
                status = rgb ? PoseDetectVideo(poseTask, image, nullptr, videoMs, &poseResult, &error) : PoseDetectImage(poseTask, image, nullptr, &poseResult, &error);
                Check(status, error);
                if (poseResult.pose_landmarks_count > 0 && poseResult.pose_landmarks) {
                    const auto& list = poseResult.pose_landmarks[0];
                    for (uint32_t i=0; i<list.landmarks_count; ++i) { const auto& p=list.landmarks[i]; snapshot.poseLandmarks.push_back({p.x,p.y,p.z,p.has_visibility?p.visibility:0,p.has_presence?p.presence:0}); }
                }
                if (poseResult.pose_world_landmarks_count > 0 && poseResult.pose_world_landmarks) {
                    const auto& list = poseResult.pose_world_landmarks[0];
                    for (uint32_t i=0; i<list.landmarks_count; ++i) { const auto& p=list.landmarks[i]; snapshot.poseWorldLandmarks.push_back({p.x,p.y,p.z,p.has_visibility?p.visibility:0,p.has_presence?p.presence:0}); }
                }
                PoseCloseResult(&poseResult);
            }
            if (handTask) {
                HandLandmarkerResult handResult{}; error = nullptr;
                status = rgb ? HandDetectVideo(handTask, image, nullptr, videoMs, &handResult, &error) : HandDetectImage(handTask, image, nullptr, &handResult, &error);
                Check(status, error);
                const uint32_t count = std::min(handResult.hand_landmarks_count, handResult.handedness_count);
                for (uint32_t h=0; h<count; ++h) {
                    Snapshot::Hand hand; const auto& categories=handResult.handedness[h];
                    if (categories.categories_count) {
                        const auto& category = categories.categories[0];
                        if (category.category_name) hand.handedness = category.category_name;
                        else if (category.display_name) hand.handedness = category.display_name;
                    }
                    if (categories.categories_count) hand.score=std::clamp(categories.categories[0].score,0.f,1.f);
                    const auto& list=handResult.hand_landmarks[h];
                    for (uint32_t i=0; i<list.landmarks_count; ++i) { const auto& p=list.landmarks[i]; hand.landmarks.push_back({p.x,p.y,p.z,p.has_visibility?p.visibility:0,p.has_presence?p.presence:0}); }
                    if (h < handResult.hand_world_landmarks_count) {
                        const auto& world=handResult.hand_world_landmarks[h];
                        for (uint32_t i=0; i<world.landmarks_count; ++i) { const auto& p=world.landmarks[i]; hand.worldLandmarks.push_back({p.x,p.y,p.z,p.has_visibility?p.visibility:0,p.has_presence?p.presence:0}); }
                    }
                    snapshot.hands.push_back(std::move(hand));
                }
                HandCloseResult(&handResult);
            }
            CloseResult(&result); ImageFree(image);
        } catch (...) { CloseResult(&result); ImageFree(image); throw; }
        snapshot.latency = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
        return snapshot;
    }
};
struct MediaFoundation {
    MediaFoundation() { CheckHr(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "COM initialization"); CheckHr(MFStartup(MF_VERSION), "Media Foundation startup"); }
    ~MediaFoundation() { MFShutdown(); CoUninitialize(); }
};
static std::vector<ComPtr<IMFActivate>> Cameras() {
    ComPtr<IMFAttributes> attributes; CheckHr(MFCreateAttributes(&attributes, 1), "Create camera attributes");
    CheckHr(attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID), "Set camera source type");
    IMFActivate** raw = nullptr; UINT32 count = 0;
    CheckHr(MFEnumDeviceSources(attributes.Get(), &raw, &count), "Enumerate cameras");
    std::vector<ComPtr<IMFActivate>> cameras;
    for (UINT32 i = 0; i < count; ++i) { ComPtr<IMFActivate> device; device.Attach(raw[i]); cameras.push_back(device); }
    CoTaskMemFree(raw); return cameras;
}
static std::string CameraName(IMFActivate* device) {
    wchar_t* name = nullptr; UINT32 length = 0;
    CheckHr(device->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &length), "Read camera name");
    std::wstring copy(name, length); CoTaskMemFree(name); return Utf8(copy);
}
class Camera {
    ComPtr<IMFMediaSource> source;
    ComPtr<IMFSourceReader> reader;
    LONG stride = 0;
public:
    UINT32 width = 0, height = 0;
    Camera(IMFActivate* activate, const Options& o) {
        CheckHr(activate->ActivateObject(IID_PPV_ARGS(&source)), "Activate camera");
        ComPtr<IMFAttributes> attributes; CheckHr(MFCreateAttributes(&attributes, 2), "Create reader attributes");
        CheckHr(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE), "Enable RGB conversion");
        CheckHr(attributes->SetUINT32(MF_SOURCE_READER_DISCONNECT_MEDIASOURCE_ON_SHUTDOWN, FALSE), "Set reader lifetime");
        CheckHr(MFCreateSourceReaderFromMediaSource(source.Get(), attributes.Get(), &reader), "Create camera reader");
        CheckHr(reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE), "Disable extra camera streams");
        CheckHr(reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE), "Select camera video");
        ComPtr<IMFMediaType> best; double bestScore = 1e30;
        for (DWORD i = 0; ; ++i) {
            ComPtr<IMFMediaType> type; HRESULT hr = reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, i, &type);
            if (hr == MF_E_NO_MORE_TYPES) break;
            CheckHr(hr, "Read camera media type");
            UINT32 w = 0, h = 0, numerator = 0, denominator = 1;
            if (FAILED(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &w, &h)) || w > 3840 || h > 2160) continue;
            MFGetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, &numerator, &denominator);
            double rate = denominator ? double(numerator) / denominator : 0;
            double score = std::abs(int(w) - o.width) + std::abs(int(h) - o.height) + 5 * std::abs(rate - o.fps);
            if (score < bestScore) { best = type; bestScore = score; }
        }
        if (!best) throw std::runtime_error("Camera exposes no supported video resolution");
        CheckHr(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, best.Get()), "Set native camera mode");
        ComPtr<IMFMediaType> rgb; CheckHr(MFCreateMediaType(&rgb), "Create RGB camera type");
        rgb->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video); rgb->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        CheckHr(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, rgb.Get()), "Set RGB32 camera output");
        CheckHr(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &rgb), "Get camera output format");
        CheckHr(MFGetAttributeSize(rgb.Get(), MF_MT_FRAME_SIZE, &width, &height), "Get camera output size");
        UINT32 strideValue = 0;
        if (SUCCEEDED(rgb->GetUINT32(MF_MT_DEFAULT_STRIDE, &strideValue))) stride = static_cast<LONG>(strideValue);
        else CheckHr(MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, width, &stride), "Get camera stride");
    }
    ~Camera() { if (source) source->Shutdown(); }
    bool Read(std::vector<uint8_t>& rgb) {
        DWORD flags = 0; LONGLONG timestamp = 0; ComPtr<IMFSample> sample;
        CheckHr(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &timestamp, &sample), "Read camera sample");
        if (flags & MF_SOURCE_READERF_ERROR) throw std::runtime_error("Camera source reported an error");
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) throw std::runtime_error("Camera stream ended");
        if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) throw std::runtime_error("Camera format changed; restart tracking");
        if (!sample) return false;
        ComPtr<IMFMediaBuffer> buffer; CheckHr(sample->ConvertToContiguousBuffer(&buffer), "Access camera sample");
        ComPtr<IMF2DBuffer> twoD; BYTE* row0 = nullptr; LONG pitch = stride; bool locked2d = false;
        DWORD length = 0;
        if (SUCCEEDED(buffer.As(&twoD)) && SUCCEEDED(twoD->Lock2D(&row0, &pitch))) locked2d = true;
        else {
            CheckHr(buffer->Lock(&row0, nullptr, &length), "Lock camera buffer");
            if (length < size_t(std::abs(stride)) * height) { buffer->Unlock(); throw std::runtime_error("Truncated camera frame"); }
            if (stride < 0) row0 += size_t(-stride) * (height - 1);
        }
        rgb.resize(size_t(width) * height * 3);
        for (UINT32 y = 0; y < height; ++y) {
            const BYTE* row = row0 + ptrdiff_t(y) * pitch;
            uint8_t* dst = rgb.data() + size_t(y) * width * 3;
            for (UINT32 x = 0; x < width; ++x) { dst[x*3] = row[x*4+2]; dst[x*3+1] = row[x*4+1]; dst[x*3+2] = row[x*4]; }
        }
        if (locked2d) twoD->Unlock2D(); else buffer->Unlock();
        return true;
    }
};
static void SelfTest() {
    float m[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    auto head = ExtractHead(m);
    if (std::abs(head.yaw) + std::abs(head.pitch) + std::abs(head.roll) > 1e-5) throw std::runtime_error("Identity rotation failed");
    constexpr double rad = 3.14159265358979323846 / 180;
    double yaw = 23*rad, pitch = -14*rad, roll = 8*rad;
    double cy = cos(yaw), sy = sin(yaw), cp = cos(pitch), sp = sin(pitch), cr = cos(roll), sr = sin(roll);
    m[0]=float(cr*cy); m[1]=float(sr*cy); m[2]=float(-sy);
    m[4]=float(cr*sy*sp-sr*cp); m[5]=float(sr*sy*sp+cr*cp); m[6]=float(cy*sp);
    m[8]=float(cr*sy*cp+sr*sp); m[9]=float(sr*sy*cp-cr*sp); m[10]=float(cy*cp);
    head = ExtractHead(m);
    if (std::abs(head.yaw-23)>0.001 || std::abs(head.pitch+14)>0.001 || std::abs(head.roll-8)>0.001) throw std::runtime_error("Column-major Euler rotation failed");
    if (JsonString("a\n\"\\") != "\"a\\u000a\\\"\\\\\"") throw std::runtime_error("JSON escaping failed");
    std::cout << "NativeTracking self-test passed\n";
}
int wmain(int argc, wchar_t** argv) {
    Options options;
    std::atomic<bool> finished{false}; std::thread guardian; HANDLE parent = nullptr;
    int exitCode = 0;
    try {
        options = ParseOptions(argc, argv);
        if (options.test) { SelfTest(); return 0; }
        SetConsoleCtrlHandler(ConsoleSignal, TRUE);
        if (options.parent) {
            parent = OpenProcess(SYNCHRONIZE, FALSE, options.parent);
            if (!parent) throw std::runtime_error("Parent process is not accessible or already exited");
        }
        const auto started = Clock::now();
        // The watchdog also covers a blocked camera driver. It never leaves an orphan when UE exits.
        guardian = std::thread([&] {
            auto stopStarted = Clock::time_point::max();
            while (!finished) {
                if (parent && WaitForSingleObject(parent, 0) == WAIT_OBJECT_0) TerminateProcess(GetCurrentProcess(), 0);
                if (options.maxSeconds > 0 && std::chrono::duration<double>(Clock::now()-started).count() >= options.maxSeconds) Stop = true;
                std::error_code ignored;
                if (!options.stopFile.empty() && fs::exists(options.stopFile, ignored)) Stop = true;
                if (Stop) {
                    if (stopStarted == Clock::time_point::max()) stopStarted = Clock::now();
                    if (Clock::now()-stopStarted > std::chrono::seconds(3)) TerminateProcess(GetCurrentProcess(), 0);
                }
                Sleep(100);
            }
        });
        if (!options.list && !options.contained && !options.probe) {
            Snapshot starting; WriteAtomic(options.output, starting.Json());
            std::unique_ptr<cv::VideoCapture> capture;
            CaptureFrame read;
            uint64_t capturedFrames = 0; double meanSum = 0, minimum = 255, maximum = 0, reportedFps = 0;
            const auto captureBegin = Clock::now();
            if (options.image.empty()) {
                std::cerr << "Opening native OpenCV CAP_DSHOW camera broker\n";
                capture = std::make_unique<cv::VideoCapture>(options.camera, cv::CAP_DSHOW);
                if (!capture->isOpened()) throw std::runtime_error("DirectShow could not open the selected camera");
                const bool mjpgSet = capture->set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M','J','P','G'));
                capture->set(cv::CAP_PROP_FRAME_WIDTH, options.width); capture->set(cv::CAP_PROP_FRAME_HEIGHT, options.height);
                const bool fpsSet = capture->set(cv::CAP_PROP_FPS, options.fps); reportedFps = capture->get(cv::CAP_PROP_FPS);
                std::cerr << "Camera properties: MJPG request=" << mjpgSet << ", FPS request=" << fpsSet << ", FPS reported=" << reportedFps << ", FOURCC=" << capture->get(cv::CAP_PROP_FOURCC) << '\n';
                read = [&](std::vector<uint8_t>& rgb, int& width, int& height) {
                    cv::Mat bgr, converted;
                    if (!capture->read(bgr) || bgr.empty()) throw std::runtime_error("DirectShow returned no camera frame");
                    const auto channels = cv::mean(bgr); double frameMin = 0, frameMax = 0;
                    cv::minMaxLoc(bgr.reshape(1), &frameMin, &frameMax);
                    ++capturedFrames; meanSum += (channels[0]+channels[1]+channels[2])/3;
                    minimum = std::min(minimum, frameMin); maximum = std::max(maximum, frameMax);
                    cv::cvtColor(bgr, converted, cv::COLOR_BGR2RGB);
                    width = converted.cols; height = converted.rows;
                    rgb.assign(converted.data, converted.data + converted.total()*converted.elemSize());
                    return true;
                };
            }
            exitCode = LaunchOfflineAppContainer(argc, argv, options.output, options.stopFile, options.dll, options.model, options.image, Stop, read);
            if (capture) {
                std::ostringstream metrics; metrics.imbue(std::locale::classic());
                metrics << "{\"backend\":\"CAP_DSHOW\",\"frames\":" << capturedFrames
                    << ",\"requested_fps\":" << options.fps << ",\"reported_fps\":" << reportedFps
                    << ",\"measured_fps\":" << capturedFrames/std::chrono::duration<double>(Clock::now()-captureBegin).count()
                    << ",\"pixel_mean_0_255\":" << (capturedFrames ? meanSum/capturedFrames : 0)
                    << ",\"pixel_min\":" << (capturedFrames ? minimum : 0) << ",\"pixel_max\":" << maximum << '}';
                WriteAtomic(options.output.parent_path()/L"broker_metrics.json", metrics.str());
                std::cout << metrics.str() << std::endl;
            }
            if (exitCode != 0) {
                Snapshot failure; failure.status = "error";
                failure.error = "Offline worker exited with code " + std::to_string(static_cast<uint32_t>(exitCode)) + "; see worker.log in the tracking job directory";
                std::ifstream previous(options.output); std::string json((std::istreambuf_iterator<char>(previous)), {}); previous.close();
                if (json.find("\"status\":\"error\"") == std::string::npos) WriteAtomic(options.output, failure.Json());
            }
            if (capture) {
                // This device hangs in OpenCV's DirectShow graph destructor. The worker/profile
                // are already cleaned up; let Windows reclaim this dedicated capture process.
                TerminateProcess(GetCurrentProcess(), static_cast<UINT>(exitCode));
            }
        } else if (options.probe) {
            MediaFoundation foundation;
            const auto cameras = Cameras();
            if (size_t(options.camera) >= cameras.size()) throw std::runtime_error("Selected camera is unavailable");
            Camera camera(cameras[options.camera].Get(), options);
            std::vector<uint8_t> rgb; uint64_t frames = 0; const auto probeStart = Clock::now();
            while (!Stop && std::chrono::duration<double>(Clock::now()-probeStart).count() < std::min(5.0, options.maxSeconds)) {
                if (camera.Read(rgb)) ++frames;
            }
            Snapshot probe; probe.status = "camera_probe_complete"; probe.width = int(camera.width); probe.height = int(camera.height);
            probe.fps = frames / std::chrono::duration<double>(Clock::now()-probeStart).count();
            WriteAtomic(options.output, probe.Json()); std::cout << probe.Json() << "\nFrames captured without SDK or disk recording: " << frames << '\n';
        } else {
        if (options.contained) { RequireOfflineAppContainer(); std::cerr << "Offline AppContainer token verified; webcam capability only.\n"; }
        MediaFoundation foundation;
        if (options.list) {
            const auto cameras = Cameras(); std::cout << "{\"cameras\":[";
            for (size_t i=0; i<cameras.size(); ++i) { if(i) std::cout << ','; std::cout << "{\"index\":" << i << ",\"name\":" << JsonString(CameraName(cameras[i].Get())) << '}'; }
            std::cout << "]}\n";
        } else {
            Snapshot starting; WriteAtomic(options.output, starting.Json());
            MediaPipe mediapipe(options);
            if (!options.image.empty()) {
                auto result = mediapipe.Infer(nullptr, 0, 0, options.image, 0);
                WriteAtomic(options.output, result.Json()); std::cout << result.Json() << '\n';
            } else {
                if (!options.sharedMap || !options.frameEvent || !options.frameMutex) throw std::runtime_error("Isolated camera worker requires broker frame handles");
                std::vector<uint8_t> rgb; uint64_t frames = 0, trackedFrames = 0; double latencySum = 0;
                int width = 0, height = 0; int64_t capturedMs = 0;
                auto frameStart = Clock::now(), previous = frameStart, next = frameStart;
                int64_t lastVideoMs = -1; double smoothedFps = 0;
                while (!Stop && (options.maxSeconds <= 0 || std::chrono::duration<double>(Clock::now()-started).count() < options.maxSeconds)) {
                    if (!ReadIsolatedFrame(options.sharedMap, options.frameEvent, options.frameMutex, rgb, width, height, capturedMs)) continue;
                    const auto now = Clock::now();
                    if (now < next) continue;
                    next = now + std::chrono::milliseconds(1000/options.fps);
                    int64_t videoMs = std::max(lastVideoMs+1, std::chrono::duration_cast<std::chrono::milliseconds>(now-frameStart).count()); lastVideoMs = videoMs;
                    auto result = mediapipe.Infer(&rgb, width, height, {}, videoMs); result.timestamp = capturedMs;
                    double dt = std::chrono::duration<double>(now-previous).count(); previous = now;
                    if (frames > 0 && dt > 0) smoothedFps = smoothedFps == 0 ? 1/dt : smoothedFps*0.9 + 0.1/dt;
                    result.fps = smoothedFps; ++frames; if (result.tracked) ++trackedFrames; latencySum += result.latency;
                    WriteAtomic(options.output, result.Json());
                }
                Snapshot stopped; stopped.status = "stopped"; WriteAtomic(options.output, stopped.Json());
                const double seconds = std::chrono::duration<double>(Clock::now()-frameStart).count();
                std::cout << "{\"summary\":true,\"frames\":" << frames << ",\"tracked_frames\":" << trackedFrames << ",\"fps\":" << (seconds>0 ? frames/seconds : 0)
                          << ",\"mean_latency_ms\":" << (frames ? latencySum/frames : 0) << "}" << std::endl;
            }
        }
        }
    } catch (const std::exception& error) {
        Snapshot failure; failure.status = "error"; failure.error = error.what();
        try { WriteAtomic(options.output, failure.Json()); } catch (...) {}
        std::cerr << failure.Json() << '\n'; exitCode = 1;
    }
    finished = true; if (guardian.joinable()) guardian.join(); if (parent) CloseHandle(parent);
    return exitCode;
}
