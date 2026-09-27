# ManoEngine Online Services (Cloudflare Worker)

ManoEngine の `OnlineService` がそのまま呼べる Cloudflare Worker の参照実装です。
ゲーム側は HTTP を直接書かず、Engine の `OnlineService` / `Leaderboard` / `CloudSave` API を使います。

## 構成

```
Game (ManoEngine)
  ↓ HTTPS
Cloudflare Workers (src/index.js)
  ↓
D1 (構造化) / KV (高速Key-Value) / R2 (大容量)
```

| ストレージ | 用途 |
|-----------|------|
| D1 | Leaderboard、Player Data、ゲーム内メッセージ、イベント情報、64KB 以下の Cloud Save |
| KV | 共有データ、Rate Limit カウンタ、簡易キャッシュ |
| R2 | 64KB を超える Cloud Save、Replay、ユーザー生成データ |

## Endpoint

| Method | Path | 用途 |
|--------|------|------|
| GET | `/health` | 疎通確認 |
| POST | `/leaderboard/submit` | Score 送信 |
| GET | `/leaderboard/top` | 上位取得 (`board` / `scope` / `count` / `gameId`) |
| GET | `/player` | Player Data 取得 |
| POST | `/player` | Player Data 保存 |
| GET | `/save` | Cloud Save 取得 (`slot`) |
| POST | `/save` | Cloud Save 保存 |
| GET | `/shared` | 共有データ取得 |
| POST | `/shared` | 共有データ書き込み（管理鍵が必要） |
| GET | `/messages` | ゲーム内メッセージ一覧 |
| GET | `/events` | グローバルイベント一覧 |
| GET | `/daily` | デイリー情報 |
| GET | `/match` | 簡易マッチ情報 |

`scope` は `global` / `daily` / `weekly` / `season` / `custom`。

## Header

Engine 側が自動で付けます。

| Header | 内容 |
|--------|------|
| `X-ManoEngine-Game-Id` | Project Settings の Game ID |
| `X-ManoEngine-Environment` | `development` / `production` |
| `X-ManoEngine-Client-Key` | Project Settings の Client Key（公開鍵） |
| `X-ManoEngine-Player-Id` | 実行中の Player ID |

Client Key は「そのゲームからの Request か」を見るだけの公開鍵です。
これ 1 つで重要データを書き換えられない構造にしてあります（共有データ書き込みは別途 `ADMIN_KEY` が必要）。

## セットアップ

```bash
npm install -g wrangler
wrangler login

# 開発用リソースを作る
wrangler d1 create manoengine-online-dev
wrangler kv namespace create ONLINE_KV
wrangler r2 bucket create manoengine-online-dev

# wrangler.toml の database_id / kv id を、作成時に表示された値へ書き換える

# スキーマ適用
wrangler d1 execute manoengine-online-dev --file=./schema.sql

# 公開鍵・管理鍵を登録（ファイルへは書かない）
wrangler secret put DEVELOPMENT_CLIENT_KEY
wrangler secret put ADMIN_KEY

# ローカル実行 / デプロイ
wrangler dev
wrangler deploy
```

本番は `--env production` を付けて、Database も KV も R2 も別リソースにします。

```bash
wrangler d1 create manoengine-online-prod
wrangler d1 execute manoengine-online-prod --file=./schema.sql
wrangler secret put PRODUCTION_CLIENT_KEY --env production
wrangler deploy --env production
```

## ManoEngine 側の設定

Inspector の「プロジェクト設定 → Online Services」で次を設定します。

- Enabled
- Provider（Cloudflare）
- API Base URL（本番）
- Development Base URL（開発）
- Game ID
- Client Key
- Environment（Development / Production）

## サーバー側検証（仕様書 58 項）

クライアントから来た値は無条件に信用しません。Worker 側で次を検査しています。

- 異常 Score（負値、上限超過、非数）
- 大量 Request（Player 単位の Rate Limit、KV で 1 分 60 回）
- 不正 Player ID（長さと使用文字）
- Game ID の空値
- Client Key 不一致
- Cloud Save サイズ上限
