
import contextlib
import importlib.util
import io
import threading
import time
from dataclasses import dataclass

from autoprofile.core import paths

@dataclass
class profile_update_settings:
    default_nickname: str = 'ZESTY JESUS'
    default_profile_summary: str = 'ZESTY JESUS'
    default_custom_url: str = ''
    default_profile_theme: str = 'Midnight'
    profile_image_path: str = 'image.jpg'
    enable_namechange: bool = True
    enable_avatarchange: bool = True
    enable_descriptionchange: bool = True
    enable_customurlchange: bool = True
    enable_themechange: bool = True
    enable_nameclear: bool = True
    enable_profile_verification: bool = True
    enable_gatherid32: bool = True
    make_commands: bool = True
    random_name: bool = False
    insert_random_chars_enabled: bool = True
    max_parallel_accounts: int = 8
    max_login_retries: int = 3
    login_timeout_seconds: int = 45
    request_timeout_seconds: int = 15
    profile_verification_attempts: int = 2
    profile_verification_retry_delay_seconds: int = 1
    profile_verification_timeout_seconds: int = 5
    loopupdateprofiles: bool = False
    use_rollids: bool = False
    loop_timeout: int = 0

@dataclass
class profile_update_result:
    success: bool
    duration_seconds: float
    error: str | None = None

def default_settings():
    return profile_update_settings()

def settings_from_dict(raw_settings):
    settings = default_settings()
    valid_keys = set(settings.__dict__)
    for key, value in raw_settings.items():
        if key not in valid_keys:
            continue
        current_value = getattr(settings, key)
        if isinstance(current_value, bool):
            if isinstance(value, str):
                setattr(settings, key, value.lower() in ('1', 'true', 'yes', 'on'))
            else:
                setattr(settings, key, bool(value))
        elif isinstance(current_value, int):
            setattr(settings, key, int(value))
        else:
            setattr(settings, key, str(value))
    settings.max_parallel_accounts = max(1, min(64, settings.max_parallel_accounts))
    settings.max_login_retries = max(1, min(10, settings.max_login_retries))
    settings.login_timeout_seconds = max(5, min(300, settings.login_timeout_seconds))
    settings.request_timeout_seconds = max(5, min(120, settings.request_timeout_seconds))
    settings.profile_verification_attempts = max(1, min(5, settings.profile_verification_attempts))
    settings.profile_verification_retry_delay_seconds = max(0, min(10, settings.profile_verification_retry_delay_seconds))
    settings.profile_verification_timeout_seconds = max(2, min(30, settings.profile_verification_timeout_seconds))
    settings.loop_timeout = max(0, settings.loop_timeout)
    return settings

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Make sure you install dependencies first:
# pip3 install -U steam[client]
# If it doesnt work try running on windows and dont bother with installing dependencies on linux

# NOTE: Avatar changing requires valid Steam authentication cookies
# The steam library's WebAuth no longer provides these due to Steam API changes (October 2025)
# 
# WORKING SOLUTIONS:
# 1. Manual: Log into Steam in your browser, export cookies, and add them to cookies.txt
# 2. Selenium: Use browser automation to log in and get real cookies
# 3. Wait for steam library update to support new Steam auth flow
#
# For now, avatar changing is DISABLED by default below.

import json
import time
import random
import logging
import os
import re
import string
import sys
import io
import threading
import unicodedata
from html import escape, unescape
from html.parser import HTMLParser
from pathlib import Path
from concurrent.futures import FIRST_COMPLETED, ThreadPoolExecutor, wait
from urllib.parse import urlparse

try:
    import requests
except ImportError as exc:
    requests = None
    requests_import_error = exc
else:
    requests_import_error = None

try:
    from gevent import Timeout as GeventTimeout
except ImportError:  # Fallback if gevent is unavailable
    GeventTimeout = None

# Fix Windows console encoding
if sys.platform == 'win32':
    try:
        sys.stdout.reconfigure(encoding='utf-8')
        sys.stderr.reconfigure(encoding='utf-8')
    except:
        pass

try:
    import steam.client
    from steam import webapi
    from steam.enums import EResult
except ImportError as exc:
    steam = None
    webapi = None
    EResult = None
    steam_import_error = exc
else:
    steam_import_error = None

from autoprofile.modules.profile_scraper import ProfileScraper, ScrapedProfile

script_dir = Path(__file__).resolve().parent
stop_event = threading.Event()
profile_scraper_instance = None



def resolve_existing_path(path_value, extra_dirs=()):
    path = Path(path_value)
    if path.is_absolute():
        return path

    candidates = [Path.cwd() / path, script_dir / path]
    current_data_dir = globals().get('data_dir')
    if current_data_dir is not None:
        candidates.insert(1, current_data_dir / path)
    candidates.extend(script_dir / extra_dir / path for extra_dir in extra_dirs)
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return candidates[0]


def resolve_output_path(path_value):
    path = Path(path_value)
    if path.is_absolute():
        return path
    return data_dir / path


def read_lines_file(path_value, encoding='utf-8'):
    path = resolve_existing_path(path_value)
    try:
        data = path.read_text(encoding=encoding)
    except FileNotFoundError:
        return []
    data = data.replace('\r\n', '\n')
    return [line.strip() for line in data.split('\n') if line.strip()]


accounts_path = resolve_existing_path('accounts.txt')
accounts = read_lines_file(accounts_path)
data_dir = accounts_path.parent if accounts_path.exists() else script_dir

proxies = []
proxies_path = resolve_existing_path('proxies.html')
try:
    proxy_data = proxies_path.read_text(encoding='utf-8').replace('\r\n', '\n')
    proxies = [proxy.strip() for proxy in proxy_data.split('\n') if proxy.strip()]
except FileNotFoundError:
    pass

profile_image_path = 'image.jpg'
default_nickname = 'ZESTY JESUS'

