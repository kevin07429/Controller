#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <restartmanager.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "Rstrtmgr.lib")

namespace {

constexpr const char* kServiceName = "WlanMonitorSvc";
constexpr const char* kUpdateExitEventName = "Global\\WlanMonitorSvc_UpdateExit_Event";
constexpr const char* kAllowServiceStopEventName = "Global\\WlanMonitorSvc_AllowServiceStop_Event";
constexpr DWORD kStopTimeoutMs = 5000;
constexpr DWORD kReplaceTimeoutMs = 60000;
constexpr DWORD kCooperativeExitTimeoutMs = 3000;
constexpr DWORD kPostStopExitTimeoutMs = 1000;
constexpr DWORD kMessageExitTimeoutMs = 1000;
constexpr DWORD kPollIntervalMs = 250;

std::filesystem::path g_logPath;

std::string NowString() {
    SYSTEMTIME st{};
    GetLocalTime(&st);

    char buf[64]{};
    sprintf_s(buf, "%04u-%02u-%02u %02u:%02u:%02u.%03u",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return buf;
}

// XOR + hex encoding, matching PowerOFF.cpp log format.
std::string EncryptString(const std::string& data) {
    const std::string key = "PowerOFF2026";
    std::string encrypted;

    for (size_t i = 0; i < data.length(); ++i) {
        unsigned char byte = (unsigned char)data[i];
        unsigned char keyChar = key[i % key.length()];
        unsigned char xorByte = byte ^ keyChar;

        // Convert to a hex string.
        char hexBuf[3];
        sprintf_s(hexBuf, sizeof(hexBuf), "%02X", xorByte);
        encrypted += hexBuf;
    }

    return encrypted;
}

void Log(const std::string& message) {
    if (g_logPath.empty()) {
        return;
    }

    std::ofstream out(g_logPath, std::ios::app);
    if (out.is_open()) {
        std::string logLine = "[" + NowString() + "] " + message;
        std::string encryptedLog = EncryptString(logLine);
        out << encryptedLog << "\r\n";
    }
}

std::string GetLastErrorText(DWORD err = GetLastError()) {
    LPSTR buffer = nullptr;
    DWORD size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        err,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buffer),
        0,
        nullptr);

    std::string text = size && buffer ? buffer : "Unknown error";
    if (buffer) {
        LocalFree(buffer);
    }

    while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' ')) {
        text.pop_back();
    }

    std::ostringstream oss;
    oss << text << " (" << err << ")";
    return oss.str();
}

std::string PathToUtf8(const std::filesystem::path& path) {
    return path.string();
}

std::wstring QuoteForCommandLine(const std::wstring& value) {
    std::wstring quoted = L"\"";
    for (wchar_t ch : value) {
        if (ch == L'\"') {
            quoted += L'\\';
        }
        quoted += ch;
    }
    quoted += L"\"";
    return quoted;
}

bool StartProcessHidden(const std::filesystem::path& exePath, const std::wstring& arguments = L"") {
    std::wstring commandLine = QuoteForCommandLine(exePath.wstring());
    if (!arguments.empty()) {
        commandLine += L" ";
        commandLine += arguments;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(
            exePath.c_str(),
            commandLine.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            exePath.parent_path().empty() ? nullptr : exePath.parent_path().c_str(),
            &si,
            &pi)) {
        Log("CreateProcessW failed for [" + PathToUtf8(exePath) + "]: " + GetLastErrorText());
        return false;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

bool IsUsableExeFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }

    auto size = file.tellg();
    if (size < 1024) {
        return false;
    }

    file.seekg(0, std::ios::beg);
    char mz[2]{};
    file.read(mz, sizeof(mz));
    return file.good() && mz[0] == 'M' && mz[1] == 'Z';
}

