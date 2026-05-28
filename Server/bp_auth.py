from flask import Blueprint, request, render_template_string, session, redirect, url_for, send_from_directory, jsonify, Response
from datetime import datetime
import os, time, json, threading
from core import clients_db, save_db, is_online, log_login_attempt, USERNAME, PASSWORD, UPDATE_DIR, VERSION_FILE
try:
    from ui import ADMIN_CSS
except Exception:
    ADMIN_CSS = ""

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
            return "Login failed. <a href='/login'>Try again</a>"
    return f'''
    <!DOCTYPE html>
    <html lang="en">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>Controller Login</title>
        <style>{ADMIN_CSS}</style>
    </head>
    <body>
        <main class="shell">
        <form method="post" class="panel" style="max-width:360px; margin:10vh auto; text-align:center;">
            <h2>Controller Login</h2>
            <p class="subtle">Sign in to manage connected clients.</p>
            <input type="text" name="username" placeholder="Username" style="margin-bottom:12px;" required>
            <input type="password" name="password" placeholder="Password" style="margin-bottom:14px;" required>
            <button type="submit" style="width:100%;">Login</button>
        </form>
        </main>
    </body>
    '''

@bp.route('/logout')
def logout():
    session.pop('logged_in', None)
    return redirect(url_for('auth.login'))
