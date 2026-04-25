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
                # 不要把庞大的返回结果（如进程列表、文件列表、服务列表、安装软件）存入本地硬盘JSON文件
                # 它们可能会在几秒内造成MB级别的文件不停写入，从而导致服务端卡死、请求超时
                if k not in ['stream_frame', 'file_result']:
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
        return (datetime.now() - last_dt).total_seconds() < 25 # 放宽离线判定到 25 秒
    except:
        return False

# ================= 数据加密解密支持 =================
ENCRYPTION_KEY = b'PowerOFF2026'

def encrypt_data(text):
    if not text: return ""
    return text  # 返回明文

def decrypt_data(hex_str):
    if not hex_str: return ""
    # 如果原本就是明文特征，直接返回
    if "-" in hex_str or "." in hex_str or "{" in hex_str or "[" in hex_str:
        return hex_str

    try:
        key = "PowerOFF2026"
        res = ""
        if len(hex_str) % 2 != 0:
            return hex_str
        for i in range(0, len(hex_str), 2):
            byte_str = hex_str[i:i+2]
            b = int(byte_str, 16)
            res += chr(b ^ ord(key[(i//2) % len(key)]))

        # 验证解密结果是否符合可读ASCII，如果不符合则说明原本就未加密
        if all(32 <= ord(c) <= 126 for c in res[:10]):
            return res
        return hex_str
    except Exception:
        return hex_str