enable_debugging = False # Debug info toggle.
enable_extra_info = False # Yap info toggle.
enable_avatarchange = True # Avatar upload via automated browser login (requires Playwright)
enable_namechange = True # Toggle for changing the nicks.
enable_descriptionchange = True
enable_customurlchange = True
enable_themechange = True
enable_profile_verification = True
enable_nameclear = False # Clears the prev nick list.  This does not work.
enable_set_up = False # Sets up the profile. Useless.
enable_gatherid32 = True # Collects steam32 ids of the accounts.
dump_response = False # I can only guess what this does.
make_commands = True # Changes steamids to cat_pl_add_id *id* CAT commands.
force_sleep = False # Too lazy to know what this does!
max_login_retries = 3 # Max login retries per account
random_name = False  # Toggle this to generate random account names
insert_random_chars_enabled = True  # Toggle this to insert random characters into the nickname
random_name_length = 32  # Length of the random account name
num_insertions = 3  # Number of random characters to insert into nickname
random_chars = [ '็', '่', '๊', '๋', '์', 'ู']  # Modify this list as you wish currently holds random semi-invis symbols for tf2
loopupdateprofiles = True  # Toggle this to loop profile updates
update_interval = 120  # Time to wait between updates in seconds
enable_parallel_updates = True  # Process multiple accounts concurrently.
max_parallel_accounts = 8  # Number of accounts to process at once when parallel updates enabled.
cookies_cache_file = 'cookies.json'  # Where to persist session cookies per account.
cookie_cache_ttl_hours = 9999  # Force refresh cookies after this many hours.
login_timeout_seconds = 45  # Abort Steam login attempts that take too long.
cookie_cache_only_mode = False  # When True, only refresh cookies; skip all profile changes.
request_timeout_seconds = 15
nickname_update_delay_seconds = 0
max_parallel_browser_logins = 2
default_profile_summary = 'ZESTY JESUS'
default_custom_url = ''
default_profile_theme = 'Midnight'
profile_verification_attempts = 2
profile_verification_retry_delay_seconds = 1
profile_verification_timeout_seconds = 5

random_name_pool = []
steam_id_names = {}
browser_login_semaphore = threading.Semaphore(max(1, max_parallel_browser_logins))

try:
    names_path = resolve_existing_path('names.txt')
    with names_path.open('r', encoding='utf-8') as names_file:
        for line in names_file:
            line = line.strip()
            if not line:
                continue
            
            # Try to parse as SteamID64,Name (SteamID64 is 17 digits)
            parts = line.split(',', 1)
            if len(parts) == 2 and parts[0].isdigit() and len(parts[0]) == 17:
                s_id = parts[0].strip()
                name = parts[1].strip()
                steam_id_names[s_id] = name
                # We also add it to the pool so it can be used randomly if needed, 
                # or maybe we shouldn't? 
                # The user said "remake randomname", implying it might still be random if ID not found.
                # But if ID IS found, we use specific.
                # If we add to pool, it might be picked for OTHER accounts.
                # Assuming unique names per account usually, but maybe not.
                # Let's add to pool for fallback entropy, or keep separate?
                # If the file is strictly mapping, maybe we shouldn't add to random pool?
                # But if the file is mixed...
                # Let's add it to pool to be safe with "random" feature name.
                random_name_pool.append(name)
            else:
                # Fallback to comma-separated list support
                names = [n.strip() for n in line.split(',') if n.strip()]
                random_name_pool.extend(names)

except FileNotFoundError:
    pass

cookie_cache_lock = threading.Lock()
cookie_cache = {}
cookie_cache_loaded = False
print_lock = threading.Lock()

web_api_key = os.getenv('STEAM_WEB_API_KEY')
if web_api_key and webapi is not None:
    webapi.DEFAULT_PARAMS['key'] = web_api_key

if enable_debugging and steam is not None:
    logger = steam.client.SteamClient._LOG
    if not any(isinstance(handler, logging.StreamHandler) for handler in logger.handlers):
        stream_handler = logging.StreamHandler()
        stream_handler.setLevel(logging.DEBUG)
        logger.addHandler(stream_handler)
    logger.setLevel(logging.DEBUG)



# --- REFACTORED run_profile_update ---
def run_profile_update(settings, log_callback, stop_event_passed):
    global default_nickname, default_profile_summary, default_custom_url, default_profile_theme
    global profile_image_path, enable_namechange, enable_avatarchange, enable_descriptionchange
    global enable_customurlchange, enable_themechange, enable_nameclear, enable_profile_verification, enable_gatherid32
    global make_commands, random_name, insert_random_chars_enabled, max_parallel_accounts
    global max_login_retries, login_timeout_seconds, request_timeout_seconds, profile_verification_attempts
    global profile_verification_retry_delay_seconds, profile_verification_timeout_seconds, loopupdateprofiles
    global stop_event, safe_print_callback, data_dir, accounts, proxies, cookie_cache_loaded
    global use_rollids, profile_scraper_instance, loop_timeout

    default_nickname = settings.default_nickname
    default_profile_summary = settings.default_profile_summary
    default_custom_url = settings.default_custom_url
    default_profile_theme = settings.default_profile_theme
    profile_image_path = settings.profile_image_path
    if steam_import_error is not None:
        return profile_update_result([], 0, f'Missing Python dependency: steam[client] ({steam_import_error})')

    enable_namechange = settings.enable_namechange
    enable_avatarchange = settings.enable_avatarchange
    enable_descriptionchange = settings.enable_descriptionchange
    enable_customurlchange = settings.enable_customurlchange
    enable_themechange = settings.enable_themechange
    enable_nameclear = settings.enable_nameclear
    enable_profile_verification = settings.enable_profile_verification
    enable_gatherid32 = settings.enable_gatherid32
    make_commands = settings.make_commands
    random_name = settings.random_name
    insert_random_chars_enabled = settings.insert_random_chars_enabled
    max_parallel_accounts = settings.max_parallel_accounts
    max_login_retries = settings.max_login_retries
    login_timeout_seconds = settings.login_timeout_seconds
    request_timeout_seconds = settings.request_timeout_seconds
    profile_verification_attempts = settings.profile_verification_attempts
    profile_verification_retry_delay_seconds = settings.profile_verification_retry_delay_seconds
    profile_verification_timeout_seconds = settings.profile_verification_timeout_seconds
    loopupdateprofiles = settings.loopupdateprofiles
    loop_timeout = getattr(settings, 'loop_timeout', 0)
    use_rollids = getattr(settings, 'use_rollids', False)
    
    stop_event = stop_event_passed
    safe_print_callback = log_callback
    
    if use_rollids:
        try:
            profile_scraper_instance = ProfileScraper()
            safe_print_callback(f'Loaded {len(profile_scraper_instance.usage_counts)} unique SteamIDs from rollids.txt')
        except Exception as e:
            safe_print_callback(f'Failed to initialize profile scraper: {e}')
            profile_scraper_instance = None
    else:
        profile_scraper_instance = None
    
    data_dir = paths.data_dir
    accounts = paths.read_lines_file('accounts.txt')
    
    try:
        proxy_data = paths.read_text_file('proxies.html').replace('\\r\\n', '\\n')
        proxies[:] = [proxy.strip() for proxy in proxy_data.split('\\n') if proxy.strip()]
    except Exception:
        proxies[:] = []

    cookie_cache_loaded = False
    
    started_at = time.perf_counter()
    try:
        while True:
            update_profiles()
            if stop_event.is_set():
                return profile_update_result(False, time.perf_counter() - started_at, 'stopped')
            
            if not loopupdateprofiles:
                break
                
            if loop_timeout > 0:
                safe_print_callback(f'Sleeping for {loop_timeout} seconds before next iteration...')
                if stop_event.wait(loop_timeout):
                    return profile_update_result(False, time.perf_counter() - started_at, 'stopped')

        return profile_update_result(True, time.perf_counter() - started_at)
    except Exception as exc:
        log_callback(f'Job failed: {exc}')
        return profile_update_result(False, time.perf_counter() - started_at, str(exc))

