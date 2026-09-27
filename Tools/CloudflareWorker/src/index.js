// ManoEngine Online Services - Cloudflare Worker 参照実装
// ------------------------------------------------------------
// ManoEngine の OnlineService が呼ぶ Endpoint をそのまま実装している(仕様書 54 項)。
// 秘密情報(管理鍵など)は必ずこちら側の Secret に置き、クライアントへ配らない(仕様書 49 項)。
// クライアントから来た値は無条件に信用せず、必ずこちらで検証する(仕様書 58 項)。

const kMaxScore = 1_000_000_000;              // これを超える Score は不正として拒否する。
const kMaxPlayerIdLength = 64;
const kMaxPlayerNameLength = 32;
const kMaxSaveBytesForD1 = 64 * 1024;         // これ以下は D1、超える場合は R2 へ置く(仕様書 42 項)。
const kMaxSaveBytes = 8 * 1024 * 1024;
const kRateLimitWindowSeconds = 60;
const kRateLimitMaxRequests = 60;             // 1 Player / 1 分あたりの上限。

function jsonResponse(bodyObject, statusCode = 200) {
	return new Response(JSON.stringify(bodyObject), {
		status: statusCode,
		headers: {
			'Content-Type': 'application/json; charset=utf-8',
			'Cache-Control': 'no-store',
		},
	});
}

function errorResponse(message, statusCode = 400) {
	return jsonResponse({ success: false, error: message }, statusCode);
}

// 環境は Header を優先し、無い場合は Body / Query を見る。テスト Score が本番へ入らないようにする(仕様書 52 項)。
function resolveEnvironment(request, payload, url) {
	const headerEnvironment = request.headers.get('X-ManoEngine-Environment');
	const bodyEnvironment = payload && typeof payload.environment === 'string' ? payload.environment : null;
	const queryEnvironment = url.searchParams.get('environment');
	const environment = (headerEnvironment || bodyEnvironment || queryEnvironment || 'development').toLowerCase();
	return environment === 'production' ? 'production' : 'development';
}

function resolveGameId(request, payload, url) {
	const headerGameId = request.headers.get('X-ManoEngine-Game-Id');
	const bodyGameId = payload && typeof payload.gameId === 'string' ? payload.gameId : null;
	const queryGameId = url.searchParams.get('gameId');
	const gameId = headerGameId || bodyGameId || queryGameId || '';
	return gameId.trim();
}

function isValidPlayerId(playerId) {
	if (typeof playerId !== 'string') {
		return false;
	}

	if (playerId.length === 0 || playerId.length > kMaxPlayerIdLength) {
		return false;
	}

	// 英数字・ハイフン・アンダースコアだけを許可する(不正 Player ID 対策)。
	return /^[A-Za-z0-9_-]+$/.test(playerId);
}

function sanitizePlayerName(playerName) {
	if (typeof playerName !== 'string' || playerName.length === 0) {
		return 'Player';
	}

	return playerName.replace(/[\u0000-\u001F<>]/g, '').slice(0, kMaxPlayerNameLength);
}

function normalizeScope(scope) {
	const allowedScopes = ['global', 'daily', 'weekly', 'season', 'custom'];
	const normalized = typeof scope === 'string' ? scope.toLowerCase() : 'global';
	return allowedScopes.includes(normalized) ? normalized : 'global';
}

// Daily / Weekly / Season のランキングを別 Bucket として持たせるためのキー。
function makeScopeBucket(scope, nowDate) {
	if (scope === 'daily') {
		return nowDate.toISOString().slice(0, 10);
	}

	if (scope === 'weekly') {
		const weekStart = new Date(nowDate);
		weekStart.setUTCDate(weekStart.getUTCDate() - weekStart.getUTCDay());
		return weekStart.toISOString().slice(0, 10);
	}

	if (scope === 'season') {
		return `${nowDate.getUTCFullYear()}-Q${Math.floor(nowDate.getUTCMonth() / 3) + 1}`;
	}

	return 'all';
}

// Client Key は「そのゲームからの Request か」を見るだけの公開鍵。
// これ 1 つで重要データを書き換えられない構造にする(仕様書 49 項)。
function verifyClientKey(request, environment, env) {
	const expectedKey = environment === 'production'
		? env.PRODUCTION_CLIENT_KEY
		: env.DEVELOPMENT_CLIENT_KEY;

	if (!expectedKey) {
		return true;  // 未設定の間は検証しない(開発初期用)。
	}

	return request.headers.get('X-ManoEngine-Client-Key') === expectedKey;
}

