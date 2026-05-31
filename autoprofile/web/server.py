import socket
import threading
import time
import uuid
from pathlib import Path
from typing import Any

from fastapi import FastAPI, HTTPException
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel

from autoprofile.core import paths
from autoprofile.modules import account_creation
from autoprofile.modules.profile_update import default_settings, run_profile_update, settings_from_dict
from autoprofile.modules.account_checker import run_account_checker


class profile_update_request(BaseModel):
    accounts: str
    proxies: str = ''
    rollids: str = ''
    settings: dict[str, Any] = {}


class job_state:
    def __init__(self):
        self.lock = threading.Lock()
        self.active_thread = None
        self.active_stop_event = None
        self.job_id = None
        self.status = 'idle'
        self.started_at = None
        self.finished_at = None
        self.error = None
        self.logs = []

    def add_log(self, line):
        with self.lock:
            timestamp = time.strftime('%H:%M:%S')
            self.logs.append({'time': timestamp, 'line': line})
            if len(self.logs) > 2000:
                self.logs = self.logs[-2000:]

    def snapshot(self):
        with self.lock:
            return {
                'job_id': self.job_id,
                'status': self.status,
                'started_at': self.started_at,
                'finished_at': self.finished_at,
                'error': self.error,
                'logs': list(self.logs),
                'running': self.status == 'running'
            }


state = job_state()
checker_state = job_state()
app = FastAPI(title='Cathook Autoprofile')
static_dir = Path(__file__).resolve().parent / 'static'
app.mount('/static', StaticFiles(directory=static_dir), name='static')


def make_settings(raw_settings):
    return settings_from_dict(raw_settings)


def write_runtime_files(request):
    paths.write_text_file('accounts.txt', request.accounts.strip() + '\n' if request.accounts.strip() else '')
    paths.write_text_file('proxies.html', request.proxies.strip() + '\n' if request.proxies.strip() else '')
    paths.write_text_file('rollids.txt', request.rollids.strip() + '\n' if request.rollids.strip() else '')
    paths.write_json_file('settings.json', request.settings)


def run_job(job_id, settings, stop_event):
    with state.lock:
        state.status = 'running'
        state.started_at = time.time()
        state.finished_at = None
        state.error = None
        state.logs = []

    state.add_log(f'Started job {job_id}')
    result = run_profile_update(settings, state.add_log, stop_event)
    with state.lock:
        state.finished_at = time.time()
        state.error = result.error
        state.status = 'stopped' if result.error == 'stopped' else 'finished'
        if result.error and result.error != 'stopped':
            state.status = 'failed'
        state.active_thread = None
        state.active_stop_event = None
    state.add_log(f'Finished in {result.duration_seconds:.1f}s')


def run_checker_job(job_id, settings, stop_event):
    with checker_state.lock:
        checker_state.status = 'running'
        checker_state.started_at = time.time()
        checker_state.finished_at = None
        checker_state.error = None
        checker_state.logs = []

    checker_state.add_log(f'Started job {job_id}')
    result = run_account_checker(settings, checker_state.add_log, stop_event)
    with checker_state.lock:
        checker_state.finished_at = time.time()
        checker_state.error = result.error
        checker_state.status = 'stopped' if result.error == 'stopped' else 'finished'
        if result.error and result.error != 'stopped':
            checker_state.status = 'failed'
        checker_state.active_thread = None
        checker_state.active_stop_event = None
    checker_state.add_log(f'Finished in {result.duration_seconds:.1f}s')


@app.get('/')
def index():
    return FileResponse(static_dir / 'index.html')


@app.get('/api/modules')
def modules():
    return {
        'modules': [
            {'name': 'profile_update', 'title': 'Profile Update', 'status': 'READY'},
            {'name': 'account_checker', 'title': 'Account Checker', 'status': 'READY'},
            account_creation.module_status()
        ]
    }


@app.get('/api/config')
def config():
    settings = default_settings().__dict__
    saved_settings = paths.read_json_file('settings.json', {})
    if isinstance(saved_settings, dict):
        settings.update(saved_settings)
    return {
        'accounts': paths.read_text_file('accounts.txt'),
        'proxies': paths.read_text_file('proxies.html'),
        'rollids': paths.read_text_file('rollids.txt'),
        'settings': settings,
        'outputs': {
            'steamid32': paths.read_text_file('steamid32.txt'),
            'goodaccounts': paths.read_text_file('goodaccounts.txt')
        }
    }