def safe_print(*args, **kwargs):
    if 'safe_print_callback' in globals() and safe_print_callback:
        text = ' '.join(map(str, args))
        safe_print_callback(text)
    else:
        with print_lock:
            print(*args, **kwargs)



def debug(message):
    if enable_debugging:
        safe_print(message)


def extra(message):
    if enable_extra_info:
        safe_print(message)


def wait_or_stop(seconds):
    if seconds is None or seconds <= 0:
        return stop_event.is_set()
    return stop_event.wait(seconds)


def write_text_atomic(path_value, text):
    path = resolve_output_path(path_value)
    path.parent.mkdir(parents=True, exist_ok=True)
    temp_path = path.with_name(f'{path.name}.tmp')
    temp_path.write_text(text, encoding='utf-8')
    os.replace(temp_path, path)


class FormFieldParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.fields = {}
        self.textarea_name = None
        self.textarea_value = []

    def handle_starttag(self, tag, attrs):
        attrs_dict = dict(attrs)
        if tag == 'input':
            name = attrs_dict.get('name')
            if name:
                self.fields[name] = attrs_dict.get('value', '')
        elif tag == 'textarea':
            name = attrs_dict.get('name')
            if name:
                self.textarea_name = name
                self.textarea_value = []

    def handle_data(self, data):
        if self.textarea_name:
            self.textarea_value.append(data)

    def handle_endtag(self, tag):
        if tag == 'textarea' and self.textarea_name:
            self.fields[self.textarea_name] = ''.join(self.textarea_value)
            self.textarea_name = None
            self.textarea_value = []


def parse_form_fields(html_text):
    parser = FormFieldParser()
    parser.feed(html_text)
    return parser.fields


def get_session_cookie(session, name):
    return session.cookies.get(name, domain='steamcommunity.com') or session.cookies.get(name)


def normalize_profile_text(text):
    normalized = unicodedata.normalize('NFKD', unescape(text))
    return ''.join(char for char in normalized if unicodedata.category(char) not in ('Mn', 'Cf'))


def profile_text_matches(actual, expected):
    return normalize_profile_text(str(actual or '')).strip() == normalize_profile_text(str(expected or '')).strip()


def sanitize_custom_url(value):
    value = ''.join(char for char in value if char.isalnum() or char in ('_', '-'))
    return value[:32]


def get_custom_url_for_account(username):
    if default_custom_url:
        return sanitize_custom_url(default_custom_url)
    return sanitize_custom_url(username)


def set_duration(result, started_at):
    result['duration_seconds'] = time.perf_counter() - started_at
    return result


def checks_are_successful(checks):
    for key in ('name_change_request', 'avatar_upload_request', 'description_change_request', 'custom_url_change_request', 'theme_change_request'):
        if checks.get(key) is False:
            return False

    verification = checks.get('profile_verification')
    if isinstance(verification, dict):
        if not verification.get('profile_loaded'):
            return False
        for key in ('name_ok', 'summary_ok'):
            value = verification.get(key)
            if value is False:
                return False

    edit_verification = checks.get('edit_state_verification')
    if isinstance(edit_verification, dict):
        if edit_verification.get('error'):
            return False
        for key in ('summary_ok', 'theme_ok'):
            value = edit_verification.get(key)
            if value is False:
                return False
        if edit_verification.get('custom_url_ok') is False and checks.get('custom_url_change_request') is not False:
            return False
    return True


def get_cookies_via_browser(username, password):
    """
    Automatically log into Steam via browser and extract cookies
    Returns: dict of cookies or None if failed
    """
    if stop_event.is_set():
        return None

    try:
        from playwright.sync_api import sync_playwright

        safe_print('🌐 Opening browser to get Steam cookies...')
        safe_print('   This will automatically log in and extract authentication cookies.')

        with sync_playwright() as p:
            # Launch browser (headless=False so you can see it working)
            browser = p.chromium.launch(headless=True)
            context = browser.new_context()
            page = context.new_page()

            # Go to Steam login page
            safe_print('   Navigating to Steam login...')
            page.goto('https://steamcommunity.com/login/home/', wait_until='domcontentloaded', timeout=30000)
            page.wait_for_selector('input[type="text"]', timeout=30000)

            # Fill in username
            safe_print(f'   Entering username: {username}')
            page.fill('input[type="text"]', username)

            # Fill in password
            safe_print('   Entering password...')
            page.fill('input[type="password"]', password)

            # Click sign in button
            safe_print('   Clicking sign in...')
            page.click('button[type="submit"]')

            # Wait for potential 2FA or login to complete
            safe_print('   ⏳ Waiting for login to complete...')
            safe_print('   ⚠️  If 2FA is required, please complete it in the browser window!')

            try:
                # Wait for redirect to profile page (Steam always redirects to /profiles/[steamid64])
                page.wait_for_url('**/profiles/**', timeout=60000)  # 60 seconds for 2FA
                current_url = page.url
                safe_print(f'   ✓ Login successful! Redirected to: {current_url}')
            except Exception:
                safe_print('   ⚠️  Could not detect login completion (timeout or no redirect)')
                safe_print('   Waiting additional 5 seconds...')
                page.wait_for_timeout(5000)

            # Make sure we're on steamcommunity to get all cookies
            if 'login' in page.url.lower():
                safe_print('   Still on login page, navigating to community...')
                page.goto('https://steamcommunity.com/', wait_until='domcontentloaded', timeout=30000)

            # Extract cookies
            safe_print('   📦 Extracting cookies...')
            cookies = context.cookies()

            # Build cookie dict
            cookie_dict = {}
            for cookie in cookies:
                if cookie['domain'] in ['.steamcommunity.com', 'steamcommunity.com']:
                    if cookie['name'] in ['sessionid', 'steamLogin', 'steamLoginSecure', 'steamCountry']:
                        cookie_dict[cookie['name']] = cookie['value']
                        safe_print(f'   ✓ Got {cookie["name"]}')

            browser.close()

            if 'sessionid' in cookie_dict and 'steamLoginSecure' in cookie_dict:
                safe_print('   ✅ Successfully extracted all required cookies!')
                return cookie_dict
            else:
                safe_print('   ❌ Missing required cookies. Login may have failed.')
                return None

    except ImportError:
        safe_print(f'Playwright is not installed for this Python: {sys.executable}')
        safe_print('Install with autoprofile/install.sh or autoprofile/install.bat, then start the UI with startweb.')
        safe_print('Manual fix: python -m pip install playwright && python -m playwright install chromium')
        return None
    except Exception as e:
        safe_print(f'❌ Browser automation failed: {e}')
        import traceback
        traceback.print_exc()
        return None


