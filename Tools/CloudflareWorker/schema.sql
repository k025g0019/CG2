-- ManoEngine Online Services - D1 スキーマ(仕様書 55 項)
-- 構造化データは D1 に置く。Leaderboard / Player Data / ゲーム内メッセージ / イベント情報。
-- environment 列で開発用と本番用を必ず分ける(仕様書 52 項)。

CREATE TABLE IF NOT EXISTS leaderboard (
	game_id      TEXT NOT NULL,
	environment  TEXT NOT NULL,
	board        TEXT NOT NULL,
	scope        TEXT NOT NULL,
	scope_bucket TEXT NOT NULL,  -- daily なら日付、weekly なら週初日、season なら年+四半期。
	player_id    TEXT NOT NULL,
	player_name  TEXT NOT NULL,
	score        INTEGER NOT NULL,
	updated_at   TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
	PRIMARY KEY (game_id, environment, board, scope, scope_bucket, player_id)
);

CREATE INDEX IF NOT EXISTS idx_leaderboard_rank
	ON leaderboard (game_id, environment, board, scope, scope_bucket, score DESC);

CREATE TABLE IF NOT EXISTS player_data (
	game_id     TEXT NOT NULL,
	environment TEXT NOT NULL,
	player_id   TEXT NOT NULL,
	key         TEXT NOT NULL,
	value       TEXT NOT NULL,
	updated_at  TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
	PRIMARY KEY (game_id, environment, player_id, key)
);

CREATE TABLE IF NOT EXISTS cloud_save (
	game_id     TEXT NOT NULL,
	environment TEXT NOT NULL,
	player_id   TEXT NOT NULL,
	slot        TEXT NOT NULL,
	storage     TEXT NOT NULL,  -- 'd1' または 'r2'。
	data        TEXT,           -- storage='d1' の時だけ使う。
	r2_key      TEXT,           -- storage='r2' の時だけ使う。
	updated_at  TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
	PRIMARY KEY (game_id, environment, player_id, slot)
);

CREATE TABLE IF NOT EXISTS game_message (
	id           INTEGER PRIMARY KEY AUTOINCREMENT,
	game_id      TEXT NOT NULL,
	environment  TEXT NOT NULL,
	title        TEXT NOT NULL,
	body         TEXT NOT NULL,
	published_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_game_message_published
	ON game_message (game_id, environment, published_at DESC);

CREATE TABLE IF NOT EXISTS global_event (
	id          INTEGER PRIMARY KEY AUTOINCREMENT,
	game_id     TEXT NOT NULL,
	environment TEXT NOT NULL,
	name        TEXT NOT NULL,
	state       TEXT NOT NULL,
	starts_at   TEXT,
	ends_at     TEXT
);

CREATE INDEX IF NOT EXISTS idx_global_event_start
	ON global_event (game_id, environment, starts_at);

CREATE TABLE IF NOT EXISTS daily_info (
	game_id     TEXT NOT NULL,
	environment TEXT NOT NULL,
	day         TEXT NOT NULL,  -- YYYY-MM-DD。
	payload     TEXT NOT NULL,
	PRIMARY KEY (game_id, environment, day)
);
