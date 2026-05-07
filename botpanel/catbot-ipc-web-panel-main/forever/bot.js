const EventEmitter = require('events');
const child_process = require('child_process');

const timestamp = require('time-stamp');
const fs = require('fs');
const procfs = require('procfs-stats');
const path = require("path");
const { Tail } = require("tail");

const accounts = require('./acc.js');
const config = require('./config');

const CATHOOK_ROOT = process.env.CATHOOK_ROOT || '/opt/cathook';
const BOT_DISPLAY = process.env.DISPLAY || ':1';
const BOT_XAUTHORITY = process.env.XAUTHORITY || path.join(process.env.HOME || '', '.Xauthority');
const VISIBLE_WINDOWS = process.env.CAT_VISIBLE_WINDOWS === '1';
const GDB_CRASH_REPORTS = process.env.CAT_GDB_CRASH_REPORTS === '1' || config.gdb_crash_reports === true;
const STEAM_WINDOW_OPTIONS = VISIBLE_WINDOWS
    ? '-noreactlogin'
    : '-silent -noreactlogin -cef-disable-gpu -nominidumps -nobreakpad -no-browser -nofriendsui -noasync -nofasthtml -noshaders -oldtraymenu -skipstreamingdrivers -nochatui';
const GAME_WINDOW_OPTIONS = VISIBLE_WINDOWS ? '-sw -w 1024 -h 768' : '-silent -sw -w 1 -h 480';
const GAME_RENDER_OPTIONS = '-gl';
const CATHOOK_ATTACH_DELAY_SECONDS = Number.parseInt(process.env.CATHOOK_ATTACH_DELAY_SECONDS || '5', 10);

const LAUNCH_OPTIONS_STEAM = `firejail --dns=1.1.1.1 %NETWORK% --noprofile --private="%HOME%" --name=%JAILNAME% --env=PULSE_SERVER="unix:/tmp/pulse.sock" --env=DISPLAY=%DISPLAY% --env=XAUTHORITY=%XAUTHORITY% --env=LD_PRELOAD=%LD_PRELOAD% %STEAM% ${STEAM_WINDOW_OPTIONS} -login %LOGIN% %PASSWORD%`
const LAUNCH_OPTIONS_STEAM_RESET = 'firejail --net=none --noprofile --private="%HOME%" %STEAM% --reset'
const LAUNCH_OPTIONS_GAME = `firejail --join=%JAILNAME% bash -c 'cd "%GAMEPATH%" && %RUNTIME_PREFIX% SteamAppId=440 SteamGameId=440 SteamOverlayGameId=440 SteamEnv=1 CATHOOK_AUTO_ATTACH=1 CATHOOK_ATTACH_DELAY_SECONDS=%CATHOOK_ATTACH_DELAY_SECONDS% CAT_BOT_ID=%BOT_ID% CAT_BOT_NAME=%BOT_NAME% LD_PRELOAD=%LD_PRELOAD% DISPLAY=%DISPLAY% XAUTHORITY="%XAUTHORITY%" PULSE_SERVER="unix:/tmp/pulse.sock" %GAME_BINARY% -steam -game tf ${GAME_RENDER_OPTIONS} ${GAME_WINDOW_OPTIONS} -novid -nojoy -noipx -noshaderapi -nomouse -nomessagebox -nominidumps -nohltv -nobreakpad -reuse -noquicktime -precachefontchars -particles 1 -snoforceformat -softparticlesdefaultoff -textmode -wavonly -forcenovsync -insecure +clientport 27006-27014'`
const GAME_LIBRARY_PATH = './bin:./bin/linux64:./tf/bin:./tf/bin/linux64:./platform:./platform/bin:./platform/bin/linux64:.';

// Adjust these values as needed to optimize catbot performance
// Static delay after Steam is ready before launching TF2
const TIMEOUT_LAUNCH_GAME = 15000;
// How long to wait for the TF2 process to be created by firejail
const TIMEOUT_START_GAME = 10000;
// Timeout for cathook to connect to the IPC server once injected
const TIMEOUT_IPC_STATE = Number.parseInt(process.env.CAT_IPC_TIMEOUT_SECONDS || '90', 10) * 1000;
// Time to wait for steam to be "ready"
const TIMEOUT_STEAM_RUNNING = Number.parseInt(process.env.CAT_STEAM_TIMEOUT_SECONDS || '300', 10) * 1000;
const TIMEOUT_STEAM_ASSUME_READY = Number.parseInt(process.env.CAT_STEAM_READY_SECONDS || '0', 10) * 1000;
const STEAMWEBHELPER_CLEANUP_ENABLED = process.env.CAT_STEAMWEBHELPER_CLEANUP !== '0';
const STEAMWEBHELPER_CLEANUP_DELAY_SECONDS_VALUE = Number.parseInt(process.env.CAT_STEAMWEBHELPER_CLEANUP_SECONDS || '10', 10);
const STEAMWEBHELPER_CLEANUP_DELAY = (Number.isFinite(STEAMWEBHELPER_CLEANUP_DELAY_SECONDS_VALUE) ? Math.max(0, STEAMWEBHELPER_CLEANUP_DELAY_SECONDS_VALUE) : 10) * 1000;
// Maximum amount of concurrently starting bots
let MAX_CONCURRENT_BOTS = 3;
// Time to delay individual bot starts by to prevent IPC ID conflicts
const DELAY_START_TIME = 1000;

const STATE = {
    INITIALIZING: 0,
    INITIALIZED: 1,
    STARTING: 3,
    WAITING: 4,
    RUNNING: 5,
    RESTARTING: 6,
    STOPPING: 7,
    NO_ACCOUNT: 8,
}

function makeid(length) {
    var result = '';
    var characters = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxy!_2$!^%z0123456789';
    var charactersLength = characters.length;
    for (var i = 0; i < length; i++) {
        result += characters.charAt(Math.floor(Math.random() * charactersLength));
    }
    return result;
}

function clearSourceLockFiles() {
    var files = fs.readdirSync('/tmp/');
    files.forEach((str, index, arr) => {
        if (str.startsWith("source_engine") && str.endsWith(".lock"))
            fs.unlink(`/tmp/${str}`, (err) => {
                if (err)
                    console.log("[ERROR] Failed to delete a source engine lock!");
            });
    });
}