def load_cookie_cache():
    global cookie_cache_loaded, cookie_cache
    if cookie_cache_loaded:
        return

    with cookie_cache_lock:
        if cookie_cache_loaded:
            return
        try:
            cookies_path = resolve_existing_path(cookies_cache_file)
            with cookies_path.open('r', encoding='utf-8') as cache_file:
                cache_data = json.load(cache_file)
                if isinstance(cache_data, dict):
                    cookie_cache.update(cache_data)
                else:
                    debug(f'Cookie cache malformed in {cookies_cache_file}; ignoring contents.')
        except FileNotFoundError:
            debug(f'Cookie cache file {cookies_cache_file} not found; a new one will be created.')
        except json.JSONDecodeError as exc:
            debug(f'Failed to decode {cookies_cache_file}: {exc}; starting with empty cache.')
        cookie_cache_loaded = True


def persist_cookie_cache():
    try:
        with cookie_cache_lock:
            cache_snapshot = dict(cookie_cache)
        path = resolve_existing_path(cookies_cache_file)
        if not path.exists():
            path = resolve_output_path(cookies_cache_file)
        path.parent.mkdir(parents=True, exist_ok=True)
        temp_path = path.with_name(f'{path.name}.tmp')
        with temp_path.open('w', encoding='utf-8') as cache_file:
            json.dump(cache_snapshot, cache_file, separators=(',', ':'))
        os.replace(temp_path, path)
    except OSError as exc:
        debug(f'Unable to write cookie cache to {cookies_cache_file}: {exc}')


def get_saved_cookies(username):
    load_cookie_cache()
    with cookie_cache_lock:
        entry = cookie_cache.get(username)
    if not entry:
        return None

    timestamp = entry.get('timestamp')
    try:
        timestamp = float(timestamp)
    except (TypeError, ValueError):
        timestamp = None

    if timestamp is not None:
        max_age_seconds = cookie_cache_ttl_hours * 3600
        if max_age_seconds > 0 and time.time() - timestamp > max_age_seconds:
            debug(f'Cached cookies for {username} expired; will refresh.')
            return None

    cookies = entry.get('cookies')
    if isinstance(cookies, dict) and cookies:
        return cookies
    return None


def store_cookies(username, cookie_dict, persist=True):
    if not username or not cookie_dict:
        return
    load_cookie_cache()
    with cookie_cache_lock:
        cookie_cache[username] = {
            'cookies': cookie_dict,
            'timestamp': time.time()
        }
    if persist:
        persist_cookie_cache()


def apply_cookies_to_session(session, cookies):
    for name, value in cookies.items():
        for domain in ('.steamcommunity.com', 'steamcommunity.com'):
            cookie = requests.cookies.create_cookie(domain=domain, name=name, value=value)
            session.cookies.set_cookie(cookie)


def validate_session_cookies(session):
    try:
        response = session.get('https://steamcommunity.com/my/edit', timeout=request_timeout_seconds, allow_redirects=True)
    except requests.RequestException as exc:
        debug(f'Cookie validation request failed: {exc}')
        return False

    final_url = response.url.lower()
    if 'login' in final_url:
        debug('Session validation redirected to login; cookies invalid.')
        return False

    if response.status_code not in (200, 302):
        debug(f'Unexpected status during cookie validation: {response.status_code}')
        return False

    debug('Saved cookies validated successfully.')
    return True


def extract_json_attr(html_text, attr_name):
    match = re.search(fr'{re.escape(attr_name)}="([^"]+)"', html_text)
    if not match:
        return None
    return json.loads(unescape(match.group(1)))


def get_profile_edit_state(session):
    try:
        response = session.get('https://steamcommunity.com/my/edit/info', timeout=request_timeout_seconds, allow_redirects=True)
    except requests.RequestException as exc:
        return None, None, f'edit page request failed: {exc}'

    if response.status_code != 200 or 'login' in response.url.lower():
        return None, None, f'edit page unavailable: status {response.status_code}'

    profile_data = extract_json_attr(response.text, 'data-profile-edit')
    loyalty_data = extract_json_attr(response.text, 'data-loyaltystore')
    if not isinstance(profile_data, dict):
        return None, None, 'profile edit state missing'
    return profile_data, loyalty_data if isinstance(loyalty_data, dict) else {}, None


def save_profile_info(session, summary=None, custom_url=None, nickname=None):
    profile_data, _, error = get_profile_edit_state(session)
    if error:
        return False, error

    location_data = profile_data.get('LocationData')
    if not isinstance(location_data, dict):
        location_data = {}

    fields = {
        'sessionID': get_session_cookie(session, 'sessionid'),
        'personaName': profile_data.get('strPersonaName', ''),
        'real_name': profile_data.get('strRealName', ''),
        'summary': profile_data.get('strSummary', ''),
        'country': location_data.get('locCountryCode', ''),
        'state': location_data.get('locStateCode', ''),
        'city': location_data.get('locCityCode', ''),
        'customURL': profile_data.get('strCustomURL', ''),
        'type': 'profileSave',
        'weblink_1_title': '',
        'weblink_1_url': '',
        'weblink_2_title': '',
        'weblink_2_url': '',
        'weblink_3_title': '',
        'weblink_3_url': '',
        'json': '1'
    }
    if summary is not None:
        fields['summary'] = summary
    if custom_url is not None:
        fields['customURL'] = custom_url
    if nickname is not None:
        fields['personaName'] = nickname

    if not fields['sessionID']:
        return False, 'missing sessionid'

    try:
        save_response = session.post('https://steamcommunity.com/my/edit', data=fields, timeout=request_timeout_seconds, allow_redirects=True)
    except requests.RequestException as exc:
        return False, f'profile save failed: {exc}'

    if save_response.status_code not in (200, 302):
        return False, f'profile save returned status {save_response.status_code}'

    if 'login' in save_response.url.lower():
        return False, 'profile save redirected to login'

    try:
        save_data = save_response.json()
    except ValueError:
        save_data = None

    if isinstance(save_data, dict) and save_data.get('success') not in (1, True, '1'):
        return False, save_data.get('errmsg') or save_data.get('message') or 'profile save request was rejected'

    saved_profile_data, _, verify_error = get_profile_edit_state(session)
    if verify_error:
        return False, f'profile save verify failed: {verify_error}'

    mismatches = []
    if summary is not None and not profile_text_matches(saved_profile_data.get('strSummary'), summary):
        mismatches.append('description')
    if nickname is not None and not profile_text_matches(saved_profile_data.get('strPersonaName'), nickname):
        mismatches.append('nickname')
    if custom_url is not None and saved_profile_data.get('strCustomURL') != custom_url:
        current_custom_url = saved_profile_data.get('strCustomURL') or ''
        mismatches.append(f'custom URL persisted as "{current_custom_url}"')

    if mismatches:
        return False, 'Steam did not persist ' + ', '.join(mismatches)

    return True, None