@app.post('/api/config')
def save_config(request: profile_update_request):
    write_runtime_files(request)
    return {'saved': True}


@app.post('/api/profile-update/start')
def start_profile_update(request: profile_update_request):
    with state.lock:
        if state.status == 'running':
            raise HTTPException(status_code=409, detail='job already running')
        job_id = str(uuid.uuid4())
        stop_event = threading.Event()
        state.job_id = job_id
        state.active_stop_event = stop_event
        state.status = 'starting'

    write_runtime_files(request)
    settings = make_settings(request.settings)
    thread = threading.Thread(target=run_job, args=(job_id, settings, stop_event), daemon=True)
    with state.lock:
        state.active_thread = thread
    thread.start()
    return {'job_id': job_id}


@app.post('/api/profile-update/stop')
def stop_profile_update():
    with state.lock:
        if state.status != 'running' or state.active_stop_event is None:
            return {'stopping': False}
        state.active_stop_event.set()
    state.add_log('Stop requested')
    return {'stopping': True}


@app.get('/api/profile-update/status')
def profile_update_status():
    snapshot = state.snapshot()
    snapshot['outputs'] = {
        'steamid32': paths.read_text_file('steamid32.txt'),
        'goodaccounts': paths.read_text_file('goodaccounts.txt')
    }
    return snapshot


@app.post('/api/account-check/start')
def start_account_check(request: profile_update_request):
    with checker_state.lock:
        if checker_state.status == 'running':
            raise HTTPException(status_code=409, detail='job already running')
        job_id = str(uuid.uuid4())
        stop_event = threading.Event()
        checker_state.job_id = job_id
        checker_state.active_stop_event = stop_event
        checker_state.status = 'starting'

    write_runtime_files(request)
    settings = make_settings(request.settings)
    thread = threading.Thread(target=run_checker_job, args=(job_id, settings, stop_event), daemon=True)
    with checker_state.lock:
        checker_state.active_thread = thread
    thread.start()
    return {'job_id': job_id}


@app.post('/api/account-check/stop')
def stop_account_check():
    with checker_state.lock:
        if checker_state.status != 'running' or checker_state.active_stop_event is None:
            return {'stopping': False}
        checker_state.active_stop_event.set()
    checker_state.add_log('Stop requested')
    return {'stopping': True}


@app.get('/api/account-check/status')
def account_check_status():
    snapshot = checker_state.snapshot()
    snapshot['outputs'] = {
        'goodaccounts': paths.read_text_file('goodaccounts.txt'),
        'badaccounts': paths.read_text_file('badaccounts.txt')
    }
    return snapshot


def get_lan_addresses():
    addresses = set()
    hostname = socket.gethostname()
    try:
        for item in socket.getaddrinfo(hostname, None, socket.AF_INET):
            address = item[4][0]
            if not address.startswith('127.'):
                addresses.add(address)
    except OSError:
        pass
    return sorted(addresses)


def kill_process_on_port(port: int):
    import platform
    import subprocess
    
    system = platform.system()
    if system == 'Windows':
        try:
            result = subprocess.check_output(f'netstat -ano | findstr :{port}', shell=True, text=True)
            for line in result.splitlines():
                if f':{port}' in line and 'LISTENING' in line:
                    parts = line.strip().split()
                    if len(parts) >= 5:
                        pid = parts[-1]
                        subprocess.call(f'taskkill /F /PID {pid}', shell=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except Exception:
            pass
    else:
        try:
            subprocess.call(f'fuser -k {port}/tcp', shell=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except Exception:
            pass


def main(host='0.0.0.0', port=8765):
    import uvicorn

    print(f'Autoprofile WebUI: http://127.0.0.1:{port}')
    for address in get_lan_addresses():
        print(f'LAN: http://{address}:{port}')
        
    print(f'Ensuring port {port} is free...')
    kill_process_on_port(port)
    
    uvicorn.run('autoprofile.web.server:app', host=host, port=port, reload=False)


if __name__ == '__main__':
    main()
