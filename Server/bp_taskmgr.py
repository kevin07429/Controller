from flask import Blueprint, request, render_template_string, session, redirect, url_for, send_from_directory, jsonify, Response
from datetime import datetime
import os, time, json, threading
from core import clients_db, save_db, is_online, log_login_attempt, USERNAME, PASSWORD, UPDATE_DIR, VERSION_FILE
try:
    from ui import ADMIN_CSS
except Exception:
    ADMIN_CSS = ""

bp = Blueprint('taskmgr', __name__)

@bp.route('/taskmgr/<mac>')
def taskmgr_page(mac):
    if not session.get('logged_in'): return redirect(url_for('auth.login'))
    if mac not in clients_db: return "设备不存在"

    TASKMGR_HTML = r"""
    <!DOCTYPE html>
    <html lang="en">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0, viewport-fit=cover">
        <title>任务管理器 - {{ info.name }} ({{ mac }})</title>
        <style>
            {{ admin_css|safe }}
            body { background:var(--bg); padding:0; }
            .tabs { background:#fff; border:1px solid var(--line); border-radius:8px; padding:10px; margin-bottom:12px; }
            .tabs button { border:1px solid var(--line); background:#fff; color:#344054; flex:0 1 auto; }
            .tabs button.active { background:var(--blue); color:#fff; border-color:var(--blue); }
            .table-responsive { overflow-x:auto; border:1px solid var(--line); border-radius:8px; background:#fff; }
            table { font-size:13px; min-width:720px; margin:0; }
            th { position:sticky; top:0; z-index:1; }
            tr:hover { background:#f8fafc; }
            #loading { color:var(--amber); font-weight:700; display:none; margin-left:10px; }
            .content-wrapper { max-height:calc(100vh - 178px); overflow:auto; border-radius:8px; }
            .metric-box { background:#fff; border:1px solid var(--line); padding:16px; border-radius:8px; margin:0 10px 12px 0; box-shadow:0 1px 2px rgba(15,23,42,.04); display:inline-block; width:calc(50% - 14px); vertical-align:top; }
            .metric-title { font-size:14px; color:var(--muted); }
            .metric-value { font-size:28px; font-weight:700; color:var(--blue); margin-top:8px; }
            @media(max-width:700px){ .metric-box{width:100%;margin-right:0}.tabs button{flex:1 1 calc(50% - 8px)}.content-wrapper{max-height:none} }
        </style>
    </head>
    <body>
        <main class="shell">
        <div class="header">
            <div><h2>Task Manager</h2><div class="subtle">{{ info.name }} [{{ mac }}] <span id="conn_status"></span> <span id="loading">Loading...</span></div></div>
            <a class="btn muted" href="/">Back</a>
        </div>
        <div class="tabs">
            <button id="tab_proc" class="active" onclick="switchTab('proc')">Processes</button>
            <button id="tab_perf" onclick="switchTab('perf')">Performance</button>
            <button id="tab_startup" onclick="switchTab('startup')">Startup</button>
            <button id="tab_software" onclick="switchTab('software')">Software</button>
            <button id="tab_svc" onclick="switchTab('svc')">Services</button>
            <button class="muted" onclick="refreshCurrentTab()">Refresh</button>
        </div>

        <div class="content-wrapper" id="content_area">
            <!-- Content dynamically generated -->
        </div>
        </main>

        <script>
            function checkPing() {
                fetch('/api/ping/{{ mac }}')
                .then(r => r.json())
                .then(data => {
                    let st = document.getElementById('conn_status');
                    if(data.status === 'online') { st.innerHTML = '🟢 实时设备在线'; st.style.color = '#28a745'; } 
                    else { st.innerHTML = '🔴 设备疑似掉线'; st.style.color = 'red'; }
                });
            }
            setInterval(checkPing, 3000); checkPing();

            let currentTab = 'proc';
            let polling = false;

            let procData = [];
            let procSortCol = 'WorkingSet';
            let procSortDir = -1;

            function sortProc(col) {
                if(procSortCol === col) procSortDir *= -1;
                else { procSortCol = col; procSortDir = 1; }
                renderProc();
            }

            function renderProc() {
                let sorted = [...procData];
                sorted.sort((a,b) => {
                    let valA = a[procSortCol];
                    let valB = b[procSortCol];
                    if(procSortCol === 'ProcessName') {
                        valA = (valA||'').toString().toLowerCase();
                        valB = (valB||'').toString().toLowerCase();
                    } else {
                        valA = parseFloat(valA)||0;
                        valB = parseFloat(valB)||0;
                    }
                    if(valA < valB) return -1 * procSortDir;
                    if(valA > valB) return 1 * procSortDir;
                    return 0;
                });

                let html = `<div class="table-responsive"><table><thead><tr>
                    <th onclick="sortProc('Id')" style="cursor:pointer; user-select:none;">PID ${procSortCol==='Id'?(procSortDir===1?'▲':'▼'):''}</th>
                    <th onclick="sortProc('ProcessName')" style="cursor:pointer; user-select:none;">进程名称 ${procSortCol==='ProcessName'?(procSortDir===1?'▲':'▼'):''}</th>
                    <th onclick="sortProc('WorkingSet')" style="cursor:pointer; user-select:none;">内存使用 ${procSortCol==='WorkingSet'?(procSortDir===1?'▲':'▼'):''}</th>
                    <th>说明</th><th>操作</th></tr></thead><tbody>`;
                sorted.forEach(p => {
                    html += `<tr><td>${p.Id}</td><td style="font-weight:bold;">${p.ProcessName}</td><td>${formatSize(p.WorkingSet)}</td><td>进程组</td><td><button class="btn-danger" onclick="killProcess(${p.Id}, '${p.ProcessName}')">结束进程</button></td></tr>`;
                });
                html += `</tbody></table></div>`;
                let content = document.getElementById('content_area');
                if(content) content.innerHTML = html;
            }

            let perfHistory = []; 
            let perfInterval = null;

            function drawChart() {
                let ctx = document.getElementById('perfChart');
                if(!ctx) return;
                ctx = ctx.getContext('2d');
                let w = ctx.canvas.width;
                let h = ctx.canvas.height;
                ctx.clearRect(0, 0, w, h);

                ctx.strokeStyle = '#eee';
                ctx.lineWidth = 1;
                for(let i=0; i<=10; i++) {
                    let y = (h - 20) - (i * (h - 40) / 10);
                    ctx.beginPath(); ctx.moveTo(40, y); ctx.lineTo(w - 10, y); ctx.stroke();
                    ctx.fillStyle = '#999'; ctx.font = '12px Arial';
                    ctx.fillText((i*10)+'%', 5, y + 4);
                }

                if(perfHistory.length < 2) return;
                let dx = (w - 50) / 50; 

                ctx.strokeStyle = 'rgba(153, 102, 255, 1)';
                ctx.lineWidth = 2;
                ctx.beginPath();
                perfHistory.forEach((p, i) => {
                    let x = 40 + i * dx;
                    let y = (h - 20) - (p.mem / 100) * (h - 40);
                    if(i===0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
                });
                ctx.stroke();

                ctx.strokeStyle = 'rgba(54, 162, 235, 1)';
                ctx.lineWidth = 2;
                ctx.beginPath();
                perfHistory.forEach((p, i) => {
                    let x = 40 + i * dx;
                    let y = (h - 20) - (p.cpu / 100) * (h - 40);
                    if(i===0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
                });
                ctx.stroke();

                ctx.fillStyle = 'rgba(54, 162, 235, 1)'; ctx.fillRect(60, 10, 10, 10);
                ctx.fillStyle = '#333'; ctx.fillText('CPU', 75, 20);
                ctx.fillStyle = 'rgba(153, 102, 255, 1)'; ctx.fillRect(120, 10, 10, 10);
                ctx.fillStyle = '#333'; ctx.fillText('内存', 135, 20);
            }

            function fetchPerfData() {
                let fullCmd = "F_CMD:TASK_PERF:ALL";
                fetch('/api/file/cmd', {
                    method: 'POST', headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({mac: '{{ mac }}', cmd: fullCmd})
                }).then(() => {
                    let tries = 0;
                    let itv = setInterval(() => {
                        fetch('/api/file/result/{{ mac }}')
                        .then(r => r.json())
                        .then(data => {
                            if(data.status === 'ready') {
                                clearInterval(itv);
                                try {
                                    let res = data.data;
                                    let idx = res.indexOf('{');
                                    if (idx >= 0) res = res.substring(idx);
                                    let jdata = JSON.parse(res);
                                    let memUsed = jdata.MemTotal - jdata.MemFree;
                                    let memPct = ((memUsed / jdata.MemTotal) * 100).toFixed(1);
                                    let cpuPct = jdata.CPU.toFixed(1);
                                    let totalGB = (jdata.MemTotal / 1024 / 1024 / 1024).toFixed(1);
                                    let usedGB = (memUsed / 1024 / 1024 / 1024).toFixed(1);

                                    let cpuEl = document.getElementById('cpu_val');
                                    if(cpuEl) cpuEl.innerText = cpuPct + ' %';
                                    let memEl = document.getElementById('mem_val');
                                    if(memEl) memEl.innerText = memPct + ' %';
                                    let cNameEl = document.getElementById('cpu_name');
                                    if(cNameEl) cNameEl.innerText = jdata.CPU_Name || 'Unknown CPU';
                                    let gNameEl = document.getElementById('gpu_name');
                                    if(gNameEl) gNameEl.innerText = jdata.GPU_Name || 'Unknown GPU';
                                    let mTextEl = document.getElementById('mem_text');
                                    if(mTextEl) mTextEl.innerText = `已用 ${usedGB} GB / 共 ${totalGB} GB`;

                                    let now = new Date();
                                    perfHistory.push({ time: now, cpu: parseFloat(cpuPct), mem: parseFloat(memPct) });
                                    if(perfHistory.length > 50) perfHistory.shift();
                                    drawChart();
                                } catch(e) { console.error(e); }
                            } else {
                                tries++;
                                if (tries > 20) { clearInterval(itv); }
                            }
                        }).catch(e => { clearInterval(itv); });
                    }, 500);
                }).catch(e => { });
            }

            function showLoading(show) { document.getElementById('loading').style.display = show ? 'inline' : 'none'; }

            function sendNativeCommand(cmdType, arg, callback) {
                showLoading(true);
                let fullCmd = "F_CMD:" + cmdType + ":" + arg;
                fetch('/api/file/cmd', {
                    method: 'POST', headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({mac: '{{ mac }}', cmd: fullCmd})
                }).then(() => pollResult(callback)).catch(err => { showLoading(false); alert(err); });
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
                            clearInterval(itv); polling = false; showLoading(false);
                            callback(data.data);
                        } else {
                            tries++;
                            // 增加最大重试次数以等待较长操作或稍微延迟的网络
                            if (tries > 300) { clearInterval(itv); polling = false; showLoading(false); console.error("超时"); }
                        }
                    }).catch(e => { clearInterval(itv); polling = false; showLoading(false); });
                }, 1000); // 增加间隔减少请求频率
            }

            function formatSize(bytes) {
                if (!bytes) return '0 MB';
                return (bytes / 1024 / 1024).toFixed(1) + ' MB';
            }

            function scrollBottom() {
                // optional function if needed
            }

            function switchTab(tab) {
                document.querySelectorAll('.tabs button').forEach(b => b.classList.remove('active'));
                document.getElementById('tab_' + tab).classList.add('active');
                currentTab = tab;
                if(perfInterval) { clearInterval(perfInterval); perfInterval = null; }
                if(currentTab === 'perf') {
                    document.getElementById('content_area').innerHTML = `
                         <div id="perf_layout">
                             <div class="metric-box">
                                 <div class="metric-title">🖥️ CPU (<span id="cpu_name" style="font-size:14px;color:#888;">获取中...</span>)</div>
                                 <div class="metric-value" id="cpu_val">0.0 %</div>
                             </div>
                             <div class="metric-box">
                                 <div class="metric-title">🧠 内存 (<span id="mem_text" style="font-size:14px;color:#888;">获取中...</span>)</div>
                                 <div class="metric-value" id="mem_val">0.0 %</div>
                             </div>
                             <div class="metric-box" style="width:94%;">
                                 <div class="metric-title">🎮 显卡 (GPU)</div>
                                 <div class="metric-value" id="gpu_name" style="font-size:20px;">获取中...</div>
                             </div>
                             <div style="width:94%; margin-top:20px;"><canvas id="perfChart" width="800" height="300" style="background:#fff; border-radius:5px; box-shadow:0 1px 3px rgba(0,0,0,0.1); width:100%;"></canvas></div>
                         </div>`;
                    perfHistory = [];
                    fetchPerfData();
                    perfInterval = setInterval(fetchPerfData, 2000);
                } else {
                    refreshCurrentTab();
                }
            }

            function refreshCurrentTab() {
                if(currentTab === 'perf') {
                    perfHistory = [];
                    fetchPerfData();
                    return;
                }

                let content = document.getElementById('content_area');
                content.innerHTML = '<div style="padding:20px;text-align:center;color:#666;">⏳ 正在下发指令并获取数据中，由于数据量可能较大，请稍候...</div>';

                if (currentTab === 'proc') {
                    sendNativeCommand('TASK_PROC', 'ALL', res => {
                        try {
                            let idx = res.indexOf('[');
                            if (idx >= 0) res = res.substring(idx);
                            procData = JSON.parse(res);
                            if(!Array.isArray(procData)) procData = [procData];
                            renderProc();
                        } catch(e) { content.innerHTML = "解析失败或无数据，原始返回结果日志:<br><pre style='color:red;white-space:pre-wrap;word-break:break-all;'>" + res + "</pre>"; }
                    });
                } else if (currentTab === 'startup') {
                    sendNativeCommand('TASK_STARTUP', 'ALL', res => {
                        try {
                            let idx = res.indexOf('[');
                            if (idx >= 0) res = res.substring(idx);
                            let json = JSON.parse(res);
                            if(!Array.isArray(json)) json = [json];
                            let html = `<div class="table-responsive"><table><thead><tr><th>名称</th><th>启动命令</th><th>位置</th></tr></thead><tbody>`;
                            json.forEach(s => {
                                html += `<tr><td><b>${s.Name||''}</b></td><td style="word-break:break-all;">${s.Command||''}</td><td>${s.Location||''}</td></tr>`;
                            });
                            html += `</tbody></table></div>`;
                            content.innerHTML = html;
                        } catch(e) { content.innerHTML = "解析失败或暂时无直接启动项:<br><pre style='color:red;white-space:pre-wrap;word-break:break-all;'>" + res + "</pre>"; }
                    });
                } else if (currentTab === 'software') {
                    sendNativeCommand('TASK_SOFTWARE', 'ALL', res => {
                        try {
                            let idx = res.indexOf('[');
                            if (idx >= 0) res = res.substring(idx);
                            softData = JSON.parse(res);
                            if(!Array.isArray(softData)) softData = [softData];
                            renderSoft();
                        } catch(e) { content.innerHTML = "解析软件列表失败:<br><pre style='color:red;white-space:pre-wrap;word-break:break-all;'>" + res + "</pre>"; }
                    });
                } else if (currentTab === 'svc') {
                    sendNativeCommand('TASK_SVC', 'ALL', res => {
                        try {
                            let idx = res.indexOf('[');
                            if (idx >= 0) res = res.substring(idx);
                            let json = JSON.parse(res);
                            if(!Array.isArray(json)) json = [json];
                            let html = `<div class="table-responsive"><table><thead><tr><th>名称</th><th>显示名称</th><th>状态</th><th>操作</th></tr></thead><tbody>`;
                            json.forEach(s => {
                                let stColor = s.Status === 'Running' ? 'green' : (s.Status === 'Stopped' ? 'red' : 'black');
                                html += `<tr>
                                    <td>${s.Name}</td><td>${s.DisplayName}</td>
                                    <td style="color:${stColor}; font-weight:bold;">${s.Status}</td>
                                    <td>
                                        ${s.Status === 'Running' ? `<button class="btn-danger" onclick="ctrlSvc('${s.Name}', 'stop')">停止</button>` : `<button class="btn-success" onclick="ctrlSvc('${s.Name}', 'start')">启动</button>`}
                                    </td>
                                </tr>`;
                            });
                            html += `</tbody></table></div>`;
                            content.innerHTML = html;
                        } catch(e) { content.innerHTML = "解析服务列表失败:<br><pre style='color:red;white-space:pre-wrap;word-break:break-all;'>" + res + "</pre>"; }
                    });
                }
            }

            let softData = [];
            let softSortCol = 'Name';
            let softSortDir = 1;

            function sortSoft(col) {
                if(softSortCol === col) softSortDir *= -1;
                else { softSortCol = col; softSortDir = 1; }
                renderSoft();
            }

            function renderSoft() {
                let sorted = [...softData];
                sorted.sort((a,b) => {
                    let valA = a[softSortCol] || '';
                    let valB = b[softSortCol] || '';
                    valA = valA.toString().toLowerCase();
                    valB = valB.toString().toLowerCase();
                    if(valA < valB) return -1 * softSortDir;
                    if(valA > valB) return 1 * softSortDir;
                    return 0;
                });

                let html = `<div class="table-responsive"><table><thead><tr>
                    <th onclick="sortSoft('Name')" style="cursor:pointer; user-select:none;">软件名称 ${softSortCol==='Name'?(softSortDir===1?'▲':'▼'):''}</th>
                    <th onclick="sortSoft('Version')" style="cursor:pointer; user-select:none;">版本 ${softSortCol==='Version'?(softSortDir===1?'▲':'▼'):''}</th>
                    <th onclick="sortSoft('Publisher')" style="cursor:pointer; user-select:none;">发布者 ${softSortCol==='Publisher'?(softSortDir===1?'▲':'▼'):''}</th>
                    <th onclick="sortSoft('InstallDate')" style="cursor:pointer; user-select:none;">安装时间 ${softSortCol==='InstallDate'?(softSortDir===1?'▲':'▼'):''}</th>
                    <th>操作</th></tr></thead><tbody>`;
                sorted.forEach(s => {
                    html += `<tr><td style="font-weight:bold;color:#333;">${s.Name||''}</td><td>${s.Version||''}</td><td>${s.Publisher||''}</td><td>${s.InstallDate||''}</td><td>`;
                    if(s.UninstallString) {
                        html += `<button class="btn-danger" style="margin-right:5px;" onclick="uninstallSoft('${encodeURIComponent(s.UninstallString)}')">卸载</button>`;
                    }
                    if(s.ModifyPath) {
                        html += `<button class="btn-success" style="background:#17a2b8;" onclick="uninstallSoft('${encodeURIComponent(s.ModifyPath)}')">修改</button>`;
                    }
                    html += `</td></tr>`;
                });
                html += `</tbody></table></div>`;
                let content = document.getElementById('content_area');
                if(content) content.innerHTML = html;
            }

            window.uninstallSoft = function(cmd) {
                let realCmd = decodeURIComponent(cmd);
                if(!confirm(`⚠️ 确定以最高权限执行：\n\n${realCmd}\n\n有些非静默程序的卸载/修改可能会弹窗等待被控端操作。`)) return;
                sendNativeCommand('UNINSTALL', realCmd, res => {
                    alert("执行已下发返回:\n" + res);
                    refreshCurrentTab();
                });
            };

            window.killProcess = function(pid, name) {
                if(!confirm(`⚠️ 危险操作：确定强制结束进程 ${name} (PID: ${pid})？`)) return;
                sendNativeCommand('TASK_KILL', pid.toString(), res => {
                    refreshCurrentTab();
                });
            };

            window.ctrlSvc = function(name, action) {
                if(!confirm(`⚠️ 确定更改系统服务 ${name} 状态？`)) return;
                sendNativeCommand('TASK_SVC_CTRL', action + '|' + name, res => {
                    refreshCurrentTab();
                });
            };

            setTimeout(() => switchTab('proc'), 500);

        </script>
    </body>
    </html>
    """
    return render_template_string(TASKMGR_HTML, admin_css=ADMIN_CSS, mac=mac, info=clients_db[mac])

# 抽离表格的HTML，供前端 AJAX 每隔几秒拉取实现真正的无感“实时在线状态更新”