def set_profile_theme(session, theme_id):
    profile_data, loyalty_data, error = get_profile_edit_state(session)
    if error:
        return False, error

    available_themes = profile_data.get('rgAvailableThemes', [])
    available_ids = {item.get('theme_id') for item in available_themes if isinstance(item, dict)}
    if theme_id not in available_ids:
        return False, f'theme unavailable: {theme_id}'

    token = loyalty_data.get('webapi_token')
    if not token:
        return False, 'missing webapi token'

    try:
        response = session.post(
            'https://api.steampowered.com/IPlayerService/SetProfileTheme/v1/',
            data={'access_token': token, 'input_json': json.dumps({'theme_id': theme_id})},
            timeout=request_timeout_seconds
        )
    except requests.RequestException as exc:
        return False, f'theme save failed: {exc}'

    if response.status_code != 200:
        return False, f'theme save returned status {response.status_code}'

    return True, None


def verify_edit_state(session, summary=None, custom_url=None, theme_id=None):
    profile_data, _, error = get_profile_edit_state(session)
    verification = {
        'summary_ok': None,
        'custom_url_ok': None,
        'theme_ok': None
    }
    if error:
        verification['error'] = error
        return verification

    if summary is not None:
        verification['summary_ok'] = profile_text_matches(profile_data.get('strSummary'), summary)

    if custom_url is not None:
        verification['custom_url_ok'] = profile_data.get('strCustomURL') == custom_url

    if theme_id is not None:
        active_theme = profile_data.get('ActiveTheme') or {}
        verification['theme_ok'] = active_theme.get('theme_id') == theme_id

    return verification


def verify_profile_page(session, profile_url, nickname=None, summary=None):
    verification = {
        'profile_loaded': False,
        'name_ok': None,
        'summary_ok': None,
        'avatar_seen': None
    }

    expected_name = normalize_profile_text(nickname) if nickname is not None else None
    expected_summary = normalize_profile_text(summary) if summary is not None else None

    for attempt in range(max(1, profile_verification_attempts)):
        try:
            response = session.get(profile_url, timeout=min(request_timeout_seconds, profile_verification_timeout_seconds), allow_redirects=True)
        except requests.RequestException as exc:
            verification['error'] = str(exc)
            if attempt < profile_verification_attempts - 1 and not wait_or_stop(profile_verification_retry_delay_seconds):
                continue
            return verification

        verification['status_code'] = response.status_code
        if response.status_code != 200:
            if attempt < profile_verification_attempts - 1 and not wait_or_stop(profile_verification_retry_delay_seconds):
                continue
            return verification

        text = response.text
        normalized_text = normalize_profile_text(text)
        verification['profile_loaded'] = True

        if nickname is not None:
            verification['name_ok'] = (
                escape(nickname, quote=False) in text
                or nickname in text
                or expected_name in normalized_text
            )

        if summary is not None:
            verification['summary_ok'] = (
                escape(summary, quote=False) in text
                or summary in text
                or expected_summary in normalized_text
            )

        verification['avatar_seen'] = 'playerAvatarAutoSizeInner' in text or 'avatarfull' in text
        verification['attempts'] = attempt + 1

        checks_done = (
            (nickname is None or verification['name_ok'])
            and (summary is None or verification['summary_ok'])
            and verification['avatar_seen']
        )
        if checks_done:
            return verification

        if attempt < profile_verification_attempts - 1 and wait_or_stop(profile_verification_retry_delay_seconds):
            return verification

    return verification


def login_with_timeout(client, username, password, timeout_seconds):
    if timeout_seconds is None or timeout_seconds <= 0 or GeventTimeout is None:
        return client.login(username, password=password)

    timeout = GeventTimeout(timeout_seconds)
    timeout.start()
    try:
        return client.login(username, password=password)
    except GeventTimeout:
        debug(f'Login timed out after {timeout_seconds}s for {username}; disconnecting client.')
        try:
            client.disconnect()
        except Exception as exc:
            debug(f'Error during forced disconnect: {exc}')
        return EResult.Timeout
    finally:
        timeout.cancel()


