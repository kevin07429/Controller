# -*- coding: utf-8 -*-
from flask import Flask, request, render_template_string, session, redirect, url_for, send_from_directory, jsonify
from datetime import datetime
import os
import json
import time
import threading


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

def log_login_attempt(req, success):
    try:
        log_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'login_log.txt')
        time_str = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        ip = req.remote_addr or req.environ.get('HTTP_X_FORWARDED_FOR', '')
        status = "成功" if success else "失败"
        ua = req.headers.get('User-Agent', '')
        with open(log_path, 'a', encoding='utf-8') as f:
            f.write(f"[{time_str}] IP: {ip} | 状态: {status} | User-Agent: {ua}\n")
    except:
        pass


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
    try:
        db_copy = {}
        for mac, info in list(clients_db.items()):
            safe_info = {}
            for k, v in list(info.items()):
                if k not in ['stream_frame']:
                    if isinstance(v, (str, int, float, bool, type(None), list, dict)):
                        safe_info[k] = v
            db_copy[mac] = safe_info
        with open(DB_FILE, 'w', encoding='utf-8') as f:
            json.dump(db_copy, f, ensure_ascii=False)
    except Exception as e:
        print(f"Save DB Error: {e}")

def is_online(last_seen_str):
    try:
        last_dt = datetime.strptime(last_seen_str, "%Y-%m-%d %H:%M:%S")
        return (datetime.now() - last_dt).total_seconds() < 15
    except:
        return False
