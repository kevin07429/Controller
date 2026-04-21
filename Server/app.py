from flask import Flask
import threading
from core import clean_cache_task
import bp_auth, bp_main, bp_file, bp_terminal, bp_taskmgr, bp_screen, bp_log, bp_keylog, bp_entertainment

app = Flask(__name__)
app.secret_key = 'super_secret_gardenia_key'

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
    app.run(host='0.0.0.0', port=5000)
