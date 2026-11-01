// SPDX-License-Identifier: MIT
// Uses the documented Windows AppContainer API; no network capability is granted.
#include "AppContainer.h"
#include <windows.h>
#include <userenv.h>
#include <aclapi.h>
#include <sddl.h>
#include <securitybaseapi.h>
#include <array>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace fs = std::filesystem;
struct SharedFrame {
    uint32_t width, height, size;
    int64_t timestamp;
    uint8_t pixels[3840 * 2160 * 3];
};
bool ReadIsolatedFrame(uintptr_t mapping, uintptr_t event, uintptr_t mutex,
    std::vector<uint8_t>& rgb, int& width, int& height, int64_t& timestamp) {
    if (WaitForSingleObject(reinterpret_cast<HANDLE>(event), 100) == WAIT_TIMEOUT) return false;
    DWORD locked = WaitForSingleObject(reinterpret_cast<HANDLE>(mutex), 100);
    if (locked != WAIT_OBJECT_0 && locked != WAIT_ABANDONED) return false;
    auto* frame = static_cast<SharedFrame*>(MapViewOfFile(reinterpret_cast<HANDLE>(mapping), FILE_MAP_READ, 0, 0, sizeof(SharedFrame)));
    if (!frame) { ReleaseMutex(reinterpret_cast<HANDLE>(mutex)); throw std::runtime_error("Cannot map isolated RGB frame"); }
    const bool valid = frame->width > 0 && frame->height > 0 && frame->width <= 3840 && frame->height <= 2160 && frame->size == frame->width*frame->height*3;
    if (valid) {
        width = static_cast<int>(frame->width); height = static_cast<int>(frame->height); timestamp = frame->timestamp;
        rgb.assign(frame->pixels, frame->pixels + frame->size);
    }
    UnmapViewOfFile(frame); ReleaseMutex(reinterpret_cast<HANDLE>(mutex));
    if (!valid) throw std::runtime_error("Invalid isolated RGB frame dimensions");
    return true;
}
static void WinCheck(bool ok, const char* operation) {
    if (!ok) throw std::runtime_error(std::string(operation) + " (Win32 " + std::to_string(GetLastError()) + ")");
}
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
static PSID WebcamSid() {
    PSID* group = nullptr; DWORD groupCount = 0; PSID* capability = nullptr; DWORD capabilityCount = 0;
    WinCheck(DeriveCapabilitySidsFromName(L"webcam", &group, &groupCount, &capability, &capabilityCount) != FALSE, "Derive webcam capability");
    for (DWORD i = 0; i < groupCount; ++i) LocalFree(group[i]); LocalFree(group);
    if (capabilityCount != 1) { for (DWORD i = 0; i < capabilityCount; ++i) LocalFree(capability[i]); LocalFree(capability); throw std::runtime_error("Unexpected webcam capability SID count"); }
    PSID result = capability[0]; LocalFree(capability); return result;
}
void RequireOfflineAppContainer() {
    Handle token; WinCheck(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value) != FALSE, "Read worker isolation token");
    DWORD appContainer = 0, bytes = 0;
    WinCheck(GetTokenInformation(token.value, TokenIsAppContainer, &appContainer, sizeof(appContainer), &bytes) != FALSE, "Check AppContainer token");
    if (!appContainer) throw std::runtime_error("Worker refused: native inference requires the offline AppContainer launcher");
    GetTokenInformation(token.value, TokenCapabilities, nullptr, 0, &bytes);
    std::vector<BYTE> storage(bytes);
    WinCheck(GetTokenInformation(token.value, TokenCapabilities, storage.data(), bytes, &bytes) != FALSE, "Read worker capabilities");
    const auto* groups = reinterpret_cast<const TOKEN_GROUPS*>(storage.data());
    PSID camera = WebcamSid(); bool unexpected = false;
    for (DWORD i=0; i<groups->GroupCount; ++i) if (!EqualSid(groups->Groups[i].Sid, camera)) unexpected = true;
    LocalFree(camera);
    if (unexpected) throw std::runtime_error("Worker refused: only the webcam capability may be granted; network capabilities are forbidden");
}
class Container {
    struct Grant { fs::path path; PSECURITY_DESCRIPTOR original = nullptr; bool directory = false; };
    std::vector<Grant> grants;
    std::wstring name = L"HonkaiVTuber.NativeTracking." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64());