bool SamePath(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::error_code ecA;
    std::error_code ecB;
    auto ca = std::filesystem::weakly_canonical(a, ecA);
    auto cb = std::filesystem::weakly_canonical(b, ecB);
    if (ecA) {
        ca = std::filesystem::absolute(a);
    }
    if (ecB) {
        cb = std::filesystem::absolute(b);
    }

    std::string sa = ca.string();
    std::string sb = cb.string();
    std::transform(sa.begin(), sa.end(), sa.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(sb.begin(), sb.end(), sb.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return sa == sb;
}

std::vector<DWORD> FindProcessesByImagePath(const std::filesystem::path& imagePath) {
    std::vector<DWORD> pids;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        Log("CreateToolhelp32Snapshot failed: " + GetLastErrorText());
        return pids;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot, &entry)) {
        Log("Process32FirstW failed: " + GetLastErrorText());
        CloseHandle(snapshot);
        return pids;
    }

    const DWORD selfPid = GetCurrentProcessId();
    do {
        if (entry.th32ProcessID == selfPid) {
            continue;
        }

        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, entry.th32ProcessID);
        if (!process) {
            continue;
        }

        wchar_t pathBuffer[MAX_PATH * 4]{};
        DWORD len = static_cast<DWORD>(std::size(pathBuffer));
        if (QueryFullProcessImageNameW(process, 0, pathBuffer, &len)) {
            if (SamePath(std::filesystem::path(pathBuffer), imagePath)) {
                pids.push_back(entry.th32ProcessID);
            }
        }
        CloseHandle(process);
    } while (Process32NextW(snapshot, &entry));

    CloseHandle(snapshot);
    return pids;
}

BOOL CALLBACK PostCloseToWindow(HWND hwnd, LPARAM lParam) {
    DWORD windowPid = 0;
    GetWindowThreadProcessId(hwnd, &windowPid);

    DWORD targetPid = static_cast<DWORD>(lParam);
    if (windowPid == targetPid) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }

    return TRUE;
}

void RequestProcessesToExitByMessages(const std::filesystem::path& imagePath) {
    auto pids = FindProcessesByImagePath(imagePath);
    if (pids.empty()) {
        return;
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        Log("CreateToolhelp32Snapshot for threads failed: " + GetLastErrorText());
        return;
    }

    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    bool postedThreadMessage = false;
    if (Thread32First(snapshot, &entry)) {
        do {
            if (std::find(pids.begin(), pids.end(), entry.th32OwnerProcessID) != pids.end()) {
                PostThreadMessageW(entry.th32ThreadID, WM_QUIT, 0, 0);
                postedThreadMessage = true;
            }
        } while (Thread32Next(snapshot, &entry));
    }

    CloseHandle(snapshot);

    for (DWORD pid : pids) {
        EnumWindows(PostCloseToWindow, static_cast<LPARAM>(pid));
    }

    Log("Posted WM_QUIT/WM_CLOSE to " + std::to_string(pids.size()) + " process(es)" + (postedThreadMessage ? "" : " (no thread messages posted)"));
}

