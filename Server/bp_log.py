from flask import Blueprint, request, render_template_string, session, redirect, url_for, send_from_directory, jsonify, Response
from datetime import datetime
import os, time, json, threading
from core import clients_db, save_db, is_online, log_login_attempt, USERNAME, PASSWORD, UPDATE_DIR, VERSION_FILE, decrypt_data

bp = Blueprint('log', __name__)

@bp.route('/api/upload_log/<mac>', methods=['POST'])
def api_upload_log(mac):
    mac = decrypt_data(mac)
    f = request.files.get('file')
    if f:
        log_dir = os.path.join(UPDATE_DIR, 'client_logs')
        os.makedirs(log_dir, exist_ok=True)
        f.save(os.path.join(log_dir, f"{mac}.log"))
    return "OK"

def decrypt_client_log(filepath):
    key = b"PowerOFF2026"
    res = []
    if not os.path.exists(filepath): return "暂无日志文件或终端尚未上传"
    with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
        for line in f:
            line = line.strip()
            if not line: continue
            try:
                raw_bytes = bytes.fromhex(line)
                decrypted = bytearray()
                for i, b in enumerate(raw_bytes):
                    decrypted.append(b ^ key[i % len(key)])
                try:
                    res.append(decrypted.decode('utf-8'))
                except:
                    res.append(decrypted.decode('gbk', errors='ignore'))
            except:
                res.append(line)
    return "\n".join(res)

@bp.route('/view_log/<mac>')
def view_log(mac):
    if not session.get('logged_in'): return redirect(url_for('auth.login'))
    log_path = os.path.join(UPDATE_DIR, 'client_logs', f"{mac}.log")
    content = decrypt_client_log(log_path)
    name = clients_db.get(mac, {}).get('name', '未命名设备')
    return f'''
    <!DOCTYPE html>
    <html lang="zh-CN">
    <head>
        <meta charset="UTF-8">
        <title>日志 - {name} ({mac})</title>
        <style>
            body {{ background: #222; color: #0f0; font-family: Consolas, monospace; padding: 20px; }}
            pre {{ white-space: pre-wrap; word-wrap: break-word; }}
        </style>
    </head>
    <body>
        <div style="margin-bottom: 20px;">
            <a href="/" style="background:#007bff; color:#fff; padding:6px 12px; text-decoration:none; border-radius:4px; font-family:sans-serif; margin-right:10px;">🔙 返回主面板</a>
            <a href="javascript:window.close();" style="background:#dc3545; color:#fff; padding:6px 12px; text-decoration:none; border-radius:4px; font-family:sans-serif;">✖️ 关闭独立页</a>
        </div>
        <h2>[{name}] - 云端程序运行日志 ({mac})</h2>
        <pre>{content}</pre>
    </body>
    </html>
    '''