public:
    PSID sid = nullptr, camera = nullptr;
    Container() {
        camera = WebcamSid(); SID_AND_ATTRIBUTES webcam{camera, SE_GROUP_ENABLED};
        const HRESULT hr = CreateAppContainerProfile(name.c_str(), L"HonkaiVTuber local tracking", L"Camera inference without network access", &webcam, 1, &sid);
        if (FAILED(hr)) { LocalFree(camera); camera = nullptr; throw std::runtime_error("Create AppContainer profile failed (HRESULT " + std::to_string(static_cast<unsigned long>(hr)) + ")"); }
    }
    void GrantPath(const fs::path& path, bool writable = false) {
        if (path.empty()) return;
        const bool directory = fs::is_directory(path);
        for (const auto& existing : grants) if (existing.path == path) return;
        PACL oldDacl = nullptr; PSECURITY_DESCRIPTOR original = nullptr;
        DWORD status = GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &oldDacl, nullptr, &original);
        if (status != ERROR_SUCCESS) throw std::runtime_error("Read AppContainer resource ACL failed (Win32 " + std::to_string(status) + ")");
        EXPLICIT_ACCESSW access{};
        access.grfAccessPermissions = FILE_GENERIC_READ | FILE_GENERIC_EXECUTE;
        if (writable) access.grfAccessPermissions |= FILE_GENERIC_WRITE | DELETE | FILE_DELETE_CHILD;
        access.grfAccessMode = GRANT_ACCESS;
        access.grfInheritance = directory && writable ? SUB_CONTAINERS_AND_OBJECTS_INHERIT : NO_INHERITANCE;
        access.Trustee.TrusteeForm = TRUSTEE_IS_SID; access.Trustee.TrusteeType = TRUSTEE_IS_UNKNOWN;
        access.Trustee.ptstrName = static_cast<LPWSTR>(sid);
        PACL dacl = nullptr; status = SetEntriesInAclW(1, &access, oldDacl, &dacl);
        if (status == ERROR_SUCCESS) status = SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, dacl, nullptr);
        LocalFree(dacl);
        if (status != ERROR_SUCCESS) { LocalFree(original); throw std::runtime_error("Grant AppContainer resource access failed (Win32 " + std::to_string(status) + ")"); }
        grants.push_back({path, original, directory});
    }
    ~Container() {
        // Each package SID is unique to this launch. Revoke it without changing anyone else's ACEs.
        for (auto it=grants.rbegin(); it!=grants.rend(); ++it) {
            PACL oldDacl = nullptr, newDacl = nullptr; PSECURITY_DESCRIPTOR current = nullptr;
            if (GetNamedSecurityInfoW(it->path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &oldDacl, nullptr, &current) == ERROR_SUCCESS) {
                EXPLICIT_ACCESSW access{}; access.grfAccessMode = REVOKE_ACCESS;
                access.Trustee.TrusteeForm = TRUSTEE_IS_SID; access.Trustee.ptstrName = static_cast<LPWSTR>(sid);
                if (SetEntriesInAclW(1, &access, oldDacl, &newDacl) == ERROR_SUCCESS)
                    SetNamedSecurityInfoW(const_cast<LPWSTR>(it->path.c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, newDacl, nullptr);
            }
            LocalFree(current); LocalFree(newDacl); LocalFree(it->original);
        }
        if (sid) { DeleteAppContainerProfile(name.c_str()); FreeSid(sid); }
        if (camera) LocalFree(camera);
    }
};
static std::wstring Quote(const std::wstring& argument) {
    std::wstring result = L"\""; size_t slashes = 0;
    for (wchar_t c : argument) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'"') result.append(slashes*2+1, L'\\'); else result.append(slashes, L'\\');
        slashes = 0; result += c;
    }
    result.append(slashes*2, L'\\'); return result + L'"';
}
int LaunchOfflineAppContainer(int argc, wchar_t** argv, const fs::path& output,
    const fs::path& stopFile, const fs::path& dll, const fs::path& model,
    const fs::path& image, std::atomic<bool>& stop, CaptureFrame capture) {
    wchar_t executable[32768]; WinCheck(GetModuleFileNameW(nullptr, executable, 32768) != 0, "Locate isolation worker");
    if (!stopFile.empty() && stopFile.parent_path() != output.parent_path()) throw std::runtime_error("Output and stop file must share a dedicated tracking job directory");
    fs::create_directories(output.parent_path());
    Container container;
    // The official binary probes its parent runfiles directory during graph initialization.
    container.GrantPath(fs::path(executable).parent_path().parent_path());
    container.GrantPath(fs::path(executable).parent_path()); container.GrantPath(executable);
    container.GrantPath(dll); container.GrantPath(model.parent_path());
    // Grant each pinned task bundle explicitly; the directory ACE intentionally has no inheritance.
    for (const auto& entry : fs::directory_iterator(model.parent_path())) if (entry.is_regular_file()) container.GrantPath(entry.path());
    container.GrantPath(image); container.GrantPath(output.parent_path(), true);
    std::wstring command = Quote(executable);
    for (int i=1; i<argc; ++i) {
        if (std::wstring(argv[i]) == L"--parent-pid") { ++i; continue; }
        command += L' ' + Quote(argv[i]);
    }
    command += L" --contained-worker";
    SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    Handle mapping, event, mutex;
    if (capture) {
        mapping.value = CreateFileMappingW(INVALID_HANDLE_VALUE, &inheritable, PAGE_READWRITE, 0, sizeof(SharedFrame), nullptr);
        event.value = CreateEventW(&inheritable, FALSE, FALSE, nullptr);
        mutex.value = CreateMutexW(&inheritable, FALSE, nullptr);
        WinCheck(mapping.value && event.value && mutex.value, "Create anonymous RGB transport");
        command += L" --shared-map " + std::to_wstring(reinterpret_cast<uintptr_t>(mapping.value))
            + L" --frame-event " + std::to_wstring(reinterpret_cast<uintptr_t>(event.value))
            + L" --frame-mutex " + std::to_wstring(reinterpret_cast<uintptr_t>(mutex.value));
    }
    Handle log, input;
    log.value = CreateFileW((output.parent_path()/L"worker.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE,
        &inheritable, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    input.value = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable, OPEN_EXISTING, 0, nullptr);
    WinCheck(log.value != INVALID_HANDLE_VALUE && input.value != INVALID_HANDLE_VALUE, "Create isolated worker log handles");
    SIZE_T bytes = 0; InitializeProcThreadAttributeList(nullptr, 2, 0, &bytes);
    std::vector<BYTE> attributes(bytes);
    auto list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    WinCheck(InitializeProcThreadAttributeList(list, 2, 0, &bytes) != FALSE, "Initialize isolation attributes");
    SID_AND_ATTRIBUTES webcam{container.camera, SE_GROUP_ENABLED};
    SECURITY_CAPABILITIES security{container.sid, &webcam, 1, 0};
    if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES, &security, sizeof(security), nullptr, nullptr)) {
        DeleteProcThreadAttributeList(list); throw std::runtime_error("Set AppContainer attributes failed");
    }
    std::vector<HANDLE> inherited{log.value, input.value};
    if (capture) { inherited.push_back(mapping.value); inherited.push_back(event.value); inherited.push_back(mutex.value); }
    if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited.data(), inherited.size()*sizeof(HANDLE), nullptr, nullptr)) {
        DeleteProcThreadAttributeList(list); throw std::runtime_error("Set isolated log handle list failed");
    }
    Handle job; job.value = CreateJobObjectW(nullptr, nullptr); WinCheck(job.value != nullptr, "Create worker lifetime job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    WinCheck(SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) != FALSE, "Configure worker lifetime job");
    STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup); startup.lpAttributeList = list;
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input.value; startup.StartupInfo.hStdOutput = log.value; startup.StartupInfo.hStdError = log.value;
    PROCESS_INFORMATION process{};
    BOOL created = CreateProcessW(executable, command.data(), nullptr, nullptr, TRUE,
        EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, fs::path(executable).parent_path().c_str(), &startup.StartupInfo, &process);
    DWORD createError = GetLastError(); DeleteProcThreadAttributeList(list);
    if (!created) throw std::runtime_error("Launch offline AppContainer failed (Win32 " + std::to_string(createError) + ")");
    Handle child, thread; child.value = process.hProcess; thread.value = process.hThread;
    if (!AssignProcessToJobObject(job.value, child.value)) { TerminateProcess(child.value, 1); throw std::runtime_error("Assign isolated worker job failed"); }
    WinCheck(ResumeThread(thread.value) != static_cast<DWORD>(-1), "Start isolated worker");
    auto stopStarted = std::chrono::steady_clock::time_point::max();
    std::vector<uint8_t> rgb; int width = 0, height = 0;
    while (WaitForSingleObject(child.value, capture ? 0 : 100) == WAIT_TIMEOUT) {
        if (stop && stopStarted == std::chrono::steady_clock::time_point::max()) {
            stopStarted = std::chrono::steady_clock::now();
            if (!stopFile.empty()) { std::ofstream requested(stopFile); requested << "stop\n"; }
        }
        if (stop && std::chrono::steady_clock::now()-stopStarted > std::chrono::seconds(1)) TerminateJobObject(job.value, 0);
        if (capture && !stop && capture(rgb, width, height)) {
            if (width <= 0 || height <= 0 || width > 3840 || height > 2160 || rgb.size() != size_t(width)*height*3) throw std::runtime_error("Invalid camera broker frame");
            DWORD locked = WaitForSingleObject(mutex.value, 100);
            if (locked == WAIT_OBJECT_0 || locked == WAIT_ABANDONED) {
                auto* frame = static_cast<SharedFrame*>(MapViewOfFile(mapping.value, FILE_MAP_WRITE, 0, 0, sizeof(SharedFrame)));
                if (!frame) { ReleaseMutex(mutex.value); throw std::runtime_error("Cannot map camera broker frame"); }
                frame->width = width; frame->height = height; frame->size = static_cast<uint32_t>(rgb.size());
                frame->timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                memcpy(frame->pixels, rgb.data(), rgb.size());
                UnmapViewOfFile(frame); ReleaseMutex(mutex.value); SetEvent(event.value);
            }
        } else if (capture) Sleep(10);
    }
    DWORD exitCode = 1; GetExitCodeProcess(child.value, &exitCode); return static_cast<int>(exitCode);
}