bool WaitForProcessesToExit(const std::filesystem::path& imagePath, DWORD timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    while (std::chrono::steady_clock::now() < deadline) {
        auto pids = FindProcessesByImagePath(imagePath);
        if (pids.empty()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
    }

    return FindProcessesByImagePath(imagePath).empty();
}

bool TerminateProcessesByImagePath(const std::filesystem::path& imagePath) {
    auto pids = FindProcessesByImagePath(imagePath);
    if (pids.empty()) {
        return true;
    }

    bool allTerminated = true;
    for (DWORD pid : pids) {
        HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
        if (!process) {
            Log("OpenProcess for terminate failed, pid " + std::to_string(pid) + ": " + GetLastErrorText());
            allTerminated = false;
            continue;
        }

        if (!TerminateProcess(process, 0)) {
            Log("TerminateProcess failed, pid " + std::to_string(pid) + ": " + GetLastErrorText());
            allTerminated = false;
        } else {
            Log("TerminateProcess succeeded, pid " + std::to_string(pid));
            WaitForSingleObject(process, 5000);
        }

        CloseHandle(process);
    }

    return allTerminated && WaitForProcessesToExit(imagePath, 10000);
}

void RequestProcessesToCloseWithRestartManager(const std::filesystem::path& imagePath) {
    DWORD sessionHandle = 0;
    WCHAR sessionKey[CCH_RM_SESSION_KEY + 1]{};
    DWORD result = RmStartSession(&sessionHandle, 0, sessionKey);
    if (result != ERROR_SUCCESS) {
        Log("RmStartSession failed: " + std::to_string(result));
        return;
    }

    std::wstring image = imagePath.wstring();
    LPCWSTR resources[] = { image.c_str() };
    result = RmRegisterResources(sessionHandle, 1, resources, 0, nullptr, 0, nullptr);
    if (result != ERROR_SUCCESS) {
        Log("RmRegisterResources failed: " + std::to_string(result));
        RmEndSession(sessionHandle);
        return;
    }

    UINT needed = 0;
    UINT count = 0;
    DWORD rebootReasons = 0;
    result = RmGetList(sessionHandle, &needed, &count, nullptr, &rebootReasons);
    if (result == ERROR_MORE_DATA && needed > 0) {
        std::vector<RM_PROCESS_INFO> processes(needed);
        count = needed;
        result = RmGetList(sessionHandle, &needed, &count, processes.data(), &rebootReasons);
        if (result == ERROR_SUCCESS) {
            Log("Restart Manager found " + std::to_string(count) + " process(es) using current exe");
        }
    }

    result = RmShutdown(sessionHandle, RmForceShutdown, nullptr);
    if (result == ERROR_SUCCESS) {
        Log("Restart Manager shutdown request completed");
    } else {
        Log("Restart Manager shutdown request failed: " + std::to_string(result));
    }

    RmEndSession(sessionHandle);
}

bool TerminateServiceProcessIfRunning(SC_HANDLE service) {
    SERVICE_STATUS_PROCESS status{};
    DWORD needed = 0;
    if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status), sizeof(status), &needed)) {
        Log("QueryServiceStatusEx before service process termination failed: " + GetLastErrorText());
        return false;
    }

    if (status.dwCurrentState == SERVICE_STOPPED) {
        Log("Service reached stopped state before forced process termination");
        return true;
    }

    if (status.dwProcessId == 0) {
        Log("Service has no process id to terminate");
        return false;
    }

    HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, status.dwProcessId);
    if (!process) {
        Log("OpenProcess for service pid " + std::to_string(status.dwProcessId) + " failed: " + GetLastErrorText());
        return false;
    }

    bool terminated = false;
    if (!TerminateProcess(process, 0)) {
        Log("TerminateProcess for service pid " + std::to_string(status.dwProcessId) + " failed: " + GetLastErrorText());
    } else {
        Log("TerminateProcess for service pid " + std::to_string(status.dwProcessId) + " succeeded");
        terminated = WaitForSingleObject(process, 10000) != WAIT_TIMEOUT;
        if (!terminated) {
            Log("Timed out waiting for terminated service process to exit");
        }
    }

    CloseHandle(process);

    for (int i = 0; i < 20; ++i) {
        if (QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status), sizeof(status), &needed)
            && status.dwCurrentState == SERVICE_STOPPED) {
            Log("Service state is stopped after forced process termination");
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
    }

    return terminated;
}

