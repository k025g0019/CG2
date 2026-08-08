# WaterRailShooter0817 仕様書

ソース: `EditorWaterRailShooterSceneBuilder.cpp` / `resources/scripts/WaterRailShooter0817/WaterRailShooter0817.cpp`（決定版）

## 1. ゲームフロー

レール進行率（normalized progress）で区間が切り替わる一本道レールシューター。

| Progress | 区間 | 内容 |
|---|---|---|
| 0.00 | Approach | 出撃。20mm 初期装備 |
| 0.08 | Battle A | 小型艇のウェーブ戦闘（全滅で進行許可） |
| 0.30 | Checkpoint 1 | Rail停止 → SHOP/LOADOUT → CONTINUE |
| 0.37 | Battle B | 小型艇 + ミサイル艇の混成戦（全滅で進行許可） |
| 0.52 | Storm | 荒天。Ocean へ RuntimeProperty で WaveHeight=2.25 / WindSpeed=26 を設定 |
| 0.64 | Checkpoint 2 | Rail停止 → SHOP/LOADOUT → CONTINUE |
| 0.73 | Boss 戦開始 | BossShip + 部位出現 |
| 0.80 | Boss 並走戦 | Rail Speed を 18 に減速し側面撃ち合い |
| — | 撃沈 | MISSION CLEAR → RESULT |

- 戦闘完了判定: Battle A クリアでないと Checkpoint 1 で Rail が Pause したまま動かない（また、Battle B 未クリアだと Checkpoint 2 で停止）。
- プレイヤー撃沈: SHIP LOST → RESTART（シーン再ロード）。
- CLEAR 後: RESULT ボタンで Result.scene へ遷移（DestroyedEnemyCount を SceneManager 浮動値で受け渡し）。

## 2. 操作

| 入力 | 動作 |
|---|---|
| LMB（押しっぱなし） | 現在装備武器で射撃 |
| R | リロード |
| E | 所有済み武器を次のスロットへ順送り |
| WASD | レール上の左右オフセット移動（+ 前後微調整） |

## 3. プレイヤー

| 項目 | 値 |
|---|---|
| 耐久 (Health) | 300 |
| 質量 | automaticMassFromCollider=true / density=165 / base mass=950 |
| 浮力 | Buoyancy あり。Hull=(4.2, 2.0, 10.0), waterDensity=1025, targetSubmersion=0.48, lateralDrag=3.4 |
| レール | Mode=1, Speed=30, Accel=8, Decel=10, LookAhead=22, Loop=false, StopAtEnd=true |
| コライダー | MeshCollider（Player.fbx） |
| Saveable | あり |

## 4. 武器 / ショップ

スロット: 0=20mm（初期所持）, 1=40mm, 2=Rocket, 3=Missile。

| 武器 | スロット | 価格 | 備考 |
|---|---|---|---|
| 20mm Machine Gun | MAIN | — | 初期装備 |
| 40mm Autocannon | MAIN | 300 | 20mm と MAIN を排他装備 |
| Rocket Pod | SECONDARY | 250 | 編隊攻撃 |
| Anti-Ship Missile | MISSILE | 400 | 追尾ミサイル |

- 購入は 1 回限り（OWNED 化。インベントリ個数管理なし）。
- 購入判定: SALVAGE >= 価格。不足時は残高維持（Log「SALVAGEが不足しています」）。
- 既購入武器の再購入は不可（Log「購入済みです」）。
- 未購入武器の装備は不可（Log「未購入の武器です」）。
- E キーは所有済みスロットのみ巡回。

## 5. SALVAGE

| 撃破対象 | 獲得 |
|---|---|
| Small Boat | +20 |
| Missile Boat | +40 |
| Boss Main Gun 部位 | +80 |
| Boss Missile Launcher 部位 | +80 |
| Boss Engine 部位 | +120 |
| Boss 本体撃沈 | +500 |

- 収支は "SALVAGE Counter"（GenericCounter + Saveable）に集約し HUD 表示。
- Checkpoint 保存時に SALVAGE / Owned40mm / OwnedRocket / OwnedMissile / EquippedSlot / CheckpointIndex を SaveSystem（water_rail_shooter_0817_progress）へ保存。

## 6. 敵

| 敵 | 耐久 | 攻撃 | 出現区間 |
|---|---|---|---|
| Small Boat | コンポーネント設定準拠 | 射撃（OnEnemyFire） | Battle A / B |
| Missile Boat | 同 | 追尾ミサイル（ProjectileEmitter Target 追尾） | Battle B |

- 撃破数は ObjectiveTracker へ反映（BattleA = 小型艇のみ、BattleB = 小型艇 + ミサイル艇の合計）。
- 敵ウェーブは EncounterController + ObjectPool で運用（SmallBoat Pool / MissileBoat Pool あり）。

## 7. Boss（大型艦）

構成: BossShip（本体）+ Boss Main Gun + Boss Missile Launcher + Boss Engine（各 HitZone + Health）。

| 部位 | 破壊効果 |
|---|---|
| Main Gun | 主砲停止（非表示）+80 |
| Missile Launcher | ミサイル停止（非表示）+80 |
| Engine | 最終 Phase へ（Rail Speed 14）+120 |

本体 Health 閾値による Phase:

| Phase | Rail Speed | 内容 |
|---|---|---|
| 1 | 24 | 接近 |
| 2 | 20 | 側面並走 |
| 3 | 16 | 艦尾 / Engine |

## 8. UI

- HUD: HULL（HP）/ SALVAGE / Reticle / BOSS HP（Boss 戦のみ）/ 操作説明
- SHOP UI（Checkpoint 時のみ表示）: BUY 40mm=300 / BUY Rocket=250 / BUY Missile=400、OWNED 表示、EQUIP ×4、CONTINUE
- Clear: MISSION CLEAR → RESULT ボタン
- Failed: SHIP LOST → RESTART ボタン

## 9. 設定変更場所

- ゲーム進行・価格・SALVAGE 量・Boss Phase: `resources/scripts/WaterRailShooter0817/WaterRailShooter0817.cpp`
- シーン構成（武器・敵・Boss 部位・UI・レール）: `Source/Engine/Editor/EditorWaterRailShooterSceneBuilder.cpp`
- シーン再生成: 起動引数 `--generate-water-rail-shooter-0817`（出力: `Assets/Scenes/WaterRailShooter_0817.scene`）

## 10. テストチェックリスト（現状の既知問題）

- [ ] 船がレール上を前進する（現在のシーンファイルは railPath=0 / railSpeed=0 の古い状態で、再生生成で直る想定）
- [ ] Battle A → Checkpoint 1 で Rail 停止 → SHOP 表示 → 購入 → EQUIP → CONTINUE で再開
- [ ] Battle B → Checkpoint 2 → Boss 戦 → 部位破壊 → 撃沈 → CLEAR → RESULT
- [ ] 被弾 → SHIP LOST → RESTART
- [ ] Wave スポーナーの連続スポーンでクラッシュしない（修正済み・要再確認）
- [ ] 荒天区間で波高・風速が変化する（Ocean への RuntimeProperty 反映）