async function enforceRateLimit(env, gameId, playerId) {
	if (!env.ONLINE_KV) {
		return true;
	}

	const key = `rate:${gameId}:${playerId}`;
	const currentValue = await env.ONLINE_KV.get(key);
	const currentCount = currentValue ? Number.parseInt(currentValue, 10) : 0;

	if (currentCount >= kRateLimitMaxRequests) {
		return false;
	}

	await env.ONLINE_KV.put(key, String(currentCount + 1), {
		expirationTtl: kRateLimitWindowSeconds,
	});
	return true;
}

async function readJsonBody(request) {
	try {
		const text = await request.text();
		return text.length === 0 ? {} : JSON.parse(text);
	}
	catch (parseError) {
		return null;
	}
}

// ManoEngine 側は { gameId, playerId, playerName, environment, payload } の形で送る。
function extractPayload(bodyObject) {
	if (!bodyObject || typeof bodyObject !== 'object') {
		return {};
	}

	if (bodyObject.payload && typeof bodyObject.payload === 'object') {
		return bodyObject.payload;
	}

	return bodyObject;
}

async function handleLeaderboardSubmit(request, url, env) {
	const bodyObject = await readJsonBody(request);

	if (bodyObject === null) {
		return errorResponse('JSON を解析できません。');
	}

	const environment = resolveEnvironment(request, bodyObject, url);

	if (!verifyClientKey(request, environment, env)) {
		return errorResponse('Client Key が一致しません。', 401);
	}

	const gameId = resolveGameId(request, bodyObject, url);
	const playerId = bodyObject.playerId;
	const payload = extractPayload(bodyObject);
	const board = typeof payload.board === 'string' && payload.board.length > 0 ? payload.board : 'Score';
	const scope = normalizeScope(payload.scope);
	const score = Number(payload.score);

	if (gameId.length === 0) {
		return errorResponse('gameId が空です。');
	}

	if (!isValidPlayerId(playerId)) {
		return errorResponse('playerId の形式が不正です。');
	}

	if (!Number.isFinite(score) || score < 0 || score > kMaxScore) {
		return errorResponse('score が範囲外です。');
	}

	if (!(await enforceRateLimit(env, gameId, playerId))) {
		return errorResponse('Request が多すぎます。', 429);
	}

	const playerName = sanitizePlayerName(bodyObject.playerName);
	const scopeBucket = makeScopeBucket(scope, new Date());

	// 同じ Player / Board / Scope では高い Score だけを残す。
	await env.ONLINE_DB.prepare(
		`INSERT INTO leaderboard
		  (game_id, environment, board, scope, scope_bucket, player_id, player_name, score, updated_at)
		 VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, CURRENT_TIMESTAMP)
		 ON CONFLICT(game_id, environment, board, scope, scope_bucket, player_id)
		 DO UPDATE SET
		   score = MAX(score, excluded.score),
		   player_name = excluded.player_name,
		   updated_at = CURRENT_TIMESTAMP`)
		.bind(gameId, environment, board, scope, scopeBucket, playerId, playerName, Math.floor(score))
		.run();

	return jsonResponse({ success: true, board, scope, score: Math.floor(score) });
}

async function handleLeaderboardTop(request, url, env) {
	const environment = resolveEnvironment(request, null, url);
	const gameId = resolveGameId(request, null, url);
	const board = url.searchParams.get('board') || 'Score';
	const scope = normalizeScope(url.searchParams.get('scope'));
	const requestedCount = Number.parseInt(url.searchParams.get('count') || '100', 10);
	const entryCount = Number.isFinite(requestedCount) ? Math.min(Math.max(requestedCount, 1), 500) : 100;

	if (gameId.length === 0) {
		return errorResponse('gameId が空です。');
	}

	const scopeBucket = makeScopeBucket(scope, new Date());
	const queryResult = await env.ONLINE_DB.prepare(
		`SELECT player_id, player_name, score
		   FROM leaderboard
		  WHERE game_id = ?1 AND environment = ?2 AND board = ?3 AND scope = ?4 AND scope_bucket = ?5
		  ORDER BY score DESC, updated_at ASC
		  LIMIT ?6`)
		.bind(gameId, environment, board, scope, scopeBucket, entryCount)
		.all();

	const entries = (queryResult.results || []).map((row, rowIndex) => ({
		playerId: row.player_id,
		playerName: row.player_name,
		score: Number(row.score),
		rank: rowIndex + 1,
	}));

	return jsonResponse({ success: true, board, scope, entries });
}

