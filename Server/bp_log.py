from flask import Blueprint, request, render_template_string, session, redirect, url_for, jsonify, Response
import os
from core import clients_db, UPDATE_DIR, decrypt_data
try:
    from ui import ADMIN_CSS
except Exception:
    ADMIN_CSS = ""

bp = Blueprint('log', __name__)

LOG_KEY = b"PowerOFF2026"
LOG_DIR = os.path.join(UPDATE_DIR, 'client_logs')
LOG_KIND_FILES = {
    'main': '{mac}.log',
    'update': '{mac}_update.log',
}


def normalize_log_kind(kind):
    return kind if kind in LOG_KIND_FILES else 'main'


def log_path_for(mac, kind):
    kind = normalize_log_kind(kind)
    return os.path.join(LOG_DIR, LOG_KIND_FILES[kind].format(mac=mac))


@bp.route('/api/upload_log/<mac>', methods=['POST'])
def api_upload_log(mac):
    mac = decrypt_data(mac)
    kind = normalize_log_kind(request.args.get('kind', 'main'))
    f = request.files.get('file')
    if f and mac:
        os.makedirs(LOG_DIR, exist_ok=True)
        f.save(log_path_for(mac, kind))
    return "OK"


def decrypt_line(line):
    line = line.strip()
    if not line:
        return ''
    try:
        raw_bytes = bytes.fromhex(line)
    except ValueError:
        return line

    decrypted = bytearray()
    for i, b in enumerate(raw_bytes):
        decrypted.append(b ^ LOG_KEY[i % len(LOG_KEY)])

    for encoding in ('utf-8', 'gbk'):
        try:
            return decrypted.decode(encoding)
        except UnicodeDecodeError:
            continue
    return decrypted.decode('utf-8', errors='replace')


def read_log(filepath, max_lines=1200):
    if not os.path.exists(filepath):
        return "No log file has been uploaded yet."

    lines = []
    with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
        for line in f:
            if line.strip():
                lines.append(line.rstrip('\r\n'))

    if len(lines) > max_lines:
        lines = lines[-max_lines:]

    return "\n".join(decrypt_line(line) for line in lines)


def log_meta(filepath):
    if not os.path.exists(filepath):
        return {'exists': False, 'size': 0, 'mtime': ''}
    stat = os.stat(filepath)
    return {
        'exists': True,
        'size': stat.st_size,
        'mtime': stat.st_mtime,
    }


@bp.route('/api/view_log/<mac>')
def api_view_log(mac):
    if not session.get('logged_in'):
        return jsonify({"status": "error"}), 403
    kind = normalize_log_kind(request.args.get('kind', 'main'))
    path = log_path_for(mac, kind)
    return jsonify({
        "status": "ok",
        "kind": kind,
        "content": read_log(path),
        "meta": log_meta(path),
    })


@bp.route('/download_log/<mac>/<kind>')
def download_log(mac, kind):
    if not session.get('logged_in'):
        return redirect(url_for('auth.login'))
    kind = normalize_log_kind(kind)
    path = log_path_for(mac, kind)
    content = read_log(path, max_lines=200000)
    filename = f"{mac}_{kind}_decrypted.log"
    return Response(
        content,
        mimetype='text/plain; charset=utf-8',
        headers={'Content-Disposition': f'attachment; filename="{filename}"'}
    )