def process_account_task(index, account_entry, avatar_payload, avatar_allowed, cached_cookies):
    started_at = time.perf_counter()
    result = {
        'account': account_entry,
        'success': False,
        'steamid32_entry': None,
        'index': index,
        'username': None,
        'new_cookies': None,
        'duration_seconds': None,
        'checks': {}
    }

    if stop_event.is_set():
        result['error'] = 'stopped'
        return set_duration(result, started_at)

    try:
        username, password = account_entry.split(':', 1)
    except ValueError:
        safe_print(f'[#{index + 1}] Invalid account entry.')
        result['error'] = 'invalid_format'
        return set_duration(result, started_at)

    result['username'] = username
    safe_print(f'[#{index + 1}] Processing {username}...')

    if cookie_cache_only_mode:
        session, new_cookies = get_authenticated_session(
            None,
            username=username,
            password=password,
            proxy_dict=None,
            cached_cookies=cached_cookies
        )

        if session is not None:
            safe_print(f'[#{index + 1}] Cookie session validated for {username}.')
            if new_cookies:
                result['new_cookies'] = new_cookies
            result['success'] = True
        else:
            safe_print(f'[#{index + 1}] Failed to refresh cookies for {username}.')
            result['error'] = 'cookie_refresh_failed'

        set_duration(result, started_at)
        safe_print(f'[#{index + 1}] Done.')
        return result

    safe_print(f'[#{index + 1}] Logging in as {username}...')

    login_successful = False
    client = None
    last_proxy_dict = None

    for retry in range(max_login_retries):
        client = steam.client.SteamClient()
        try:
            eresult = login_with_timeout(client, username, password, login_timeout_seconds)
        except Exception as exc:
            safe_print(f'[#{index + 1}] Login encountered an error: {exc}')
            debug(f'Login exception for {username}: {exc}')
            eresult = EResult.Fail

        try:
            eresult_enum = EResult(eresult)
        except (TypeError, ValueError):
            eresult_enum = EResult.Fail

        status = 'OK' if eresult_enum == EResult.OK else 'FAIL'
        safe_print(f'[#{index + 1}] Login status: {status} - {eresult_enum.name}')

        if eresult_enum == EResult.OK:
            login_successful = True
            break

        if retry < max_login_retries - 1:
            safe_print(f'[#{index + 1}] Retrying login... (attempt {retry + 2}/{max_login_retries})')
            if wait_or_stop(2):
                result['error'] = 'stopped'
                break

        try:
            client.disconnect()
        except Exception:
            pass

    if not login_successful or client is None:
        safe_print(f'[#{index + 1}] Skipping account {username}.')
        result['error'] = 'login_failed'
        return set_duration(result, started_at)

    safe_print(f'[#{index + 1}] Logged in as: {client.user.name}')
    safe_print(f'[#{index + 1}] Community profile: {client.steam_id.community_url}')
    extra(f'[#{index + 1}] Last logon (UTC): {client.user.last_logon}')
    extra(f'[#{index + 1}] Last logoff (UTC): {client.user.last_logoff}')

    if enable_gatherid32:
        id32 = str(client.steam_id.as_32)
        if make_commands:
            result['steamid32_entry'] = f'cat_ignore {id32} CAT\n'
            safe_print(f'[#{index + 1}] Saved the SteamID32 as a cat_ignore command.')
        else:
            result['steamid32_entry'] = f'{id32}\n'
            safe_print(f'[#{index + 1}] Saved the SteamID32 as raw.')

    scraped_profile = None
    if use_rollids and profile_scraper_instance:
        safe_print(f'[#{index + 1}] Scraping random profile from rollids...')
        scraped_profile = profile_scraper_instance.scrape_random_profile()
        if scraped_profile:
            safe_print(f'[#{index + 1}] Scraped profile {scraped_profile.steamid64} ("{scraped_profile.persona_name}")')
        else:
            safe_print(f'[#{index + 1}] Failed to scrape a profile from rollids.')

    if enable_namechange:
        if scraped_profile and scraped_profile.persona_name:
            nickname = scraped_profile.persona_name
        elif random_name:
            nickname = get_random_name_from_file(client.steam_id.as_64)
        else:
            nickname = default_nickname
        if insert_random_chars_enabled:
            nickname = insert_random_chars(nickname, random_chars, num_insertions)
        if wait_or_stop(nickname_update_delay_seconds):
            result['error'] = 'stopped'
        else:
            try:
                client.change_status(persona_state=1, player_name=nickname)
                safe_print(f'[#{index + 1}] Changed Steam nickname to "{nickname}"')
                result['checks']['name_change_request'] = True
            except Exception as exc:
                safe_print(f'[#{index + 1}] Failed to change nickname: {exc}')
                result['checks']['name_change_request'] = False
    else:
        nickname = None

    if scraped_profile and scraped_profile.avatar_bytes:
        avatar_payload = scraped_profile.avatar_bytes
        avatar_allowed = True

    profile_summary = default_profile_summary
    if scraped_profile and scraped_profile.summary:
        profile_summary = scraped_profile.summary
        
    profile_theme = default_profile_theme
    if scraped_profile and scraped_profile.theme_id:
        profile_theme = scraped_profile.theme_id

    profile_setup_successful = True

    if scraped_profile and scraped_profile.custom_url:
        custom_url = scraped_profile.custom_url
    else:
        custom_url = get_custom_url_for_account(username) if enable_customurlchange else None

    if enable_avatarchange or enable_nameclear or enable_set_up or enable_descriptionchange or enable_customurlchange or enable_themechange or enable_profile_verification:
        last_proxy_dict, _, _ = pick_proxy_for_account(set())
        safe_print(f'[#{index + 1}] Getting web_session...')
        session, new_cookies = get_authenticated_session(
            client,
            username=username,
            password=password,
            proxy_dict=last_proxy_dict,
            cached_cookies=cached_cookies
        )
        if session is not None:
            if new_cookies:
                result['new_cookies'] = new_cookies

            if enable_avatarchange and avatar_allowed and avatar_payload:
                url = 'https://steamcommunity.com/actions/FileUploader'
                id64 = client.steam_id.as_64

                data = {
                    'MAX_FILE_SIZE': '1048576',
                    'type': 'player_avatar_image',
                    'sId': str(id64),
                    'sessionid': get_session_cookie(session, 'sessionid'),
                    'doSub': '1',
                }

                safe_print(f'[#{index + 1}] Setting profile picture...')
                try:
                    response = session.post(
                        url=url,
                        params={'type': 'player_avatar_image', 'sId': str(id64)},
                        files={'avatar': ('avatar.jpg', io.BytesIO(avatar_payload), 'image/jpeg')},
                        data=data,
                        timeout=30
                    )
                except requests.RequestException as exc:
                    safe_print(f'[#{index + 1}] ⚠ Avatar upload request failed: {exc}')
                    result['checks']['avatar_upload_request'] = False
                else:
                    debug(f'Avatar upload status code: {response.status_code}')
                    try:
                        content = response.content.decode('utf-8', errors='replace')
                    except Exception:
                        content = str(response.content)

                    if dump_response:
                        safe_print(f'[#{index + 1}] response: {content[:500]}')

                    if 'Error' in content or 'error' in content.lower():
                        safe_print(f'[#{index + 1}] ⚠ Avatar upload error: {content.strip()}')
                    elif content.strip().startswith('{'):
                        try:
                            data_json = json.loads(content)
                        except json.JSONDecodeError:
                            if response.status_code == 200:
                                safe_print(f'[#{index + 1}] ✓ Profile picture uploaded (status {response.status_code})')
                                result['checks']['avatar_upload_request'] = True
                            else:
                                safe_print(f'[#{index + 1}] ⚠ Avatar upload returned status {response.status_code}')
                                result['checks']['avatar_upload_request'] = False
                        else:
                            message = data_json.get('message')
                            if data_json.get('success'):
                                safe_print(f'[#{index + 1}] ✓ Profile picture set successfully!')
                                result['checks']['avatar_upload_request'] = True
                            elif message:
                                safe_print(f'[#{index + 1}] ⚠ Avatar upload response: {message}')
                                result['checks']['avatar_upload_request'] = False
                            else:
                                safe_print(f'[#{index + 1}] ⚠ Unexpected avatar upload response: {data_json}')
                                result['checks']['avatar_upload_request'] = False
                    elif response.status_code == 200:
                        safe_print(f'[#{index + 1}] ✓ Profile picture uploaded successfully!')
                        result['checks']['avatar_upload_request'] = True
                    else:
                        safe_print(f'[#{index + 1}] ⚠ Avatar upload returned status {response.status_code}')
                        result['checks']['avatar_upload_request'] = False

            if enable_descriptionchange or (enable_namechange and nickname):
                safe_print(f'[#{index + 1}] Setting profile info (desc/name)...')
                summary_success, summary_error = save_profile_info(session, summary=profile_summary if enable_descriptionchange else None, nickname=nickname if enable_namechange else None)
                result['checks']['description_change_request'] = summary_success
                if summary_success:
                    safe_print(f'[#{index + 1}] Profile info saved.')
                else:
                    safe_print(f'[#{index + 1}] Failed to save profile info: {summary_error}')
                    profile_setup_successful = False

            if enable_customurlchange and custom_url:
                safe_print(f'[#{index + 1}] Setting custom Steam URL...')
                custom_url_success, custom_url_error = save_profile_info(session, custom_url=custom_url)
                result['checks']['custom_url_change_request'] = custom_url_success
                if custom_url_success:
                    safe_print(f'[#{index + 1}] Custom Steam URL saved: https://steamcommunity.com/id/{custom_url}/')
                else:
                    safe_print(f'[#{index + 1}] Failed to save custom Steam URL: {custom_url_error}')
                    profile_setup_successful = False

            if enable_themechange:
                safe_print(f'[#{index + 1}] Setting profile theme...')
                theme_success, theme_error = set_profile_theme(session, profile_theme)
                result['checks']['theme_change_request'] = theme_success
                if theme_success:
                    safe_print(f'[#{index + 1}] Profile theme saved: {default_profile_theme}')
                else:
                    safe_print(f'[#{index + 1}] Failed to save profile theme: {theme_error}')
                    profile_setup_successful = False

            if enable_nameclear:
                safe_print(f'[#{index + 1}] Clearing nickname history...')
                try:
                    res = session.post(
                        f'https://steamcommunity.com/profiles/{client.steam_id.as_64}/ajaxclearaliashistory/',
                        data={'sessionid': get_session_cookie(session, 'sessionid')},
                        cookies={'sessionid': get_session_cookie(session, 'sessionid'),
                                 'steamLoginSecure': get_session_cookie(session, 'steamLoginSecure')},
                        timeout=request_timeout_seconds
                    )
                    if res.status_code == 200:
                        safe_print(f'[#{index + 1}] Nickname history cleared successfully.')
                    else:
                        safe_print(f'[#{index + 1}] Failed to clear nickname history: HTTP {res.status_code}')
                except requests.RequestException as exc:
                    safe_print(f'[#{index + 1}] Failed to clear nickname history: {exc}')

            if enable_set_up:
                safe_print(f'[#{index + 1}] Setting up community profile...')
                try:
                    session.post(
                        'https://steamcommunity.com/my/edit?welcomed=1',
                        data={'sessionid': get_session_cookie(session, 'sessionid')},
                        cookies={'sessionid': get_session_cookie(session, 'sessionid'),
                                 'steamLoginSecure': get_session_cookie(session, 'steamLoginSecure')},
                        timeout=request_timeout_seconds
                    )
                except requests.RequestException as exc:
                    safe_print(f'[#{index + 1}] Failed to set up community profile: {exc}')

            if enable_profile_verification:
                safe_print(f'[#{index + 1}] Verifying profile page...')
                verification = verify_profile_page(
                    session,
                    client.steam_id.community_url,
                    nickname=nickname if enable_namechange else None,
                    summary=None
                )
                result['checks']['profile_verification'] = verification
                edit_verification = verify_edit_state(
                    session,
                    summary=profile_summary if enable_descriptionchange else None,
                    custom_url=custom_url if enable_customurlchange else None,
                    theme_id=profile_theme if enable_themechange else None
                )
                result['checks']['edit_state_verification'] = edit_verification
                if verification.get('profile_loaded'):
                    safe_print(f"[#{index + 1}] Verify name={verification.get('name_ok')} desc={edit_verification.get('summary_ok')} url={edit_verification.get('custom_url_ok')} theme={edit_verification.get('theme_ok')} avatar_seen={verification.get('avatar_seen')}")
                    if edit_verification.get('custom_url_ok') is False:
                        safe_print(f"[#{index + 1}] ⚠ Custom URL couldn't be set! (Likely due to F2P/Limited account restrictions)")
                else:
                    safe_print(f"[#{index + 1}] Profile verification failed: {verification.get('error', verification.get('status_code'))}")
        else:
            debug('Failed to obtain web session after retries.')
            safe_print(f'[#{index + 1}] Failed to create a session. Check authentication, Steam Guard, or network.')
            profile_setup_successful = False

    result['success'] = login_successful and profile_setup_successful and checks_are_successful(result['checks'])

    try:
        client.logout()
    except Exception:
        pass
    try:
        client.disconnect()
    except Exception:
        pass

    if force_sleep:
        wait_or_stop(31)

    set_duration(result, started_at)
    safe_print(f"[#{index + 1}] Done in {result['duration_seconds']:.1f}s.")
    return result

