const fs = require('fs');
const { clone } = require('underscore');
const Bot = require('./bot');
const BanTracker = require('./ban_tracker');
const steam_id = require('../steam_id');

class BotManager {
    constructor(cc) {
        var self = this;
        try {
            fs.mkdirSync('logs');
        } catch (e) { }
        this.bots = [];
        this.cc = cc;
        this.quota = 0;
        this.wanted_quota = 0;
        this.lastQuery = {};
        this.ban_tracker = new BanTracker(this);
        this.updateTimeout = null;
        this.query_in_progress = false;
        this.stopping = false;
        this.recover_existing_until = 0;
        this.list_json = '{"quota":0,"count":0,"bots":{}}';
        this.state_json = '{"bots":{}}';
        this.schedule_update(1000);
    }
    schedule_update(delay) {
        if (this.updateTimeout)
            return;

        this.updateTimeout = setTimeout(() => {
            this.updateTimeout = null;
            this.update();
        }, delay);
    }
    rebuild_snapshots() {
        const list = {
            quota: this.quota,
            count: this.bots.length,
            bots: {}
        };
        const state = { bots: {} };

        for (const bot of this.bots) {
            list.bots[bot.name] = {
                user: bot.user
            };

            const steamid32 = bot.ipcState && bot.ipcState.friendid ? String(bot.ipcState.friendid) : null;
            const steamid64 = steamid32 ? steam_id.account_id32_to_steamid64(steamid32) : null;
            state.bots[bot.name] = {
                ipc: bot.ipcState,
                restarts: bot.restarts,
                ipcID: bot.ipcID,
                state: bot.state,
                started: bot.gameStarted,
                pid: bot.game,
                steamid32: steamid32,
                steamid64: steamid64,
                profile_url: steamid64 ? `https://steamcommunity.com/profiles/${steamid64}` : null,
                ban_tracker: this.ban_tracker.status_for_bot(bot)
            };
        }

        this.list_json = JSON.stringify(list);
        this.state_json = JSON.stringify(state);
    }
    get_list_json() {
        return this.list_json;
    }
    get_state_json() {
        return this.state_json;
    }
    log_exception(context, error) {
        const stack = error && error.stack ? error.stack : String(error);
        console.log(`[ERROR] ${context}: ${stack}`);
    }
    stop_failed_bot(bot, context, error) {
        this.log_exception(`${context} for ${bot ? bot.name : 'unknown bot'}`, error);
        if (!bot)
            return;

        try {
            bot.shouldRun = false;
            bot.shouldRestart = false;
            bot.killGame();
            bot.killSteam();
        } catch (stop_error) {
            this.log_exception(`failed to stop ${bot.name} after exception`, stop_error);
        }
    }
    update() {
        var self = this;
        Bot.currentlyStartingGames = 0;
        Bot.currentlyBootingSteam = 0;

        try {
            this.enforceQuota();
        } catch (error) {
            this.log_exception('failed to enforce bot quota during update', error);
            this.quota = this.bots.length;
            this.wanted_quota = this.bots.length;
        }

        if (!this.quota && !this.bots.length) {
            this.rebuild_snapshots();
            if (!this.stopping)
                self.schedule_update(5000);
            return;
        }

        const process_table = Bot.read_process_table();
        const children_by_parent = Bot.build_process_children_by_parent(process_table);
        for (var i = self.bots.length - 1; i >= 0; i--) {
            var b = self.bots[i];
            if (b.start_slot_in_use())
                Bot.currentlyStartingGames++;
            if (b.steam_boot_in_progress())
                Bot.currentlyBootingSteam++;
            if (i + 1 > this.quota) {
                let stopped = false;
                try {
                    stopped = b.full_stop();
                } catch (error) {
                    this.stop_failed_bot(b, 'bot full stop failed', error);
                }
                if (stopped) {
                    self.bots.splice(i, 1);
                    continue;
                }
            }
            try {
                if (this.recover_existing_until && Date.now() < this.recover_existing_until && !b.procFirejailSteam && !b.procFirejailGame) {
                    b.adopt_runtime_processes(process_table, children_by_parent);
                    if (!b.procFirejailSteam && !b.procFirejailGame)
                        continue;
                }

                b.update(process_table, children_by_parent);
            } catch (error) {
                this.stop_failed_bot(b, 'bot update failed', error);
            }
        }
        try {
            this.ban_tracker.update();
        } catch (error) {
            this.log_exception('ban tracker update failed', error);
        }

        if (!this.stopping && self.bots.length && !this.query_in_progress) {
            this.query_in_progress = true;
            try {
                self.cc.command('query', {}, function (data) {
                    self.query_in_progress = false;
                    try {
                        if (!data)
                            return;
                        self.lastQuery = data;
                        if (data.result) {
                            const query_time = Date.now();
                            const query_process_table = Bot.read_process_table();
                            const query_children_by_parent = Bot.build_process_children_by_parent(query_process_table);
                            const bots_by_owned_pid = new Map();
                            const bots_by_ipc_id = new Map();
                            for (const bot of self.bots) {
                                if (bot.ipcID >= 0)
                                    bots_by_ipc_id.set(String(bot.ipcID), bot);
                                bot.index_owned_processes(query_process_table, query_children_by_parent, bots_by_owned_pid);
                            }
                            for (var q in data.result) {
                                const peer = data.result[q];
                                let best_bot = null;
                                let best_score = 0;
                                const assigned_bot = bots_by_ipc_id.get(String(q));
                                if (assigned_bot) {
                                    best_bot = assigned_bot;
                                    best_score = assigned_bot.ipc_peer_match_score(q, peer, query_process_table, bots_by_owned_pid.get(peer && peer.pid), query_children_by_parent);
                                }
                                if (!best_bot || best_score <= 0) {
                                    const owner_bot = peer && peer.pid ? bots_by_owned_pid.get(peer.pid) : null;
                                    if (owner_bot) {
                                        best_bot = owner_bot;
                                        best_score = owner_bot.ipc_peer_match_score(q, peer, query_process_table, owner_bot, query_children_by_parent);
                                    }
                                }
                                if (!best_bot || best_score <= 0) {
                                    const owner_bot = peer && peer.pid ? bots_by_owned_pid.get(peer.pid) : null;
                                    for (var b of self.bots) {
                                        const score = b.ipc_peer_match_score(q, peer, query_process_table, owner_bot || null, query_children_by_parent);
                                        if (score > best_score) {
                                            best_bot = b;
                                            best_score = score;
                                        }
                                    }
                                }

                                if (best_bot)
                                    best_bot.accept_ipc_peer(q, peer, query_process_table, query_children_by_parent);
                            }
                            for (const bot of self.bots) {
                                const assigned_peer = bot.ipcID >= 0 ? data.result[String(bot.ipcID)] : null;
                                if (bot.ipcID >= 0 && (!assigned_peer || !assigned_peer.pid || !assigned_peer.heartbeat))
                                    bot.mark_ipc_peer_missing(query_time);
                            }
                            self.rebuild_snapshots();
                        }
                    } catch (error) {
                        self.log_exception('IPC query callback failed', error);
                    }
                });
            } catch (error) {
                self.query_in_progress = false;
                self.log_exception('IPC query command failed', error);
            }
        }

        this.rebuild_snapshots();
        if (!this.stopping || self.bots.length)
            self.schedule_update(1000);
    }
    enforceQuota() {
        while (this.bots.length < this.quota) {
            try {
                const bot = new Bot.bot(this.bots.length);
                bot.shouldRun = true;
                this.bots.push(bot);
            } catch (error) {
                this.log_exception(`failed to create bot b${this.bots.length}`, error);
                this.quota = this.bots.length;
                this.wanted_quota = this.bots.length;
                return false;
            }
        }
        return true;
    }
    bot(name) {
        for (var bot of this.bots) {
            if (bot.name == name) return bot;
        }
        return null;
    }
    setQuota(quota) {
        quota = parseInt(quota);
        if (!isFinite(quota) || isNaN(quota)) {
            return;
        }
        this.wanted_quota = quota;
        this.quota = quota;
        this.recover_existing_until = Date.now() + 30000;
        this.enforceQuota();
        this.rebuild_snapshots();
        this.schedule_update(0);
    }
    getJSONStatus() {
        var result = {};
        return result;
    }
    stop() {
        this.setQuota(0);
        this.stopping = true;
    }
}

module.exports = BotManager;
