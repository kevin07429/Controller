ADMIN_CSS = r"""
:root {
    --bg:#f5f7fb; --panel:#fff; --line:#d9e0ea; --text:#1f2937; --muted:#667085;
    --blue:#2563eb; --green:#16a34a; --red:#dc2626; --amber:#d97706; --slate:#475569;
}
* { box-sizing: border-box; }
body { margin:0; background:var(--bg); color:var(--text); font-family:Segoe UI, Arial, sans-serif; }
.shell { max-width:1180px; margin:0 auto; padding:24px 18px 40px; }
.panel, .container {
    background:var(--panel); border:1px solid var(--line); border-radius:8px; padding:16px;
    margin-bottom:14px; box-shadow:0 1px 2px rgba(15,23,42,.04);
}
.topbar, .header {
    display:flex; justify-content:space-between; gap:16px; align-items:center; margin-bottom:14px;
    border-bottom:1px solid var(--line); padding-bottom:12px;
}
h1, h2, h3 { margin-top:0; letter-spacing:0; }
a { color:var(--blue); text-decoration:none; }
a:hover { text-decoration:underline; }
.btn, button {
    border:0; border-radius:6px; padding:9px 12px; background:var(--blue); color:#fff;
    cursor:pointer; font-weight:600; text-decoration:none; display:inline-block; min-height:36px;
}
.btn:hover, button:hover { filter:brightness(.94); }
.btn-success, .btn-on, .ok, button.ok { background:var(--green); }
.btn-danger, .btn-del, .danger, button.danger { background:var(--red); }
.btn-warning, .btn-off, .warn, button.warn { background:var(--amber); color:#fff; }
.btn-info, .muted, button.muted { background:var(--slate); }
input[type=text], input[type=password], input[type=file], input[type=number], select, textarea {
    width:100%; padding:9px 10px; border:1px solid var(--line); border-radius:6px; background:#fff; min-height:38px;
}
.toolbar, .controls, .tabs { display:flex; flex-wrap:wrap; gap:8px; align-items:center; }
.tabs button.active { background:var(--blue); color:#fff; }
.table-responsive, .table-scroll { overflow-x:auto; -webkit-overflow-scrolling:touch; }
table { width:100%; border-collapse:separate; border-spacing:0; background:#fff; }
th { text-align:left; font-size:12px; color:#475467; background:#f8fafc; border-bottom:1px solid var(--line); padding:10px; }
td { border-bottom:1px solid #edf1f6; padding:10px; vertical-align:top; font-size:13px; }
code { background:#eef2ff; color:#3730a3; padding:2px 5px; border-radius:4px; }
.subtle { color:var(--muted); font-size:12px; }
.status-ok { color:var(--green); font-weight:700; }
.status-bad { color:var(--red); font-weight:700; }
.mini {
    border:1px solid var(--line); background:#fff; color:#344054; border-radius:5px; padding:5px 8px;
    margin:2px; font-size:12px; font-weight:600; text-decoration:none; display:inline-block; min-height:28px;
}
.mini.danger { background:#fee2e2; border-color:#fecaca; color:#b91c1c; }
@media (max-width:900px) {
    .shell { padding:16px 10px 28px; }
    .topbar, .header { align-items:stretch; flex-direction:column; }
    .toolbar .btn, .toolbar button, .controls button, .tabs button { flex:1 1 calc(50% - 8px); }
}
@media (max-width:640px) {
    .panel, .container { padding:12px; border-radius:8px; }
    h1 { font-size:24px; }
    table { min-width:720px; }
    input[type=file] { font-size:13px; }
}
"""


def status_text():
    return {
        "online": '<span class="status-ok">Online</span>',
        "offline": '<span class="status-bad">Offline</span>',
    }