def insert_random_chars(name, chars, num_insertions):
    name_list = list(name)
    for _ in range(num_insertions):
        pos = random.randint(0, len(name_list))
        char = random.choice(chars)
        name_list.insert(pos, char)
    return ''.join(name_list)

def generate_random_string(length):
    return ''.join(random.choices(string.ascii_letters + string.digits, k=length))


def get_random_name_from_file(steam_id=None):
    if steam_id:
        s_id_str = str(steam_id)
        if s_id_str in steam_id_names:
            return steam_id_names[s_id_str]

    if random_name_pool:
        return random.choice(random_name_pool)
    return generate_random_string(random_name_length)


def parse_proxy(proxy_string):
    try:
        if '://' in proxy_string:
            parsed_proxy = urlparse(proxy_string)
            if parsed_proxy.scheme not in ('http', 'https', 'socks4', 'socks4a', 'socks5', 'socks5h'):
                return None
            if not parsed_proxy.hostname or parsed_proxy.port is None:
                return None
            return {
                'http': proxy_string,
                'https': proxy_string
            }

        parts = proxy_string.split(':')
        if len(parts) == 4:
            host, port, username, password = parts
            return {
                'http': f'http://{username}:{password}@{host}:{port}',
                'https': f'http://{username}:{password}@{host}:{port}'
            }
        elif len(parts) == 2:
            host, port = parts
            return {
                'http': f'http://{host}:{port}',
                'https': f'http://{host}:{port}'
            }
    except Exception as e:
        debug(f'Failed to parse proxy: {proxy_string}, error: {e}')
    return None


def format_proxy_label(proxy_string):
    try:
        if '://' in proxy_string:
            parsed_proxy = urlparse(proxy_string)
            if parsed_proxy.hostname and parsed_proxy.port is not None:
                return f'{parsed_proxy.hostname}:{parsed_proxy.port}'
        parts = proxy_string.split(':')
        if len(parts) >= 2:
            return f'{parts[0]}:{parts[1]}'
    except Exception as e:
        debug(f'Failed to format proxy label: {proxy_string}, error: {e}')
    return proxy_string