async function handlePlayerGet(request, url, env) {
	const environment = resolveEnvironment(request, null, url);
	const gameId = resolveGameId(request, null, url);
	const playerId = url.searchParams.get('playerId') || request.headers.get('X-ManoEngine-Player-Id') || '';

	if (gameId.length === 0 || !isValidPlayerId(playerId)) {
		return errorResponse('gameId または playerId が不正です。');
	}

	const queryResult = await env.ONLINE_DB.prepare(
		`SELECT key, value FROM player_data
		  WHERE game_id = ?1 AND environment = ?2 AND player_id = ?3`)
		.bind(gameId, environment, playerId)
		.all();

	const values = {};

	for (const row of queryResult.results || []) {
		values[row.key] = row.value;
	}

	return jsonResponse({ success: true, playerId, values });
}

async function handlePlayerPost(request, url, env) {
	const bodyObject = await readJsonBody(request);

	if (bodyObject === null) {
		return errorResponse('JSON を解析できません。');
	}

	const environment = resolveEnvironment(request, bodyObject, url);

	if (!verifyClientKey(request, environment, env)) {
		return errorResponse('Client Key が一致しません。', 401);
	}

	const gameId = resolveGameId(request, bodyObject, url);
	const playerId = bodyObject.playerId;
	const payload = extractPayload(bodyObject);
	const values = payload.values;

	if (gameId.length === 0 || !isValidPlayerId(playerId)) {
		return errorResponse('gameId または playerId が不正です。');
	}

	if (!values || typeof values !== 'object') {
		return errorResponse('values がありません。');
	}

	if (!(await enforceRateLimit(env, gameId, playerId))) {
		return errorResponse('Request が多すぎます。', 429);
	}

	const statements = [];

	for (const [key, value] of Object.entries(values)) {
		if (typeof key !== 'string' || key.length === 0 || key.length > 64) {
			continue;
		}

		const valueText = typeof value === 'string' ? value : JSON.stringify(value);

		if (valueText.length > 4096) {
			continue;
		}

		statements.push(env.ONLINE_DB.prepare(
			`INSERT INTO player_data (game_id, environment, player_id, key, value, updated_at)
			 VALUES (?1, ?2, ?3, ?4, ?5, CURRENT_TIMESTAMP)
			 ON CONFLICT(game_id, environment, player_id, key)
			 DO UPDATE SET value = excluded.value, updated_at = CURRENT_TIMESTAMP`)
			.bind(gameId, environment, playerId, key, valueText));
	}

	if (statements.length === 0) {
		return errorResponse('保存できる values がありません。');
	}

	await env.ONLINE_DB.batch(statements);
	return jsonResponse({ success: true, savedCount: statements.length });
}

async function handleSaveGet(request, url, env) {
	const environment = resolveEnvironment(request, null, url);
	const gameId = resolveGameId(request, null, url);
	const playerId = url.searchParams.get('playerId') || request.headers.get('X-ManoEngine-Player-Id') || '';
	const slot = url.searchParams.get('slot') || 'default';

	if (gameId.length === 0 || !isValidPlayerId(playerId)) {
		return errorResponse('gameId または playerId が不正です。');
	}

	const row = await env.ONLINE_DB.prepare(
		`SELECT storage, data, r2_key FROM cloud_save
		  WHERE game_id = ?1 AND environment = ?2 AND player_id = ?3 AND slot = ?4`)
		.bind(gameId, environment, playerId, slot)
		.first();

	if (!row) {
		return jsonResponse({ success: false, error: 'セーブデータがありません。' }, 404);
	}

	if (row.storage === 'r2') {
		if (!env.ONLINE_R2) {
			return errorResponse('R2 Bucket が設定されていません。', 500);
		}

		const object = await env.ONLINE_R2.get(row.r2_key);

		if (!object) {
			return jsonResponse({ success: false, error: 'R2 上のセーブデータが見つかりません。' }, 404);
		}

		const text = await object.text();
		return jsonResponse({ success: true, slot, storage: 'r2', data: text });
	}

	return jsonResponse({ success: true, slot, storage: 'd1', data: row.data });
}

