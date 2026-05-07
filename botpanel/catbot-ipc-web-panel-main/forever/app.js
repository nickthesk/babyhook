const BotManager = require('./botmanager');
const config = require('./config');
const Bot = require('./bot');
const steam_id = require('../steam_id');

var manager = null;

function parse_config_value(value) {
	if (value === 'true')
		return true;
	if (value === 'false')
		return false;
	if (value === '1')
		return true;
	if (value === '0')
		return false;
	return value;
}

function bot_steam_state(bot) {
	const steamid32 = bot.ipcState && bot.ipcState.friendid ? String(bot.ipcState.friendid) : null;
	const steamid64 = steam_id.account_id32_to_steamid64(steamid32);
	const profile_url = steamid64 ? `https://steamcommunity.com/profiles/${steamid64}` : null;
	return { steamid32, steamid64, profile_url };
}

function parse_positive_integer(value) {
	const text = String(value).trim();
	if (!/^[0-9]+$/.test(text))
		return null;

	const number = Number.parseInt(text, 10);
	if (!Number.isSafeInteger(number) || number < 1)
		return null;

	return number;
}

class app {
	constructor(app, cc) {
		if (process.getuid() != 0) {
			console.log('[FATAL] Bot manager needs superuser privileges, please restart as root');
			process.exit(1);
		}
		if (manager) {
			console.log('[FATAL] Initialized function for bot manager called twice');
			process.exit(1);
		} else {
			manager = new BotManager(cc);
		}

		this.manager = manager;

		app.post('/api/config/:option/:value', (req, res) => {
			if (!config.hasOwnProperty(req.params.option))
				res.status(404).end();
			else {
				console.log(`Switching ${req.params.option} to ${req.params.value}`)
				config[req.params.option] = parse_config_value(req.params.value);
				res.status(200).end('' + config[req.params.option]);
			}
		});
		app.get('/api/config/:option', (req, res) => {
			if (!config.hasOwnProperty(req.params.option))
				res.status(404).end();
			else
				res.status(200).end('' + config[req.params.option]);
		});

		app.get('/api/list', function (req, res) {
			var result = {};
			result.quota = manager.quota;
			result.count = manager.bots.length;
			result.bots = {};
			for (var i of manager.bots) {
				result.bots[i.name] = {
					user: i.user
				};
			}
			res.send(result);
		});

		app.get('/api/state', function (req, res) {
			var result = { bots: {} };
			for (var i of manager.bots) {
				const steam_state = bot_steam_state(i);
				result.bots[i.name] = {
					ipc: i.ipcState,
					restarts: i.restarts,
					ipcID: i.ipcID,
					state: i.state,
					started: i.gameStarted,
					pid: i.game,
					steamid32: steam_state.steamid32,
					steamid64: steam_state.steamid64,
					profile_url: steam_state.profile_url,
					ban_tracker: manager.ban_tracker.status_for_bot(i)
				};
			}
			res.send(result);
		});

		app.get('/api/bot/:bot/restart', function (req, res) {
			if (req.params.bot === "all") {
				for (var bot of manager.bots)
					bot.restart();
				res.status(200).end();
				return;
			}
			var bot = manager.bot(req.params.bot);
			if (bot) {
				bot.restart();
				res.status(200).end();
			} else {
				res.status(400).send({
					'error': 'Bot does not exist'
				})
			}
		});

		app.get('/api/bot/:bot/terminate', function (req, res) {
			if (req.params.bot === "all") {
				for (var bot of manager.bots)
					bot.stop();
				res.status(200).end();
				return;
			}
			var bot = manager.bot(req.params.bot);
			if (bot) {
				bot.stop();
				res.status(200).end();
			} else {
				res.status(400).send({
					'error': 'Bot does not exist'
				})
			}
		});

		app.get('/api/quota/:quota', function (req, res) {
			manager.setQuota(req.params.quota);
			res.send({
				quota: manager.quota
			});
		});

		const set_concurrent_limit = function (raw_value, res) {
			const value = parse_positive_integer(raw_value);
			if (value === null) {
				res.status(400).send({ error: 'Invalid value' });
				return;
			}

			Bot.MAX_CONCURRENT_BOTS = value;
			res.status(200).send({ value: Bot.MAX_CONCURRENT_BOTS });
		};

		app.post('/api/concurrent', function (req, res) {
			set_concurrent_limit(req.body.value, res);
		});

		app.get('/api/concurrent/:value', function (req, res) {
			set_concurrent_limit(req.params.value, res);
		});

		app.get('/api/concurrent', function (req, res) {
			res.status(200).send({ value: Bot.MAX_CONCURRENT_BOTS });
		});
	}

	stop()
	{
		this.manager.stop();
	}
}

exports.Forever = app;