function shell_quote(value) {
    return "'" + String(value).replace(/'/g, "'\\''") + "'";
}

function log_file_tail(file_path, line_count) {
    try {
        if (!fs.existsSync(file_path))
            return '';

        const lines = fs.readFileSync(file_path, 'utf8').trimEnd().split(/\r?\n/);
        return lines.slice(-line_count).join('\n');
    } catch (error) {
        return `failed to read ${file_path}: ${error.message}`;
    }
}

function bash_double_quote_escape(value) {
    return String(value).replace(/(["\\$`])/g, '\\$1');
}

function tf2_install_ready(tf2_path) {
    return fs.existsSync(path.join(tf2_path, 'tf_linux64'));
}

function preload_value(primary_library) {
    const extra_preload = process.env.STEAM_LD_PRELOAD || '';
    return extra_preload ? `${primary_library}:${extra_preload}` : primary_library;
}

function cathook_textmode_library() {
    const candidates = [
        path.join(CATHOOK_ROOT, 'bin/libcathooktextmode.so'),
        '/opt/cathook/bin/libcathooktextmode.so',
        path.join(CATHOOK_ROOT, 'bin/libcathook-textmode.so'),
        '/opt/cathook/bin/libcathook-textmode.so'
    ];

    for (const candidate of unique_paths(candidates)) {
        if (fs.existsSync(candidate))
            return candidate;
    }

    return candidates[0];
}

function path_is_inside(child_path, parent_path) {
    const relative_path = path.relative(parent_path, child_path);
    return Boolean(relative_path) && !relative_path.startsWith('..') && !path.isAbsolute(relative_path);
}

function path_is_at_or_inside(child_path, parent_path) {
    const relative_path = path.relative(parent_path, child_path);
    return relative_path === '' || (Boolean(relative_path) && !relative_path.startsWith('..') && !path.isAbsolute(relative_path));
}

function real_path_or_null(target_path) {
    try {
        return fs.realpathSync(target_path);
    } catch (error) {
        return null;
    }
}

function unique_paths(paths) {
    return [...new Set(paths.filter(Boolean))];
}

function read_proc_stat(pid) {
    try {
        const stat = fs.readFileSync(`/proc/${pid}/stat`, 'utf8');
        const end_comm = stat.lastIndexOf(')');
        if (end_comm < 0)
            return null;

        const comm = stat.slice(stat.indexOf('(') + 1, end_comm);
        const fields = stat.slice(end_comm + 2).trim().split(/\s+/);
        return {
            pid: pid,
            comm: comm,
            ppid: Number.parseInt(fields[1], 10),
            starttime: Number.parseInt(fields[19], 10) || 0,
            cmdline: read_proc_cmdline(pid)
        };
    } catch (error) {
        return null;
    }
}

function read_proc_cmdline(pid) {
    try {
        return fs.readFileSync(`/proc/${pid}/cmdline`, 'utf8').replace(/\0/g, ' ').trim();
    } catch (error) {
        return '';
    }
}

function read_process_table() {
    const processes = new Map();
    try {
        for (const entry of fs.readdirSync('/proc')) {
            if (!/^\d+$/.test(entry))
                continue;

            const pid = Number.parseInt(entry, 10);
            const info = read_proc_stat(pid);
            if (info)
                processes.set(pid, info);
        }
    } catch (error) { }

    return processes;
}

function collect_descendant_pids(root_pid, processes) {
    const children_by_parent = new Map();
    for (const info of processes.values()) {
        if (!children_by_parent.has(info.ppid))
            children_by_parent.set(info.ppid, []);
        children_by_parent.get(info.ppid).push(info.pid);
    }

    const descendants = [];
    const stack = [root_pid];
    while (stack.length) {
        const parent_pid = stack.pop();
        for (const child_pid of children_by_parent.get(parent_pid) || []) {
            descendants.push(child_pid);
            stack.push(child_pid);
        }
    }

    return descendants;
}

function kill_process_tree(root_pid, signal) {
    if (!root_pid || root_pid <= 0)
        return 0;

    const processes = read_process_table();
    const pids = collect_descendant_pids(root_pid, processes).reverse();
    pids.push(root_pid);

    var killed_count = 0;
    for (const pid of pids) {
        try {
            process.kill(pid, signal);
            killed_count++;
        } catch (error) { }
    }

    return killed_count;
}

function is_steamwebhelper_process(info) {
    if (!info)
        return false;

    const command = info.cmdline || '';
    const executable = path.basename(command.split(/\s+/)[0] || '');
    return info.comm === 'steamwebhelper' || executable.includes('steamwebhelper') || command.includes('/steamwebhelper');
}

function find_main_steamwebhelper(steam_launcher_pid) {
    const processes = read_process_table();
    const steam_tree = new Set(collect_descendant_pids(steam_launcher_pid, processes));
    steam_tree.add(steam_launcher_pid);

    const helpers = [...steam_tree]
        .map((pid) => processes.get(pid))
        .filter(is_steamwebhelper_process);
    const helper_pids = new Set(helpers.map((info) => info.pid));
    const root_helpers = helpers.filter((info) => !helper_pids.has(info.ppid));
    const candidates = root_helpers.length ? root_helpers : helpers;
    candidates.sort((left, right) => (left.starttime - right.starttime) || (left.pid - right.pid));

    if (!candidates.length)
        return { main: null, child_pids: [], helper_count: 0 };

    return {
        main: candidates[0],
        child_pids: collect_descendant_pids(candidates[0].pid, processes),
        helper_count: helpers.length
    };
}

function copy_directory_contents(source_path, target_path) {
    fs.mkdirSync(target_path, { recursive: true });
    for (const entry of fs.readdirSync(source_path, { withFileTypes: true })) {
        const source_entry = path.join(source_path, entry.name);
        const target_entry = path.join(target_path, entry.name);
        const source_stat = fs.statSync(source_entry);

        if (source_stat.isDirectory()) {
            copy_directory_contents(source_entry, target_entry);
        } else if (source_stat.isFile()) {
            fs.copyFileSync(source_entry, target_entry);
        }
    }
}

function chown_tree(target_path, uid, gid) {
    try {
        fs.chownSync(target_path, uid, gid);
        if (!fs.lstatSync(target_path).isDirectory())
            return;

        for (const entry of fs.readdirSync(target_path)) {
            chown_tree(path.join(target_path, entry), uid, gid);
        }
    } catch (error) { }
}

function ensure_directory_not_symlink(directory_path) {
    try {
        const status = fs.lstatSync(directory_path);
        if (status.isSymbolicLink() || !status.isDirectory())
            fs.rmSync(directory_path, { recursive: true, force: true });
    } catch (error) {
        if (error.code !== 'ENOENT')
            throw error;
    }

    fs.mkdirSync(directory_path, { recursive: true });
}

if (!process.env.SUDO_USER) {
    console.error('[ERROR] Could not find $SUDO_USER');
    process.exit(1);
}

const USER = { name: process.env.SUDO_USER, uid: Number.parseInt(child_process.execSync("id -u " + process.env.SUDO_USER).toString().trim()), home: child_process.execSync(`printf ~${process.env.SUDO_USER}`).toString(), interface: child_process.execSync("route -n | grep '^0\.0\.0\.0' | grep -o '[^ ]*$' | head -n 1").toString().trim(), SUPPORTS_FJ_NET: true };
try {
    child_process.execSync(`firejail --quiet --noprofile --net=${USER.interface} bash -c "ping -q -c 1 -W 1 1.1.1.1 >/dev/null && echo ok"`)
} catch (error) {
    USER.SUPPORTS_FJ_NET = false;
}

console.log('Main user name: ' + USER.name);
console.log('Visible windows: ' + (VISIBLE_WINDOWS ? 'yes' : 'no') + ', display: ' + BOT_DISPLAY);
console.log('Bot runtime version: steam_login_wait_no_auto_assume_opengl_v1');

class Bot extends EventEmitter {
    constructor(botid) {
        super();
        var self = this;
        this.state = STATE.INITIALIZING;

        this.name = "b" + botid;
        this.botid = botid;
        this.home = path.join(__dirname, "..", "..", "user_instances", this.name)

        // Create a network namespace for this bot
        if (!USER.SUPPORTS_FJ_NET)
            child_process.execSync("./scripts/ns-inet " + this.botid)

        this.stopped = false;
        this.account = null;
        this.account_generation = 0;
        this.restarts = 0;

        this.log(`Initializing, folder = ${self.name}`);

        this.procFirejailSteam = null;
        this.procFirejailGame = null;

        // Start timestamp
        this.startTime = null;

        this.ipcState = null;
        this.ipcLastHeartbeat = 0;
        this.ipcID = -1;

        this.gameStarted = 0;
        this.gamePid = -1;
        this.gamePreloadLibrary = null;
        this.gdbSnapshotRunning = false;
        this.time_steamwebhelper_cleanup = 0;
        this.steamwebhelper_cleanup_done = false;
        this.steamwebhelper_frozen_pid = -1;

        this.logSteam = null;
        this.logGame = null;

        this.nativeSteam = fs.existsSync("/usr/bin/steam-native");

        this.spawnOptions = {
            shell: 'bash',
            uid: USER.uid,
            env: {
                PATH: process.env.PATH,
                HOME: USER.home
            }
        }

        this.on('ipc-data', function (obj) {
            if (!self.procFirejailGame)
                return;
            if (self.state != STATE.RUNNING && self.state != STATE.WAITING)
                return;
            var id = obj.id;
            var data = obj.data;

            // There is no point in storing the same ipc data again. Removing this will actually cause IPC data to be set while the bot is not running.
            // This is only really a problem between tf2 crash/exit and IPC server auto removal
            if (data.heartbeat == self.ipcLastHeartbeat)
                return;
            self.ipcLastHeartbeat = data.heartbeat;

            self.ipcID = id;
            if (!self.ipcState) {
                self.log(`Assigned IPC ID ${id}`);
                self.schedule_steamwebhelper_cleanup();
            }
            self.ipcState = data;
        });

        this.state = STATE.INITIALIZED;

        this.shouldRun = false;
        this.shouldRestart = false;
        this.isSteamWorking = false;
        this.time_steamWorking = 0;
        this.time_steamAssumeReady = 0;
        this.time_game_launch = 0;
        this.time_gameCheck = 0;
        this.time_ipcState = 0;
        this.time_steamStatusLog = 0;
        this.shouldResetSteam = false;
        this.steamReadyLogged = false;
    }

    log(message) {
        console.log(`[${timestamp('HH:mm:ss')}][${this.name}][${this.state}] ${message}`);
    }

    ensureSteamappsLink() {
        if (fs.existsSync(this.steamApps))
            return true;

        if (!fs.existsSync('/opt/steamapps'))
            return false;

        const steamapps_parent = path.dirname(this.steamApps);
        const home_real_path = real_path_or_null(this.home);
        const parent_real_path = real_path_or_null(steamapps_parent);
        if (!home_real_path || (parent_real_path && !path_is_at_or_inside(parent_real_path, home_real_path))) {
            this.log(`[ERROR] Refusing to create steamapps link outside bot home: ${this.steamApps}`);
            return false;
        }

        fs.mkdirSync(steamapps_parent, { recursive: true });
        fs.symlinkSync('/opt/steamapps/', this.steamApps);
        return true;
    }

    steamRootPaths() {
        return [
            path.join(this.home, '.steam/steam'),
            path.join(this.home, '.steam/debian-installation'),
            path.join(this.home, '.steam/root'),
            path.join(this.home, '.local/share/Steam')
        ];
    }

    hostSteamRootPaths() {
        return [
            '/opt/CATHOOK_steam_root',
            path.join(USER.home, '.steam/steam'),
            path.join(USER.home, '.steam/debian-installation'),
            path.join(USER.home, '.steam/root'),
            path.join(USER.home, '.local/share/Steam')
        ];
    }

    ensureSteamRootLinks() {
        const steam_dir = path.join(this.home, '.steam');
        ensure_directory_not_symlink(steam_dir);

        const home_real_path = real_path_or_null(this.home);
        for (const link_name of ['steam', 'root', 'debian-installation']) {
            const link_path = path.join(steam_dir, link_name);
            try {
                const link_status = fs.lstatSync(link_path);
                if (!link_status.isSymbolicLink())
                    continue;

                const link_real_path = real_path_or_null(link_path);
                if (home_real_path && link_real_path && path_is_at_or_inside(link_real_path, home_real_path))
                    continue;

                fs.rmSync(link_path, { recursive: true, force: true });
                fs.mkdirSync(link_path, { recursive: true });
                chown_tree(link_path, USER.uid, USER.uid);
                this.log(`Replaced unsafe Steam root symlink with local directory: ${link_path}`);
            } catch (error) {
                if (error.code !== 'ENOENT')
                    this.log(`[ERROR] Failed to sanitize ${link_path}: ${error.message}`);
            }
        }
    }

    steamSdk64Source() {
        const steam_roots = unique_paths([this.steamPath, ...this.hostSteamRootPaths(), ...this.steamRootPaths()]);
        for (const steam_root of steam_roots) {
            for (const sdk_dir_name of ['ubuntu12_64', 'linux64']) {
                const sdk_dir = path.join(steam_root, sdk_dir_name);
                if (fs.existsSync(path.join(sdk_dir, 'steamclient.so')))
                    return fs.realpathSync(sdk_dir);
            }
        }

        return null;
    }

    repairSteamSdk64() {
        const source_path = this.steamSdk64Source();
        const steam_dir = path.join(this.home, '.steam');
        const target_path = path.join(this.home, '.steam/sdk64');
        if (!source_path) {
            this.log(`[ERROR] Could not find 64-bit steamclient.so for ${target_path}`);
            return false;
        }

        try {
            ensure_directory_not_symlink(steam_dir);
            fs.rmSync(target_path, { recursive: true, force: true });
            copy_directory_contents(source_path, target_path);
            chown_tree(target_path, USER.uid, USER.uid);
            if (!fs.existsSync(path.join(target_path, 'steamclient.so')))
                throw new Error(`steamclient.so missing after copy from ${source_path}`);
            this.log(`Steam sdk64 ready, source=${source_path}, target=${target_path}, mode=copy`);
            return true;
        } catch (error) {
            this.log(`[ERROR] Failed to prepare Steam sdk64: ${error.message}`);
            return false;
        }
    }

    steamLogPaths() {
        var log_paths = ['./logs/' + this.name + '.steam.log'];

        for (var steam_root of this.steamRootPaths()) {
            log_paths.push(
                path.join(steam_root, 'error.log'),
                path.join(steam_root, 'logs/bootstrap_log.txt'),
                path.join(steam_root, 'logs/cef_log.txt'),
                path.join(steam_root, 'logs/connection_log.txt'),
                path.join(steam_root, 'logs/content_log.txt'),
                path.join(steam_root, 'logs/systemdisplaymanager.txt')
            );

            const logs_dir = path.join(steam_root, 'logs');
            try {
                for (var entry of fs.readdirSync(logs_dir)) {
                    if (entry.endsWith('.log') || entry.endsWith('.txt'))
                        log_paths.push(path.join(logs_dir, entry));
                }
            } catch (error) { }
        }

        return unique_paths(log_paths);
    }

    existingSteamLogPaths() {
        return this.steamLogPaths().filter((log_path) => {
            try {
                return fs.existsSync(log_path) && fs.statSync(log_path).isFile();
            } catch (error) {
                return false;
            }
        });
    }

    logSteamTails(prefix, line_count) {
        const logs = this.existingSteamLogPaths();
        if (!logs.length) {
            this.log(`${prefix}: no Steam log files found under bot home yet.`);
            return;
        }

        this.log(`${prefix}: found Steam logs: ${logs.join(', ')}`);
        for (var log_path of logs) {
            const tail = log_file_tail(log_path, line_count);
            if (tail)
                this.log(`${prefix} ${log_path}:\n${tail}`);
        }
    }

    markSteamReady(preferredSteamPath) {
        this.ensureSteamRootLinks();
        var candidates = [];
        if (preferredSteamPath)
            candidates.push(preferredSteamPath);
        candidates.push(
            path.join(this.home, '.steam/steam'),
            path.join(this.home, '.steam/debian-installation'),
            path.join(this.home, '.steam/root'),
            path.join(this.home, '.local/share/Steam')
        );

        for (var candidate of candidates) {
            if (candidate && fs.existsSync(candidate)) {
                this.steamPath = candidate;
                break;
            }
        }

        if (!this.steamPath)
            this.steamPath = path.join(this.home, '.steam/steam');

        this.steamApps = path.join(this.steamPath, 'steamapps');
        this.ensureSteamappsLink();

        const tf2_candidates = [
            process.env.TF2_PATH,
            '/opt/steamapps/common/Team Fortress 2',
            path.join(this.steamApps, 'common/Team Fortress 2'),
            path.join(USER.home, '.steam/steam/steamapps/common/Team Fortress 2'),
            path.join(USER.home, '.steam/debian-installation/steamapps/common/Team Fortress 2'),
            path.join(USER.home, '.local/share/Steam/steamapps/common/Team Fortress 2')
        ].filter(Boolean);
        this.tf2Path = tf2_candidates.find(tf2_install_ready) || tf2_candidates[tf2_candidates.length - 1];
        this.repairSteamSdk64();
        this.isSteamWorking = true;

        if (!this.steamReadyLogged) {
            this.log(`Steam ready, steam_path=${this.steamPath}, tf2_path=${this.tf2Path}`);
            this.steamReadyLogged = true;
        }
    }

    gameLaunchPath() {
        const host_steamapps_paths = [
            path.join(USER.home, '.steam/steam/steamapps'),
            path.join(USER.home, '.steam/debian-installation/steamapps'),
            path.join(USER.home, '.local/share/Steam/steamapps')
        ];

        for (var steamapps_path of host_steamapps_paths) {
            if (path_is_inside(this.tf2Path, steamapps_path)) {
                const relative_path = path.relative(steamapps_path, this.tf2Path);
                const opt_path = path.join('/opt/steamapps', relative_path);
                if (tf2_install_ready(opt_path))
                    return opt_path;
            }
        }

        const bot_relative_path = path.relative(this.home, this.tf2Path);
        if (path_is_inside(this.tf2Path, this.home))
            return path.join(USER.home, bot_relative_path);

        return this.tf2Path;
    }

    gameBinary() {
        return './tf_linux64';
    }

    steamRuntimeScript() {
        const runtime_dirs = ['ubuntu12_64/steam-runtime/run.sh', 'linux64/steam-runtime/run.sh'];
        for (var runtime_dir of runtime_dirs) {
            const host_runtime_path = path.join(this.steamPath, runtime_dir);
            if (fs.existsSync(host_runtime_path))
                return path.join(USER.home, path.relative(this.home, host_runtime_path));
        }

        return null;
    }

    gameRuntimePrefix() {
        const runtime_script = this.nativeSteam ? null : this.steamRuntimeScript();
        if (runtime_script)
            return `LD_LIBRARY_PATH="$(${bash_double_quote_escape(runtime_script)} printenv LD_LIBRARY_PATH):${GAME_LIBRARY_PATH}"`;

        return `LD_LIBRARY_PATH="\${LD_LIBRARY_PATH:-}:${GAME_LIBRARY_PATH}"`;
    }

    pollSteamReady() {
        const ready_patterns = [
            'System startup time:',
            'Logged in OK',
            'logged in OK',
            'Logged into Steam',
            'logged into Steam',
            'Steam client ready',
            'Steam signed in',
            'Refresh complete'
        ];

        for (var logPath of this.steamLogPaths()) {
            try {
                const log_text = fs.existsSync(logPath) ? fs.readFileSync(logPath, 'utf8') : '';
                if (ready_patterns.some((pattern) => log_text.includes(pattern))) {
                    this.markSteamReady(path.basename(logPath) === 'error.log' ? path.dirname(logPath) : null);
                    return true;
                }
            } catch (error) { }
        }

        return false;
    }

    spawnSteam() {
        var self = this;
        if (self.procFirejailSteam) {
            self.log('[ERROR] Steam is already running!');
            return;
        }

        if (!fs.existsSync(self.home)) {
            fs.mkdirSync(self.home);
            fs.chownSync(self.home, USER.uid, USER.uid);
        }
        self.ensureSteamRootLinks();

        var steambin = this.nativeSteam ? "steam-native" : "steam";

        self.procFirejailSteam = child_process.spawn(([this.shouldResetSteam, this.shouldResetSteam = 0][0] ? LAUNCH_OPTIONS_STEAM_RESET : LAUNCH_OPTIONS_STEAM)
            // Username
            .replace("%LOGIN%", shell_quote(self.account.login))
            // Password
            .replace("%PASSWORD%", shell_quote(self.account.password))
            // Name of the firejail jail
            .replace("%JAILNAME%", shell_quote(self.name))
            .replace("%LD_PRELOAD%", shell_quote(process.env.STEAM_LD_PRELOAD || ''))
            // XOrg Display
            .replace("%DISPLAY%", shell_quote(BOT_DISPLAY))
            .replace("%XAUTHORITY%", shell_quote(BOT_XAUTHORITY))
            // Network
            .replace("%NETWORK%", USER.SUPPORTS_FJ_NET ? `--net=${USER.interface}` : `--netns=catbotns${this.botid}`)
            // Home folder
            .replace("%HOME%", self.home.replace(/"/g, '\\"'))
            .replace("%STEAM%", steambin),
            self.spawnOptions);
        self.logSteam = fs.createWriteStream('./logs/' + self.name + '.steam.log');
        self.logSteam.on('error', (err) => { self.log(`error on logSteam pipe: ${err}`) });
        self.procFirejailSteam.stdout.pipe(self.logSteam);

        var tail_steam_err_logs = [];
        var steam_path = path.join(this.home, ".steam/steam");

        function processErrorLogs(text) {
            if (text.includes("System startup time:")) {
                self.markSteamReady(steam_path);

                for (var i = 0; i < tail_steam_err_logs.length; i++) {
                    if (tail_steam_err_logs[i]) {
                        tail_steam_err_logs[i].unwatch();
                    }
                }
                tail_steam_err_logs = [];
            }
            if (RegExp("Failed to load .*\.so: cannot open shared object file: .*").test(text)) {
                this.shouldRestart = true;
                this.shouldResetSteam = true;
            }
        }

        function registerDebianListener() {
            try {
                tail_steam_err_logs.push(new Tail(path.join(this.home, ".steam/debian-installation/error.log")));
                tail_steam_err_logs[tail_steam_err_logs.length-1].on('line', (data) => {
                    processErrorLogs.bind(this)(data);
                })
            } catch (error) {
                self.log("No debian-installation/error.log file found.");
                tail_steam_err_logs.pop();
            }
        }

        // FUCK YOU DEBIAN AND EVERYTHING YOU DO
        // FUCK YOU UBUNTU AND EVERYTHING YOU DO
        // WHY THE FUCK DO YOU NEED TO DO YOUR OWN THING EVERY TIME INSTEAD OF STICKING WITH THE FUCKKING STANDARDS/DEFAULTS

        // WHY THE FUCK DO YOU RESTRICT NETWORK FUNCTIONS FOR NON PRIVILEDGED FIREJAIL AND DIVERGING FROM THE DEFAULTS?
        // WHY THE FUCK DO YOU INSIST ON FORWARDING THE STEAM ERROR LOGS (THE ONLY USEFUL LOGS) TO A RANDOM ASS FILE FOR NO REASON?
        var isDebian = !fs.existsSync("/usr/bin/steam") && fs.existsSync("/usr/games/steam");


        self.procFirejailSteam.stderr.on("data", (data) => {
            var text = data.toString();
            processErrorLogs.bind(this)(text);
        });

        self.procFirejailSteam.stdout.on("data", (data) => {
            var text = data.toString();
            // Extend time if we are downloading updates.
            if (text.includes(" Downloading update (")) {
                self.time_steamWorking = Date.now() + TIMEOUT_STEAM_RUNNING;
            }
            if (text.includes("Error: You are missing the following 32-bit libraries, and Steam may not run:")
                || text.includes("Error: Couldn't set up the Steam Runtime. Are you running low on disk space?")) {
                this.shouldRestart = true;
                this.shouldResetSteam = true;
            }
            if (isDebian && text.includes("Running Steam on"))
                registerDebianListener.bind(this)();
        });
        self.procFirejailSteam.stderr.pipe(self.logSteam);
        self.procFirejailSteam.on('exit', self.handleSteamExit.bind(self));
        self.procFirejailSteam.on('exit', () => {
            for (var i = 0; i < tail_steam_err_logs.length; i++) {
                if (tail_steam_err_logs[i]) {
                    tail_steam_err_logs[i].unwatch();
                    tail_steam_err_logs[i] = null;
                }
            }
            tail_steam_err_logs = [];
        });
        self.log(`Launched ${steambin} (${self.procFirejailSteam.pid})`);
        self.log(`Steam log capture: ./logs/${self.name}.steam.log plus ${self.steamRootPaths().map((root_path) => path.join(root_path, 'logs')).join(', ')}`);
        self.emit('start-steam', self.procFirejailSteam.pid);
    }

    spawnGame() {
        var self = this;
        this.restarts++;

        var filename = `/tmp/.gl${makeid(6)}`;
        const source_library = cathook_textmode_library();
        fs.copyFileSync(source_library, filename);
        fs.chmodSync(filename, 0o755);
        self.gamePreloadLibrary = filename;

        clearSourceLockFiles();
        if (!self.repairSteamSdk64()) {
            self.shouldRestart = true;
            return;
        }

        const game_launch_path = self.gameLaunchPath();
        const game_binary = self.gameBinary();
        if (!fs.existsSync(path.join(self.tf2Path, 'tf_linux64'))) {
            self.log(`[ERROR] Missing tf_linux64 in ${self.tf2Path}`);
            self.shouldRestart = true;
            return;
        }

        const game_preload = preload_value(filename);
        self.log(`Launching TF2 from ${game_launch_path} binary=${game_binary} source_library=${source_library} attach_delay_seconds=${CATHOOK_ATTACH_DELAY_SECONDS} preload=${game_preload}`);
        self.procFirejailGame = child_process.spawn(LAUNCH_OPTIONS_GAME.replace("%GAMEPATH%", bash_double_quote_escape(game_launch_path))
            .replace("%RUNTIME_PREFIX%", self.gameRuntimePrefix())
            .replace("%GAME_BINARY%", game_binary)
            .replace("%CATHOOK_ATTACH_DELAY_SECONDS%", String(CATHOOK_ATTACH_DELAY_SECONDS))
            .replace("%BOT_ID%", String(self.botid))
            .replace("%BOT_NAME%", self.name)
            // Firejail jail name used by this users steam
            .replace("%JAILNAME%", self.name)
            // cathook
            .replace("%LD_PRELOAD%", `"${game_preload}"`)
            // XORG display
            .replace("%DISPLAY%", BOT_DISPLAY)
            .replace("%XAUTHORITY%", bash_double_quote_escape(BOT_XAUTHORITY)),
            [], self.spawnOptions);
        self.logGame = fs.createWriteStream('./logs/' + self.name + '.game.log');
        self.logGame.on('error', (err) => { self.log(`error on logGame pipe: ${err}`) });
        self.procFirejailGame.stdout.pipe(self.logGame);
        self.procFirejailGame.stderr.pipe(self.logGame);
        self.procFirejailGame.on('exit', self.handleGameExit.bind(self));
    }

    handleSteamExit(code, signal) {
        this.log(`Steam (${this.procFirejailSteam.pid}) exited with code ${code}, signal ${signal}`);
        const steam_log_tail = log_file_tail('./logs/' + this.name + '.steam.log', 25);
        if (steam_log_tail)
            this.log(`Steam log tail:\n${steam_log_tail}`);
        this.emit('exit-steam');

        this.isSteamWorking = false;
        this.time_steamwebhelper_cleanup = 0;
        this.steamwebhelper_cleanup_done = false;
        this.steamwebhelper_frozen_pid = -1;

        delete this.procFirejailSteam;
    }
    handleGameExit(code, signal) {
        const launcher_pid = this.procFirejailGame.pid;
        const game_pid = this.gamePid;
        this.log(`Game (${launcher_pid}) exited with code ${code}, signal ${signal}`);
        const game_log_tail = log_file_tail('./logs/' + this.name + '.game.log', 25);
        if (game_log_tail)
            this.log(`Game log tail:\n${game_log_tail}`);
        const crashed = (code !== null && code !== 0) || signal !== null;
        if (crashed && game_pid > 0 && GDB_CRASH_REPORTS)
            this.runGdbCrashReport(game_pid, code, signal);
        else
            this.removeGamePreloadLibrary();
        this.ipcState = null;
        this.ipcID = -1;
        this.ipcLastHeartbeat = 0;
        this.gameStarted = 0;
        this.gamePid = -1;
        this.time_game_launch = 0;
        this.time_gameCheck = 0;
        this.time_ipcState = 0;
        this.time_steamwebhelper_cleanup = 0;
        this.steamwebhelper_cleanup_done = false;
        if (this.shouldRun && this.procFirejailSteam)
            this.state = STATE.STARTING;
        delete this.procFirejailGame;
    }

    reset() {
        this.procFirejailSteam = null;
        this.procFirejailSteam = null;
        this.isSteamWorking = false;
        this.time_steamWorking = 0;
        this.time_steamAssumeReady = 0;
        this.time_game_launch = 0;
        this.time_gameCheck = 0;
        this.time_ipcState = 0;
        this.time_steamwebhelper_cleanup = 0;
        this.time_steamStatusLog = 0;
        this.shouldRestart = false;
        this.steamReadyLogged = false;
        this.steamwebhelper_cleanup_done = false;
        this.steamwebhelper_frozen_pid = -1;
        this.gamePid = -1;
        this.removeGamePreloadLibrary();
        // Needs to be reset here because resetting it in handleGameExit is not enough
        this.ipcState = null;
    }

    killSteam() {
        this.log('Killing steam');
        this.resume_steamwebhelper();
        // Firejail will handle smooth termination
        if (this.procFirejailSteam)
            this.procFirejailSteam.kill("SIGINT");
    }
    killGame() {
        this.log('Killing game');
        if (this.procFirejailGame)
            this.procFirejailGame.kill("SIGINT");
    }

    force_kill_runtime_processes(delay_ms) {
        const steam_pid = this.procFirejailSteam ? this.procFirejailSteam.pid : 0;
        const game_pid = this.procFirejailGame ? this.procFirejailGame.pid : 0;
        const run = () => {
            const killed_game = kill_process_tree(game_pid, 'SIGKILL');
            const killed_steam = kill_process_tree(steam_pid, 'SIGKILL');
            if (killed_game || killed_steam)
                this.log(`Force-killed runtime processes game=${killed_game} steam=${killed_steam}`);
        };

        if (delay_ms && delay_ms > 0)
            setTimeout(run, delay_ms);
        else
            run();
    }

    advance_account_generation(reason) {
        this.account_generation++;
        this.log(`Advancing to account generation ${this.account_generation}: ${reason}`);
        this.shouldRun = true;
        this.shouldRestart = true;
        this.account = null;
        this.ipcState = null;
        this.ipcID = -1;
        this.ipcLastHeartbeat = 0;
        this.killGame();
        this.killSteam();
        this.force_kill_runtime_processes(1000);
    }

    gdbLogPath() {
        return './logs/' + this.name + '.gdb.log';
    }

    appendGdbLog(text) {
        fs.mkdirSync('./logs', { recursive: true });
        fs.appendFileSync(this.gdbLogPath(), text);
    }

    removeGamePreloadLibrary() {
        if (!this.gamePreloadLibrary)
            return;

        const preload_library = this.gamePreloadLibrary;
        this.gamePreloadLibrary = null;
        try {
            fs.unlinkSync(preload_library);
            this.log(`Removed temp cathook preload ${preload_library}`);
        } catch (error) {
            if (error.code !== 'ENOENT')
                this.log(`[ERROR] Failed to remove temp cathook preload ${preload_library}: ${error.message}`);
        }
    }

    runGdbCrashReport(pid, code, signal) {
        if (!pid || pid <= 0)
            return;
        if (this.gdbSnapshotRunning) {
            this.appendGdbLog(`\n[${new Date().toISOString()}] skipped gdb crash report pid=${pid}; previous report still running\n`);
            this.removeGamePreloadLibrary();
            return;
        }

        this.gdbSnapshotRunning = true;
        this.log(`Writing gdb crash report pid=${pid} log=${this.gdbLogPath()}`);
        this.appendGdbLog(`\n========== ${new Date().toISOString()} crash pid=${pid} code=${code} signal=${signal} ==========\n`);

        const core_path = `/tmp/${this.name}.${pid}.core`;
        const binary_path = path.join(this.gameLaunchPath(), this.gameBinary());
        const script = [
            'set -u',
            `echo '[coredumpctl info]'`,
            `coredumpctl info ${pid} 2>&1 || true`,
            `echo '[coredumpctl dump]'`,
            `rm -f ${shell_quote(core_path)}`,
            `if coredumpctl dump ${pid} --output=${shell_quote(core_path)} >/dev/null 2>&1 && [ -s ${shell_quote(core_path)} ]; then`,
            `  gdb -n -q --batch ${shell_quote(binary_path)} ${shell_quote(core_path)} -ex 'set pagination off' -ex 'info threads' -ex 'info sharedlibrary' -ex 'thread apply all bt' 2>&1 || true`,
            `  rm -f ${shell_quote(core_path)}`,
            'else',
            `  echo 'no core dump available for pid ${pid}; live gdb attach skipped to avoid pausing a running/restarting game'`,
            'fi'
        ].join('\n');
        const gdb = child_process.spawn('sh', ['-lc', script], { uid: 0, gid: 0 });

        gdb.stdout.on('data', (data) => this.appendGdbLog(data.toString()));
        gdb.stderr.on('data', (data) => this.appendGdbLog(data.toString()));
        gdb.on('error', (error) => {
            this.appendGdbLog(`\n[gdb error] ${error.message}\n`);
            this.gdbSnapshotRunning = false;
            this.removeGamePreloadLibrary();
        });
        gdb.on('exit', (code, signal) => {
            this.appendGdbLog(`\n[gdb exit] code=${code} signal=${signal}\n`);
            this.gdbSnapshotRunning = false;
            this.removeGamePreloadLibrary();
        });
    }

    schedule_steamwebhelper_cleanup() {
        if (!STEAMWEBHELPER_CLEANUP_ENABLED || this.steamwebhelper_cleanup_done || this.time_steamwebhelper_cleanup)
            return;

        this.time_steamwebhelper_cleanup = Date.now() + STEAMWEBHELPER_CLEANUP_DELAY;
        this.log(`Steam webhelper cleanup scheduled in ${STEAMWEBHELPER_CLEANUP_DELAY / 1000} seconds.`);
    }

    run_steamwebhelper_cleanup_if_ready(time) {
        if (!STEAMWEBHELPER_CLEANUP_ENABLED || this.steamwebhelper_cleanup_done || !this.ipcState)
            return;

        if (!this.procFirejailSteam) {
            this.steamwebhelper_cleanup_done = true;
            return;
        }

        if (!this.time_steamwebhelper_cleanup) {
            this.schedule_steamwebhelper_cleanup();
            return;
        }

        if (time < this.time_steamwebhelper_cleanup)
            return;

        this.freeze_steamwebhelper_and_kill_children();
        this.steamwebhelper_cleanup_done = true;
        this.time_steamwebhelper_cleanup = 0;
    }

    freeze_steamwebhelper_and_kill_children() {
        const result = find_main_steamwebhelper(this.procFirejailSteam.pid);
        if (!result.main) {
            this.log('Steam webhelper cleanup skipped: no steamwebhelper process found in Steam process tree.');
            return;
        }

        try {
            process.kill(result.main.pid, 'SIGSTOP');
            this.steamwebhelper_frozen_pid = result.main.pid;
            this.log(`Froze main steamwebhelper pid=${result.main.pid} helpers_in_tree=${result.helper_count}.`);
        } catch (error) {
            this.log(`[ERROR] Failed to freeze main steamwebhelper pid=${result.main.pid}: ${error.message}`);
            return;
        }

        var killed_count = 0;
        const child_pids = [...result.child_pids].reverse();
        for (const child_pid of child_pids) {
            if (child_pid === this.gamePid)
                continue;

            try {
                process.kill(child_pid, 'SIGKILL');
                killed_count++;
            } catch (error) { }
        }

        this.log(`Killed ${killed_count} steamwebhelper child processes after IPC stayed connected.`);
    }

    resume_steamwebhelper() {
        if (this.steamwebhelper_frozen_pid <= 0)
            return;

        const frozen_pid = this.steamwebhelper_frozen_pid;
        this.steamwebhelper_frozen_pid = -1;
        try {
            process.kill(frozen_pid, 'SIGCONT');
            this.log(`Resumed frozen steamwebhelper pid=${frozen_pid}.`);
        } catch (error) { }
    }

    gameCheck() {
        try {
            var gamepid = Number.parseInt(child_process.execSync(`pgrep -P ${this.procFirejailGame.pid}`).toString().trim());
            this.gamePid = gamepid;

            var self = this;
            procfs(gamepid).stat(function (err, ret) {
                if (err) {
                    self.log("Error while getting stat.");
                } else {
                    self.startTime = ret.starttime;
                }
            })

            this.log(`Found game (${gamepid})`);
            this.emit('start-game', this.procFirejailGame.pid);
            clearSourceLockFiles();
        } catch (error) {
            this.log('[ERROR] Could not find running game!');
            return false;
        }
        return true;
    }

    // Apply current state
    update() {
        var time = Date.now();
        if (this.shouldRun && !this.shouldRestart) {
            if (this.procFirejailSteam) {
                if (!this.isSteamWorking) {
                    this.pollSteamReady();
                    if (this.isSteamWorking)
                        return;

                    if (!this.time_steamStatusLog || time > this.time_steamStatusLog) {
                        const remaining = this.time_steamWorking ? Math.max(0, Math.ceil((this.time_steamWorking - time) / 1000)) : 0;
                        this.log(`Waiting for Steam login/readiness, remaining_seconds=${remaining}`);
                        const logs = this.existingSteamLogPaths();
                        this.log(logs.length ? `Visible Steam logs: ${logs.join(', ')}` : 'Visible Steam logs: none yet');
                        this.time_steamStatusLog = time + 10000;
                    }

                    if (this.time_steamAssumeReady && time > this.time_steamAssumeReady) {
                        this.log('Steam readiness marker was not found; continuing because CAT_STEAM_READY_SECONDS fallback is enabled.');
                        this.markSteamReady();
                        return;
                    }

                    if (this.time_steamWorking && time > this.time_steamWorking) {
                        this.log('Steam login/readiness timed out.');
                        this.logSteamTails('Steam startup log tail', 12);
                        this.shouldRestart = true;
                        this.time_steamWorking = 0;
                    }
                    return;
                }
                else {
                    if (!this.procFirejailGame) {
                        if (!this.time_game_launch) {
                            this.time_game_launch = time + TIMEOUT_LAUNCH_GAME;
                            this.log(`Steam ready; launching game in ${TIMEOUT_LAUNCH_GAME / 1000} seconds.`);
                            return;
                        }
                        if (time < this.time_game_launch)
                            return;

                        this.time_game_launch = 0;
                        this.spawnGame();
                        this.state = STATE.WAITING;
                        this.time_gameCheck = time + TIMEOUT_START_GAME;
                    }
                    else {
                        if (this.time_gameCheck) {
                            if (time > this.time_gameCheck) {
                                if (!this.gameCheck()) {
                                    this.shouldRestart = true;
                                    this.time_gameCheck = Number.MAX_SAFE_INTEGER;
                                }
                                else {
                                    this.time_gameCheck = 0;
                                    this.time_ipcState = time + TIMEOUT_IPC_STATE;
                                }
                            }
                        }
                        else {
                            if (this.ipcState) {
                                this.time_ipcState = 0;
                                if (this.state != STATE.RUNNING) {
                                    this.state = STATE.RUNNING;
                                    this.gameStarted = time;
                                }
                                this.run_steamwebhelper_cleanup_if_ready(time);
                            } else if (this.time_ipcState && time > this.time_ipcState) {
                                this.killGame();
                                this.time_ipcState = 0;
                            }

                        }

                    }
                }
            }
            else {
                if (this.procFirejailGame) {
                    this.killGame();
                }
                else {
                    if (!this.account) {
                        this.state = STATE.NO_ACCOUNT;
                        this.log(`Preparing to restart with account generation ${this.account_generation}...`);
                        this.account = accounts.get(this.botid, this.account_generation);
                    }
                    if (this.account && module.exports.currentlyStartingGames < MAX_CONCURRENT_BOTS && module.exports.lastStartTime + DELAY_START_TIME < time) {
                        module.exports.lastStartTime = time;
                        module.exports.currentlyStartingGames++;
                        this.state = STATE.STARTING;
                        this.reset();
                        this.spawnSteam();
                        this.time_steamWorking = time + TIMEOUT_STEAM_RUNNING;
                        this.time_steamAssumeReady = time + TIMEOUT_STEAM_ASSUME_READY;
                    }
                }
            }
        }
        else {
            if (this.procFirejailGame) {
                this.killGame();
            }
            if (this.procFirejailSteam) {
                this.killSteam();
            }
            this.state = STATE.STOPPING;
            if (!this.procFirejailSteam && !this.procFirejailGame) {
                this.state = this.shouldRestart ? STATE.RESTARTING : STATE.INITIALIZED;
                this.shouldRestart = false;
            }
            if (this.account)
                this.account = null;
        }
    }

    restart() {
        if (this.shouldRun)
            this.shouldRestart = true;
        else
            this.shouldRun = true;
    }
    stop() {
        this.shouldRun = false;
    }
    full_stop() {
        this.stop();
        // Delete the network namespace for this bot
        if (!USER.SUPPORTS_FJ_NET && fs.existsSync(`/var/run/netns/catbotns${this.botid}`))
            child_process.execSync(`./scripts/ns-delete ${this.botid}`)
        return !(this.procFirejailGame || this.procFirejailSteam)
    }
}

module.exports.bot = Bot;
module.exports.currentlyStartingGames = 0;
module.exports.lastStartTime = 0;
module.exports.states = STATE;
module.exports.MAX_CONCURRENT_BOTS = MAX_CONCURRENT_BOTS;
Object.defineProperty(module.exports, 'MAX_CONCURRENT_BOTS', {
    get: function() { return MAX_CONCURRENT_BOTS; },
    set: function(value) { MAX_CONCURRENT_BOTS = value; }
});