async function handleSavePost(request, url, env) {
	const bodyObject = await readJsonBody(request);

	if (bodyObject === null) {
		return errorResponse('JSON を解析できません。');
	}

	const environment = resolveEnvironment(request, bodyObject, url);

	if (!verifyClientKey(request, environment, env)) {
		return errorResponse('Client Key が一致しません。', 401);
	}

	const gameId = resolveGameId(request, bodyObject, url);
	const playerId = bodyObject.playerId;
	const payload = extractPayload(bodyObject);
	const slot = typeof payload.slot === 'string' && payload.slot.length > 0 ? payload.slot : 'default';
	const data = typeof payload.data === 'string' ? payload.data : '';

	if (gameId.length === 0 || !isValidPlayerId(playerId)) {
		return errorResponse('gameId または playerId が不正です。');
	}

	if (data.length === 0) {
		return errorResponse('data が空です。');
	}

	if (data.length > kMaxSaveBytes) {
		return errorResponse('data が大きすぎます。', 413);
	}

	if (!(await enforceRateLimit(env, gameId, playerId))) {
		return errorResponse('Request が多すぎます。', 429);
	}

	// 小さいデータは D1、大きいデータは R2 へ置く(仕様書 42 項 / 57 項)。
	if (data.length > kMaxSaveBytesForD1 && env.ONLINE_R2) {
		const r2Key = `${gameId}/${environment}/${playerId}/${slot}.save`;
		await env.ONLINE_R2.put(r2Key, data);
		await env.ONLINE_DB.prepare(
			`INSERT INTO cloud_save (game_id, environment, player_id, slot, storage, data, r2_key, updated_at)
			 VALUES (?1, ?2, ?3, ?4, 'r2', NULL, ?5, CURRENT_TIMESTAMP)
			 ON CONFLICT(game_id, environment, player_id, slot)
			 DO UPDATE SET storage = 'r2', data = NULL, r2_key = excluded.r2_key, updated_at = CURRENT_TIMESTAMP`)
			.bind(gameId, environment, playerId, slot, r2Key)
			.run();

		return jsonResponse({ success: true, slot, storage: 'r2', size: data.length });
	}

	await env.ONLINE_DB.prepare(
		`INSERT INTO cloud_save (game_id, environment, player_id, slot, storage, data, r2_key, updated_at)
		 VALUES (?1, ?2, ?3, ?4, 'd1', ?5, NULL, CURRENT_TIMESTAMP)
		 ON CONFLICT(game_id, environment, player_id, slot)
		 DO UPDATE SET storage = 'd1', data = excluded.data, r2_key = NULL, updated_at = CURRENT_TIMESTAMP`)
		.bind(gameId, environment, playerId, slot, data)
		.run();

	return jsonResponse({ success: true, slot, storage: 'd1', size: data.length });
}

async function handleSharedGet(request, url, env) {
	const gameId = resolveGameId(request, null, url);
	const key = url.searchParams.get('key') || '';

	if (gameId.length === 0 || key.length === 0) {
		return errorResponse('gameId または key が空です。');
	}

	// 高速に読むだけの値は KV へ置く(仕様書 56 項)。
	if (!env.ONLINE_KV) {
		return errorResponse('KV が設定されていません。', 500);
	}

	const value = await env.ONLINE_KV.get(`shared:${gameId}:${key}`);
	return jsonResponse({ success: value !== null, key, value: value || '' });
}

async function handleSharedPost(request, url, env) {
	const bodyObject = await readJsonBody(request);

	if (bodyObject === null) {
		return errorResponse('JSON を解析できません。');
	}

	const environment = resolveEnvironment(request, bodyObject, url);

	if (!verifyClientKey(request, environment, env)) {
		return errorResponse('Client Key が一致しません。', 401);
	}

	// 共有データの書き込みは管理鍵を持つ Request だけに許す。
	if (env.ADMIN_KEY && request.headers.get('X-ManoEngine-Admin-Key') !== env.ADMIN_KEY) {
		return errorResponse('共有データの書き込みには管理鍵が必要です。', 403);
	}

	const gameId = resolveGameId(request, bodyObject, url);
	const payload = extractPayload(bodyObject);
	const key = typeof payload.key === 'string' ? payload.key : '';
	const value = typeof payload.value === 'string' ? payload.value : '';

	if (gameId.length === 0 || key.length === 0) {
		return errorResponse('gameId または key が空です。');
	}

	if (!env.ONLINE_KV) {
		return errorResponse('KV が設定されていません。', 500);
	}

	await env.ONLINE_KV.put(`shared:${gameId}:${key}`, value);
	return jsonResponse({ success: true, key });
}