bool StopServiceIfPresent() {
    // Only stop the service when updater has explicitly entered update mode.
    HANDLE hAllowStopEvent = OpenEventA(SYNCHRONIZE, FALSE, kAllowServiceStopEventName);
    if (!hAllowStopEvent) {
        Log("Service stop is not allowed (not in update mode); skipping service stop");
        return true;
    }
    CloseHandle(hAllowStopEvent);

    SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        Log("OpenSCManager failed: " + GetLastErrorText());
        return false;
    }

    SC_HANDLE service = OpenServiceA(scm, kServiceName, SERVICE_STOP | SERVICE_QUERY_STATUS | SERVICE_START);
    if (!service) {
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_DOES_NOT_EXIST) {
            Log("Service not installed, skip service stop");
            CloseServiceHandle(scm);
            return true;
        }

        Log("OpenService for stop failed: " + GetLastErrorText(err));
        CloseServiceHandle(scm);
        return false;
    }

    SERVICE_STATUS_PROCESS status{};
    DWORD needed = 0;
    if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status), sizeof(status), &needed)) {
        Log("QueryServiceStatusEx before stop failed: " + GetLastErrorText());
        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        return false;
    }

    if (status.dwCurrentState == SERVICE_STOPPED) {
        Log("Service already stopped");
        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        return true;
    }

    if (status.dwCurrentState != SERVICE_STOP_PENDING) {
        SERVICE_STATUS stopStatus{};
        if (!ControlService(service, SERVICE_CONTROL_STOP, &stopStatus)) {
            DWORD err = GetLastError();
            if (err != ERROR_SERVICE_NOT_ACTIVE) {
                Log("ControlService stop failed: " + GetLastErrorText(err));
                if (err == ERROR_INVALID_SERVICE_CONTROL || err == ERROR_SERVICE_CANNOT_ACCEPT_CTRL) {
                    Log("Service does not accept stop control; forcing service process shutdown for update");
                    bool forced = TerminateServiceProcessIfRunning(service);
                    CloseServiceHandle(service);
                    CloseServiceHandle(scm);
                    return forced;
                }
            }
        } else {
            Log("Service stop requested");
        }
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kStopTimeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status), sizeof(status), &needed)) {
            Log("QueryServiceStatusEx while stopping failed: " + GetLastErrorText());
            break;
        }

        if (status.dwCurrentState == SERVICE_STOPPED) {
            Log("Service stopped");
            CloseServiceHandle(service);
            CloseServiceHandle(scm);
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
    }

    Log("Service did not stop before timeout");
    bool forced = TerminateServiceProcessIfRunning(service);
    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return forced;
}

bool StartServiceIfPresent(const std::filesystem::path& currentExePath) {
    SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        Log("OpenSCManager for start failed: " + GetLastErrorText());
        return false;
    }

    SC_HANDLE service = OpenServiceA(scm, kServiceName, SERVICE_START | SERVICE_QUERY_STATUS);
    if (!service) {
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_DOES_NOT_EXIST) {
            Log("Service not installed, launching exe directly");
            CloseServiceHandle(scm);
            return StartProcessHidden(currentExePath);
        }

        Log("OpenService for start failed: " + GetLastErrorText(err));
        CloseServiceHandle(scm);
        return false;
    }

    SERVICE_STATUS_PROCESS status{};
    DWORD needed = 0;
    if (QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status), sizeof(status), &needed)
        && status.dwCurrentState == SERVICE_RUNNING) {
        Log("Service already running");
        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        return true;
    }

    if (!StartServiceA(service, 0, nullptr)) {
        DWORD err = GetLastError();
        if (err != ERROR_SERVICE_ALREADY_RUNNING) {
            Log("StartService failed: " + GetLastErrorText(err));
            CloseServiceHandle(service);
            CloseServiceHandle(scm);
            return false;
        }
    }

    Log("Service start requested");
    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return true;
}

bool CanOpenForExclusiveWrite(const std::filesystem::path& path) {
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    CloseHandle(file);
    return true;
}

bool WaitUntilReplaceable(const std::filesystem::path& path, DWORD timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (CanOpenForExclusiveWrite(path)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
    }
    return CanOpenForExclusiveWrite(path);
}

bool MoveFileReplace(const std::filesystem::path& from, const std::filesystem::path& to) {
    if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return true;
    }

    Log("MoveFileEx failed from [" + PathToUtf8(from) + "] to [" + PathToUtf8(to) + "]: " + GetLastErrorText());
    return false;
}

std::filesystem::path StageNewExe(const std::filesystem::path& newExePath) {
    std::error_code ec;
    if (!std::filesystem::exists(newExePath, ec)) {
        Log("New exe does not exist before staging: " + PathToUtf8(newExePath));
        return {};
    }

    std::filesystem::path stagedPath = newExePath.parent_path()
        / (newExePath.filename().string() + ".updating_" + std::to_string(GetCurrentProcessId()));

    DeleteFileW(stagedPath.c_str());
    if (!MoveFileExW(newExePath.c_str(), stagedPath.c_str(), MOVEFILE_WRITE_THROUGH)) {
        Log("Failed to stage new exe: " + GetLastErrorText());
        return {};
    }

    Log("Staged new exe: " + PathToUtf8(stagedPath));
    return stagedPath;
}

