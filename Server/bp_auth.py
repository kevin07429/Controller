from flask import Blueprint, request, render_template_string, session, redirect, url_for, send_from_directory, jsonify, Response
from datetime import datetime
import os, time, json, threading
from core import clients_db, save_db, is_online, log_login_attempt, USERNAME, PASSWORD, UPDATE_DIR, VERSION_FILE

bp = Blueprint('auth', __name__)

@bp.route('/login', methods=['GET', 'POST'])
def login():
    if request.method == 'POST':
        if request.form.get('username') == USERNAME and request.form.get('password') == PASSWORD:
            session['logged_in'] = True
            log_login_attempt(request, True)
            return redirect(url_for('main.index'))
        else:
            log_login_attempt(request, False)
            return "账号或密码错误！<a href='/login'>返回重试</a>"
    return '''
    <!DOCTYPE html>
    <html lang="zh-CN">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
    </head>
    <body style="background:#f0f2f5; font-family:sans-serif; padding: 20px; margin: 0;">
        <form method="post" style="max-width:300px; margin:10% auto; background:#fff; padding:30px; border-radius:8px; box-shadow:0 0 10px rgba(0,0,0,0.1); text-align:center; box-sizing: border-box; width: 100%;">
            <h2>后台管理登录</h2>
            <input type="text" name="username" placeholder="账号" style="width:100%; padding:10px; margin-bottom:15px; box-sizing:border-box;" required>
            <input type="password" name="password" placeholder="密码" style="width:100%; padding:10px; margin-bottom:15px; box-sizing:border-box;" required>
            <button type="submit" style="width:100%; padding:10px; background:#007bff; color:#fff; border:none; border-radius:5px; cursor:pointer;">登录</button>
        </form>
    </body>
    '''

@bp.route('/logout')
def logout():
    session.pop('logged_in', None)
    return redirect(url_for('auth.login'))