async function handleMessages(request, url, env) {
	const environment = resolveEnvironment(request, null, url);
	const gameId = resolveGameId(request, null, url);

	if (gameId.length === 0) {
		return errorResponse('gameId が空です。');
	}

	const queryResult = await env.ONLINE_DB.prepare(
		`SELECT id, title, body, published_at FROM game_message
		  WHERE game_id = ?1 AND environment = ?2
		  ORDER BY published_at DESC LIMIT 50`)
		.bind(gameId, environment)
		.all();

	return jsonResponse({ success: true, messages: queryResult.results || [] });
}

async function handleEvents(request, url, env) {
	const environment = resolveEnvironment(request, null, url);
	const gameId = resolveGameId(request, null, url);

	if (gameId.length === 0) {
		return errorResponse('gameId が空です。');
	}

	const queryResult = await env.ONLINE_DB.prepare(
		`SELECT id, name, state, starts_at, ends_at FROM global_event
		  WHERE game_id = ?1 AND environment = ?2
		  ORDER BY starts_at ASC LIMIT 50`)
		.bind(gameId, environment)
		.all();

	return jsonResponse({ success: true, events: queryResult.results || [] });
}

async function handleDaily(request, url, env) {
	const environment = resolveEnvironment(request, null, url);
	const gameId = resolveGameId(request, null, url);

	if (gameId.length === 0) {
		return errorResponse('gameId が空です。');
	}

	const today = new Date().toISOString().slice(0, 10);
	const row = await env.ONLINE_DB.prepare(
		`SELECT day, payload FROM daily_info
		  WHERE game_id = ?1 AND environment = ?2 AND day = ?3`)
		.bind(gameId, environment, today)
		.first();

	return jsonResponse({
		success: true,
		day: today,
		payload: row ? row.payload : '',
	});
}

async function handleMatch(request, url, env) {
	const environment = resolveEnvironment(request, null, url);
	const gameId = resolveGameId(request, null, url);
	const group = url.searchParams.get('group') || 'default';

	if (gameId.length === 0) {
		return errorResponse('gameId が空です。');
	}

	// 簡易マッチ情報。実対戦ではなく「今この Group に何人いるか」だけを返す(仕様書 36 項)。
	const row = await env.ONLINE_DB.prepare(
		`SELECT COUNT(*) AS player_count FROM player_data
		  WHERE game_id = ?1 AND environment = ?2 AND key = 'matchGroup' AND value = ?3`)
		.bind(gameId, environment, group)
		.first();

	return jsonResponse({
		success: true,
		group,
		playerCount: row ? Number(row.player_count) : 0,
	});
}

export default {
	async fetch(request, env) {
		const url = new URL(request.url);
		const path = url.pathname.replace(/\/+$/, '') || '/';

		try {
			if (path === '/health') {
				return jsonResponse({ success: true, service: 'ManoEngine Online', time: new Date().toISOString() });
			}

			if (path === '/leaderboard/submit' && request.method === 'POST') {
				return await handleLeaderboardSubmit(request, url, env);
			}

			if (path === '/leaderboard/top' && request.method === 'GET') {
				return await handleLeaderboardTop(request, url, env);
			}

			if (path === '/player') {
				if (request.method === 'GET') {
					return await handlePlayerGet(request, url, env);
				}

				if (request.method === 'POST') {
					return await handlePlayerPost(request, url, env);
				}
			}

			if (path === '/save') {
				if (request.method === 'GET') {
					return await handleSaveGet(request, url, env);
				}

				if (request.method === 'POST') {
					return await handleSavePost(request, url, env);
				}
			}

			if (path === '/shared') {
				if (request.method === 'GET') {
					return await handleSharedGet(request, url, env);
				}

				if (request.method === 'POST') {
					return await handleSharedPost(request, url, env);
				}
			}

			if (path === '/messages' && request.method === 'GET') {
				return await handleMessages(request, url, env);
			}

			if (path === '/events' && request.method === 'GET') {
				return await handleEvents(request, url, env);
			}

			if (path === '/daily' && request.method === 'GET') {
				return await handleDaily(request, url, env);
			}

			if (path === '/match' && request.method === 'GET') {
				return await handleMatch(request, url, env);
			}

			return errorResponse('Endpoint がありません。', 404);
		}
		catch (handlerError) {
			// 例外の詳細はクライアントへ返さず、Worker のログへ残す。
			console.error(handlerError);
			return errorResponse('サーバー側でエラーが発生しました。', 500);
		}
	},
};