def pick_proxy_for_account(used_proxy_indices):
    if not proxies:
        return None, None, None

    available_indices = [i for i in range(len(proxies)) if i not in used_proxy_indices]
    if available_indices:
        proxy_index = random.choice(available_indices)
        used_proxy_indices.add(proxy_index)
    else:
        proxy_index = random.randint(0, len(proxies) - 1)

    proxy_string = proxies[proxy_index]
    proxy_dict = parse_proxy(proxy_string)

    if proxy_dict:
        safe_print(f'Using proxy #{proxy_index + 1}: {format_proxy_label(proxy_string)}')
    else:
        safe_print(f'Failed to parse proxy: {proxy_string}')

    return proxy_dict, proxy_index, proxy_string


def get_authenticated_session(client, username=None, password=None, attempts=1, delay=3, proxy_dict=None, cached_cookies=None):
    if not username or not password:
        debug('❌ No username/password provided.')
        return None, None

    def new_session():
        sess = requests.Session()
        if proxy_dict:
            sess.proxies.update(proxy_dict)
        return sess

    session = new_session()

    if cached_cookies:
        debug(f'🔁 Using provided cached cookies for account: {username}')
        apply_cookies_to_session(session, cached_cookies)
        if validate_session_cookies(session):
            return session, None
        debug('Cached cookies failed validation; refreshing via browser automation.')
        session = new_session()

    attempt_count = max(1, attempts)
    cookie_dict = None
    for attempt_index in range(attempt_count):
        if stop_event.is_set():
            return None, None
        debug(f'⚡ Getting Steam cookies via browser for account: {username}')
        with browser_login_semaphore:
            cookie_dict = get_cookies_via_browser(username, password)
        if cookie_dict:
            break
        if attempt_index < attempt_count - 1 and wait_or_stop(delay):
            return None, None

    if not cookie_dict:
        debug('❌ Browser automation failed to get cookies')
        return None, None

    apply_cookies_to_session(session, cookie_dict)
    if validate_session_cookies(session):
        sessionid = session.cookies.get('sessionid', domain='steamcommunity.com') or session.cookies.get('sessionid')
        steamLoginSecure = session.cookies.get('steamLoginSecure', domain='steamcommunity.com') or session.cookies.get('steamLoginSecure')
        if sessionid and steamLoginSecure:
            debug('✓ Session ready with validated cookies from browser!')
        else:
            debug('⚠️ Session validated but expected cookies missing; proceeding anyway.')
        return session, cookie_dict

    debug('❌ Failed to validate cookies obtained from browser')
    return None, None


def update_profiles():
    avatar_bytes = None
    avatar_enabled = enable_avatarchange

    if enable_avatarchange:
        avatar_path = resolve_existing_path(profile_image_path, extra_dirs=('img',))
        try:
            with avatar_path.open('rb') as avatar_file:
                avatar_bytes = avatar_file.read()
        except FileNotFoundError:
            safe_print(f'Avatar image {profile_image_path} not found; disabling avatar updates.')
            avatar_enabled = False

    results = []
    valid_tasks = []
    cookie_updates = []

    for idx, account in enumerate(accounts):
        try:
            username, _ = account.split(':', 1)
        except ValueError:
            safe_print(f'[#{idx + 1}] Invalid account entry.')
            results.append({
                'account': account,
                'success': False,
                'steamid32_entry': None,
                'index': idx,
                'username': None,
                'new_cookies': None,
                'error': 'invalid_format'
            })
            continue

        cached_cookies = get_saved_cookies(username)
        valid_tasks.append((idx, account, username, cached_cookies))

    executor_required = enable_parallel_updates and max_parallel_accounts > 1 and len(valid_tasks) > 1
    max_workers = min(max_parallel_accounts, len(valid_tasks)) if executor_required else 1

    def handle_result(result):
        if not isinstance(result, dict):
            return
        username = result.get('username')
        new_cookies = result.get('new_cookies')
        if username and new_cookies:
            cookie_updates.append((username, new_cookies))
        results.append(result)

    if executor_required:
        executor = ThreadPoolExecutor(max_workers=max_workers)
        future_map = {}
        try:
            for idx, account, username, cached in valid_tasks:
                future = executor.submit(
                    process_account_task,
                    idx,
                    account,
                    avatar_bytes,
                    avatar_enabled,
                    cached
                )
                future_map[future] = (idx, username, account)

            pending = set(future_map)
            while pending and not stop_event.is_set():
                done, pending = wait(pending, timeout=0.5, return_when=FIRST_COMPLETED)
                for future in done:
                    idx, username, account = future_map[future]
                    try:
                        result = future.result()
                    except Exception as exc:
                        safe_print(f'[#{idx + 1}] Unexpected error while processing account: {exc}')
                        result = {
                            'account': account,
                            'success': False,
                            'steamid32_entry': None,
                            'index': idx,
                            'username': username,
                            'new_cookies': None,
                            'error': 'exception'
                        }
                    handle_result(result)
        except KeyboardInterrupt:
            stop_event.set()
            safe_print('Stopping after current in-flight account operations finish...')
            raise
        finally:
            executor.shutdown(wait=not stop_event.is_set(), cancel_futures=True)
    else:
        for idx, account, username, cached in valid_tasks:
            if stop_event.is_set():
                break
            try:
                result = process_account_task(idx, account, avatar_bytes, avatar_enabled, cached)
            except Exception as exc:
                safe_print(f'[#{idx + 1}] Unexpected error while processing account: {exc}')
                result = {
                    'account': account,
                    'success': False,
                    'steamid32_entry': None,
                    'index': idx,
                    'username': username,
                    'new_cookies': None,
                    'error': 'exception'
                }
            handle_result(result)

    ordered_results = sorted(results, key=lambda item: item.get('index', 0))

    if cookie_updates:
        for username, new_cookies in cookie_updates:
            store_cookies(username, new_cookies, persist=False)
        persist_cookie_cache()

    if enable_gatherid32:
        steamid_text = ''.join(item.get('steamid32_entry') or '' for item in ordered_results)
        write_text_atomic('steamid32.txt', steamid_text)

    good_accounts_text = ''.join(f"{item['account']}\n" for item in ordered_results if item.get('success'))
    write_text_atomic('goodaccounts.txt', good_accounts_text)

    successful_count = sum(1 for item in ordered_results if item.get('success'))
    durations = [item.get('duration_seconds') for item in ordered_results if item.get('duration_seconds') is not None]

    safe_print('Done.')
    safe_print('\n=== Summary ===')
    safe_print(f'Total accounts processed: {len(accounts)}')
    safe_print(f'Successful accounts: {successful_count}')
    safe_print(f'Failed accounts: {len(accounts) - successful_count}')
    if durations:
        safe_print(f'Average account time: {sum(durations) / len(durations):.1f}s')
        safe_print(f'Fastest account time: {min(durations):.1f}s')
        safe_print(f'Slowest account time: {max(durations):.1f}s')
    safe_print('Good accounts saved to: goodaccounts.txt')