@bp.route('/view_log/<mac>')
def view_log(mac):
    if not session.get('logged_in'):
        return redirect(url_for('auth.login'))

    name = clients_db.get(mac, {}).get('name', 'Unnamed device')
    main_content = read_log(log_path_for(mac, 'main'))
    update_content = read_log(log_path_for(mac, 'update'))

    return render_template_string('''
    <!DOCTYPE html>
    <html lang="en">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>Run Logs - {{ name }} ({{ mac }})</title>
        <style>
            {{ admin_css|safe }}
            body { background:#1f2328; color:#d7ffd7; font-family:Consolas, monospace; margin:0; padding:18px; }
            .toolbar { display:flex; flex-wrap:wrap; gap:8px; align-items:center; margin-bottom:14px; font-family:sans-serif; }
            .btn { background:#2f6feb; color:#fff; padding:7px 12px; text-decoration:none; border:0; border-radius:4px; cursor:pointer; }
            .btn.secondary { background:#6e7681; }
            .btn.danger { background:#da3633; }
            .tabs { display:flex; gap:8px; margin:12px 0; font-family:sans-serif; }
            .tab { background:#30363d; color:#c9d1d9; border:1px solid #484f58; padding:8px 12px; border-radius:4px; cursor:pointer; }
            .tab.active { background:#238636; color:#fff; border-color:#2ea043; }
            .meta { color:#9da7b3; font-family:sans-serif; font-size:13px; margin-bottom:8px; }
            .logbox { background:#0d1117; color:#7CFC8A; border:1px solid #30363d; padding:14px; min-height:70vh; white-space:pre-wrap; word-break:break-word; overflow:auto; line-height:1.35; }
            input { background:#0d1117; color:#fff; border:1px solid #484f58; border-radius:4px; padding:8px; min-width:240px; }
            mark { background:#ffd33d; color:#000; }
            .float-nav { position:fixed; right:18px; bottom:18px; display:flex; flex-direction:column; gap:8px; z-index:20; }
            .float-nav button { min-width:76px; box-shadow:0 8px 20px rgba(0,0,0,.28); }
            @media (max-width:640px) {
                body { padding:12px; }
                .toolbar input { min-width:100%; }
                .float-nav { right:10px; bottom:10px; }
                .float-nav button { min-width:58px; padding:8px 10px; }
            }
        </style>
    </head>
    <body>
        <div class="toolbar">
            <a class="btn" href="/">Back</a>
            <button class="btn secondary" onclick="refreshLog()">Refresh</button>
            <button class="btn secondary" onclick="toggleFollow()">Follow: <span id="followState">On</span></button>
            <input id="filter" placeholder="Filter log text" oninput="renderFiltered()">
            <a id="download" class="btn secondary" href="/download_log/{{ mac }}/main">Download decrypted log</a>
        </div>
        <h2>[{{ name }}] Run Logs ({{ mac }})</h2>
        <div class="tabs">
            <button id="tab-main" class="tab active" onclick="switchKind('main')">Main Log</button>
            <button id="tab-update" class="tab" onclick="switchKind('update')">Update Log</button>
        </div>
        <div class="meta" id="meta">Showing recent 1200 lines. Auto-refresh every 10 seconds.</div>
        <pre class="logbox" id="logbox">{{ main_content }}</pre>
        <div class="float-nav">
            <button class="btn secondary" onclick="scrollPageTop()">Top</button>
            <button class="btn secondary" onclick="scrollLogBottom()">Bottom</button>
        </div>

        <script>
            const mac = {{ mac_json|tojson }};
            let kind = 'main';
            let follow = true;
            let rawContent = {
                main: {{ main_content_raw|tojson }},
                update: {{ update_content_raw|tojson }}
            };

            function setActiveTab() {
                document.getElementById('tab-main').classList.toggle('active', kind === 'main');
                document.getElementById('tab-update').classList.toggle('active', kind === 'update');
                document.getElementById('download').href = '/download_log/' + encodeURIComponent(mac) + '/' + kind;
            }

            function escapeHtml(text) {
                return text.replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
            }

            function renderFiltered() {
                const box = document.getElementById('logbox');
                const q = document.getElementById('filter').value.trim().toLowerCase();
                const content = rawContent[kind] || '';
                if (!q) {
                    box.textContent = content;
                } else {
                    const rows = content.split('\\n').filter(line => line.toLowerCase().includes(q));
                    box.textContent = rows.join('\\n') || 'No matching lines.';
                }
                if (follow) box.scrollTop = box.scrollHeight;
            }

            function scrollPageTop() {
                window.scrollTo({ top: 0, behavior: 'smooth' });
                document.getElementById('logbox').scrollTop = 0;
            }

            function scrollLogBottom() {
                const box = document.getElementById('logbox');
                box.scrollTop = box.scrollHeight;
                window.scrollTo({ top: document.body.scrollHeight, behavior: 'smooth' });
            }

            function switchKind(nextKind) {
                kind = nextKind;
                setActiveTab();
                renderFiltered();
                refreshLog();
            }

            function toggleFollow() {
                follow = !follow;
                document.getElementById('followState').textContent = follow ? 'On' : 'Off';
            }

            async function refreshLog() {
                const res = await fetch('/api/view_log/' + encodeURIComponent(mac) + '?kind=' + kind);
                if (!res.ok) return;
                const data = await res.json();
                rawContent[kind] = data.content || '';
                const meta = data.meta || {};
                document.getElementById('meta').textContent = meta.exists
                    ? 'File size ' + meta.size + ' bytes. Showing recent 1200 lines. Auto-refresh every 10 seconds.'
                    : 'No log of this type has been uploaded yet.';
                renderFiltered();
            }

            setActiveTab();
            renderFiltered();
            setInterval(refreshLog, 10000);
        </script>
    </body>
    </html>
    ''', admin_css=ADMIN_CSS, name=name, mac=mac, mac_json=mac,
       main_content=main_content, main_content_raw=read_log(log_path_for(mac, 'main')),
       update_content_raw=update_content)