HANDLE SignalNamedEvent(const char* eventName, const char* description) {
    HANDLE hEvent = CreateEventA(nullptr, TRUE, FALSE, eventName);
    if (!hEvent) {
        Log(std::string("Create ") + description + " event failed: " + GetLastErrorText());
        return nullptr;
    }

    if (!SetEvent(hEvent)) {
        Log(std::string("Set ") + description + " event failed: " + GetLastErrorText());
        CloseHandle(hEvent);
        return nullptr;
    }

    Log(std::string(description) + " event signaled");
    return hEvent;
}

HANDLE SignalUpdateExitEvent() {
    return SignalNamedEvent(kUpdateExitEventName, "update exit");
}

HANDLE SignalAllowServiceStopEvent() {
    return SignalNamedEvent(kAllowServiceStopEventName, "allow service stop");
}

std::filesystem::path UpdateExitFlagPath(const std::filesystem::path& currentExePath) {
    return std::filesystem::path(currentExePath.string() + ".update_exit");
}

bool CreateUpdateExitFlag(const std::filesystem::path& currentExePath) {
    auto flagPath = UpdateExitFlagPath(currentExePath);
    std::ofstream flag(flagPath, std::ios::trunc);
    if (!flag.is_open()) {
        Log("Failed to create update exit flag [" + PathToUtf8(flagPath) + "]");
        return false;
    }

    flag << GetCurrentProcessId() << "\r\n";
    Log("Update exit flag created: " + PathToUtf8(flagPath));
    return true;
}

void DeleteUpdateExitFlag(const std::filesystem::path& currentExePath) {
    auto flagPath = UpdateExitFlagPath(currentExePath);
    if (DeleteFileW(flagPath.c_str())) {
        Log("Update exit flag deleted: " + PathToUtf8(flagPath));
    }
}

bool ApplyUpdate(const std::filesystem::path& currentExePath, const std::filesystem::path& newExePath) {
    std::error_code ec;
    if (!std::filesystem::exists(newExePath, ec)) {
        Log("New exe does not exist: " + PathToUtf8(newExePath));
        return false;
    }

    if (std::filesystem::file_size(newExePath, ec) == 0 || ec) {
        Log("New exe is empty or inaccessible: " + PathToUtf8(newExePath));
        return false;
    }

    if (!IsUsableExeFile(newExePath)) {
        Log("New exe failed executable header validation: " + PathToUtf8(newExePath));
        return false;
    }

    if (!WaitUntilReplaceable(currentExePath, kReplaceTimeoutMs)) {
        Log("Current exe is still locked, cannot replace: " + PathToUtf8(currentExePath));
        return false;
    }

    const auto backupPath = currentExePath.string() + ".old_" + std::to_string(GetTickCount64());
    Log("Replacing current exe, backup path: " + backupPath);

    if (!ReplaceFileW(
            currentExePath.c_str(),
            newExePath.c_str(),
            std::filesystem::path(backupPath).c_str(),
            REPLACEFILE_WRITE_THROUGH,
            nullptr,
            nullptr)) {
        Log("ReplaceFileW failed: " + GetLastErrorText());
        Log("Falling back to MoveFileEx replacement");

        if (!MoveFileReplace(currentExePath, backupPath)) {
            return false;
        }

        if (!MoveFileReplace(newExePath, currentExePath)) {
            Log("New exe move failed, attempting rollback");
            if (!MoveFileReplace(backupPath, currentExePath)) {
                Log("Rollback failed; manual repair may be required");
            }
            return false;
        }
    }

    DeleteFileA((currentExePath.string() + ".up_lock").c_str());
    Log("Update applied successfully");
    return true;
}

