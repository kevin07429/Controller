from flask import Blueprint, render_template_string, session, redirect, url_for, request, jsonify
from core import clients_db, save_db, encrypt_data, decrypt_data, add_cmd_to_queue, init_client_queue

bp = Blueprint('entertainment', __name__)

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
        "song": clients_db[mac].get('media_song', '等待同步中...')
    })

@bp.route('/entertainment/<mac>')
def entertainment_page(mac):
    if not session.get('logged_in'):
        return redirect(url_for('auth.login'))

    if mac not in clients_db:
        return "设备不存在", 404

    info = clients_db[mac]

    HTML = """
    <!DOCTYPE html>
    <html lang="zh-CN">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>娱乐控制 - {{ info.name }}</title>
        <style>
            body { font-family: Arial, sans-serif; background: #f4f6f9; margin: 0; padding: 20px; }
            .container { max-width: 600px; margin: 0 auto; background: #fff; padding: 30px; border-radius: 8px; box-shadow: 0 4px 10px rgba(0,0,0,0.1); }
            h2 { margin-top: 0; color: #333; font-size: 24px; border-bottom: 2px solid #eee; padding-bottom: 10px;}
            .btn { display: inline-block; padding: 12px 18px; margin: 8px 5px; color: #fff; background: #007bff; border: none; border-radius: 5px; cursor: pointer; text-decoration: none; font-size: 15px; font-weight: bold; transition: background 0.3s; }
            .btn:hover { background: #0056b3; }
            .btn-danger { background: #dc3545; }
            .btn-danger:hover { background: #c82333; }
            .btn-success { background: #28a745; }
            .btn-success:hover { background: #218838; }
            .btn-warning { background: #ffc107; color: #000; }
            .btn-warning:hover { background: #e0a800; }
            .btn-info { background: #17a2b8; }
            .btn-info:hover { background: #138496; }
            .btn-info.active { background: #ff6b6b; animation: pulse 0.5s infinite; }
            @keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.7; } }
            .header-link { margin-bottom: 20px; display: inline-block; text-decoration: none; color: #007bff; font-weight: bold; }
            .header-link:hover { text-decoration: underline; }
            .section { border-top: 1px solid #eee; padding-top: 20px; margin-top: 20px; }
            .section p { color: #666; font-size: 14px; margin-bottom: 15px; }
            .status-text { color: green; font-size: 13px; margin-top: 10px; display: none; }
            .media-now-playing { background: #e9ecef; padding: 15px; border-radius: 8px; margin-bottom: 20px; text-align: center; }
            .media-title { font-size: 18px; font-weight: bold; color: #333; margin-top: 8px; }
            .volume-slider-container { display: flex; align-items: center; margin-top: 15px; }
            input[type=range] { flex-grow: 1; margin: 0 15px; }
            .vol-value { font-weight: bold; min-width: 40px; }
        </style>
    </head>
    <body>
        <div class="container">
            <a href="/" class="header-link">← 返回控制台主页</a>
            <h2>🎮 娱乐遥控器 - {{ info.name }}</h2>
            <div style="color:#888; font-size:13px; margin-top:-10px; margin-bottom:20px;">设备 MAC: {{ mac }}</div>

            <div class="media-now-playing">
                <div style="color: #666; font-size: 12px;">🎵 当前正在播放媒体 / 音乐</div>
                <div class="media-title" id="song-title">同步中...</div>
            </div>

            <div class="section">
                <h3>🔊 远程音量控制</h3>
                <p>调节或静音这台电脑当前的系统主音量：</p>
                <div class="volume-slider-container">
                    <span>🔈</span>
                    <input type="range" id="vol-slider" min="0" max="100" value="50" onchange="setVolume(this.value)" oninput="document.getElementById('vol-display').innerText=this.value">
                    <span class="vol-value" id="vol-display">50</span>%
                </div>
                <br>
                <button class="btn btn-danger" onclick="sendVolumeCmd('mute')">🔇 切换系统静音</button>
                <button class="btn btn-info" id="bounce-btn" onclick="toggleBounceMode()">🎉 蹦迪模式</button>
            </div>

            <div class="section">
                <h3>🎵 媒体播放遥控</h3>
                <p>控制系统主流音乐或视频播放器（如网易云音乐、QQ音乐、B站客户端等）：</p>
                <button class="btn" onclick="sendMediaCmd('playpause')">⏯ 播放 / 暂停</button>
                <button class="btn" onclick="sendMediaCmd('prev')">⏮ 上一首</button>
                <button class="btn" onclick="sendMediaCmd('next')">⏭ 下一首</button>
            </div>

            <div class="section">
                <h3>📱 短视频遥控（抖音等）</h3>
                <p>通过模拟常见快捷键控制短视频客户端（需客户端窗口在前台）：</p>
                <button class="btn" onclick="sendShortVideoCmd('playpause')">⏯ 暂停 / 继续（空格）</button>
                <button class="btn" onclick="sendShortVideoCmd('next')">⏭ 下一条（↓）</button>
                <button class="btn" onclick="sendShortVideoCmd('prev')">⏮ 上一条（↑）</button>
            </div>

            <div class="section">
                <h3>💻 屏幕电源控制</h3>
                <p>远程控制显示器电源状态，防止长时间挂机烧屏：</p>
                <button class="btn btn-warning" onclick="sendCustomCmd('F_CMD:MONITOR_OFF:')">💤 关闭显示器</button>
                <button class="btn btn-success" onclick="sendCustomCmd('F_CMD:MONITOR_ON:')">☀️ 唤醒显示器</button>
                <button class="btn btn-info" id="monitor-bounce-btn" onclick="toggleMonitorBounceMode()">🎆 显示器蹦迪</button>
            </div>

            <div id="status-msg" class="status-text">指令已下发！</div>
            <div id="status-error" class="status-text" style="color: red; display: none;">指令投递失败，请稍后重试！</div>

            <script>
                let bounceMode = false;
                let bounceInterval = null;
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
                    bounceMode = !bounceMode;
                    const btn = document.getElementById('bounce-btn');

                    if (bounceMode) {
                        btn.classList.add('active');
                        btn.innerText = '🎉 蹦迪模式（运行中...）';

                        // Start bouncing effect - change volume every 5 seconds
                        bounceInterval = setInterval(() => {
                            const randomVol = Math.floor(Math.random() * 101); // 0-100
                            setVolume(randomVol);
                            document.getElementById('vol-slider').value = randomVol;
                            document.getElementById('vol-display').innerText = randomVol;
                        }, 5000);

                        // Send first random volume immediately
                        const initialVol = Math.floor(Math.random() * 101);
                        setVolume(initialVol);
                        document.getElementById('vol-slider').value = initialVol;
                        document.getElementById('vol-display').innerText = initialVol;
                    } else {
                        btn.classList.remove('active');
                        btn.innerText = '🎉 蹦迪模式';
                        clearInterval(bounceInterval);
                        bounceInterval = null;
                    }
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
                setInterval(pollInfo, 4000);
                pollInfo();
            </script>
        </div>
    </body>
    </html>
    """
    return render_template_string(HTML, mac=mac, info=info)
