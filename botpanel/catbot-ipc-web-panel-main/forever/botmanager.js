const fs = require('fs');
const { clone } = require('underscore');
const Bot = require('./bot');
const BanTracker = require('./ban_tracker');

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
        this.updateTimeout = setTimeout(this.update.bind(this), 1000);
        this.stopping = false;
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

        // Add new bots
        try {
            this.enforceQuota();
        } catch (error) {
            this.log_exception('failed to enforce bot quota during update', error);
            this.quota = this.bots.length;
            this.wanted_quota = this.bots.length;
        }

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
                b.update();
            } catch (error) {
                this.stop_failed_bot(b, 'bot update failed', error);
            }
        }
        try {
            this.ban_tracker.update();
        } catch (error) {
            this.log_exception('ban tracker update failed', error);
        }

        if (!this.stopping) {
            try {
                self.cc.command('query', {}, function (data) {
                    try {
                        if (!data) {
                            self.updateTimeout = setTimeout(self.update.bind(self), 1000);
                            return;
                        }
                        self.updateTimeout = setTimeout(self.update.bind(self), 1000);
                        self.lastQuery = data;
                        if (data.result) {
                            const query_time = Date.now();
                            for (var q in data.result) {
                                const peer = data.result[q];
                                var best_bot = null;
                                var best_score = 0;
                                for (var b of self.bots) {
                                    const score = b.ipc_peer_match_score(q, peer);
                                    if (score > best_score) {
                                        best_bot = b;
                                        best_score = score;
                                    }
                                }

                                if (best_bot)
                                    best_bot.accept_ipc_peer(q, peer);
                            }
                            for (const bot of self.bots) {
                                const assigned_peer = bot.ipcID >= 0 ? data.result[String(bot.ipcID)] : null;
                                if (bot.ipcID >= 0 && (!assigned_peer || !assigned_peer.pid || !assigned_peer.heartbeat))
                                    bot.mark_ipc_peer_missing(query_time);
                            }
                        }
                    } catch (error) {
                        self.log_exception('IPC query callback failed', error);
                        self.updateTimeout = setTimeout(self.update.bind(self), 1000);
                    }
                });
            } catch (error) {
                self.log_exception('IPC query command failed', error);
                self.updateTimeout = setTimeout(self.update.bind(self), 1000);
            }
        }
        else if (self.bots.length)
            self.updateTimeout = setTimeout(self.update.bind(self), 1000);
    }
    enforceQuota() {
        if (this.bots.length == this.quota)
            this.quota = this.wanted_quota;
        while (this.bots.length < this.quota) {
            try {
                this.bots.push(new Bot.bot(this.bots.length));
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
        try {
            this.enforceQuota();
        } catch (error) {
            this.log_exception('failed to enforce bot quota', error);
            this.quota = this.bots.length;
            this.wanted_quota = this.bots.length;
        }
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
