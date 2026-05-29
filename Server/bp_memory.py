from flask import Blueprint, render_template_string, session, redirect, url_for
from core import clients_db

try:
    from ui import ADMIN_CSS
except Exception:
    ADMIN_CSS = ""

bp = Blueprint('memory', __name__)


@bp.route('/memory/<mac>')
def memory_page(mac):
    if not session.get('logged_in'):
        return redirect(url_for('auth.login'))
    if mac not in clients_db:
        return "设备不存在"

    html = r"""
    <!DOCTYPE html>
    <html lang="zh-CN">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
        <title>Memory - {{ info.name }}</title>
        <style>
            {{ admin_css|safe }}
            body { background:var(--bg); padding:0; }
            .memory-grid { display:grid; grid-template-columns:minmax(320px,.85fr) minmax(0,1.15fr); gap:12px; align-items:start; }
            .mem-tools { display:flex; flex-wrap:wrap; gap:8px; align-items:center; margin-top:12px; }
            .mem-tools input { max-width:260px; }
            .table-wrap { overflow:auto; border:1px solid var(--line); border-radius:8px; background:#fff; max-height:calc(100vh - 250px); }
            table { min-width:760px; margin:0; }
            tr:hover { background:#f8fafc; }
            tr.selected { background:#eff6ff; }
            th.sortable { cursor:pointer; user-select:none; }
            th.sortable:hover { color:var(--blue); }
            .mono { font-family:Consolas, "Cascadia Mono", monospace; }
            .small { color:var(--muted); font-size:12px; }
            .read-panel { background:#0b1020; color:#d1fae5; border:1px solid #1f2937; border-radius:8px; padding:12px; overflow:auto; max-height:45vh; font:12px/1.55 Consolas, "Cascadia Mono", monospace; }
            .read-meta { color:#bfdbfe; margin-bottom:10px; white-space:pre-wrap; }
            .read-columns { display:grid; grid-template-columns:120px minmax(520px,1fr) minmax(220px,.65fr); gap:0; min-width:880px; border:1px solid #1f2937; border-radius:6px; overflow:hidden; }
            .read-col { min-width:0; border-right:1px solid #1f2937; background:#0b1020; }
            .read-col:last-child { border-right:0; }
            .read-col h3 { margin:0; padding:7px 9px; background:#111827; color:#bfdbfe; border-bottom:1px solid #334155; font-size:12px; line-height:1.4; }
            .read-col pre { margin:0; padding:8px 9px; min-height:120px; line-height:1.72; user-select:text; white-space:pre; overflow:visible; }
            .read-offset pre { color:#93c5fd; }
            .read-hex pre { color:#bbf7d0; }
            .read-ascii pre { color:#fde68a; }
            .read-ascii { background:#101827; }
            .split-read { display:grid; grid-template-columns:1fr; gap:10px; }
            .selected-title { color:var(--blue); font-weight:700; }
            .search-summary { color:var(--muted); font-size:13px; margin-top:8px; }
            @media(max-width:900px){ .memory-grid{grid-template-columns:1fr}.table-wrap{max-height:none}.mem-tools input{max-width:none;flex:1 1 100%} }
            @media(max-width:640px){ .mem-tools button,.mem-tools input,.mem-tools select{flex:1 1 100%}.read-panel{font-size:11px;max-height:none} table{min-width:720px} }
        </style>
    </head>
    <body>
        <main class="shell">
            <div class="topbar">
                <div class="title">
                    <h1>Memory Viewer</h1>
                    <p>{{ info.name }} <code>{{ mac }}</code></p>
                </div>
                <a class="btn muted" href="/">Back</a>
            </div>

            <div class="memory-grid">
                <section class="panel">
                    <div class="section-head">
                        <div>
                            <h2>Processes</h2>
                            <p>只读查看进程内存指标和地址空间。</p>
                        </div>
                        <button onclick="loadProcesses()">Refresh</button>
                    </div>
                    <div class="mem-tools">
                        <input id="filter" type="text" placeholder="Filter process name or PID" oninput="renderProcesses()">
                        <span id="status" class="small">Ready</span>
                    </div>
                    <div class="table-wrap" style="margin-top:12px">
                        <table>
                            <thead><tr><th class="sortable" onclick="sortProcesses('Pid')">PID</th><th class="sortable" onclick="sortProcesses('Name')">Name</th><th class="sortable" onclick="sortProcesses('WorkingSet')">Working set</th><th class="sortable" onclick="sortProcesses('PrivateBytes')">Private</th><th class="sortable" onclick="sortProcesses('Threads')">Threads</th><th class="sortable" onclick="sortProcesses('Handles')">Handles</th></tr></thead>
                            <tbody id="proc-body"><tr><td colspan="6" class="empty">Click Refresh.</td></tr></tbody>
                        </table>
                    </div>
                </section>

                <section class="panel">
                    <div class="section-head">
                        <div>
                            <h2>Memory Map</h2>
                            <p id="selected-proc">Select a process first.</p>
                        </div>
                        <button class="muted" onclick="loadMap()" id="map-refresh" disabled>Reload map</button>
                    </div>
                    <div class="table-wrap">
                        <table>
                            <thead><tr><th class="sortable" onclick="sortMap('Base')">Base</th><th class="sortable" onclick="sortMap('Size')">Size</th><th class="sortable" onclick="sortMap('State')">State</th><th class="sortable" onclick="sortMap('Protect')">Protect</th><th class="sortable" onclick="sortMap('Type')">Type</th><th>Read</th></tr></thead>
                            <tbody id="map-body"><tr><td colspan="6" class="empty">No process selected.</td></tr></tbody>
                        </table>
                    </div>
                </section>
            </div>

            <section class="panel">
                <div class="section-head">
                    <div>
                        <h2>Search Memory</h2>
                        <p>像 Cheat Engine 一样扫描目标进程的只读匹配地址。最多返回 200 条结果。</p>
                    </div>
                </div>
                <div class="mem-tools">
                    <select id="search-mode">
                        <option value="ascii">ASCII / UTF-8 text</option>
                        <option value="utf16">UTF-16LE text</option>
                        <option value="hex">Hex bytes</option>
                    </select>
                    <input id="search-query" class="mono" type="text" placeholder="Text, or hex like 48 65 6C 6C 6F">
                    <button onclick="searchMemory(false)">First scan</button>
                    <button class="muted" onclick="searchMemory(true)">Next scan</button>
                    <button class="warn" onclick="resetSearch()">Reset</button>
                    <span id="search-status" class="small"></span>
                </div>
                <div id="search-summary" class="search-summary"></div>
                <div class="table-wrap" style="margin-top:12px; max-height:360px">
                    <table>
                        <thead><tr><th class="sortable" onclick="sortSearch('Address')">Address</th><th class="sortable" onclick="sortSearch('RegionBase')">Region</th><th class="sortable" onclick="sortSearch('Protect')">Protect</th><th class="sortable" onclick="sortSearch('Type')">Type</th><th class="sortable" onclick="sortSearch('Preview')">Preview</th><th>Read</th></tr></thead>
                        <tbody id="search-body"><tr><td colspan="6" class="empty">No search yet.</td></tr></tbody>
                    </table>
                </div>
            </section>

            <section class="panel">
                <div class="section-head">
                    <div>
                        <h2>Read Small Block</h2>
                        <p>最多读取 64KB，用于调试预览。不会写入目标进程内存。</p>
                    </div>
                </div>
                <div class="mem-tools">
                    <input id="read-address" class="mono" type="text" placeholder="Address, e.g. 0x7FF...">
                    <input id="read-size" type="number" min="1" max="65536" value="256">
                    <button onclick="readMemory()">Read</button>
                    <span id="read-status" class="small"></span>
                </div>
                <div class="split-read" style="margin-top:12px">
                    <div id="read-output" class="read-panel">No data.</div>
                </div>
            </section>
        </main>

        <script>
            let processes = [];
            let memoryMap = [];
            let searchResults = [];
            let selectedPid = null;
            let selectedName = '';
            let polling = false;
            let procSort = { col: 'PrivateBytes', dir: -1 };
            let mapSort = { col: 'Base', dir: 1 };
            let searchSort = { col: 'Address', dir: 1 };
            let lastReadRows = [];
            let lastReadMeta = '';

            function fmt(bytes) {
                bytes = Number(bytes || 0);
                if (bytes < 1024) return bytes + ' B';
                const units = ['KB','MB','GB','TB'];
                let val = bytes / 1024, i = 0;
                while (val >= 1024 && i < units.length - 1) { val /= 1024; i++; }
                return val.toFixed(val >= 100 ? 0 : 1) + ' ' + units[i];
            }

            function esc(s) {
                return String(s ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
            }

            function setStatus(text) {
                document.getElementById('status').textContent = text;
            }

            function numericAddress(value) {
                if (typeof value === 'number') return value;
                const s = String(value || '').trim();
                if (!s) return 0;
                return Number.parseInt(s.startsWith('0x') || s.startsWith('0X') ? s.slice(2) : s, s.startsWith('0x') || s.startsWith('0X') ? 16 : 10) || 0;
            }

            function compareRows(a, b, col) {
                let av = a[col], bv = b[col];
                if (col === 'Base' || col === 'Address' || col === 'RegionBase') {
                    av = numericAddress(av);
                    bv = numericAddress(bv);
                } else if (['Pid','WorkingSet','PrivateBytes','Threads','Handles','Size'].includes(col)) {
                    av = Number(av || 0);
                    bv = Number(bv || 0);
                } else {
                    av = String(av || '').toLowerCase();
                    bv = String(bv || '').toLowerCase();
                }
                if (av < bv) return -1;
                if (av > bv) return 1;
                return 0;
            }

            function updateSort(sortObj, col) {
                if (sortObj.col === col) sortObj.dir *= -1;
                else { sortObj.col = col; sortObj.dir = 1; }
            }

            function sendNative(cmd, callback) {
                if (polling) return;
                polling = true;
                setStatus('Waiting for client...');
                fetch('/api/file/cmd', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({mac: '{{ mac }}', cmd})
                }).then(() => {
                    let tries = 0;
                    const timer = setInterval(() => {
                        fetch('/api/file/result/{{ mac }}')
                        .then(r => r.json())
                        .then(data => {
                            if (data.status === 'ready') {
                                clearInterval(timer);
                                polling = false;
                                setStatus('Ready');
                                callback(data.data || '');
                            } else if (++tries > 90) {
                                clearInterval(timer);
                                polling = false;
                                setStatus('Timeout');
                            }
                        }).catch(err => {
                            clearInterval(timer);
                            polling = false;
                            setStatus('Error: ' + err);
                        });
                    }, 1000);
                }).catch(err => {
                    polling = false;
                    setStatus('Error: ' + err);
                });
            }

            function parseJsonPayload(text) {
                const startObj = text.indexOf('{');
                const startArr = text.indexOf('[');
                let idx = -1;
                if (startObj >= 0 && startArr >= 0) idx = Math.min(startObj, startArr);
                else idx = Math.max(startObj, startArr);
                if (idx > 0) text = text.slice(idx);
                return JSON.parse(text);
            }

            function loadProcesses() {
                document.getElementById('proc-body').innerHTML = '<tr><td colspan="6" class="empty">Loading...</td></tr>';
                sendNative('F_CMD:MEM_PROC:ALL', res => {
                    try {
                        processes = parseJsonPayload(res);
                        if (!Array.isArray(processes)) processes = [];
                        renderProcesses();
                    } catch(e) {
                        document.getElementById('proc-body').innerHTML = '<tr><td colspan="6"><pre style="color:red;white-space:pre-wrap">' + esc(res) + '</pre></td></tr>';
                    }
                });
            }

            function renderProcesses() {
                const q = document.getElementById('filter').value.trim().toLowerCase();
                const rows = processes
                    .filter(p => !q || String(p.Pid).includes(q) || String(p.Name || '').toLowerCase().includes(q))
                    .sort((a, b) => compareRows(a, b, procSort.col) * procSort.dir);
                document.getElementById('proc-body').innerHTML = rows.map(p => `
                    <tr class="${selectedPid === p.Pid ? 'selected' : ''}" onclick="selectProcess(${Number(p.Pid)})">
                        <td class="mono">${p.Pid}</td>
                        <td><b>${esc(p.Name)}</b><div class="small">${esc(p.Path)}</div></td>
                        <td>${fmt(p.WorkingSet)}</td>
                        <td>${fmt(p.PrivateBytes)}</td>
                        <td>${p.Threads || 0}</td>
                        <td>${p.Handles || 0}</td>
                    </tr>`).join('') || '<tr><td colspan="6" class="empty">No processes.</td></tr>';
            }

            window.sortProcesses = function(col) {
                updateSort(procSort, col);
                renderProcesses();
            }

            window.selectProcess = function(pid) {
                selectedPid = pid;
                const proc = processes.find(p => Number(p.Pid) === Number(pid)) || {};
                selectedName = proc.Name || String(pid);
                document.getElementById('selected-proc').innerHTML = '<span class="selected-title">' + esc(selectedName) + '</span> PID <code>' + pid + '</code>';
                document.getElementById('map-refresh').disabled = false;
                renderProcesses();
                loadMap();
            }

            function loadMap() {
                if (!selectedPid) return;
                document.getElementById('map-body').innerHTML = '<tr><td colspan="6" class="empty">Loading...</td></tr>';
                sendNative('F_CMD:MEM_MAP:' + selectedPid, res => {
                    try {
                        const data = parseJsonPayload(res);
                        if (data.error) throw new Error(data.error);
                        memoryMap = Array.isArray(data) ? data : [];
                        renderMap();
                    } catch(e) {
                        document.getElementById('map-body').innerHTML = '<tr><td colspan="6"><pre style="color:red;white-space:pre-wrap">' + esc(String(e.message || e)) + '</pre></td></tr>';
                    }
                });
            }

            function renderMap() {
                const rows = [...memoryMap].sort((a, b) => compareRows(a, b, mapSort.col) * mapSort.dir);
                document.getElementById('map-body').innerHTML = rows.map(r => `
                    <tr>
                        <td class="mono">${esc(r.Base)}</td>
                        <td>${fmt(r.Size)}</td>
                        <td>${esc(r.State)}</td>
                        <td>${esc(r.Protect)}</td>
                        <td>${esc(r.Type)}</td>
                        <td><button class="mini" onclick="prepareRead('${esc(r.Base)}', ${Math.min(Number(r.Size || 256), 4096)})">Read</button></td>
                    </tr>`).join('') || '<tr><td colspan="6" class="empty">No regions.</td></tr>';
            }

            window.sortMap = function(col) {
                updateSort(mapSort, col);
                renderMap();
            }

            function renderSearchResults() {
                const rows = [...searchResults].sort((a, b) => compareRows(a, b, searchSort.col) * searchSort.dir);
                document.getElementById('search-body').innerHTML = rows.map(r => `
                    <tr>
                        <td class="mono">${esc(r.Address)}</td>
                        <td class="mono">${esc(r.RegionBase)}</td>
                        <td>${esc(r.Protect)}</td>
                        <td>${esc(r.Type)}</td>
                        <td class="mono">${esc(r.Preview)}</td>
                        <td><button class="mini" onclick="prepareRead('${esc(r.Address)}', 256)">Read</button></td>
                    </tr>`).join('') || '<tr><td colspan="6" class="empty">No matches.</td></tr>';
            }

            window.sortSearch = function(col) {
                updateSort(searchSort, col);
                renderSearchResults();
            }

            window.resetSearch = function() {
                searchResults = [];
                document.getElementById('search-summary').textContent = '';
                document.getElementById('search-body').innerHTML = '<tr><td colspan="6" class="empty">No search yet.</td></tr>';
            }

            function searchMemory(nextScan) {
                if (!selectedPid) {
                    alert('Select a process first.');
                    return;
                }
                const mode = document.getElementById('search-mode').value;
                const query = document.getElementById('search-query').value;
                if (!query.trim()) {
                    alert('Input search text or hex bytes first.');
                    return;
                }
                if (query.includes('|')) {
                    alert('Search text cannot contain | for now.');
                    return;
                }
                if (nextScan && searchResults.length === 0) {
                    alert('Run First scan before Next scan.');
                    return;
                }
                document.getElementById('search-status').textContent = nextScan ? 'Filtering...' : 'Searching...';
                document.getElementById('search-summary').textContent = '';
                document.getElementById('search-body').innerHTML = '<tr><td colspan="6" class="empty">' + (nextScan ? 'Filtering current result addresses...' : 'Scanning readable committed memory...') + '</td></tr>';
                const cmd = nextScan
                    ? 'F_CMD:MEM_FILTER:' + selectedPid + '|' + mode + '|' + query + '|' + searchResults.map(r => r.Address).join(',')
                    : 'F_CMD:MEM_SEARCH:' + selectedPid + '|' + mode + '|' + query;
                sendNative(cmd, res => {
                    document.getElementById('search-status').textContent = '';
                    try {
                        const data = parseJsonPayload(res);
                        if (data.error) throw new Error(data.error);
                        searchResults = Array.isArray(data.Results) ? data.Results : [];
                        const checkedText = nextScan ? ', checked ' + (data.Checked || 0) : ', scanned ' + fmt(data.Scanned);
                        document.getElementById('search-summary').textContent =
                            (nextScan ? 'Next scan' : 'First scan') + ': pattern ' + data.PatternBytes + ' byte(s)' +
                            checkedText + ', found ' + searchResults.length + (data.Truncated ? ' (truncated)' : '');
                        renderSearchResults();
                    } catch(e) {
                        document.getElementById('search-summary').textContent = '';
                        document.getElementById('search-body').innerHTML = '<tr><td colspan="6"><pre style="color:red;white-space:pre-wrap">' + esc(String(e.message || e)) + '</pre></td></tr>';
                    }
                });
            }

            window.prepareRead = function(address, size) {
                document.getElementById('read-address').value = address;
                document.getElementById('read-size').value = Math.min(size || 256, 4096);
                readMemory();
            }

            function copyText(text) {
                if (navigator.clipboard && window.isSecureContext) {
                    navigator.clipboard.writeText(text).catch(() => fallbackCopy(text));
                } else {
                    fallbackCopy(text);
                }
            }

            function fallbackCopy(text) {
                const ta = document.createElement('textarea');
                ta.value = text;
                ta.style.position = 'fixed';
                ta.style.left = '-9999px';
                document.body.appendChild(ta);
                ta.focus();
                ta.select();
                document.execCommand('copy');
                document.body.removeChild(ta);
            }

            function buildReadRows(hex, ascii) {
                const rows = [];
                for (let i = 0; i < hex.length; i += 32) {
                    const chunk = hex.slice(i, i + 32);
                    const bytes = chunk.match(/../g) || [];
                    rows.push({
                        offset: (i / 2).toString(16).toUpperCase().padStart(8, '0'),
                        hex: bytes.join(' '),
                        ascii: (ascii || '').slice(i / 2, i / 2 + 16)
                    });
                }
                return rows;
            }

            function renderReadDump(meta, rows) {
                lastReadMeta = meta;
                lastReadRows = rows;
                const output = document.getElementById('read-output');
                if (!rows.length) {
                    output.textContent = meta + '\n\nNo bytes read.';
                    return;
                }
                output.innerHTML = `
                    <div class="read-meta">${esc(meta)}</div>
                    <div class="read-columns">
                        <div class="read-col read-offset"><h3>Offset</h3><pre class="mono">${esc(rows.map(r => r.offset).join('\n'))}</pre></div>
                        <div class="read-col read-hex"><h3>Hex</h3><pre class="mono">${esc(rows.map(r => r.hex).join('\n'))}</pre></div>
                        <div class="read-col read-ascii"><h3>ASCII</h3><pre class="mono">${esc(rows.map(r => r.ascii).join('\n'))}</pre></div>
                    </div>`;
            }

            function readMemory() {
                if (!selectedPid) {
                    alert('Select a process first.');
                    return;
                }
                const address = document.getElementById('read-address').value.trim();
                const size = Math.min(Math.max(parseInt(document.getElementById('read-size').value || '256', 10), 1), 65536);
                if (!address) {
                    alert('Input an address first.');
                    return;
                }
                document.getElementById('read-status').textContent = 'Reading...';
                sendNative('F_CMD:MEM_READ:' + selectedPid + '|' + address + '|' + size, res => {
                    document.getElementById('read-status').textContent = '';
                    try {
                        const data = parseJsonPayload(res);
                        if (data.error) throw new Error(data.error);
                        let meta = 'PID: ' + selectedPid + '  Address: ' + data.Address + '  Read: ' + data.Read + '/' + data.Requested;
                        if (data.warning) meta += '\nWarning: ' + data.warning;
                        renderReadDump(meta, buildReadRows(data.Hex || '', data.Ascii || ''));
                    } catch(e) {
                        document.getElementById('read-output').textContent = String(e.message || e) + '\n\n' + res;
                    }
                });
            }

            setTimeout(loadProcesses, 300);
        </script>
    </body>
    </html>
    """
    return render_template_string(html, admin_css=ADMIN_CSS, mac=mac, info=clients_db[mac])
