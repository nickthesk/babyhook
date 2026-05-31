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
import string
import sys
import io
import threading
import multiprocessing
from concurrent.futures import ProcessPoolExecutor, as_completed

import requests

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

import steam.client
from steam import webapi
from steam.webauth import WebAuth, WebAuthException
from steam.enums import EResult

f = open('accounts.txt', 'r')
data = f.read()
f.close()

data = data.replace('\r\n', '\n')
accounts = data.split('\n')
accounts = [account for account in accounts if account.strip()]

# Load proxies from proxies.html
proxies = []
try:
    with open('proxies.html', 'r') as proxy_file:
        proxy_data = proxy_file.read().replace('\r\n', '\n')
        proxies = [proxy.strip() for proxy in proxy_data.split('\n') if proxy.strip()]
    print(f'Loaded {len(proxies)} proxies from proxies.html')
except FileNotFoundError:
    print('proxies.html not found. Running without proxy support.')

## Change stuff below to your liking.
profile_image_path = 'image.jpg'
default_nickname = 'ZESTY JESUS'

enable_debugging = False # Debug info toggle.
enable_extra_info = False # Yap info toggle.
enable_avatarchange = True # Avatar upload via automated browser login (requires Playwright)
enable_namechange = True # Toggle for changing the nicks.
enable_nameclear = False # Clears the prev nick list.  This does not work.
enable_set_up = False # Sets up the profile. Useless.
enable_gatherid32 = True # Collects steam32 ids of the accounts.
dump_response = False # I can only guess what this does.
make_commands = True # Changes steamids to cat_pl_add_id *id* CAT commands.
force_sleep = False # Too lazy to know what this does!
max_login_retries = 3 # Max retries per account with different proxies
Randomname = False  # Toggle this to generate random account names
InsertRandomChars = True  # Toggle this to insert random characters into the nickname
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

random_name_pool = []
steam_id_names = {}

try:
    with open('names.txt', 'r', encoding='utf-8') as names_file:
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
if web_api_key:
    webapi.DEFAULT_PARAMS['key'] = web_api_key

if enable_debugging:
    logger = steam.client.SteamClient._LOG
    if not any(isinstance(handler, logging.StreamHandler) for handler in logger.handlers):
        stream_handler = logging.StreamHandler()
        stream_handler.setLevel(logging.DEBUG)
        logger.addHandler(stream_handler)
    logger.setLevel(logging.DEBUG)


def safe_print(*args, **kwargs):
    with print_lock:
        print(*args, **kwargs)


def debug(message):
    if enable_debugging:
        safe_print(message)


def extra(message):
    if enable_extra_info:
        safe_print(message)


def get_cookies_via_browser(username, password):
    """
    Automatically log into Steam via browser and extract cookies
    Returns: dict of cookies or None if failed
    """
    try:
        from playwright.sync_api import sync_playwright
        import time

        safe_print('🌐 Opening browser to get Steam cookies...')
        safe_print('   This will automatically log in and extract authentication cookies.')

        with sync_playwright() as p:
            # Launch browser (headless=False so you can see it working)
            browser = p.chromium.launch(headless=True)
            context = browser.new_context()
            page = context.new_page()

            # Go to Steam login page
            safe_print('   Navigating to Steam login...')
            page.goto('https://steamcommunity.com/login/home/')
            time.sleep(2)

            # Fill in username
            safe_print(f'   Entering username: {username}')
            page.fill('input[type="text"]', username)
            time.sleep(0.5)

            # Fill in password
            safe_print('   Entering password...')
            page.fill('input[type="password"]', password)
            time.sleep(0.5)

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
                safe_print('   Waiting additional 10 seconds...')
                time.sleep(10)

            # Make sure we're on steamcommunity to get all cookies
            if 'login' in page.url.lower():
                safe_print('   Still on login page, navigating to community...')
                page.goto('https://steamcommunity.com/')
                time.sleep(2)

            # Extract cookies
            safe_print('   📦 Extracting cookies...')
            cookies = context.cookies()

            # Build cookie dict
            cookie_dict = {}
            for cookie in cookies:
                if cookie['domain'] in ['.steamcommunity.com', 'steamcommunity.com']:
                    if cookie['name'] in ['sessionid', 'steamLogin', 'steamLoginSecure', 'steamCountry']:
                        cookie_dict[cookie['name']] = cookie['value']
                        safe_print(f'   ✓ Got {cookie["name"]}: {cookie["value"][:20]}...')

            browser.close()

            if 'sessionid' in cookie_dict and 'steamLoginSecure' in cookie_dict:
                safe_print('   ✅ Successfully extracted all required cookies!')
                return cookie_dict
            else:
                safe_print('   ❌ Missing required cookies. Login may have failed.')
                return None

    except ImportError:
        safe_print('❌ Playwright not installed!')
        safe_print('   Install it with: pip install playwright')
        safe_print('   Then run: playwright install chromium')
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
            with open(cookies_cache_file, 'r', encoding='utf-8') as cache_file:
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
        with open(cookies_cache_file, 'w', encoding='utf-8') as cache_file:
            json.dump(cookie_cache, cache_file, separators=(',', ':'))
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