bool ScheduleReplaceOnReboot(const std::filesystem::path& currentExePath, const std::filesystem::path& stagedNewExePath) {
    const auto backupPath = currentExePath.string() + ".old_reboot_" + std::to_string(GetTickCount64());

    if (!MoveFileExW(currentExePath.c_str(), std::filesystem::path(backupPath).c_str(), MOVEFILE_DELAY_UNTIL_REBOOT)) {
        Log("Schedule current exe backup on reboot failed: " + GetLastErrorText());
        return false;
    }

    if (!MoveFileExW(stagedNewExePath.c_str(), currentExePath.c_str(), MOVEFILE_DELAY_UNTIL_REBOOT)) {
        Log("Schedule staged exe replacement on reboot failed: " + GetLastErrorText());
        return false;
    }

    Log("Replacement has been scheduled for next reboot");
    Log("Scheduled backup path: " + backupPath);
    return true;
}

} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc < 3) {
        if (argv) {
            LocalFree(argv);
        }
        return 2;
    }

    std::filesystem::path currentExePath = std::filesystem::absolute(argv[1]);
    std::filesystem::path newExePath = std::filesystem::absolute(argv[2]);
    LocalFree(argv);

    g_logPath = currentExePath.parent_path() / "WlanMonitorSvc_Updater_Log.txt";
    Log("Updater started");
    Log("Current exe: " + PathToUtf8(currentExePath));
    Log("New exe: " + PathToUtf8(newExePath));

    std::filesystem::path stagedNewExePath = StageNewExe(newExePath);
    if (stagedNewExePath.empty()) {
        Log("Updater finished with failure");
        return 1;
    }

    CreateUpdateExitFlag(currentExePath);
    HANDLE hAllowStopEvent = SignalAllowServiceStopEvent();
    HANDLE hUpdateExitEvent = SignalUpdateExitEvent();

    if (WaitForProcessesToExit(currentExePath, kCooperativeExitTimeoutMs)) {
        Log("Process exited after update exit event");
    } else {
        Log("Process still running after update exit event; requesting service stop");
        StopServiceIfPresent();
    }

    if (!WaitForProcessesToExit(currentExePath, kPostStopExitTimeoutMs)) {
        Log("Process still running after service stop request; posting quit/close messages");
        RequestProcessesToExitByMessages(currentExePath);
        WaitForProcessesToExit(currentExePath, kMessageExitTimeoutMs);
    }

    if (!WaitForProcessesToExit(currentExePath, 1000)) {
        Log("Process still running after cooperative shutdown attempts; terminating exact-path leftovers to complete update");
        TerminateProcessesByImagePath(currentExePath);
    }

    if (!WaitForProcessesToExit(currentExePath, 1000)) {
        Log("Process still running after termination attempt; trying Restart Manager as last resort");
        RequestProcessesToCloseWithRestartManager(currentExePath);
        WaitForProcessesToExit(currentExePath, 3000);
    }

    if (!WaitForProcessesToExit(currentExePath, 1000)) {
        Log("Process still running after final termination attempt; update may fail if the executable is locked");
    }

    bool updated = ApplyUpdate(currentExePath, stagedNewExePath);
    bool scheduledForReboot = false;

    if (!updated) {
        scheduledForReboot = ScheduleReplaceOnReboot(currentExePath, stagedNewExePath);
    }

    if (hUpdateExitEvent) {
        ResetEvent(hUpdateExitEvent);
        CloseHandle(hUpdateExitEvent);
        hUpdateExitEvent = nullptr;
    }

    if (hAllowStopEvent) {
        ResetEvent(hAllowStopEvent);
        CloseHandle(hAllowStopEvent);
        hAllowStopEvent = nullptr;
    }

    DeleteUpdateExitFlag(currentExePath);

    StartServiceIfPresent(currentExePath);

    if (updated) {
        DeleteFileW(stagedNewExePath.c_str());
    }

    if (updated) {
        Log("Updater finished successfully");
        return 0;
    }

    if (scheduledForReboot) {
        Log("Updater finished; replacement is pending reboot");
        return 0;
    }

    Log("Updater finished with failure");
    return 1;
}