def store_cookies(username, cookie_dict):
    if not username or not cookie_dict:
        return
    load_cookie_cache()
    with cookie_cache_lock:
        cookie_cache[username] = {
            'cookies': cookie_dict,
            'timestamp': time.time()
        }
        persist_cookie_cache()


def apply_cookies_to_session(session, cookies):
    for name, value in cookies.items():
        for domain in ('.steamcommunity.com', 'steamcommunity.com'):
            cookie = requests.cookies.create_cookie(domain=domain, name=name, value=value)
            session.cookies.set_cookie(cookie)


def validate_session_cookies(session):
    try:
        response = session.get('https://steamcommunity.com/my/edit', timeout=15, allow_redirects=True)
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
    result = {
        'account': account_entry,
        'success': False,
        'steamid32_entry': None,
        'index': index,
        'username': None,
        'new_cookies': None
    }

    try:
        username, password = account_entry.split(':', 1)
    except ValueError:
        safe_print(f'[#{index + 1}] Invalid account entry: {account_entry}')
        result['error'] = 'invalid_format'
        return result

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

        safe_print(f'[#{index + 1}] Done.')
        return result

    safe_print(f'[#{index + 1}] Logging in as {username}...')

    used_proxy_indices = set()
    login_successful = False
    client = None
    last_proxy_dict = None

    for retry in range(max_login_retries):
        proxy_dict, proxy_index, proxy_string = pick_proxy_for_account(used_proxy_indices)
        last_proxy_dict = proxy_dict

        client = steam.client.SteamClient()
        try:
            eresult = login_with_timeout(client, username, password, login_timeout_seconds)
        except Exception as exc:
            safe_print(f'[#{index + 1}] Login encountered an error: {exc}')
            debug(f'Login exception for {username}: {exc}')
            eresult = EResult.Fail

        try:
            eresult_enum = EResult(eresult)
        except ValueError:
            eresult_enum = EResult.Fail

        status = 'OK' if eresult_enum == EResult.OK else 'FAIL'
        safe_print(f'[#{index + 1}] Login status: {status} - {eresult_enum.name}')

        if eresult_enum == EResult.OK:
            login_successful = True
            break

        if retry < max_login_retries - 1:
            safe_print(f'[#{index + 1}] Retrying with different proxy... (attempt {retry + 2}/{max_login_retries})')
            time.sleep(2)

        try:
            client.disconnect()
        except Exception:
            pass

    if not login_successful or client is None:
        safe_print(f'[#{index + 1}] Skipping account {username}.')
        result['error'] = 'login_failed'
        return result

    safe_print(f'[#{index + 1}] Logged in as: {client.user.name}')
    safe_print(f'[#{index + 1}] Community profile: {client.steam_id.community_url}')
    extra(f'[#{index + 1}] Last logon (UTC): {client.user.last_logon}')
    extra(f'[#{index + 1}] Last logoff (UTC): {client.user.last_logoff}')

    if enable_gatherid32:
        id32 = str(client.steam_id.as_32)
        if make_commands:
            result['steamid32_entry'] = f'cat_ignore {id32} IGNORED\n'
            safe_print(f'[#{index + 1}] Saved the SteamID32 as a Amalgam change playerstate command.')
        else:
            result['steamid32_entry'] = f'{id32}\n'
            safe_print(f'[#{index + 1}] Saved the SteamID32 as raw.')

    if enable_namechange:
        if Randomname:
            nickname = get_random_name_from_file(client.steam_id.as_64)
        else:
            nickname = default_nickname
        if InsertRandomChars:
            nickname = insert_random_chars(nickname, random_chars, num_insertions)
        time.sleep(5)
        client.change_status(persona_state=1, player_name=nickname)
        safe_print(f'[#{index + 1}] Changed Steam nickname to "{nickname}"')

    profile_setup_successful = True

    if enable_avatarchange or enable_nameclear or enable_set_up:
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
            debug(f'session.cookies: {session.cookies}')

            if enable_avatarchange and avatar_allowed and avatar_payload:
                url = 'https://steamcommunity.com/actions/FileUploader'
                id64 = client.steam_id.as_64

                data = {
                    'MAX_FILE_SIZE': '1048576',
                    'type': 'player_avatar_image',
                    'sId': str(id64),
                    'sessionid': session.cookies.get('sessionid', domain='steamcommunity.com') or session.cookies.get('sessionid'),
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
                            else:
                                safe_print(f'[#{index + 1}] ⚠ Avatar upload returned status {response.status_code}')
                        else:
                            message = data_json.get('message')
                            if data_json.get('success'):
                                safe_print(f'[#{index + 1}] ✓ Profile picture set successfully!')
                            elif message:
                                safe_print(f'[#{index + 1}] ⚠ Avatar upload response: {message}')
                            else:
                                safe_print(f'[#{index + 1}] ⚠ Unexpected avatar upload response: {data_json}')
                    elif response.status_code == 200:
                        safe_print(f'[#{index + 1}] ✓ Profile picture uploaded successfully!')
                    else:
                        safe_print(f'[#{index + 1}] ⚠ Avatar upload returned status {response.status_code}')

            if enable_nameclear:
                safe_print(f'[#{index + 1}] Clearing nickname history...')
                session.post(
                    'https://steamcommunity.com/my/ajaxclearaliashistory/',
                    data={'sessionid': session.cookies.get('sessionid', domain='steamcommunity.com')},
                    cookies={'sessionid': session.cookies.get('sessionid', domain='steamcommunity.com'),
                             'steamLoginSecure': session.cookies.get('steamLoginSecure', domain='steamcommunity.com')},
                    timeout=15
                )

            if enable_set_up:
                safe_print(f'[#{index + 1}] Setting up community profile...')
                session.post(
                    'https://steamcommunity.com/my/edit?welcomed=1',
                    data={'sessionid': session.cookies.get('sessionid', domain='steamcommunity.com')},
                    cookies={'sessionid': session.cookies.get('sessionid', domain='steamcommunity.com'),
                             'steamLoginSecure': session.cookies.get('steamLoginSecure', domain='steamcommunity.com')},
                    timeout=15
                )
        else:
            debug('Failed to obtain web session after retries.')
            safe_print(f'[#{index + 1}] Failed to create a session. Check authentication, Steam Guard, or network.')
            profile_setup_successful = False

    result['success'] = login_successful and profile_setup_successful

    try:
        client.logout()
    except Exception:
        pass
    try:
        client.disconnect()
    except Exception:
        pass

    if force_sleep:
        time.sleep(31)

    safe_print(f'[#{index + 1}] Done.')
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
    debug('names.txt missing or empty; falling back to default nickname.')
    return default_nickname


def parse_proxy(proxy_string):
    """Parse proxy string in format host:port:username:password"""
    try:
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
        host, port = proxy_string.split(':', 1)[:2]
        safe_print(f'Using proxy #{proxy_index + 1}: {host}:{port}')
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

    debug(f'⚡ Getting Steam cookies via browser for account: {username}')
    cookie_dict = get_cookies_via_browser(username, password)
    if not cookie_dict:
        debug('❌ Browser automation failed to get cookies')
        return None, None

    apply_cookies_to_session(session, cookie_dict)
    if validate_session_cookies(session):
        sessionid = session.cookies.get('sessionid', domain='steamcommunity.com') or session.cookies.get('sessionid')
        steamLoginSecure = session.cookies.get('steamLoginSecure', domain='steamcommunity.com') or session.cookies.get('steamLoginSecure')
        if sessionid and steamLoginSecure:
            debug('✓ Session ready with validated cookies from browser!')
            debug(f'  sessionid: {sessionid[:8]}...')
            debug(f'  steamLoginSecure: {steamLoginSecure[:30]}...')
        else:
            debug('⚠️ Session validated but expected cookies missing; proceeding anyway.')
        return session, cookie_dict

    debug('❌ Failed to validate cookies obtained from browser')
    return None, None


def update_profiles():
    avatar_bytes = None
    avatar_enabled = enable_avatarchange

    if enable_avatarchange:
        try:
            with open(profile_image_path, 'rb') as avatar_file:
                avatar_bytes = avatar_file.read()
        except FileNotFoundError:
            safe_print(f'Avatar image {profile_image_path} not found; disabling avatar updates.')
            avatar_enabled = False

    results = []
    valid_tasks = []

    for idx, account in enumerate(accounts):
        try:
            username, _ = account.split(':', 1)
        except ValueError:
            safe_print(f'[#{idx + 1}] Invalid account entry: {account}')
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
            store_cookies(username, new_cookies)
        results.append(result)

    if executor_required:
        mp_ctx = multiprocessing.get_context('spawn')
        with ProcessPoolExecutor(max_workers=max_workers, mp_context=mp_ctx) as executor:
            future_map = {}
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

            for future in as_completed(future_map):
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
    else:
        for idx, account, username, cached in valid_tasks:
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

    if enable_gatherid32:
        with open('steamid32.txt', 'w', encoding='utf-8') as id_file:
            for item in ordered_results:
                entry = item.get('steamid32_entry')
                if entry:
                    id_file.write(entry)

    with open('goodaccounts.txt', 'w', encoding='utf-8') as good_accounts_file:
        for item in ordered_results:
            if item.get('success'):
                good_accounts_file.write(f"{item['account']}\n")

    successful_count = sum(1 for item in ordered_results if item.get('success'))

    safe_print('Done.')
    safe_print('\n=== Summary ===')
    safe_print(f'Total accounts processed: {len(accounts)}')
    safe_print(f'Successful accounts: {successful_count}')
    safe_print(f'Failed accounts: {len(accounts) - successful_count}')
    safe_print('Good accounts saved to: goodaccounts.txt')

def main():
    if loopupdateprofiles:
        while True:
            update_profiles()
            safe_print(f'Waiting {update_interval} seconds before next update...')
            time.sleep(update_interval)
    else:
        update_profiles()


if __name__ == '__main__':
    main()
