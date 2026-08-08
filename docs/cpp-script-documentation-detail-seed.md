# CG2 C++ スクリプト詳細下書き

このファイルは、使用者向けサイトの「C++ スクリプト」ページに載せるための下書きである。
実在する API は `Source/Engine/Core/EditorScriptApi.h` を基準にする。

## C++ スクリプトでできること

C++ スクリプトは、GameObject に付けた DLL を Play 中に読み込み、使用者が書いた処理を呼び出す仕組みである。

主に次のことを行う。

- GameObject の位置、回転、スケールを読む。
- GameObject の位置、回転、スケールを書き換える。
- Keyboard の状態を読む。
- Input Action の値を読む。
- Rigidbody の速度を読む。
- Rigidbody の速度や回転速度を書き換える。
- Rigidbody に Force、Impulse、Torque を加える。
- Collision / Trigger のイベントを受け取る。
- AI Sensor の結果を読む。
- Material の状態を読む。
- Animation の状態を読む。
- UI や Input Action から任意関数を呼ぶ。
- Inspector に C++ 側の公開変数を表示する。
- Scene を Path または Build Index で切り替える。
- GameObject を検索し、有効状態を切り替える。
- RailFollower を停止、再開、移動、切り替えする。
- Animation Parameter、Animation Action、Effect を操作する。
- Wave、Timeline、Threshold などの汎用Componentから名前付きActionを受け取る。

## 必要な構成

C++ スクリプトを使うには、最低限次が必要である。

| 必要なもの | 内容 |
| --- | --- |
| Script Component | GameObject に追加する C++ スクリプト Component。 |
| C++ ソース | `EditorScript_Load` などを export する `.cpp`。 |
| Header | Script の状態や関数を宣言する `.h`。 |
| Build Script | Debug / Release DLL を作る `.bat`。 |
| DLL | Play 中に Engine が読み込む Script 本体。 |
| DLL Path | Inspector の Script Component に設定する DLL の場所。 |

## DLL ライフサイクル関数

これらは Engine から呼ばれる export 関数である。
使用者は名前と引数を変えてはいけない。

| 関数 | 呼ばれるタイミング | 主な用途 |
| --- | --- | --- |
| `EditorScript_Load` | DLL 読み込み時に 1 回。 | API Version 確認、`runtimeApi` 保存。 |
| `EditorScript_Unload` | DLL 解放時に 1 回。 | DLL 全体の参照を解放。Component実体は先にEngineが破棄する。 |
| `EditorScript_CreateInstance` | Script Component開始時にComponentごとに1回。 | 独立したC++クラス実体を生成する。 |
| `EditorScript_DestroyInstance` | Script Component終了時に生成回数と同じ回数。 | `CreateInstance`が返した実体を破棄する。 |
| `EditorScript_StartInstance` | 実体生成とInspector値反映の後に1回。 | 初期化、ログ、初期値設定。 |
| `EditorScript_UpdateInstance` | ActiveなComponentへ毎フレーム。 | 入力、Transform操作、通常更新。 |
| `EditorScript_FixedUpdateInstance` | ActiveなComponentへ固定時間更新。 | 物理Force、Impulse、Torque。 |
| `EditorScript_OnPhysicsEventInstance` | 所有ObjectのCollision / Trigger発生時。 | 当たり判定イベント処理。 |
| `EditorScript_OnAnimationEventInstance` | Animation ClipのEvent時刻通過時。 | Event名、文字列、数値を受けて演出や処理を起動。 |
| `EditorScript_StopInstance` | Play停止またはScript停止時。 | 終了処理。実体のdeleteは`DestroyInstance`で行う。 |
| `EditorScript_InvokeActionInstance` | UI / Input Action Eventから呼ばれた時。 | 選択されたComponent実体の名前付き処理を実行する。 |
| `EditorScript_GetActionCount` | InspectorがDLLのAction候補を取得する時。 | `BindAction`済み名称数を返す。 |
| `EditorScript_GetActionName` | Inspectorが候補名を列挙する時。 | Indexに対応するAction名を返す。 |

## Inspector 公開変数用関数

これらは C++ 側の変数を Inspector に出すための関数である。

| 関数 | 目的 |
| --- | --- |
| `EditorScript_GetFieldCount` | Inspector に出す公開変数の数を返す。 |
| `EditorScript_GetFieldDescriptor` | 変数名、表示名、型、初期値、範囲を返す。 |
| `EditorScript_GetFieldValueInstance` | Component実体の現在値を返す。 |
| `EditorScript_SetFieldValueInstance` | Inspectorで変更された値を対象Component実体へ反映する。 |

対応する型は次である。

旧`EditorScript_Start`、`EditorScript_Update`、`EditorScript_GetFieldValue`などの`gameObjectId`版は、既存DLLを動かす互換ABIとして残る。新規生成Scriptでは使用しない。

| 型 | enum | 用途 |
| --- | --- | --- |
| Bool | `EditorScriptFieldTypeBool` | ON / OFF。 |
| Int32 | `EditorScriptFieldTypeInt32` | 個数、ID、選択番号。 |
| Float | `EditorScriptFieldTypeFloat` | 速度、強さ、時間、距離。 |
| Vector2 | `EditorScriptFieldTypeVector2` | 2D方向、画面座標。 |
| Vector3 | `EditorScriptFieldTypeVector3` | 位置、方向、速度。 |
| String | `EditorScriptFieldTypeString` | 名前、Path、Command。 |
| GameObject | `EditorScriptFieldTypeGameObject` | Inspector で Scene 内 GameObject を参照する。 |
| Scene Asset | `EditorScriptFieldTypeSceneAsset` | Inspector で `.scene`を参照する。 |

高水準基底クラス`EditorNativeScript`では、次の関数で登録する。

| 関数 | Inspector表示 |
| --- | --- |
| `ExposeBool` | Checkbox。 |
| `ExposeInt32` | 整数入力。範囲とStepを指定できる。 |
| `ExposeFloat` | Float入力。範囲とStepを指定できる。 |
| `ExposeVector2` | 2成分入力。 |
| `ExposeVector3` | 3成分入力。 |
| `ExposeString` | 文字列入力。 |
| `ExposeGameObject` | Scene内GameObject候補。IDまたは`GameObject`へ保持する。 |
| `ExposeScene` | Project内`.scene`候補。Pathを`std::string`へ保持する。 |

## Runtime API 関数一覧

`EditorScript_Load` で受け取った `runtimeApi` から呼ぶ関数である。

### Log

```cpp
runtimeApi->Log("message");
```

- 用途: Console に文字列を出す。
- 呼ぶ場所: `Start`、デバッグ時、エラー時。
- 注意: `Update` や `OnPhysicsEvent` で毎フレーム大量に出さない。

### IsKeyDown

```cpp
bool isDown = runtimeApi->IsKeyDown(Key::W);
```

- 用途: キーが押されている間 true。
- 呼ぶ場所: `Update`。
- 注意: `Key::W` は W キーであり、前進という意味ではない。前進に使うかは使用者が決める。

### IsKeyPressed

```cpp
bool isPressed = runtimeApi->IsKeyPressed(Key::Space);
```

- 用途: 押した瞬間を判定する。
- 呼ぶ場所: `Update`。
- 注意: 実装上の「押した瞬間」が前フレーム比較かどうかを確認して説明する。

### GetActionVector2

```cpp
EditorScriptVector2 move = runtimeApi->GetActionVector2(gameObjectId, "Player", "Move");
```

- 用途: Input Action の Vector2 値を読む。
- 必要条件: Input Action Asset、Action Map、Action、Binding、PlayerInput。
- 呼ぶ場所: `Update`。
- 注意: Action Map 名と Action 名が一致していないと 0 が返る。

### IsActionPressed

```cpp
bool isPressed = runtimeApi->IsActionPressed(gameObjectId, "Player", "Fire");
```

- 用途: Button Action が押されている間 true。
- 必要条件: Button Action。
- 呼ぶ場所: `Update`。

### WasActionJustPressed

```cpp
bool isJump = runtimeApi->WasActionJustPressed(gameObjectId, "Player", "Jump");
```

- 用途: Button Action の押した瞬間だけ true。
- 呼ぶ場所: `Update`。
- 注意: ジャンプや発射のように 1 回だけ起こしたい処理に使う。

### GetMousePosition

```cpp
EditorScriptVector2 mouse = runtimeApi->GetMousePosition();
```

- 用途: Mouse 座標を読む。
- 注意: Screen、Scene View、Game View のどの座標系か確認して説明する。

### GetTransform

```cpp
EditorScriptTransform transform = runtimeApi->GetTransform(gameObjectId);
```

- 用途: GameObject の Transform を読む。
- 呼ぶ場所: `Start`、`Update`。
- 注意: 回転の単位が度かラジアンかを UI と実装で確認する。

### SetTransform

```cpp
runtimeApi->SetTransform(gameObjectId, &transform);
```

- 用途: GameObject の Transform を書き換える。
- 呼ぶ場所: `Update`。
- 注意: Rigidbody と併用すると物理結果を上書きすることがある。

### GetVelocity

```cpp
EditorScriptVector3 velocity = runtimeApi->GetVelocity(gameObjectId);
```

- 用途: Rigidbody の現在速度を読む。
- 必要条件: Rigidbody。

### SetVelocity

```cpp
runtimeApi->SetVelocity(gameObjectId, &velocity);
```

- 用途: Rigidbody の速度を直接指定する。
- 必要条件: Rigidbody。
- 呼ぶ場所: 原則 `FixedUpdate`。
- 注意: 毎フレーム 0 を入れると物理の自然な動きが消える。

### GetAngularVelocity

```cpp
EditorScriptVector3 angularVelocity = runtimeApi->GetAngularVelocity(gameObjectId);
```

- 用途: Rigidbody の回転速度を読む。
- 必要条件: Rigidbody。

### SetAngularVelocity

```cpp
runtimeApi->SetAngularVelocity(gameObjectId, &angularVelocity);
```

- 用途: Rigidbody の回転速度を直接指定する。
- 必要条件: Rigidbody。
- 注意: Freeze Rotation と競合する。

### AddForce

```cpp
EditorScriptVector3 force{0.0f, 10.0f, 0.0f};
runtimeApi->AddForce(gameObjectId, &force);
```

- 用途: 継続的な力を加える。
- 必要条件: Rigidbody。
- 呼ぶ場所: `FixedUpdate`。
- 注意: 移動量ではなく力である。

### AddImpulse

```cpp
EditorScriptVector3 impulse{0.0f, 6.0f, 0.0f};
runtimeApi->AddImpulse(gameObjectId, &impulse);
```

- 用途: 瞬間的な衝撃を加える。
- 必要条件: Rigidbody。
- 使う場面: ジャンプ、ノックバック、爆発。

### AddTorque

```cpp
EditorScriptVector3 torque{0.0f, 0.0f, 10.0f};
runtimeApi->AddTorque(gameObjectId, &torque);
```

- 用途: 回転力を加える。
- 必要条件: Rigidbody。
- 使う場面: 球を転がす、タイヤを回す。
- 注意: Torque 軸と進む方向は摩擦と接地面に依存する。

### GetAiSensorState

```cpp
EditorScriptAiSensorState sensor =
	runtimeApi->GetAiSensorState(gameObjectId, EditorScriptAiSensorKindVision);
```

- 用途: AI Sensor の結果を読む。
- 必要条件: 対応する AI Sensor Component。
- 注意: Sensor 種類ごとに有効な値が違う。

### GetMaterialState

```cpp
EditorScriptMaterialState material = runtimeApi->GetMaterialState(gameObjectId);
```

- 用途: MeshRenderer / Material の状態を読む。
- 読める例: 色、Texture Path、Metallic、Roughness、IOR、Alpha、Reflection。

### GetAnimationState

```cpp
EditorScriptAnimationState animation = runtimeApi->GetAnimationState(gameObjectId);
```

- 用途: Animation / Animator の状態を読む。
- 読める例: 再生中、Loop、Speed、現在 Clip 名、現在時間。

## 追加済み Runtime API

この章は、旧下書きに載っていなかった現行`EditorScriptRuntimeApi`を省略せず扱う。

### Animator Parameter

| API | 用途 | 戻り値 |
| --- | --- | --- |
| `SetAnimatorFloat` | Float Parameterを設定する。 | 対象とParameterが有効ならtrue。 |
| `SetAnimatorInt` | Int Parameterを設定する。 | 同上。 |
| `SetAnimatorBool` | Bool Parameterを設定する。 | 同上。 |
| `SetAnimatorTrigger` | Triggerを立てる。 | 同上。 |
| `ResetAnimatorTrigger` | Triggerを解除する。 | 同上。 |
| `SetAnimatorVector2` | Vector2 Parameterを設定する。 | 同上。 |
| `SetAnimatorVector3` | Vector3 Parameterを設定する。 | 同上。 |
| `GetAnimatorFloat` | Float Parameterを取得する。 | 出力へ書けた場合true。 |
| `GetAnimatorInt` | Int Parameterを取得する。 | 同上。 |
| `GetAnimatorBool` | Bool Parameterを取得する。 | 同上。 |
| `GetAnimatorVector2` | Vector2 Parameterを取得する。 | 同上。 |
| `GetAnimatorVector3` | Vector3 Parameterを取得する。 | 同上。 |

Parameter名はAnimation Graph内の名前と完全一致させる。取得APIは戻り値を確認してから出力値を使う。

### Animation 再生

| API | 用途 |
| --- | --- |
| `PlayAnimationAction` | Clip Index、Blend In / Out、速度、優先度、Loopを指定してAction再生する。 |
| `PlayAnimation` | Animation Componentの再生を開始する。 |
| `StopAnimation` | 再生を停止する。 |
| `IsAnimationPlaying` | 現在再生中か調べる。 |
| `GetAnimationTime` | 現在時間を秒で取得する。 |
| `SetAnimationTime` | 再生位置を秒で変更する。 |
| `SetAnimationSpeed` | 再生速度倍率を変更する。 |
| `GetAnimatorStateName` | 現在State名を呼出側Bufferへ取得する。 |

`GetAnimatorStateName`はBufferと容量を渡し、falseの場合は文字列を使用しない。

### Effect

| API | 用途 |
| --- | --- |
| `PlayEffect` | 対象GameObjectのParticleSystem / VisualEffectを再生する。 |
| `PlayEffectAt` | Effect Asset PathとLocal Offsetを指定して再生する。 |
| `StopEffect` | 新規発生を停止する。 |
| `GetAliveParticleCount` | 現在生存しているParticle数を取得する。 |
| `IsEffectPlaying` | Effectが再生状態か調べる。 |

Effectは毎Frame作り直さず、Emitterを再利用する。弾着、水しぶき、爆発を大量に出す場合はGameObjectまたはゲーム側Poolと組み合わせる。

### GameObject

低水準API:

| API | 用途 |
| --- | --- |
| `FindGameObjectByName` | 名前からGameObject IDを取得する。見つからない場合は負値。 |
| `SetGameObjectActive` | GameObjectの有効状態を変更する。 |
| `IsGameObjectActive` | 現在の有効状態を取得する。 |
| `HasComponent` | 指定したComponent型名が対象GameObjectにあるか確認する。 |
| `InvokeAction` | 対象GameObjectに付いたScript / MonoBehaviourへ名前付きActionを送る。 |

高水準API:

```cpp
GameObject target = GameObject::Find("Target");

if (target.HasReference()) {
	target.SetActive(true);

	if (target.HasComponent("Health")) {
		target.InvokeAction("OnSpawned");
	}

	EditorScriptTransform transform = target.GetTransform();
	transform.position.y += 1.0f;
	target.SetTransform(transform);
}
```

頻繁に使う参照は毎Frame名前検索せず、`ExposeGameObject`でInspectorから設定して保持する。名前検索は初期化や任意候補探索に限定する。
`HasComponent`の型名はScene保存に使う英語名を指定する。日本語表示名ではなく、`RailMovement`、`Health`、`Rigidbody`、`AudioSource`のような内部Component型名を使う。
`InvokeAction`はScript Actionの接着剤であり、戻り値で受信側がActionを処理したか確認する。敵撃破、会話開始、BGM切替などの意味は受信Script側で決める。

### SceneManager

低水準APIは`LoadScene`と`LoadSceneByBuildIndex`、高水準APIは`SceneManager::LoadScene`である。

```cpp
std::string nextScenePath_;

MyScript::MyScript() {
	ExposeScene("nextScenePath", "次のScene", nextScenePath_);
}

void MyScript::Update(int32_t gameObjectId, float deltaTime) {
	(void)gameObjectId;
	(void)deltaTime;

	if (!nextScenePath_.empty() && Input::GetKeyDown(KeyCode::Space)) {
		SceneManager::LoadScene(nextScenePath_);
	}
}
```

Path遷移では`.scene`が存在すること、Standalone BuildではそのSceneがBuild Settingsに含まれることを確認する。Build Indexは`ゲームをビルド...`のScene一覧順を使う。

### RailFollower

| 高水準API | 低水準Runtime API | 用途 |
| --- | --- | --- |
| `Pause` / `Resume` | `SetRailPaused` | 移動を停止 / 再開する。 |
| `SetPaused` | `SetRailPaused` | boolで停止状態を指定する。 |
| `IsPaused` | `IsRailPaused` | 停止状態を取得する。 |
| `SetSpeed` | `SetRailSpeed` | RailMovementの目標速度を変更する。 |
| `SetReverse` | `SetRailReverse` | 逆方向フラグを変更する。 |
| `JumpTo` | `SetRailNormalizedProgress` | 0～1の正規化進行率へ移動する。 |
| `SetDistance` | `SetRailDistance` | Path先頭からの距離で現在位置を指定する。 |
| `SwitchRail` | `SetRailPath` | 別Rail Pathへ切り替える。進行率を維持するか指定できる。 |
| `SetMoveInput` | `SetRailMoveInput` | レール基準の左右X・上下Y入力を渡す。Inspectorの範囲と速度が適用される。 |
| `SetOffset` | `SetRailOffset` | レール中心からの左右X・上下Yオフセットを直接設定する。設定範囲内へ制限される。 |
| `GetOffset` | `GetRailOffset` | 現在のレール内オフセットを取得する。 |
| `GetNormalizedProgress` | `GetRailNormalizedProgress` | 現在進行率を取得する。 |
| `GetState` | `GetRailState` | 進行率、距離、速度、停止、逆方向、終端到達などをまとめて取得する。 |
| `GetLength` | `GetRailLength` | Rail全長を取得する。 |
| `GetPosition` | `GetRailPosition` | 任意進行率のWorld位置を取得する。 |
| `GetDirection` | `GetRailDirection` | 任意進行率の進行方向を取得する。 |
| `GetFrame` | `GetRailFrame` | 任意進行率の位置、前方、右、上をまとめて取得する。 |
| `GetClosestProgress` | `GetRailClosestProgress` | World位置に最も近いRail進行率を取得する。 |
| `ConsumeEndReached` | `ConsumeRailEndReached` | 終端到達通知を1回消費する。 |

```cpp
GameObject followerObject_;

MyScript::MyScript() {
	ExposeGameObject("follower", "Rail移動対象", followerObject_);
}

void MyScript::Update(int32_t gameObjectId, float deltaTime) {
	(void)gameObjectId;
	(void)deltaTime;

	if (followerObject_.HasReference() && Input::GetKeyDown(KeyCode::Space)) {
		RailFollower railFollower{followerObject_};
		railFollower.SetPaused(!railFollower.IsPaused());
	}
}
```

`RailFollower`はGameObject IDを参照する軽量Wrapperなので、値として作る。生ポインターや手動`new`は不要である。

Input Action Eventからレール内移動を制御する場合は、Vector2値を`SetMoveInput`へ渡す。Inspectorの`PlayerInputから移動入力`をONにした場合は、同じ処理をRailMovementが自動で行うためScript側から重ねて入力しない。

```cpp
bool MyScript::OnAction(
	int32_t gameObjectId,
	const char* functionName,
	const EditorScriptInputActionContext& inputContext) {
	if (std::strcmp(functionName, "OnMove") != 0) {
		return false;
	}

	RailFollower railFollower{gameObjectId};
	railFollower.SetMoveInput(inputContext.vector2Value);
	return true;
}
```

Rail上の近い位置へ敵やCameraを寄せる場合は、World位置から進行率を取得してから距離や向きを計算する。

```cpp
RailFollower railFollower{railObject_};
float closestProgress = 0.0f;

if (railFollower.GetClosestProgress(targetPosition_, closestProgress)) {
	EditorScriptRailFrame railFrame{};
	if (railFollower.GetFrame(closestProgress, railFrame)) {
		// railFrame.position / forward / right / up を使って配置や向きを作る。
	}
}
```

`GetState`は、UI表示、分岐、Debug表示で個別APIを何度も呼ばないためのまとめ取得である。

```cpp
EditorScriptRailState railState{};

if (railFollower.GetState(railState) && railState.endReached) {
	railFollower.SetPaused(true);
}
```

### RuntimeProperty

`RuntimeProperty`は、Componentの公開Propertyを名前で読み書きする共通APIである。
Component専用Setterを増やしすぎず、演出、難易度変化、Timeline Actionから共通的に値を変えるために使う。

| API | 用途 |
| --- | --- |
| `SetFloat` / `GetFloat` | 速度、強度、距離、時間などのfloat値。 |
| `SetInt` / `GetInt` | Mode、Index、Countなどのint値。 |
| `SetBool` / `GetBool` | Loop、有効/無効、停止などのbool値。 |
| `SetVector2` / `GetVector2` | RailMovementの移動範囲、開始Offsetなどの2成分値。 |
| `SetVector3` / `GetVector3` | 色、方向、位置Offsetなどの3成分値。 |

RailMovementで現在登録されている代表Propertyは次の通り。

| Component | Property | 型 | 用途 |
| --- | --- | --- | --- |
| `RailMovement` | `Speed` | float | 目標移動速度。 |
| `RailMovement` | `Acceleration` | float | 加速。 |
| `RailMovement` | `Deceleration` | float | 減速。 |
| `RailMovement` | `LookAheadDistance` | float | 向き計算の先読み距離。 |
| `RailMovement` | `OffsetMoveSpeed` | float | レール内Offset移動速度。 |
| `RailMovement` | `Paused` / `StartPaused` | bool | 停止状態。 |
| `RailMovement` | `Loop` | bool | Loop。 |
| `RailMovement` | `OrientToPath` | bool | 進行方向へ回転。 |
| `RailMovement` | `StopAtEnd` | bool | 終端停止。 |
| `RailMovement` | `UsePlayerInput` | bool | PlayerInputからOffset入力を読む。 |
| `RailMovement` | `MovementRange` | Vector2 | 左右X、上下Yの移動可能範囲。 |
| `RailMovement` | `StartOffset` | Vector2 | 開始時のレール内Offset。 |
| `ParticleSystem` / `VisualEffect` | `BillboardMode` | int | Render Asset未設定時の板の向き。0=Camera Facing、1=Y軸固定、2=Velocity Facing、3=World XY固定。 |
| `ParticleSystem` / `VisualEffect` | `BillboardStretch` | float | Velocity Facing時の速度方向Stretch。0.01以上。 |

```cpp
GameObject player = GameObject::Find("PlayerShip");

RuntimeProperty::SetFloat(player, "RailMovement", "Speed", 18.0f);
RuntimeProperty::SetVector2(
	player,
	"RailMovement",
	"MovementRange",
	EditorScriptVector2{8.0f, 4.0f});

GameObject splashEffect = GameObject::Find("BowSplash");

// 速度方向へ正対させ、船首飛沫を細長く見せる。
RuntimeProperty::SetInt(
	splashEffect,
	"VisualEffect",
	"BillboardMode",
	2);
RuntimeProperty::SetFloat(
	splashEffect,
	"VisualEffect",
	"BillboardStretch",
	3.5f);
```

存在しないComponent名、存在しないProperty名、型違いはfalseを返す。
ゲーム固有の意味を持つ値は、Scriptの公開Fieldとして持たせるか、Data Asset側へ置く。

### Rigidbody 高水準API

`Rigidbody` Wrapperは`GetVelocity`、`SetVelocity`、`AddForce`、`AddForceAtPosition`、`AddImpulse`、`AddTorque`を提供する。

```cpp
Rigidbody rigidbody{gameObjectId};
rigidbody.AddImpulse(EditorScriptVector3{0.0f, 5.0f, 0.0f});
```

`AddForceAtPosition`はForceとWorld作用点を渡す。重心から外した推進力、反動、船体への局所浮力など、並進と回転を同時に発生させる用途に使う。

Rigidbodyの`重心オフセット`を設定した場合、Force、Impulse、Torque、`AddForceAtPosition`はOffset適用後のJolt重心と慣性を使用する。同じForceでも作用点から重心までの腕が変わるため、回転Momentも変化する。船専用・箱専用の分岐はなく、Collider形状、質量、慣性、重心と作用点の組み合わせから挙動差を作る。

`Colliderから質量を計算`を有効にしたRigidbodyは、Play開始時にCollider体積と実質密度からJolt質量・慣性を決める。Buoyancyの`自動物理`が有効な場合は、Inspectorを開いていなくても`水密度 x 目標水没率`を実質密度として優先する。ScriptのForce APIはこの解決済み質量を持つBodyへ作用するため、Script側で同じ質量計算を重複実装しない。

自動BuoyancyはJolt排水体積から合計浮力を厳密に保ち、局所FFT水深から作用点だけを移動する。さらに平滑化した相対加速度と角加速度から並進・回転付加慣性、船長と速度からFroude造波抵抗、水線面積とメタセンタ高さからPitch / Roll放射減衰をFixedUpdate前に適用する。Script側で人工的な上向きTorqueや追加の一様Angular Dragを重ねると形状由来の姿勢応答を壊すため、ゲーム固有の制御は推進力、舵、積荷移動、浸水などの入力へ限定する。

### RopeConstraint 高水準API

`RopeConstraint`は既にGameObjectへ追加済みのロープ拘束を、Play中に接続、解除、巻き取り、修復するWrapperである。キー、Input Action、AI、Timeline、Triggerのどこからでも同じAPIを呼ぶ。

| API | 用途 |
| --- | --- |
| `Attach(target, ownerAnchor, targetAnchor, length)` | 別GameObjectへ接続する。Anchorは両方Local座標。 |
| `AttachToWorld(ownerAnchor, worldAnchor, length)` | World固定点へ接続する。 |
| `Detach()` | Componentを無効化し、張力を止める。 |
| `SetLength(length)` | ウインチの巻取り・繰出し用に最大長を変える。 |
| `Repair()` | 破断状態を解除して再接続する。 |
| `GetState()` | 接続、破断、対象ID、最大長、現在長、現在張力を読む。 |

Eキーで掴み、同じキーで離す最小例は次の通り。対象がなければWorld固定点へ接続するなど、ゲーム側で分岐を追加できる。

```cpp
void GrappleScript::Update(int32_t gameObjectId, float deltaTime) {
	(void)deltaTime;

	if (!Input::GetKeyDown(KeyCode::E)) {
		return;
	}

	RopeConstraint rope{gameObjectId};
	const EditorScriptRopeState ropeState = rope.GetState();

	if (ropeState.isActive) {
		rope.Detach();
		return;
	}

	const GameObject hookTarget = GameObject::Find("HookTarget");
	if (hookTarget.HasReference()) {
		rope.Attach(
			hookTarget,
			EditorScriptVector3{0.0f, 0.5f, 0.0f},
			EditorScriptVector3{0.0f, 0.0f, 0.0f},
			12.0f);
	}
}
```

入力をComponent内部へ固定していないため、ゲームパッドはPlayerInput Actionから同じ`Attach`を呼び、敵AIは行動Scriptから呼べる。

### ExplosionとComponent Active

`Physics::AddExplosionImpulse(center, radius, impulseStrength, upwardModifier)`は範囲内のDynamic Rigidbodyへ距離減衰付きImpulseを加え、影響したBody数を返す。爆発、衝撃波、着弾ノックバックに使う。`upwardModifier`を正にすると爆心を下へずらし、上向き成分を増やす。

```cpp
const EditorScriptVector3 explosionCenter{0.0f, 1.0f, 0.0f};
const int32_t affectedCount = Physics::AddExplosionImpulse(
	 explosionCenter,
	 8.0f,
	 20.0f,
	 1.5f);
```

`GameObject::SetComponentActive("RopeConstraint", true)`と`IsComponentActive`は、保存に使う英語のComponent型名で任意Componentを有効・無効化する。専用APIが必要な状態変更はRopeConstraintなどの高水準Wrapperを優先し、単純な実行ON/OFFだけを共通APIで扱う。

## Script Action 登録と汎用Component連携

### 設計方針

Script Actionは、挙動の差し替えやライフサイクル通知が必要なComponentだけへ接続する。Transform、Renderer、Colliderのような基礎データComponentすべてへ意味のないHookを追加しない。

- Presetは共通設定値を再利用するもの。
- Componentは開始条件、値の監視、子の有効化など汎用責務を持つ。
- Script Actionはゲーム固有処理を受け取る任意の拡張点。
- `OnBossAppear`のようなゲーム固有名をEngine Managerへ固定しない。

### Actionを登録する

新規生成したC++ Scriptは`EditorNativeScript`を継承する。Constructorで`BindAction`する。

```cpp
class StageEventReceiver final : public EditorNativeScript {
public:
	StageEventReceiver();

private:
	void OnWaveStarted(const EditorScriptInputActionContext& inputContext);
	void OnWaveSpawned(const EditorScriptInputActionContext& inputContext);
	void OnWaveCompleted(const EditorScriptInputActionContext& inputContext);
};

StageEventReceiver::StageEventReceiver() {
	BindAction("OnWaveStarted", [this](const EditorScriptInputActionContext& inputContext) {
		OnWaveStarted(inputContext);
	});
	BindAction("OnWaveSpawned", [this](const EditorScriptInputActionContext& inputContext) {
		OnWaveSpawned(inputContext);
	});
	BindAction("OnWaveCompleted", [this](const EditorScriptInputActionContext& inputContext) {
		OnWaveCompleted(inputContext);
	});
}
```

同じ名前を再登録した場合は関数を置き換え、Inspector候補名は重複させない。

### EditorへAction候補を公開する

新しいScript Templateは次のDLL Exportを自動生成する。

| Export | 用途 |
| --- | --- |
| `EditorScript_GetActionCount` | 登録済みAction数を返す。 |
| `EditorScript_GetActionName` | Indexに対応するAction名を返す。 |

使用者がExport本体を手書きする必要はない。古いTemplateから作成したDLLにはこのExportがない場合がある。その場合もAction名の直接入力は使えるが、Inspectorの候補Comboは表示されない。候補が必要なら新Templateと同じExportを追加してDLLを再Buildする。

### Inspectorで接続する

1. ProjectでC++ Script Assetを作成する。
2. Header / Sourceで`BindAction`を追加する。
3. Editorと同じx64構成でDLLをBuildする。
4. Actionを受け取るGameObjectへ`Script`または`MonoBehaviour`を追加し、DLL Pathを設定する。
5. WaveSpawner、TimelineEvent、ThresholdStateの`Action 対象`へそのGameObjectを設定する。
6. Action名を直接入力するか、表示された`... 候補`から選ぶ。
7. Sceneを保存してPlayする。
8. Consoleに未登録Action Warningがないか確認する。

候補一覧はAction対象GameObjectに付いたScript / MonoBehaviour DLLから収集する。Action対象が`このObject`なら、通知ComponentとScriptを同じGameObjectへ置く。

### 受信値

| 送信元 | Action | Payload |
| --- | --- | --- |
| WaveSpawner | 開始 | `EditorScriptActionPayloadTypeNone`。 |
| WaveSpawner | 各生成 | `EditorScriptActionPayloadTypeGameObject`。`payloadGameObjectId`が今回貸し出したObject。 |
| WaveSpawner | 完了条件 | `EditorScriptActionPayloadTypeInt`。`payloadInt`が生成総数。発火時点はInspectorの完了条件による。 |
| WaveSpawner | 全撃破 | `EditorScriptActionPayloadTypeInt`。`payloadInt`が生成総数。 |
| TimelineEvent | 発火 | Input互換の`buttonValue`へ発火秒または発火進行率。 |
| ThresholdState | State変更 | Input互換の`buttonValue`へState 1 / 2 / 3。 |

受信側は`inputContext.phase == EditorScriptInputPhasePerformed`と`payloadType`を確認する。WaveSpawnerはGameObject IDをfloatへ変換しない。

```cpp
void StageEventReceiver::OnWaveSpawned(
	const EditorScriptInputActionContext& inputContext) {
	if (inputContext.phase != EditorScriptInputPhasePerformed ||
		inputContext.payloadType != EditorScriptActionPayloadTypeGameObject) {
		return;
	}

	GameObject spawnedObject{inputContext.payloadGameObjectId};

	if (spawnedObject.HasReference()) {
		spawnedObject.SetActive(true);
	}
}
```

### Action候補が出ない時

1. `Action 対象`が正しいGameObjectか確認する。
2. 対象に有効なScript / MonoBehaviourがあるか確認する。
3. DLL PathがBuild出力先と一致するか確認する。
4. DLLを再Buildする。
5. 古いDLLで`EditorScript_GetActionCount` / `EditorScript_GetActionName`がない場合は直接入力するかExportを追加する。
6. Action名の大文字小文字と空白を確認する。
7. ConsoleのDLL Load、API Version、未登録Actionを確認する。

## 基本テンプレート

テンプレートは最小にする。
全 API を最初から詰め込むと読みにくくなるため、詳細例は後続のサンプルへ分ける。

```cpp
#include "NewNativeScript.h"

#include <new>

namespace {
	const EditorScriptRuntimeApi* runtimeApi = nullptr;

	struct ScriptInstance {
		explicit ScriptInstance(int32_t ownerGameObjectId)
			: gameObjectId(ownerGameObjectId) {
		}

		int32_t gameObjectId = -1;
		NewNativeScript script;
	};
}

extern "C" __declspec(dllexport) bool EditorScript_Load(
	uint32_t apiVersion,
	const EditorScriptRuntimeApi* api) {
	if (apiVersion != kEditorScriptApiVersion || api == nullptr) {
		return false;
	}

	runtimeApi = api;
	return true;
}

extern "C" __declspec(dllexport) void EditorScript_Unload() {
	runtimeApi = nullptr;
}

extern "C" __declspec(dllexport) void* EditorScript_CreateInstance(int32_t gameObjectId) {
	return new (std::nothrow) ScriptInstance(gameObjectId);
}

extern "C" __declspec(dllexport) void EditorScript_DestroyInstance(void* instance) {
	delete static_cast<ScriptInstance*>(instance);
}

extern "C" __declspec(dllexport) void EditorScript_StartInstance(void* instance) {
	ScriptInstance* scriptInstance = static_cast<ScriptInstance*>(instance);

	if (runtimeApi != nullptr && scriptInstance != nullptr) {
		runtimeApi->Log("Script Start");
	}
}

extern "C" __declspec(dllexport) void EditorScript_UpdateInstance(void* instance, float deltaTime) {
	ScriptInstance* scriptInstance = static_cast<ScriptInstance*>(instance);

	if (runtimeApi == nullptr || scriptInstance == nullptr) {
		return;
	}

	EditorScriptTransform transform = runtimeApi->GetTransform(scriptInstance->gameObjectId);
	transform.position.x += scriptInstance->script.moveSpeed * deltaTime;
	runtimeApi->SetTransform(scriptInstance->gameObjectId, &transform);
}

extern "C" __declspec(dllexport) void EditorScript_StopInstance(void* instance) {
	(void)instance;
}
```

実際の自動生成テンプレートは`NewNativeScript`基底クラスの`Start`、`Update`、`FixedUpdate`、物理Event、Animation Event、公開Field、Actionを上記実体へ転送する。利用者はexport関数ではなくクラス本体を編集する。

### 作成時に選べるTemplate一覧

ProjectでC++ Script Assetを作成する時、Templateを選ぶ。
Templateは「完成したゲーム機能」ではなく、よく使う接続コードの開始点である。
生成後はHeader / Sourceを編集して、ゲーム固有の条件、数値、Action名を調整する。

| Template | Category | 表示名 | 説明 | 推奨Component |
| --- | --- | --- | --- | --- |
| `Empty` | 基本 | 空のスクリプト | 最小構成から独自処理を書く。 | Script / MonoBehaviour |
| `PlayerController` | 移動・入力 | プレイヤー移動 | Vector2入力でTransformを移動する。 | PlayerInput / Input / FreeTransform |
| `RailPlayer` | 移動・入力 | レール移動操作 | 入力をRailMovementの左右・上下Offsetへ渡す。 | RailMovement / PlayerInput / MovementModifier |
| `EnemyController` | 戦闘・AI | 敵の基本制御 | TargetSelectorの結果を使う敵処理の開始コード。 | TargetSelector / Health / HitscanWeapon / ProjectileEmitter |
| `TurretController` | 戦闘・AI | 砲塔制御 | 選択TargetへYaw/Pitchを向けて射撃する開始コード。 | TargetSelector / HitscanWeapon / ProjectileEmitter |
| `HomingController` | 戦闘・AI | 追尾制御 | TargetSteeringへ追尾開始・終了条件を追加する。 | TargetSelector / TargetSteering / Rigidbody |
| `BossController` | 戦闘・AI | 体力フェーズ制御 | Health比率からフェーズを切り替える開始コード。 | Health / ThresholdState / ActionSequence |
| `StageController` | Scene・進行 | Scene進行 | 入力やゲーム条件からSceneを切り替える。 | TimelineEvent / ActionSequence / Scene Asset |
| `LoadoutController` | 戦闘・入力 | 武器切替 | WeaponLoadoutの切替・射撃・リロードを入力へ接続する。 | WeaponLoadout / WeaponLoadoutSlot / PlayerInput |
| `PhysicsController` | 物理 | Rigidbody移動 | FixedUpdateで入力方向へ力を加える。 | RigidBody / Collider / ConstantForce |
| `HealthDamageController` | 戦闘 | 体力・破壊 | Healthを監視し0以下の終了処理を書く開始コード。 | Health / DamageReceiver / Collider |
| `SpawnPoolController` | 生成 | 生成・Pool | PrefabSpawnerまたはObjectPoolからObjectを生成する。 | PrefabSpawner / ObjectPool / WaveSpawner |
| `CameraEffectsController` | カメラ | カメラ演出 | Camera BlendとShakeを入力・イベントから再生する。 | Camera / CameraBlend / CameraShake |
| `AnimationEffectController` | Animation・VFX | Animation・Effect | AnimationとParticle/VFXを同時に起動する開始コード。 | Animator / Animation / ParticleSystem / VisualEffect |
| `AudioController` | Audio | 音量・音響制御 | AudioSourceの公開Propertyをゲーム中に変更する。 | AudioSource / AudioReverbZone / Audio Filter |
| `UiController` | UI | UIイベント | Button等からBindActionを呼ぶUI処理の開始コード。 | Canvas / Button / Text / Image / UIValueBinding |
| `ActionEventController` | イベント | Action・Sequence | ActionRelayとActionSequenceをゲーム条件へ接続する。 | ActionRelay / ActionSequence / TimelineEvent / PropertyTween |
| `SaveCheckpointController` | 保存 | Save・Checkpoint | Save SlotとCheckpointを入力・イベントへ接続する。 | Saveable / Checkpoint |
| `OceanBuoyancyController` | 海・物理 | 海面問い合わせ | 描画と浮力が共有するOcean表面情報を取得する。 | Ocean / Buoyancy / RigidBody |
| `NavigationAiController` | Navigation・AI | Target・経路AI | Target取得後のNavigation/Steering条件を書く開始コード。 | NavigationAgent / AIPathRequest / TargetSelector / AISteeringAgent |
| `RuntimePropertyController` | Component連携 | Component Property操作 | Component存在確認と公開Property変更を行う。 | 任意Component / PropertyTween / ActionRelay |

### Templateを選ぶ基準

- 何も決まっていない処理は`Empty`から始める。
- 入力からTransformを動かすだけなら`PlayerController`を使う。
- RailMovementの左右上下入力を作るなら`RailPlayer`を使う。
- 敵、砲台、追尾弾は、攻撃方法をComponentで固定せず、TargetとWeaponをTemplate内で接続する。
- Phase、Stage、Save、UI、Audioは、Action受信とRuntimeProperty変更の例として使う。
- 生成されたTemplateに不要なActionやFieldがある場合は削る。Engine側へゲーム固有Managerを追加しない。

## サンプル 1: W キーで前へ移動する

これは直接キーを見る例である。
Input Action を使わない最小確認に向いている。

必要 Component:

- GameObject + C++ Script。

```cpp
extern "C" __declspec(dllexport) void EditorScript_Update(
	int32_t gameObjectId,
	float deltaTime) {
	if (runtimeApi == nullptr) {
		return;
	}

	EditorScriptTransform transform = runtimeApi->GetTransform(gameObjectId);

	if (runtimeApi->IsKeyDown(Key::W)) {
		transform.position.z += 3.0f * deltaTime;
	}

	runtimeApi->SetTransform(gameObjectId, &transform);
}
```

注意:

- `Key::W` は W キーである。
- W を前進として使っているのは、このサンプルの決め方である。
- Rigidbody がある場合は、この方法ではなく Velocity や Force を使う方が自然である。

## サンプル 2: Input Action の Move で移動する

これは Unity の Input System 風に、キーではなく Action を読む例である。

必要 Component:

- GameObject + C++ Script。
- PlayerInput。
- Input Action Asset。
- `Player/Move` Action。
- Move Action は Vector2。

```cpp
extern "C" __declspec(dllexport) void EditorScript_Update(
	int32_t gameObjectId,
	float deltaTime) {
	if (runtimeApi == nullptr) {
		return;
	}

	EditorScriptTransform transform = runtimeApi->GetTransform(gameObjectId);
	EditorScriptVector2 move =
		runtimeApi->GetActionVector2(gameObjectId, "Player", "Move");

	transform.position.x += move.x * 3.0f * deltaTime;
	transform.position.z += move.y * 3.0f * deltaTime;

	runtimeApi->SetTransform(gameObjectId, &transform);
}
```

注意:

- `Player` は Action Map 名。
- `Move` は Action 名。
- Binding は Project Settings または Input Action Asset 側で設定する。

## サンプル 3: Space でジャンプする

必要 Component:

- Rigidbody。
- Collider。
- GameObject + C++ Script。
- PlayerInput。
- `Player/Jump` Action。

```cpp
namespace {
	std::unordered_map<int32_t, bool> jumpRequests;
}

extern "C" __declspec(dllexport) void EditorScript_Update(
	int32_t gameObjectId,
	float deltaTime) {
	(void)deltaTime;

	if (runtimeApi == nullptr) {
		return;
	}

	if (runtimeApi->WasActionJustPressed(gameObjectId, "Player", "Jump")) {
		jumpRequests[gameObjectId] = true;
	}
}

extern "C" __declspec(dllexport) void EditorScript_FixedUpdate(
	int32_t gameObjectId,
	float fixedDeltaTime) {
	(void)fixedDeltaTime;

	if (runtimeApi == nullptr) {
		return;
	}

	if (jumpRequests[gameObjectId]) {
		EditorScriptVector3 impulse{0.0f, 6.0f, 0.0f};
		runtimeApi->AddImpulse(gameObjectId, &impulse);
		jumpRequests[gameObjectId] = false;
	}
}
```

注意:

- 入力は `Update` で読む。
- 物理操作は `FixedUpdate` で行う。
- 地面にいるかどうかの判定を入れないと空中でも連続ジャンプできる。

## サンプル 4: Torque で球を転がす

必要 Component:

- Rigidbody。
- SphereCollider。
- GameObject + C++ Script。

```cpp
extern "C" __declspec(dllexport) void EditorScript_FixedUpdate(
	int32_t gameObjectId,
	float fixedDeltaTime) {
	(void)fixedDeltaTime;

	if (runtimeApi == nullptr) {
		return;
	}

	EditorScriptVector3 torque{};

	if (runtimeApi->IsKeyDown(Key::W)) {
		torque.x += 10.0f;
	}

	if (runtimeApi->IsKeyDown(Key::S)) {
		torque.x -= 10.0f;
	}

	if (runtimeApi->IsKeyDown(Key::A)) {
		torque.z += 10.0f;
	}

	if (runtimeApi->IsKeyDown(Key::D)) {
		torque.z -= 10.0f;
	}

	runtimeApi->AddTorque(gameObjectId, &torque);
}
```

注意:

- Torque は回転力であり、直接移動量ではない。
- 実際に進むには、接地、摩擦、Freeze Rotation の設定が関係する。
- 軸はモデルの向きや座標系に合わせて調整する。

## サンプル 5: Collision / Trigger を受け取る

必要 Component:

- Rigidbody。
- Collider。
- `Generate Contact Events` 相当の設定。
- GameObject + C++ Script。

```cpp
extern "C" __declspec(dllexport) void EditorScript_OnPhysicsEvent(
	int32_t gameObjectId,
	const EditorScriptPhysicsEvent* physicsEvent) {
	if (runtimeApi == nullptr || physicsEvent == nullptr) {
		return;
	}

	if (physicsEvent->type == EditorScriptPhysicsEventTypeCollisionEnter) {
		runtimeApi->Log("Collision Enter");
	}

	if (physicsEvent->type == EditorScriptPhysicsEventTypeTriggerEnter) {
		runtimeApi->Log("Trigger Enter");
	}

	(void)gameObjectId;
}
```

`EditorScriptPhysicsEvent` で見る値:

| フィールド | 意味 |
| --- | --- |
| `type` | Collision / Trigger の Enter、Stay、Exit。 |
| `selfGameObjectId` | 自分の GameObject ID。 |
| `otherGameObjectId` | 相手の GameObject ID。 |
| `point` | 接触点。 |
| `normal` | 接触面の法線。 |
| `relativeVelocity` | 相対速度。 |
| `separation` | 分離距離またはめり込み量。 |
| `isTrigger` | Trigger 判定かどうか。 |

## サンプル 6: Material の状態を見る

必要 Component:

- MeshRenderer / ModelRenderer。
- GameObject + C++ Script。

```cpp
extern "C" __declspec(dllexport) void EditorScript_Start(int32_t gameObjectId) {
	if (runtimeApi == nullptr) {
		return;
	}

	EditorScriptMaterialState material = runtimeApi->GetMaterialState(gameObjectId);

	if (material.hasComponent) {
		runtimeApi->Log(material.materialName);
	}
}
```

見る値:

- `color`
- `intensity`
- `metallic`
- `roughness`
- `ior`
- `alpha`
- `reflectionStrength`
- `texturePath`
- `uvLayoutTexturePath`

## サンプル 7: AI Sensor の結果を見る

必要 Component:

- AI Sensor。
- GameObject + C++ Script。

```cpp
extern "C" __declspec(dllexport) void EditorScript_Update(
	int32_t gameObjectId,
	float deltaTime) {
	(void)deltaTime;

	if (runtimeApi == nullptr) {
		return;
	}

	EditorScriptAiSensorState vision =
		runtimeApi->GetAiSensorState(gameObjectId, EditorScriptAiSensorKindVision);

	if (vision.hasComponent && vision.isDetected) {
		runtimeApi->Log("Target detected");
	}
}
```

Sensor ごとの主な値:

| Sensor | 主に見る値 |
| --- | --- |
| Vision | `isDetected`、`detectedGameObjectId`、`distance`、`direction`。 |
| Object Detection | `label`、`confidence`、`boundsPosition`、`boundsSize`。 |
| Color Tracking | `label`、`screenPosition`、`confidence`。 |
| Motion Detection | `motion`、`motionMagnitude`、`confidence`。 |
| Whisper Speech | `text`、`confidence`。 |
| Voice Command | `command`、`commandId`、`text`。 |

## サンプル 8: Animation の状態を見る

必要 Component:

- Animation または Animator。
- GameObject + C++ Script。

```cpp
extern "C" __declspec(dllexport) void EditorScript_Update(
	int32_t gameObjectId,
	float deltaTime) {
	(void)deltaTime;

	if (runtimeApi == nullptr) {
		return;
	}

	EditorScriptAnimationState animation = runtimeApi->GetAnimationState(gameObjectId);

	if (animation.hasComponent && animation.isPlaying) {
		runtimeApi->Log(animation.currentClipName);
	}
}
```

見る値:

- `isPlaying`
- `isLoop`
- `playOnAwake`
- `animationType`
- `clipCount`
- `animationSpeed`
- `animationAmplitude`
- `currentTime`
- `currentClipDuration`
- `assetPath`
- `currentClipName`

## サンプル 9: Input Action Event から任意関数を呼ぶ

必要 Component:

- PlayerInput。
- Input Action Asset。
- GameObject + C++ Script。
- PlayerInput の Event に関数名を設定。

```cpp
extern "C" __declspec(dllexport) bool EditorScript_InvokeAction(
	int32_t gameObjectId,
	const char* functionName,
	const EditorScriptInputActionContext* inputContext) {
	if (runtimeApi == nullptr || functionName == nullptr || inputContext == nullptr) {
		return false;
	}

	if (std::strcmp(functionName, "OnSubmit") == 0) {
		runtimeApi->Log("OnSubmit");
		return true;
	}

	if (std::strcmp(functionName, "OnMove") == 0) {
		EditorScriptVector2 value = inputContext->vector2Value;
		(void)value;
		return true;
	}

	(void)gameObjectId;
	return false;
}
```

`EditorScriptInputActionContext` で見る値:

| フィールド | 意味 |
| --- | --- |
| `gameObjectId` | 呼び出し元の GameObject ID。 |
| `phase` | Started / Performed / Canceled。 |
| `valueType` | Button / Vector2。 |
| `buttonValue` | Button の値。 |
| `vector2Value` | Vector2 の値。 |
| `actionMapName` | Action Map 名。 |
| `actionName` | Action 名。 |
| `bindingPath` | 実際の Binding Path。 |

## よくある失敗

| 現象 | 確認すること |
| --- | --- |
| DLL が読み込まれない | DLL Path、Debug / Release、x64、API Version、依存 DLL。 |
| Build しても変わらない | Component の DLL Path と出力先が一致しているか。 |
| W が反応しない | Play 中か、Focus、`IsKeyDown` の keyCode、GameObject の Script 有効状態。 |
| Action が反応しない | Project Settings、Input Action Asset、Action Map 名、Action 名、PlayerInput。 |
| 物理が動かない | Rigidbody、Collider、Kinematic、Freeze、FixedUpdate で呼んでいるか。 |
| Torque が効かない | Freeze Rotation、摩擦、接地、軸の向き。 |
| Collision が来ない | Collider、Rigidbody、Trigger、Contact Event、Layer Collision Matrix。 |
| クラッシュする | `runtimeApi == nullptr`、null pointer、破棄済み state、構造体 Version 不一致。 |

## サンプル 10: 画面照準からRay射撃する

```cpp
GameObject aimObject = GameObject::Find("Player Aim");
EditorScriptRay aimRay{};
EditorScriptPhysicsHit hit{};

if (Physics::GetAimRay(aimObject, aimRay) && Physics::Raycast(aimRay, 2000.0f, hit)) {
	Health targetHealth{GameObject{hit.gameObjectId}};
	targetHealth.Damage(10.0f, GameObject{gameObjectId});
}
```

- `ViewportPointToRay`は左上`0,0`、右下`1,1`のGame View座標を使う。
- `Physics::Raycast`、`SphereCast`、`CapsuleCast`は同じ`EditorScriptRay`を受け取る。
- Component設定で発射する場合は同じ処理を持つ`レイ射撃`を使い、Scriptへ重複実装しない。

## Scene Streaming・Sequence・Save API

### 非同期Scene

```cpp
if (!SceneManager::IsLoaded("Assets/Scenes/BossEffects.scene")) {
	SceneManager::LoadSceneAdditiveAsync("Assets/Scenes/BossEffects.scene");
}

if (!SceneManager::IsLoading() && SceneManager::IsLoaded("Assets/Scenes/BossEffects.scene")) {
	SceneManager::UnloadScene("Assets/Scenes/BossEffects.scene");
}
```

`LoadSceneAsync`はPrimary置換、`LoadSceneAdditiveAsync`は追加読込である。要求を受理したtrueは読込成功を意味しない。完了は`IsLoading`と`IsLoaded`で確認する。

### Scene間の一時データ

```cpp
SceneManager::SetFloat("result.score", score_);
SceneManager::SetString("result.rank", rank_);
SceneManager::LoadSceneAsync("Assets/Scenes/GameResult.scene");
```

遷移先では`GetFloat`と`GetString`を使う。この値はPlayセッション内の引き渡し用で、永続保存には`SaveSystem`を使う。

### 汎用Sequenceの外部制御

```cpp
ActionSequence stageSequence{GameObject::Find("Stage Sequence")};

if (bossDefeated_) {
	stageSequence.Signal("BossDefeated");
}

if (shouldPausePresentation_) {
	stageSequence.Pause();
}
```

Sequenceはゲーム条件を判定しない。Script側が条件を決め、Play/Pause/Stop/Signalだけを命令する。

### Slot保存とCheckpoint

```cpp
SaveSystem::SetFloat("player.score", score_);
SaveSystem::SetString("player.weapon", weaponName_);

if (!SaveSystem::Save("slot_01")) {
	return;
}

float restoredScore = 0.0f;
if (SaveSystem::Load("slot_01") &&
	SaveSystem::GetFloat("player.score", restoredScore)) {
	score_ = restoredScore;
}

Checkpoint checkpoint{GameObject::Find("Stage Checkpoint")};
checkpoint.Save();
```

`SaveSystem::Exists`で存在確認、`SaveSystem::Delete`で削除する。GameObject状態はInspectorの`保存対象`が登録し、ゲーム固有値だけをSetFloat/SetStringで追加する。生のSave file Path、fstream、Editor内部ManagerをゲームScriptから扱わない。


## サンプル 11: Pool生成と返却

```cpp
ObjectPool projectilePool{GameObject::Find("Projectile Pool")};
EditorScriptVector3 position{0.0f, 2.0f, 5.0f};
GameObject projectile = projectilePool.Spawn(position);

if (projectile.HasReference()) {
	ObjectPool::Release(projectile);
}
```

- 自動生成設定を使う場合は`Spawner{spawnerObject}.Spawn()`を呼ぶ。
- 物理ObjectはInspectorの遅延生成容量へ最大同時数を設定する。未使用枠はGameObject化されず、初回`Spawn`時に物理BodyとScriptが登録される。

## サンプル 12: Componentを外部イベントから実行する

```cpp
Weapon weapon{GameObject::Find("Player Weapon")};
CameraEffects cameraEffects{GameObject::Find("Hit Camera Effect")};
RailBranch branch{GameObject::Find("Route Branch")};

weapon.FireProjectile();
cameraEffects.PlayShake();
branch.Trigger();
```

- これらのクラスはゲームルールを持たず、Inspector設定済みComponentへ命令する。
- 誘導弾、敵全滅分岐、ボス演出などの条件はScript側で判定し、汎用Componentを呼び分ける。

## C++ Script詳細ページの完成条件

このファイルを元に使用者向けページを作る時は、API名の一覧だけで終わらせない。
各APIとTemplateには「いつ、どこから、何を渡すと、何が変わるか」を書く。

### APIごとに必ず書く項目

| 項目 | 必須内容 |
| --- | --- |
| 目的 | そのAPIで解決する作業。 |
| 呼ぶ場所 | `Start`、`Update`、`FixedUpdate`、`OnAction`、物理Event、Animation Eventのどこで呼ぶか。 |
| 必要Component | 対象GameObjectに必要なComponent、参照Fieldで指定するObject、Scene側の事前設定。 |
| 引数 | 単位、座標系、範囲、null許可、空文字許可。 |
| 戻り値 | `true`と`false`の意味、失敗時にSceneへ変更が入るか。 |
| 実行タイミング | 即時反映、次Frame反映、FixedUpdate反映、描画Frame反映の区別。 |
| 失敗条件 | Componentなし、非Active、Play外、Pathなし、Build対象外、型違い、Layer不一致。 |
| 競合 | Transform直書き、Rigidbody、Animation、RailMovement、Constraintが同時に触る場合の優先順位。 |
| 最小コード | 10～40行で動く確認例。 |
| 実用コード | 公開Field、Action、Component確認、戻り値確認を含む例。 |
| Debug方法 | Consoleログ、Scene View表示、Inspector値、戻り値、警告を見る順序。 |

### GameObject APIの詳細方針

`GameObject::Find`は便利だが、毎Frame検索用ではない。
ドキュメントでは次を分けて説明する。

- 一度だけ探す: `Start`で名前検索して保持する。
- 使用者が設定する: `ExposeGameObject`でInspector参照を持つ。
- 存在を確認する: `HasReference`を必ず見る。
- Component構成を見る: `HasComponent("Health")`のように内部型名で確認する。
- 処理を呼ぶ: `InvokeAction("OnSpawned")`でScript Actionを呼び、戻り値を見る。

`InvokeAction`はGameObjectへ付いたScript / MonoBehaviourを対象にする。
Button、Timeline、Wave、Threshold以外から同じActionを呼びたい時に使う。

```cpp
GameObject receiver = GameObject::Find("Stage Event Receiver");

if (receiver.HasReference() &&
	receiver.HasComponent("Script") &&
	!receiver.InvokeAction("OnStageStarted")) {
	Debug::Log("OnStageStarted was not handled.");
}
```

### RailFollower APIの詳細方針

RailFollowerはレールシューティング専用ではない。
Camera、乗り物、敵、移動床、演出Objectなど、Rail Path上の位置と向きを使う全てのObjectに使える。

使用者向け説明では次を分ける。

- `SetSpeed`: 速度だけを変える。進行率は維持する。
- `SetDistance`: Path先頭からの実距離へ移動する。長さが変わるPathでは進行率より直感的である。
- `JumpTo`: 0～1の正規化進行率へ移動する。UI SliderやTimelineに向く。
- `SwitchRail`: 別Pathへ切り替える。分岐、Stage切替、Camera演出に使う。
- `SetMoveInput`: 入力値を渡してInspectorの範囲と速度でOffsetを動かす。
- `SetOffset`: 入力補間を通さずOffsetを直接指定する。
- `GetState`: UI、Debug、分岐条件でまとめて読む。
- `GetFrame`: Rail上の位置、前方、右、上を同時に取り、Spawn位置やCamera向きを作る。
- `GetClosestProgress`: World位置からRail上の最寄り位置を探す。

RailMovementのInspectorで`PlayerInputから移動入力`をONにした場合、Scriptから`SetMoveInput`を重ねると入力が二重になる。
自動入力かScript入力かをSceneごとに決め、ドキュメントでは両方の設定を同時にONにする例を出さない。

### RuntimeProperty APIの詳細方針

RuntimePropertyは、ゲーム進行や演出からComponent値を変えるための共通経路である。
専用APIがあるものは専用APIを優先し、汎用的な値変更はRuntimePropertyを使う。

例:

- RailMovementの速度をBoss戦だけ上げる。
- AudioSourceのVolumeをFadeする。
- CameraShakeの強さを演出ごとに変える。
- Oceanの波高や泡の強さをStage進行で変える。
- PostProcessの露出やBloomを演出で変える。

ドキュメントには、登録済みProperty以外はfalseになることを書く。
「名前を書けば何でも変えられる」と説明しない。
登録済みPropertyの一覧は、`EditorRuntimePropertyManager`の実装と照合して更新する。

### Action設計の詳細方針

Action名は自由文字列だが、無秩序に増やすと追跡しにくい。
使用者向けには次の命名を推奨する。

| 種類 | 例 | 用途 |
| --- | --- | --- |
| 開始 | `OnStageStarted`、`OnWaveStarted` | 処理開始。 |
| 完了 | `OnWaveCompleted`、`OnSequenceCompleted` | 後続処理へ進む。 |
| 入力 | `OnFire`、`OnMove`、`OnSubmit` | PlayerInputやUIから呼ぶ。 |
| 状態 | `OnPhaseChanged`、`OnHealthLow` | ThresholdやHealth状態。 |
| 演出 | `OnExplosion`、`OnCameraShake` | Effect、Audio、Camera。 |

Actionの送信側は「いつ呼ぶか」だけを担当する。
Actionの意味、対象、分岐、失敗時処理は受信Script側へ置く。

### Template詳細ページの必須構成

各Templateページは次の順番で書く。

1. 何を作る時に使うか。
2. 作成手順。
3. 生成されるファイル。
4. Inspectorに出る公開Field。
5. 生成済みAction名。
6. 必要Componentと任意Component。
7. 最小確認手順。
8. よく変更する場所。
9. 削ってよい場所。
10. 失敗時の確認順。

Templateのコードは、全部を暗記させるためではなく、使用者が安全に書き始めるための入口として説明する。

## Target・Loadout・Damage API実装例

### WeaponLoadout

```cpp
void OnUpdate(int32_t gameObjectId, float deltaTime) {
	(void)deltaTime;
	WeaponLoadout loadout{GameObject{gameObjectId}};

	if (Input::GetKeyDown(KeyCode::Q)) {
		loadout.Previous();
	}

	if (Input::GetKeyDown(KeyCode::E)) {
		loadout.Next();
	}

	if (Input::GetKeyDown(KeyCode::R)) {
		loadout.Reload();
	}
}
```

`Fire()`は選択SlotのWeaponを発射し、成功時だけ弾を減らす。`GetAmmo(current, reserve)`はHUD更新に使う。Slot自体をScriptで別配列管理せず、HierarchyとComponent設定を装備データにする。

### Targeting

```cpp
Targeting targeting{GameObject{gameObjectId}};
GameObject target = targeting.GetCurrentTarget();

if (target.HasReference()) {
	// TargetPointが選ばれた場合も、その子GameObject参照が返る。
}
```

`SetTarget()`は自動選択を明示Targetへ置き換える。空の`GameObject{}`を渡すと明示指定を解除する。Team Filter、遮蔽、Priorityなどの選択条件はComponent側に置く。

### Runtime Property

```cpp
const GameObject ocean = GameObject::Find("Ocean");
RuntimeProperty::SetFloat(ocean, "Ocean", "WaveHeight", 4.0f);

const GameObject targetPoint = GameObject::Find("MainGunTarget");
RuntimeProperty::SetFloat(targetPoint, "TargetPoint", "Priority", 10.0f);

const GameObject enemy = GameObject::Find("EnemyRoot");
RuntimeProperty::SetInt(enemy, "Team", "TeamId", 1);
```

Component名とProperty名は大文字小文字を含めて登録名と一致させる。戻り値が`false`の場合はGameObject参照、Component存在、登録Property、値型の順に確認する。

### Ocean Query

```cpp
EditorScriptOceanSurfaceHit surfaceHit{};
const EditorScriptVector3 queryPosition{0.0f, 0.0f, 10.0f};
	float foam = 0.0f;

if (Ocean::SampleDetailed(GameObject{}, queryPosition, surfaceHit, foam)) {
	const float waterHeight = surfaceHit.point.y;
	const EditorScriptVector3 normal = surfaceHit.normal;
	const EditorScriptVector3 velocity = surfaceHit.velocity;
	// foamは0～1。波頭の音、飛沫、AI判断等へ使える。
}
```

返される位置、法線、速度、泡率はBuoyancyと同じOcean sample経路を使う。GPU FFTの最初のReadback前は設定変更時だけ再生成する有限水深CPU Spectrumを使い、GPU値が届いた時だけ0.12秒で位置・法線・速度・泡率を移行する。移行後はGPU値へ常時平滑化を掛けないため、Scriptの波速度判定や船体応答へ余分な遅延を足さない。GPU Managerと波高・風・水深等が一致しない別Oceanは主OceanのReadbackを誤用せず、自身のCPU Spectrumを使う。`IsBigWave`などのゲーム固有判定は返却値からScript側で組み立てる。

### Damage Context

```cpp
EditorScriptDamageContext damageContext{};
damageContext.sourceGameObjectId = projectile.GetInstanceId();
damageContext.instigatorGameObjectId = shooter.GetInstanceId();
damageContext.baseDamage = 25.0f;
damageContext.impulse = {0.0f, 2.0f, 8.0f};
damageContext.userTag = 1001;

Health{target}.Damage(damageContext);
```

被弾Action側では`Health{owner}.GetLastDamageContext(damageContext)`で最後の情報を読む。`sourceGameObjectId`は弾やWeapon、`instigatorGameObjectId`は発射者を指定する。ゲーム固有のFire/Ice等はEngine固定Enumへ追加せず、`userTag`またはScript側データで解釈する。

## Runtime API全件対応表

`EditorScriptRuntimeApi`はDLL境界の低水準ABIである。通常のゲームScriptでは`EditorNativeScript.h`の型付きWrapperを使い、Wrapperがない高度な処理だけRuntime APIへ降りる。以下の表は、従来の説明から名称が抜けていたAPIを省略せず対応付けたものである。

### Physics Query・Damage・Spawn・Weapon

| Runtime API | 推奨入口 | 必要条件 | 成功と失敗 |
| --- | --- | --- | --- |
| `PhysicsRaycast` | `Physics::Raycast` | 有効なRay、距離、3D Collider。 | 最初のHitがあればtrue。未命中はfalseでありエラーではない。 |
| `PhysicsSphereCast` | `Physics::SphereCast` | 半径>0、Ray、3D Collider。 | Sweep中の最初のHitを返す。開始重複の扱いを確認する。 |
| `PhysicsCapsuleCast` | `Physics::CapsuleCast` | 半径、高さ、Ray、3D Collider。 | Capsule Sweepが命中すればtrue。高さは直径以上を使う。 |
| `ApplyDamage` | `Health::Damage(float)` | TargetにHealth/DamageReceiver。 | Damageが適用されればtrue。Target不正または受信Componentなしはfalse。 |
| `ApplyDamageContext` | `Health::Damage(context)` | ContextのTarget IDとDamage受信先。 | Contextを保持してDamageを適用できればtrue。入力Contextは適用値で更新され得る。 |
| `GetLastDamageContext` | `Health::GetLastDamageContext` | 対象が過去にDamageを受けている。 | 最後のContextがあればtrue。履歴なしはfalse。 |
| `SetHealth` | `Health::SetCurrent` | Health Component。 | Clamp後の現在HPを設定できればtrue。Healthなしはfalse。 |
| `SpawnFromPool` | `ObjectPool::Spawn` | ObjectPoolと生成可能Instance。 | 生成Object ID、失敗時は負値を返す。 |
| `SpawnFromSpawner` | `PrefabSpawner::Spawn` | PrefabSpawner、Prefab/Pool設定。 | 生成Object ID、設定不足または上限時は負値。 |
| `ReleaseToPool` | `ObjectPool::Release`または`GameObject::ReleaseToPool` | Pool由来Object。 | Poolへ返せればtrue。所有Pool不明ならfalse。 |
| `FireHitscan` | `HitscanWeapon::Fire` | HitscanWeapon、Cooldown完了。 | 発射処理を開始できればtrue。命中したかどうかとは別。 |

`ApplyDamage`は互換用の簡易入口である。攻撃者、命中位置、法線、Impulse、部位Tagを使う処理は`ApplyDamageContext`を使う。

### Camera・Rail・Scene

| Runtime API | 推奨入口 | 入出力 | 失敗時の確認 |
| --- | --- | --- | --- |
| `PlayCameraBlend` | `CameraBlend::Play` | Component設定からBlendを開始する。 | Owner、CameraBlend、From/To Camera、Duration。 |
| `PlayCameraShake` | `CameraShake::Play` | Shake profileを開始する。 | Owner、CameraShake、対象Camera、振幅、Duration。 |
| `TriggerRailBranch` | `RailBranch::Trigger` | 設定済み分岐先へRailを切り替える。 | RailMovement、現在Path、Branch path、条件。 |
| `GetSceneLoadProgress` | `SceneManager::GetLoadProgress` | 0.0～1.0の非同期読込進捗。 | Async load開始前は完了値として扱わず`IsSceneLoading`も見る。 |
| `IsSceneLoading` | `SceneManager::IsLoading` | 非同期Scene処理中か返す。 | falseは成功完了と未開始の両方がある。 |
| `IsSceneLoaded` | `SceneManager::IsLoaded` | 指定PathのSceneがLoadedか返す。 | Project相対Path、拡張子、正規化を確認する。 |
| `SetSceneFloat` | `SceneData::SetFloat` | Scene切替時に渡す一時float。 | 保存Slotではない。Keyの所有者をゲーム側で決める。 |
| `GetSceneFloat` | `SceneData::GetFloat` | Key値を出力する。 | Key未登録はfalse。既定値をScript側で用意する。 |
| `SetSceneString` | `SceneData::SetString` | Scene切替用文字列を設定する。 | UTF-8文字列とKey衝突に注意する。 |
| `GetSceneString` | `SceneData::GetString` | 呼出側Bufferへ文字列をコピーする。 | Key未登録、Buffer不足、null Bufferはfalse。 |

Scene間一時DataとSave Dataを混同しない。Scene DataはTitleからStageへ選択値を渡す用途、Save Dataはアプリ再起動後も残す用途である。

### ActionSequence

| Runtime API | 推奨入口 | 動作 | falseになる主条件 |
| --- | --- | --- | --- |
| `PlayActionSequence` | `ActionSequence::Play` | 先頭または停止位置からSequenceを開始する。 | 対象なし、ActionSequenceなし、Stepなし。 |
| `PauseActionSequence` | `ActionSequence::SetPaused` | 時刻と実行Stepを保持して一時停止/再開する。 | 対象Componentなし。 |
| `StopActionSequence` | `ActionSequence::Stop` | 実行を停止し、再生状態を初期化する。 | 対象Componentなし。 |
| `SignalActionSequence` | `ActionSequence::Signal` | Signal待機Stepへ名前を通知する。 | 再生中でない、待機名不一致、対象なし。 |
| `IsActionSequencePlaying` | `ActionSequence::IsPlaying` | Pause中を含む再生所有状態を返す。 | falseは停止または対象なし。必要ならComponent存在も別確認する。 |

### Save・Checkpoint

| Runtime API | 推奨入口 | 保存範囲 | 注意 |
| --- | --- | --- | --- |
| `SaveSlot` | `SaveSystem::Save` | 登録SaveableとSave DataをSlotへ書く。 | Slot名、書込先、重複Save Key、Scene参照を確認する。 |
| `LoadSlot` | `SaveSystem::Load` | Slotを読み、登録対象へ値を戻す。 | Version、欠落Object、Scene読込順を確認する。 |
| `DeleteSlot` | `SaveSystem::Delete` | 指定Slotを削除する。 | Slotなしはfalse。Directory自体を削除しない。 |
| `HasSlot` | `SaveSystem::Exists` | Slotの存在を返す。 | 読込可能性やVersion互換までは保証しない。 |
| `ActivateCheckpoint` | `Checkpoint::Activate` | Checkpointを現在地点にし、任意でLoad処理を行う。 | Checkpoint Component、Save key、shouldLoadの意味を確認する。 |
| `SetSaveFloat` | `SaveData::SetFloat` | 次回Save対象のfloatをMemoryへ置く。 | 呼んだだけではDiskへ確定しない。`SaveSlot`が必要。 |
| `GetSaveFloat` | `SaveData::GetFloat` | Memory/Loaded dataからfloatを読む。 | Key未登録はfalse。 |
| `SetSaveString` | `SaveData::SetString` | 次回Save対象のUTF-8文字列を置く。 | Key命名とVersion移行を決める。 |
| `GetSaveString` | `SaveData::GetString` | Bufferへ保存文字列をコピーする。 | Buffer容量、Key、Load完了を確認する。 |

### Rope Runtime操作

| Runtime API | 推奨入口 | 動作 | 必要条件 |
| --- | --- | --- | --- |
| `AttachRope` | `Rope::Attach` | Owner/Target Anchorと最大長で接続を作る。 | OwnerにRigidbodyとRopeConstraint。TargetはWorld Anchorにもできる。 |
| `DetachRope` | `Rope::Detach` | 接続を解除する。 | RopeConstraintが存在すること。 |
| `SetRopeLength` | `Rope::SetLength` | 最大長を変更し、巻取り/繰出しを実現する。 | 正の長さ。急変時のImpulse上限を確認する。 |
| `RepairRope` | `Rope::Repair` | 破断状態を解除して再接続可能にする。 | Anchor/Target設定が残っていること。 |
| `GetRopeState` | `Rope::GetState` | 接続、破断、現在長、張力等を返す。 | 対象なしでは既定State。使用前にComponentを確認する。 |

### WeaponLoadout・Targeting

| Runtime API | 高水準Wrapper | 動作 | 戻り値の意味 |
| --- | --- | --- | --- |
| `LoadoutSelectSlot` | `WeaponLoadout::Select` | 指定Indexを選ぶ。 | Index有効かつ選択できればtrue。 |
| `LoadoutSelectNext` | `WeaponLoadout::Next` | 次の有効Slotへ進む。 | 有効Slotがあればtrue。 |
| `LoadoutSelectPrevious` | `WeaponLoadout::Previous` | 前の有効Slotへ戻る。 | 有効Slotがあればtrue。 |
| `LoadoutFire` | `WeaponLoadout::Fire` | 選択Weaponを発射し、成功時だけ弾を減らす。 | 発射成立時true。未命中でもtrueになり得る。 |
| `LoadoutReload` | `WeaponLoadout::Reload` | MagazineへReserveから補充を開始する。 | Reload開始時true。満弾/Reserveなし/Reload中はfalse。 |
| `LoadoutGetAmmo` | `WeaponLoadout::GetAmmo` | Current/Reserveを出力する。 | 選択Slotがあればtrue。Reserve=-1は無限。 |
| `SetExplicitTarget` | `Targeting::SetTarget` | 自動選択を明示Targetで上書きする。 | SelectorがありTarget参照が有効ならtrue。空IDで解除する。 |

### Runtime Property

| Runtime API | 値型 | 典型例 | falseになる条件 |
| --- | --- | --- | --- |
| `SetRuntimeFloat` / `GetRuntimeFloat` | float | Ocean WaveHeight、Light Intensity、Rail Speed。 | Object、Component、Property、登録型の不一致。 |
| `SetRuntimeInt` / `GetRuntimeInt` | int32 | TeamId、選択Mode、品質段階。 | 同上。Enumも登録上はint32として扱う。 |
| `SetRuntimeBool` / `GetRuntimeBool` | bool | Targetable、Loop、Effect有効。 | 同上。0/1のintとして渡さない。 |
| `SetRuntimeVector2` / `GetRuntimeVector2` | Vector2 | Rail Offset Input、UI値、UV Offset。 | Vector3 Propertyへは使えない。 |
| `SetRuntimeVector3` / `GetRuntimeVector3` | Vector3 | Wind Direction、Aim Offset、Color/Position。 | Vector2 Propertyへは使えない。 |

Property名はC++メンバー名を推測せず、登録一覧にある公開名を使う。Getterは失敗時に出力値を有効値として扱わない。

### PropertyTween・Action・Component照会

| Runtime API | 推奨入口 | 動作 | 確認事項 |
| --- | --- | --- | --- |
| `PlayPropertyTween` | `PropertyTween::Play` | Component設定から補間を開始する。 | Target、Component名、Property名、型、Duration。 |
| `StopPropertyTween` | `PropertyTween::Stop` | 補間を停止する。 | 停止時にStart/EndへSnapするかはComponent設定に従う。 |
| `IsPropertyTweenPlaying` | `PropertyTween::IsPlaying` | 現在補間中か返す。 | Componentなしと停止中を区別する場合は`HasComponent`も使う。 |
| `RelayAction` | `ActionRelay::Relay` | 子ActionRelayTargetへHierarchy順でActionを送る。 | Target、Action名、子の有効状態。 |
| `InvokeScriptAction` | `GameObject::InvokeAction` | 対象ObjectのScript/MonoBehaviourへ名前付きActionを送る。 | DLL、Export、Action名、Component Active。 |
| `HasComponent` | `GameObject::HasComponent` | 内部型名のComponent有無を返す。 | 日本語表示名ではなく`Health`等の内部名を使う。 |

## 低水準APIを直接使う場合の共通規約

1. `EditorScriptRuntimeApi*`を永続保存せず、Engineが渡したVersionとSizeを確認する。
2. GameObject IDはScene reload後も永続する保証がない。Scene切替後は参照を取り直す。
3. `bool`戻り値を無視しない。未命中、未登録、Component不足、入力不正をConsoleで区別する。
4. 出力Pointerを渡すAPIはnullを渡さない。false時の出力値を使用しない。
5. `const char*`はUTF-8とし、Engineが呼出後も保持すると明記されたAPI以外では一時Bufferを渡してよい。
6. Updateで`FindGameObjectByName`を毎Frame呼ばず、StartまたはInspector参照でIDを保持する。
7. Physics、Animation、Effectの更新を描画Callbackから呼ばない。
8. Runtime Propertyは専用Wrapperがない汎用値に使い、Damage、Spawn、Scene load等を文字列Propertyへ偽装しない。

## C++ Script型付きWrapper完全リファレンス

### DLLとInstanceの生成単位

1つのDLLを複数GameObjectへ設定しても、ゲーム状態をDLLのグローバル変数で共有しない。EngineはGameObjectごとにScript Instanceを作り、各Instanceが自分のGameObject IDと公開Fieldを持つ。共有状態が必要なら、専用Manager GameObject、Scene Data、Save Data、または明示参照を使う。

| 段階 | 呼出内容 | Script側の責務 | 禁止事項 |
| --- | --- | --- | --- |
| DLL Load | API Versionと`EditorScriptRuntimeApi`を受け取る。 | Version、必要Function Pointerを確認する。 | Runtime API Pointerが有効になる前に呼び出さない。 |
| Instance Create | GameObject IDごとの`EditorNativeScript`を生成する。 | Instance memberを初期化する。 | GameObject別状態をDLL global mapへ逃がさない。 |
| Expose | 公開FieldとAction候補を列挙する。 | Pointer、型、範囲、説明を登録する。 | 一時変数のAddressを公開しない。 |
| Start | Scene/Physics登録後の開始処理。 | 参照検索、初期値反映、Component検証を行う。 | 毎Frameの検索をここで準備せずUpdateへ残さない。 |
| FixedUpdate | 固定刻みの物理処理。 | Force、Torque、物理制御を行う。 | 描画、可変delta依存のUI更新を行わない。 |
| Update | 可変刻みの入力・ゲーム処理。 | Input、Timer、Action、非物理状態を更新する。 | Dynamic RigidbodyのTransformを継続上書きしない。 |
| Action | Button/Sequence/Componentからの通知。 | 1回のイベントとして処理する。 | Action名の意味をEngineへ固定しない。 |
| Stop/Destroy | Play停止、Scene unload、Object破棄。 | 外部Handleと一時状態を解放する。 | 破棄後GameObject IDを使わない。 |

### `GameObject` Wrapper

| Method | Signature | 引数・戻り値 | 失敗条件と使用場所 |
| --- | --- | --- | --- |
| Find | `static GameObject Find(const char* name)` | Scene内の名前一致Object。失敗時はID=-1。 | null/空名、Runtime APIなし、未検出。Startで一度だけ使う。 |
| GetInstanceId | `int32_t GetInstanceId() const` | 現在Scene内ID。 | Scene reload後の永続IDではない。Save Keyに使わない。 |
| HasReference | `bool HasReference() const` | ID>=0ならtrue。 | Objectが既に破棄されたかまでは保証しない。 |
| IsActive | `bool IsActive() const` | GameObject Active。 | 無効参照/APIなしはfalse。 |
| SetActive | `bool SetActive(bool)` | GameObjectとRuntime登録状態を切り替える。 | Pool itemは可能ならPool APIで返却する。 |
| HasComponent | `bool HasComponent(const char*) const` | 内部型名のComponent有無。 | `体力`ではなく`Health`を渡す。空名はfalse。 |
| IsComponentActive | `bool IsComponentActive(const char*) const` | Componentの有効Check。 | Componentなしと無効を区別する時はHasComponentも見る。 |
| SetComponentActive | `bool SetComponentActive(const char*, bool) const` | Componentだけを有効/無効化する。 | Managerが切替を対応していない型ではfalseになり得る。 |
| InvokeAction | `bool InvokeAction(const char*) const` | Script/MonoBehaviourの公開ActionをQueueする。 | Exportなし、名前不一致、対象Scriptなし、空名。 |
| GetTransform | `EditorScriptTransform GetTransform() const` | Position/Rotation/Scaleを値で返す。 | 失敗時はゼロ初期化値。参照確認後に使う。 |
| SetTransform | `bool SetTransform(const EditorScriptTransform&) const` | Transform全体を書き戻す。 | Dynamic Rigidbody、Rail、Animationとの競合に注意する。 |

```cpp
GameObject target = GameObject::Find("Target");

if (!target.HasReference() || !target.HasComponent("Health")) {
	Debug::Log("Target or Health is missing.");
	return;
}

target.InvokeAction("OnSelected");
```

### `Input`と入力Action

| Method | 入力 | 戻り値 | 呼出規則 |
| --- | --- | --- | --- |
| `Input::GetKey` | `KeyCode` | 現在押されている間true。 | Updateで連続入力に使う。 |
| `Input::GetKeyDown` | `KeyCode` | 押したFrameだけtrue。 | 発射、決定、切替など1回入力に使う。 |
| `Input::GetActionVector2` | Owner ID、Action Map、Action | Vector2。未設定は0。 | PlayerInputとAction名をInspector設定と一致させる。 |
| `Input::GetAction` | Owner ID、Map、Action | 押下中bool。 | 連射やHoldに使う。 |
| `Input::GetActionDown` | Owner ID、Map、Action | 立上がりFrameだけtrue。 | UI/Eventから同じActionを二重処理しない。 |
| `Input::GetMousePosition` | なし | Game View内Mouse座標。 | Window全体座標かViewport座標かを変換前に確認する。 |

`PlayerInput`がAction EventからScript Actionを呼ぶ方式と、Scriptが毎Frame Action値を読む方式を同じ処理へ同時接続しない。

### `SceneManager` Wrapper

| Method | Signatureの意味 | 成功条件 | 注意 |
| --- | --- | --- | --- |
| `LoadScene(path)` | 指定`.scene`へ同期切替。 | Scene解決と読込要求成功。 | Frame途中のObject参照は切替後無効になる。 |
| `LoadScene(index)` | Build SettingsのScene indexで切替。 | indexが登録範囲内。 | Project表示順ではなくBuild順。 |
| `LoadSceneAsync(path)` | 現Sceneを置換する非同期読込。 | Async job開始。 | trueは完了ではない。`IsLoading`と進捗を見る。 |
| `LoadSceneAdditiveAsync(path)` | 現Sceneを残して追加読込。 | Async job開始。 | 名前/ID衝突、複数Camera/Listenerを確認する。 |
| `UnloadScene(path)` | Additive Sceneだけを破棄。 | Loaded Sceneが一致。 | Main SceneをUnloadする設計にしない。 |
| `GetLoadProgress()` | 0-1進捗。 | Async job中。 | 未開始と完了を値だけで区別しない。 |
| `IsLoading()` | Job実行中か。 | Runtime state。 | false後に`IsLoaded`を確認する。 |
| `IsLoaded(path)` | SceneがLoadedか。 | 正規化Path一致。 | 大文字小文字、拡張子、Project相対Path。 |
| `SetFloat/GetFloat` | 次Sceneへ渡す一時float。 | Key登録済み。 | Disk保存ではない。 |
| `SetString/GetString` | 次Sceneへ渡す一時UTF-8文字列。 | Key/Bufferが有効。 | Wrapperの取得Bufferは512 byte上限。 |

### `RailFollower` Wrapper

| Method | 値の意味 | Runtime結果 | falseになる条件 |
| --- | --- | --- | --- |
| `Pause/Resume/SetPaused` | 現在速度状態を保持して停止/再開。 | 距離更新を停止または再開する。 | RailMovementなし、無効ID、APIなし。 |
| `IsPaused` | 現在の停止状態。 | bool。 | Componentなしもfalseなので事前確認する。 |
| `SetSpeed` | m/s相当の符号付き目標速度。 | 加減速設定に従って追従する。 | RailMovementなし。 |
| `SetReverse` | 進行方向Flag。 | 距離増減方向を反転する。 | Pathなし。 |
| `JumpTo` | 0-1正規化進行率。 | RangeへClampして即時移動する。 | Path長0、制御点不足。 |
| `SetDistance` | Path先頭からの実距離。 | Loop/Stop規則で補正する。 | Path sample失敗。 |
| `SwitchRail` | Path Object、進行率維持Flag。 | Target Pathへ参照変更する。 | Targetなし、制御点2未満。 |
| `SetMoveInput` | X=右、Y=上の-1～1入力。 | Range内Offsetを速度付きで更新する。 | PlayerInput自動読込と重ねると二重入力になる。 |
| `SetOffset` | 右/上方向の距離m。 | RangeへClampして直接設定する。 | Rail Frameなし。 |
| `ClearMoveInput` | `{0,0}`を設定。 | 入力加算を止める。 | Offset自体は0へ戻さない。 |
| `GetOffset` | 現在の右/上Offset。 | 出力引数へ書く。 | Componentなし。 |
| `GetNormalizedProgress` | 0-1進行率。 | Path距離/全長。 | Path長0。 |
| `GetLength` | Path全長m。 | Sample cacheの長さ。 | 制御点不足。 |
| `GetPosition/GetDirection` | 任意進行率のWorld位置/接線。 | Spawn、Camera、Markerに使う。 | Path sample失敗。 |
| `GetFrame` | Position/Forward/Right/Up。 | 1回でRail座標系を取得する。 | 接線が作れないPath。 |
| `GetClosestProgress` | World点に最も近い進行率。 | Path復帰や配置に使う。 | Pathなし。高頻度大量呼出は避ける。 |
| `GetState` | 進行率、距離、速度、Offset、Pause等。 | HUD/Debugのまとめ読みに使う。 | Componentなし。 |
| `ConsumeEndReached` | 終端到達Eventを1回取得。 | trueを返した後Flagを消費する。 | 毎Frame複数箇所から読まない。 |

### `Physics` Wrapper

| Method | 入力単位 | 出力 | 使用上の注意 |
| --- | --- | --- | --- |
| `ViewportPointToRay` | 0-1 Viewport座標。 | World origin/direction。 | Pixel座標を直接渡さない。 |
| `GetAimRay` | ScreenAim所有Object。空参照は画面中央。 | World Ray。 | Game View採用CameraとScreenAim位置を使う。 |
| `Raycast` | Ray、distance m。 | GameObject ID、point、normal、distance。 | 未命中falseは通常分岐。Layer filterは現API範囲を確認する。 |
| `SphereCast` | Ray、radius m、distance m。 | 最初のSweep Hit。 | 発射物の厚みや近接判定に使う。 |
| `CapsuleCast` | Ray、radius/height m、distance m。 | 最初のSweep Hit。 | heightは直径以上。Character形状に使う。 |
| `SampleOceanSurface` | 任意Ocean候補Object、World位置。 | point、normal、displacement、velocity。 | API Version 6以上。Buoyancyと同じSample経路を使う。 |
| `AddExplosionImpulse` | center m、radius m、impulse、upward。 | 影響Body数。 | Damageは与えない。0は対象なしまたはAPIなし。 |

```cpp
EditorScriptRay ray{};
EditorScriptPhysicsHit hit{};

if (Physics::GetAimRay(screenAim, ray) &&
	Physics::SphereCast(ray, 0.15f, 500.0f, hit)) {
	GameObject hitObject{hit.gameObjectId};
	EditorScriptDamageContext context{};
	context.sourceGameObjectId = gameObjectId;
	context.hitPosition = hit.point;
	context.hitNormal = hit.normal;
	context.baseDamage = 20.0f;
	Health{hitObject}.Damage(context);
}
```

### `Health`とDamage Context

| Method | 動作 | 戻り値 | 注意 |
| --- | --- | --- | --- |
| `Damage(float, source)` | 簡易Damageを送る。 | 適用できればtrue。 | Hit位置等が不要な互換入口。 |
| `Damage(context)` | ContextのTarget IDを所有Objectへ設定して送る。 | DamageReceiverが処理すればtrue。 | 引数Contextは適用後値へ更新され得るため非const。 |
| `GetLastDamageContext` | 最後の1Hitを読む。 | 履歴があればtrue。 | 複数Hit履歴ではない。Action内で必要情報を転記する。 |
| `Get` | Current/Maximumを出力。 | Healthがあればtrue。 | false時の出力値を使わない。 |
| `Set` | Currentを設定。 | Healthがあればtrue。 | Damage Action、無敵時間を経由しない直接設定。 |

`EditorScriptDamageContext`の`sourceGameObjectId`は弾/Weapon、`instigatorGameObjectId`はPlayer/敵など最終所有者、`targetGameObjectId`は受信対象、`hitPosition/normal`はWorld、`impulse`はWorld impulse、`baseDamage`は受信前、`appliedDamage`は倍率等適用後、`userTag`はゲーム側定義である。

### Spawn・Weapon・Target Wrapper

| Class/Method | 必要Component | 成功結果 | 失敗確認 |
| --- | --- | --- | --- |
| `ObjectPool::Spawn(position, rotation)` | ObjectPool。 | 貸出Object参照。空きがなければ容量内で遅延生成する。 | Template、遅延生成容量、Allow Expand、動的物理・Script登録。 |
| `ObjectPool::Release(object)` | Pool由来Object。 | Active解除と状態reset。 | 所有Pool不明、既返却、無効参照。 |
| `Spawner::Spawn()` | PrefabSpawner。 | 設定位置へ1体生成。 | Pool/Spawn Point、上限。 |
| `Weapon::FireHitscan()` | HitscanWeapon。 | 発射成立true。 | Cooldown、入力とは独立、Component Active。 |
| `Weapon::FireProjectile()` | ProjectileEmitter。 | 弾貸出と発射成立true。 | Pool、Spawn Point、Cooldown。 |
| `WeaponLoadout::Select(index)` | WeaponLoadout+子Slot。 | 選択/Visual切替。 | Index、Slot数、Weapon参照。 |
| `WeaponLoadout::Next/Previous` | 同上。 | 有効Slotを循環選択。 | Slotなし。 |
| `WeaponLoadout::Fire` | 選択Slot+Weapon。 | 発射成功時だけ弾消費。 | Ammo、Reload、Weapon cooldown。 |
| `WeaponLoadout::Reload` | 選択Slot。 | Reload開始。 | 満弾、Reserve 0、Reload中。 |
| `WeaponLoadout::GetAmmo` | 選択Slot。 | Current/Reserve出力。 | Slotなし。Reserve=-1は無限。 |
| `Targeting::GetCurrentTarget` | TargetSelector。 | TargetPointを含む現在Target。 | 候補なしは空参照。 |
| `Targeting::SetTarget` | TargetSelector。 | 明示Target固定。 | 空参照を渡すと固定解除。 |

### Runtime PropertyとTween

`RuntimeProperty::Set/GetVector2/Float/Int/Bool/Vector3`は全て、GameObject参照、内部Component名、登録Property名、値型の4条件が一致した時だけtrueを返す。Propertyが存在しても型が異なればfalseである。

```cpp
float currentSpeed = 0.0f;

if (!RuntimeProperty::GetFloat(ship, "RailMovement", "Speed", currentSpeed)) {
	Debug::Log("RailMovement.Speed is not registered.");
	return;
}

RuntimeProperty::SetFloat(ship, "RailMovement", "Speed", currentSpeed + 2.0f);
```

| Wrapper | 動作 | 注意 |
| --- | --- | --- |
| `PropertyTween::Play` | Inspector設定のStartからEndへ補間を開始する。 | 対象Propertyを他Scriptが同時更新しない。 |
| `PropertyTween::Stop` | 現在位置で補間を停止する。 | Start/Endへ自動Snapすると仮定しない。 |
| `PropertyTween::IsPlaying` | 補間中か返す。 | Componentなしもfalse。 |
| `ActionRelay::Relay` | 子Relay TargetへActionを送る。 | 1つ失敗しても他Target処理を続ける実装規則を確認する。 |

### Camera・Sequence・Save・Rope Wrapper

| Wrapper | Method | 詳細契約 |
| --- | --- | --- |
| CameraEffects | `PlayBlend` | OwnerのCameraBlend設定を再生する。trueは開始成功で完了ではない。 |
| CameraEffects | `PlayShake` | OwnerのCameraShakeを追加再生する。複数Shakeは加算される。 |
| RailBranch | `Trigger` | 外部命令方式でも進行率方式でも即時分岐要求を行う。Once済みはfalseになり得る。 |
| ActionSequence | `Play` | 停止状態から開始する。LoopはComponent設定。 |
| ActionSequence | `Pause(bool)` | Step時間と状態を保持して停止/再開する。 |
| ActionSequence | `Stop` | Step状態を初期化して停止する。 |
| ActionSequence | `Signal(name)` | 現在待機中の一致Signalへ通知する。空名はfalse。 |
| ActionSequence | `IsPlaying` | Pause中を再生所有状態に含むかをRuntime規則として文書化する。 |
| SaveSystem | `Save/Load/Delete/Exists` | 空でないSlot名を使う。trueはSerialize/Deserialize全体成功。 |
| SaveSystem | `Set/GetFloat` | Memory Save Dataへ値を置く/読む。Disk確定にはSaveが必要。 |
| SaveSystem | `Set/GetString` | UTF-8文字列。Wrapper取得は512 byte Buffer。 |
| Checkpoint | `Save/Load` | ComponentのSlot名でSaveSystemを呼び、完了Actionを送る。 |
| Rigidbody | `Get/SetVelocity` | m/s。Setは直接上書き、Forceとは異なる。 |
| Rigidbody | `AddForce` | N相当をFixedUpdateで加える。 |
| Rigidbody | `AddForceAtPosition` | World作用点へForceを加え、Torqueも発生させる。 |
| Rigidbody | `AddImpulse` | 1回の運動量変化。deltaTimeを掛けない。 |
| Rigidbody | `AddTorque` | N m相当。Angular velocity直接設定とは異なる。 |
| RopeConstraint | `Attach/AttachToWorld` | AnchorはOwner local、Target localまたはWorld。Maximum Length>0。 |
| RopeConstraint | `Detach` | 張力を止める。Component設定自体は残す。 |
| RopeConstraint | `SetLength` | 巻取り/繰出し。急変時は最大張力とFixed timestepを調整する。 |
| RopeConstraint | `Repair` | Broken flagを解除する。Anchor設定は再利用する。 |
| RopeConstraint | `GetState` | attached/broken/currentLength/currentTension等を値で返す。APIなしはゼロState。 |

## Script実装時の詳細チェックリスト

1. 公開FieldはInstance memberで、寿命中Addressが変わらないか。
2. GameObject参照をInspectorで設定できる場合、名前検索より参照Fieldを優先したか。
3. Startで必要Componentを`HasComponent`確認したか。
4. FixedUpdateとUpdateの責務を分離したか。
5. `deltaTime`を秒として使い、Impulseへ二重に掛けていないか。
6. degreeとradian、pixelと0-1 Viewport、LocalとWorldを混同していないか。
7. bool/ID/Pointer戻り値の失敗を処理したか。
8. Scene reload、Pool返却、Object無効化後に古いIDを使っていないか。
9. 同じ値をComponent自動更新とScriptの両方から書いていないか。
10. Action名、Component内部名、Runtime Property名の大文字小文字が一致するか。
11. Debug DLLとRelease exeのRuntime library/配置が一致するか。
12. Standalone BuildへDLL、依存DLL、Scene、AssetがCopyされるか。

## Runtime API Version 7: 時間・状態・属性・ロック・型付きAction

`kEditorScriptApiVersion`は7である。Version 7は`EditorScriptRuntimeApi`末尾へ9 Entryを追加し、Action Context末尾へ型付きPayloadを追加する。既存Entryの並び順は変更しない。新Headerへ更新したC++ Script DLLはDebug/Releaseをそれぞれ再ビルドし、古いHeaderで作ったDLLと新しいDLLを同じComponentへ混在させない。

### ActionPayloadの作成と送信

高水準入口は`GameObject::InvokeAction(functionName, payload)`である。Payload作成には`ActionPayload` Factoryを使い、`EditorScriptActionPayload::type`へ数値を直接書かない。

| Factory | Payload Type | 読み出すContext Member | 用途例 |
| --- | --- | --- | --- |
| `ActionPayload::GameObjectValue(object)` | `GameObject` | `payloadGameObjectId` | Target、生成Object、破壊部位。 |
| `ActionPayload::Int(value)` | `Int` | `payloadInt` | Slot Index、段階番号。 |
| `ActionPayload::Float(value)` | `Float` | `payloadFloat` | Attribute、倍率、時間。 |
| `ActionPayload::Bool(value)` | `Bool` | `payloadBool` | Enable、成功状態。 |
| `ActionPayload::Vector3(value)` | `Vector3` | `payloadVector3` | Hit位置、移動先。 |
| `ActionPayload::String(value)` | `String` | `payloadString` | State名、Tag。UTF-8最大255 byte。 |

```cpp
void NotifySelectedTarget(const GameObject& receiver, const GameObject& target) {
	const bool isQueued = receiver.InvokeAction(
		"OnTargetSelected",
		ActionPayload::GameObjectValue(target));

	if (!isQueued) {
		Debug::Log("OnTargetSelected could not be queued.");
	}
}
```

`InvokeAction`のtrueはAction処理完了ではなく、対象と関数名が有効で通知をQueueへ積めたことを表す。受信側は`BindAction`し、必ず`payloadType`を検査してから対応Memberを読む。

```cpp
BindAction("OnAttributeChanged", [this](const EditorScriptInputActionContext& context) {
	if (context.payloadType != EditorScriptActionPayloadTypeFloat) {
		Debug::Log("OnAttributeChanged received an invalid payload type.");
		return;
	}

	currentBoost_ = context.payloadFloat;
});
```

PayloadなしActionでは`payloadType == EditorScriptActionPayloadTypeNone`である。`buttonValue`と`vector2Value`はInput Action互換値であり、GameObject IDや任意のイベント値を詰める用途には使わない。Stringは受信Callbackの間に必要な`std::string`へ複製し、Context内BufferへのPointerを保持しない。

### Timer Wrapper

```cpp
Timer reloadTimer(gameObject);

if (!reloadTimer.Start()) {
	Debug::Log("Timer Component is missing.");
}

float remainingSeconds = 0.0f;
reloadTimer.GetRemaining(remainingSeconds);
reloadTimer.Pause();
reloadTimer.Resume();
```

| Method | 低水準Entry | 成功条件 | 副作用・注意 |
| --- | --- | --- | --- |
| `Timer::Start()` | `StartTimer(id)` | OwnerにTimerがある。 | Durationから再開始する。Pause解除だけではない。 |
| `Timer::Pause(bool)` | `PauseTimer(id, bool)` | Timerがある。 | trueで停止、falseで再開。残り時間を保持する。 |
| `Timer::Resume()` | `PauseTimer(id, false)` | Timerがある。 | 現在の残り時間から続ける。 |
| `Timer::GetRemaining(float&)` | `GetTimerRemaining(id, float*)` | Timerと出力Pointerがある。 | 秒。false時の出力を使わない。 |

Timer ActionはPayloadなしである。発火処理内でStartを呼ぶと次周期を先頭から開始する。繰り返しTimerのActionから毎回Startを重ねない。

### GenericStateMachine Wrapper

```cpp
GenericStateMachine stateMachine(gameObject);

if (!stateMachine.ChangeState("Attack")) {
	Debug::Log("State did not change.");
}

std::string currentState;
stateMachine.GetState(currentState);
```

`ChangeState`は空文字、同一State、Component不在でfalseを返す。成功時は変更ActionをQueueし、String Payloadへ変更後State名を渡す。`GetState`は内部256 byte Bufferから`std::string`へ複製する。Stateの遷移許可、Enter/Exit処理、時間条件はWrapperではなくゲームScript側で管理する。

| 低水準Entry | Signature | 失敗条件 |
| --- | --- | --- |
| `ChangeGenericState` | `bool(int32_t, const char*)` | Manager/Componentなし、null/空、同一State。 |
| `GetGenericState` | `bool(int32_t, char*, int32_t)` | Componentなし、null Buffer、Capacity 0以下。 |

### Attribute Wrapper

```cpp
Attribute boost(gameObject);
float currentBoost = 0.0f;
float maximumBoost = 0.0f;

if (boost.Get(currentBoost, maximumBoost)) {
	boost.Set(currentBoost - 20.0f);
}
```

| Method | 低水準Entry | 契約 |
| --- | --- | --- |
| `Attribute::Set(float)` | `SetAttributeValue(id, value)` | Min-MaxへClampしてCurrentを直接設定する。 |
| `Attribute::Get(float&, float&)` | `GetAttributeValue(id, current*, maximum*)` | CurrentとMaximumを同時取得する。 |

値変更ActionはFloat Payloadで変更後Currentを渡す。Set直後のActionはRuntime Updateで差分検出してQueueされるため、同じCall stackで同期実行されると仮定しない。複数Resourceを1 Objectで名前検索するAPIではない。

### TargetLock Wrapper

```cpp
TargetLock targetLock(gameObject);
float lockProgress = 0.0f;
bool isLocked = false;
GameObject lockedTarget;

if (targetLock.GetState(lockProgress, isLocked, lockedTarget) &&
	isLocked && lockedTarget.HasReference()) {
	WeaponLoadout(gameObject).Fire();
}
```

`GetState`はTargetLock Component、進行率、Locked、Current Target IDを一度に取得する。Current Targetなしでは空GameObjectとなる。開始・完了・解除ActionはGameObject Payloadを持つため、受信時にSelectorを再検索せず通知対象を確定できる。ただしScene切替、Destroy、Pool返却後はIDが無効になり得るため、使用直前に`HasReference`と必要Componentを確認する。

### Version 7低水準Entry一覧

| Entry | 引数と出力 | 高水準入口 |
| --- | --- | --- |
| `InvokeScriptActionPayload` | Target ID、Action名、Payload pointer。 | `GameObject::InvokeAction(name, payload)` |
| `StartTimer` | Timer Owner ID。 | `Timer::Start` |
| `PauseTimer` | Timer Owner ID、pause bool。 | `Timer::Pause/Resume` |
| `GetTimerRemaining` | Timer Owner ID、float出力。 | `Timer::GetRemaining` |
| `ChangeGenericState` | State Owner ID、UTF-8 State名。 | `GenericStateMachine::ChangeState` |
| `GetGenericState` | State Owner ID、出力Buffer、Capacity。 | `GenericStateMachine::GetState` |
| `SetAttributeValue` | Attribute Owner ID、値。 | `Attribute::Set` |
| `GetAttributeValue` | Attribute Owner ID、Current/Maximum出力。 | `Attribute::Get` |
| `GetTargetLockState` | Lock Owner ID、Progress/Locked/Target出力。 | `TargetLock::GetState` |

全EntryはMain threadのPlay中に使う。Manager未初期化、Component不在、無効ID、null出力でfalseになる。API Pointerを直接呼ぶ場合は`runtimeApi != nullptr`、`apiVersion >= 7U`、対象Entry非nullを確認する。通常は同じ検査を行う高水準Wrapperを使う。

## 複数Lock・名前付き値・条件・Data API

```cpp
const GameObject owner{gameObjectId};

for (const MultiTargetLock::Entry& entry : MultiTargetLock{owner}.GetEntries()) {
	if (!entry.isLocked || !entry.target.HasReference()) {
		continue;
	}

	// TargetAssignmentを同じProjectileEmitterへ追加すれば、Weapon::FireProjectileがTarget別Queueを自動生成する。
}

AttributeSet resources{owner};
resources.Add("Heat", 12.0f);

GenericCounter defeatedCount{owner};
defeatedCount.Add(1.0f);

bool conditionResult = false;
GenericCondition{owner}.Evaluate(conditionResult);

float damage = 0.0f;
GameplayData{owner}.GetFloat("Damage", damage);
```

| Wrapper | 失敗条件 |
| --- | --- |
| `MultiTargetLock::GetEntries` | Component不在または停止中なら空配列。返却後にPool返却されたTargetは無効になり得る。 |
| `AttributeSet::Set/Get/Add` | Name空、該当Entryなし、Owner無効でfalse。Set/AddはMin/MaxへClamp。 |
| `GenericCounter::Set/Add/Get` | Component不在でfalse。変更Actionは同期CallbackではなくQueue。 |
| `GenericCondition::Evaluate` | 比較元Component/Propertyが見つからない場合、評価結果false。 |
| `GameplayData::GetString/GetInt/GetFloat/GetBool` | Key不在または保存TypeとGetter不一致でfalse。 |

Runtime API末尾には`Set/GetNamedAttributeValue`、`Set/Add/GetCounterValue`、`EvaluateGenericCondition`、`GetMultiTargetLockCount/Target`、`GetGameplayDataValue`を追加した。既存Entryの順序は変更していない。

## Damage・Projectile・Threat・Cooldown・Reset API

新規EntryはABI互換のため`EditorScriptRuntimeApi`末尾へ追加する。この時点のRuntime API Entryは169件である。Tagは固定Enumではなく、EngineとScriptが同じ`HashDamageTag`を使って`DamageContext.userTag`へ格納する。

## Weapon Pattern・Accuracy・TimeScale API

`Weapon`の発射Methodは低水準Weaponへ直結するだけではなく、同じOwner上の追加Componentを次の順で合成する。

```text
FireProjectile
  -> TargetAssignment（有効なLock対象がある場合）
  -> WeaponFirePattern（Target列を作れなかった場合）
  -> WeaponAccuracy（各実Shotの方向）
  -> ProjectileEmitter（Pool生成）
  -> WeaponRecoil（各実Shot）
  -> ImpactResponder（命中後）
```

```cpp
Weapon cannon{gameObject};

if (cannon.FireProjectile()) {
	const float currentSpread = cannon.GetAccuracySpread();
	Debug::Log("Projectile request accepted.");
}

TimeScale hitStop{gameObject};
hitStop.HitStop(0.08f);
hitStop.Play(0.25f, 0.5f);
const float currentTimeScale = TimeScale::GetCurrent();
```

| 高水準Method | 低水準Entry | 成功・失敗条件 |
| --- | --- | --- |
| `Weapon::FireHitscan` | `FireHitscan` | Hitscan有効、Cooldown 0で要求を受理。Pattern後の個別命中有無は戻り値へ含めない。 |
| `Weapon::FireProjectile` | `FireProjectile` | Emitter有効、Cooldown 0で要求を受理。Pool不足で一部Shotが生成できなくても列は継続する。 |
| `Weapon::GetAccuracySpread` | `GetWeaponAccuracySpread` | WeaponAccuracy不在/無効なら0。成功時はBase＋現在蓄積角度。 |
| `TimeScale::Play` | `PlayTimeScale` | Ownerに有効TimeScaleが必要。override -1はInspector値、0以上は指定値。 |
| `TimeScale::HitStop` | `PlayTimeScale` | Scale override 0で呼ぶ短縮Method。Durationは正値へ制限する。 |
| `TimeScale::GetCurrent` | `GetTimeScale` | Manager不在時1。0を含む現在倍率。 |

発射Methodは「要求受理」を返す。Charge/Burstの後段Shot、命中、Damage適用は後のUpdateで起きるため、同期してHit Objectを返すAPIではない。発射完了、命中、起爆はComponent Actionを受ける。

### 低水準Entry追加一覧

| Entry | Signature | null/失敗時 |
| --- | --- | --- |
| `GetWeaponAccuracySpread` | `bool(int32_t, float*)` | ID不正、Component不在、出力nullでfalse。 |
| `PlayTimeScale` | `bool(int32_t, float, float)` | TimeScale不在/無効でfalse。 |
| `GetTimeScale` | `float()` | Runtime未接続で1。 |

3 Entryは既存順序を変えず`EditorScriptRuntimeApi`末尾へ追加している。追加時点は172件であり、現行Runtime API Entryは211件である。

### Damage Tagと範囲Damage

```cpp
EditorScriptDamageContext damage{};
damage.targetGameObjectId = hitObject.GetInstanceId();
damage.sourceGameObjectId = gameObject.GetInstanceId();
damage.instigatorGameObjectId = gameObject.GetInstanceId();
damage.baseDamage = 20.0f;
damage.userTag = DamageTag::Id("Bullet");
gameObject.ApplyDamage(damage);

const int32_t damagedCount = AreaDamage{explosionObject}.Apply(gameObject);
```

| 高水準API | 低水準Entry | 戻り値・失敗条件 |
| --- | --- | --- |
| `DamageTag::Id(string)` | `HashDamageTag(const char*)` | 空文字またはAPIなしで0。大文字小文字を区別する安定ID。 |
| `AreaDamage{owner}.Apply(instigator)` | `ApplyAreaDamage(areaId, instigatorId)` | 実際にDamageを適用したHealth数。Component不在または対象なしは0。 |

`ApplyAreaDamage`は同期してPhysics OverlapとDamage適用を行う。大量の爆発を毎Frame呼ばず、起爆Event時だけ呼ぶ。HitZone、DamageReceiver、DamageTagModifierはManager内部で自動適用されるためScript側で倍率を重ねない。

### ProjectileDetonator

```cpp
ProjectileDetonator detonator{missileObject};

if (Input::GetKeyDown(KeyCode::Space)) {
	detonator.Detonate();
}
```

`Detonate()`はWeaponManagerが追跡中のActive Projectileだけtrueになる。成功時は起爆位置でAreaDamageとActionを実行し、Poolへ返却する。同じProjectileへ2回目を呼ぶとfalse。通常の接触・近接・寿命起爆はComponent設定だけで動作する。

### ThreatTracker

```cpp
for (const ThreatTracker::Entry& threat : ThreatTracker{player}.GetEntries()) {
	if (!threat.projectile.HasReference()) {
		continue;
	}

	const float warningSeconds = threat.estimatedArrivalSeconds;
	// HUD表示や回避規則はゲームScript側へ置く。
}
```

| 低水準Entry | Signature | 契約 |
| --- | --- | --- |
| `GetThreatTrackerCount` | `bool(id, int32_t*)` | Component内部snapshot件数。Component不在、null出力でfalse。 |
| `GetThreatTrackerEntry` | `bool(id, index, EditorScriptThreatInfo*)` | 0始まり。範囲外、null出力でfalse。 |

`EditorScriptThreatInfo`はProjectile ID、Source ID、Distance、Closing Speed、Estimated Arrival Secondsを持つ。返却IDは次FrameまたはPool返却で無効になり得るため、使用時に`HasReference()`を確認する。

### CooldownSet

```cpp
CooldownSet abilities{player};

if (abilities.IsReady("Boost")) {
	abilities.Start("Boost");
}

float remainingSeconds = 0.0f;
bool isReady = false;
abilities.Get("Boost", remainingSeconds, isReady);
abilities.Start("Special", 8.0f);
abilities.Reset("Boost");
```

| Method | 低水準Entry | 契約 |
| --- | --- | --- |
| `Start(name, durationOverride)` | `StartNamedCooldown` | -1はInspector既定時間、0以上は上書き。名前不在でfalse。 |
| `Reset(name)` | `ResetNamedCooldown` | 残り0へして使用可能にする。削除や初期時間への巻き戻しではない。 |
| `Get(name, remaining, ready)` | `GetNamedCooldown` | 残り秒と使用可否を同時取得。false時は出力を使わない。 |
| `IsReady(name)` | `GetNamedCooldown` | Entry不在もfalseなので「未登録=使用可能」にはしない。 |

完了ActionはString PayloadでCooldown名を渡す。Script側で毎Frame0跨ぎを再判定せず、HUD更新や再使用通知はActionを利用できる。

### RuntimeStateReset

```cpp
RuntimeStateReset pooledEnemy{enemy};

if (!pooledEnemy.Reset()) {
	Debug::Log("Runtime reset target was not found.");
}
```

`ResetRuntimeState(id)`はObjectPoolと同じReset callbackを明示実行する。RuntimeStateReset Componentがあれば選択Flagに従い、なければ互換動作として全対応状態をResetする。GameObject不在、ObjectPoolManager未接続でfalse。Prefabの編集値やSceneファイルは変更しない。

### 低水準Entry追加一覧

| Entry | 引数 | 戻り値 |
| --- | --- | --- |
| `HashDamageTag` | UTF-8 Tag | int32 stable ID。 |
| `ApplyAreaDamage` | AreaDamage Owner、Instigator | 適用Health数。 |
| `DetonateProjectile` | Active Projectile ID | 起爆できたか。 |
| `GetThreatTrackerCount` | Tracker Owner、count出力 | 取得可否。 |
| `GetThreatTrackerEntry` | Owner、index、info出力 | 取得可否。 |
| `StartNamedCooldown` | Owner、name、override秒 | 開始可否。 |
| `ResetNamedCooldown` | Owner、name | Reset可否。 |
| `GetNamedCooldown` | Owner、name、remaining/ready出力 | 取得可否。 |
| `ResetRuntimeState` | Reset対象ID | 実行可否。 |

全てMain threadのPlay中に使う。Pointerを直接呼ぶ場合はRuntime API本体とEntryのnullを確認する。通常は高水準Wrapperを使い、ManagerやComponent不在をfalseまたは0として処理する。

## 照準予測・Objective・Encounter Runtime API

新規7 Entryは既存DLLの関数Pointer位置を変えないため、`EditorScriptRuntimeApi`末尾へ追加する。通常のゲームコードでは低水準Pointerを直接保持せず、`EditorNativeScript.h`の高水準Wrapperを使う。

### Targeting::GetInterceptPrediction

```cpp
Targeting targeting{weaponObject};
EditorScriptVector3 leadPosition{};
float arrivalSeconds = 0.0f;

if (targeting.GetInterceptPrediction(leadPosition, arrivalSeconds)) {
	// Lead Marker表示、砲塔LookAt、非誘導弾のAimへ使う。
}
```

Ownerには有効`InterceptPrediction`が必要である。Componentが参照する明示TargetまたはTargetSelector、Projectile速度、最大秒から毎Frame更新済みの結果を返す。Targetなし、解析解なし、最大秒超過、出力Pointer不正でfalseとなる。false時の出力値を使わない。

### ObjectiveTracker

```cpp
ObjectiveTracker mission{missionObject};
mission.Set("DestroyRadar", ObjectiveState::Active, 0.0f);
mission.Set("DestroyRadar", ObjectiveState::Active, 1.0f);

ObjectiveState state = ObjectiveState::Inactive;
float current = 0.0f;
float target = 0.0f;

if (mission.Get("DestroyRadar", state, current, target)) {
	// HUDへ表示する。
}
```

| Method | 低水準Entry | 契約 |
| --- | --- | --- |
| `Set(id,state,current)` | `SetObjective` | ID一致Entryを更新し、変更ActionへString Payloadを送る。 |
| `Get(id,state,current,target)` | `GetObjective` | ID一致Entryのsnapshotを返す。 |

Stateは`Inactive=0`、`Active=1`、`Completed=2`、`Failed=3`である。存在しないIDを自動追加しないため、Scene側へObjectiveを明示登録する。ゲーム固有の報酬やMission分岐は変更Actionを受けたScriptで実装する。

### EncounterController

```cpp
EncounterController encounter{encounterObject};

if (!encounter.Start()) {
	Debug::Log("EncounterController was not ready.");
}
```

`StartEncounter(id)`はEncounterの現在Index、Delay、開始Flagを先頭へ戻す。参照WaveSpawnerは開始条件を`外部開始`に設定する。Play開始時実行とC++ Startを同じFrameで重複させない。完了通知はComponentの完了Actionを使い、同期戻り値は「開始要求を受理したか」だけを示す。

### SpawnPointSet

```cpp
SpawnPointSet spawnPoints{spawnPointSetObject};
EditorScriptVector3 position = spawnPointSetObject.GetTransform().position;
EditorScriptVector3 rotation{};

if (spawnPoints.Resolve(position, rotation)) {
	// Prefab生成APIへ渡す。
}
```

`ResolveSpawnPoint`はPoint Modeでは登録GameObjectのWorld位置・回転へ出力を置き換える。Volume Modeでは入力Positionを中心としてランダムOffsetを加えるため、呼出前に基準位置を入れる。候補なし、全参照無効、Component不在でfalseとなる。

### DifficultyParameterSet

```cpp
DifficultyParameterSet difficulty{settingsObject};

if (!difficulty.Apply(2)) {
	Debug::Log("Difficulty index or property mapping is invalid.");
}
```

`ApplyDifficulty(id,index)`は選択IndexのFloat/Int/Bool OverrideをRuntime Property経路で順に適用する。範囲外Indexでfalse、Override 0件は有効なPreset選択としてtrueである。一部Property不正でも残りを継続し、少なくとも1件成功すればtrueを返す。

### DamageDirectionIndicator

```cpp
DamageDirectionIndicator damageDirection{player};
EditorScriptVector2 direction{};
float alpha = 0.0f;
GameObject source{};

if (damageDirection.Get(direction, alpha, source)) {
	// UI位置 = 画面中心 + direction * Inspectorの画面端半径。
}
```

`GetDamageDirection`は表示残り時間が正の時だけtrueとなる。DirectionはGame Camera基準で正規化済み、AlphaはFade区間を含む0～1、Sourceは最後のDamage Sourceである。UI Image、回転、色、複数同時IndicatorはゲームUI側で構成する。

### 低水準Entry一覧

| Entry | Signature | false条件 |
| --- | --- | --- |
| `GetInterceptPrediction` | `bool(id, Vector3*, float*)` | Component不在、解なし、出力null。 |
| `SetObjective` | `bool(id, const char*, int32, float)` | 空ID、未登録ID、Manager不在。 |
| `GetObjective` | `bool(id, const char*, int32*, float*, float*)` | 未登録ID、いずれかの出力null。 |
| `StartEncounter` | `bool(id)` | Encounter不在、Play Runtime未構築。 |
| `ResolveSpawnPoint` | `bool(id, Vector3*, Vector3*)` | 候補なし、参照不正、出力null。 |
| `ApplyDifficulty` | `bool(id, int32)` | Index範囲外、Component不在、全Override失敗。 |
| `GetDamageDirection` | `bool(id, Vector2*, float*, int32*)` | 表示時間0、Component不在、出力null。 |

### 実行順とThread契約

これらはMain ThreadのPlay中に呼ぶ。Target/被弾方向は各Manager Update後のsnapshotであり、同じFrame内のComponent更新順を越えて即時計算し直すAPIではない。EncounterとDifficultyはScene状態を変更するため、描画Threadや非同期Scene読込Threadから呼ばない。

この章の追加時点ではRuntime API Entry 179件である。`GetInterceptPrediction`から`GetDamageDirection`までの7 Entryが既存172 Entryの後へ連続し、順序を変更しないことを機械照合する。現行総数は211件である。

## 弾道・複数被弾・Pause・航跡 C++ API

### BallisticPrediction

```cpp
const GameObject cannonMuzzle{gameObjectId};
BallisticPrediction ballistic{cannonMuzzle};
EditorScriptBallisticPrediction prediction{};

if (ballistic.Get(prediction)) {
	// Projectileの初速方向へ使う。
	const EditorScriptVector3 launchDirection = prediction.launchDirection;
	const EditorScriptVector3 impactPosition = prediction.impactPosition;
	const float flightTime = prediction.flightTime;
	const std::vector<EditorScriptVector3> points = ballistic.GetTrajectoryPoints();
}
```

| 高水準Method | 低水準Entry | 契約 |
| --- | --- | --- |
| `Get(prediction)` | `GetBallisticPrediction` | Valid時に発射方向、着弾位置、飛行秒、点数をsnapshotで返す。 |
| `GetTrajectoryPoints()` | `GetBallisticTrajectoryPoint` | 0からPointCount-1まで読み、失敗Pointを飛ばす。 |

Component不在、非Active、Targetなし、解なし、出力nullでfalseとなる。APIはProjectileを生成せず、戻した方向をProjectileEmitterまたはゲーム固有発射処理へ渡す。点列を毎Frameコピーしたくない場合はGetだけを使い、TrajectoryRendererへ描画を任せる。

### DamageEventBuffer

```cpp
DamageEventBuffer damageEvents{playerObject};

for (const EditorScriptDamageEvent& damageEvent : damageEvents.GetEntries()) {
	const GameObject source{damageEvent.sourceGameObjectId};
	const EditorScriptVector3 worldDirection = damageEvent.worldDirection;
	const float appliedDamage = damageEvent.damage;
	const float remainingSeconds = damageEvent.remainingSeconds;
	// World方向をGame Camera基準へ変換してHUD Imageへ割り当てる。
}
```

`GetDamageEventBufferCount`はComponentが有効なら0件でもtrueである。`GetDamageEventBufferEntry`はIndex範囲外、Component不在、出力nullでfalse。取得中にDamageManager Updateが走らないMain Thread契約なので、Count取得後の同Frame内ではIndexが安定する。

### GamePause

```cpp
GamePause pauseController{pauseManagerObject};

void PauseMenuController::OnPause(const EditorScriptInputActionContext& context) {
	if (context.phase == EditorScriptInputPhasePerformed) {
		pauseController.Pause();
	}
}

void PauseMenuController::OnResume(const EditorScriptInputActionContext& context) {
	if (context.phase == EditorScriptInputPhasePerformed) {
		pauseController.Resume();
	}
}
```

`SetGamePaused(id,bool)`はGamePause Component不在または別Ownerから不正にResumeした場合false。`IsGamePaused()`はGlobal状態を返す。Pause中もScript UpdateはDelta 0で呼ばれるため、時間積算を行うScriptはDeltaを使い、実時間が必要なPause UIはInput Action/Event駆動にする。

### SurfaceWakeEmitter

```cpp
SurfaceWakeEmitter wake{boatObject};
float speed = 0.0f;
float intensity = 0.0f;

if (wake.GetState(speed, intensity)) {
	// HUD、Audio、追加Spray強度などへ利用する。
}
```

`GetSurfaceWakeState`はComponentのRuntime snapshotを返す。SpeedはWorld移動速度ではなく、船の水平速度からOcean Surface Velocityを引いた水面相対速度である。Intensityはその速度を正規化し、FFTの泡率で0.85～1.15倍する。Effect再生やOcean SampleはComponent自身が担当するため、通常のScriptは毎FramePlayEffectを呼ばない。船速によるエンジン音Pitch等を追加する場合だけ状態を読む。

### 追加した低水準Entry

| Entry | Signature | false条件 |
| --- | --- | --- |
| `GetBallisticPrediction` | `bool(id, EditorScriptBallisticPrediction*)` | Component不在、解なし、出力null。 |
| `GetBallisticTrajectoryPoint` | `bool(id, index, Vector3*)` | Invalid、Index範囲外、出力null。 |
| `GetDamageEventBufferCount` | `bool(id, int32*)` | Component不在、出力null。 |
| `GetDamageEventBufferEntry` | `bool(id, index, EditorScriptDamageEvent*)` | Index範囲外、出力null。 |
| `SetGamePaused` | `bool(id, bool)` | Component不在、別OwnerからResume。 |
| `IsGamePaused` | `bool()` | Pause中だけtrue。失敗概念なし。 |
| `GetSurfaceWakeState` | `bool(id, float*, float*)` | Component不在、出力null。 |

7 Entryは既存179 Entryの順序を変えず`EditorScriptRuntimeApi`末尾へ追加する。現行Runtime API Entryは211件である。

## Score・Combo・Stage Result Script Template

### ScoreController

`OnClick`へ型付きActionを接続する。PayloadがFloatならその値、Intならfloatへ明示変換した値、それ以外は1.0を`GenericCounter::Add`へ渡す。Damage、命中部位、難易度、Combo倍率から最終Deltaを決める処理は生成後のScriptへ書く。

### ComboController

命中Actionを`OnClick`へ接続するとCounterを1増やしてTimerを先頭から開始する。Timerの満了Actionを同Scriptの`OnValueChanged`へ接続するとCounterを0へ戻す。Timer DurationがCombo猶予、UIValueBindingが表示更新を担当する。

### StageResultController

ステージ終了Actionを`OnClick`へ接続すると同ObjectのGenericCounterを読み、既定境界でS/A/B/Cを決める。結果は`SceneManager::SetFloat("StageScore", score)`と`SetString("StageRank", rank)`へ保存する。生成後にRank境界、命中率、被弾、残り時間、難易度倍率を変更し、Result SceneはSceneManagerから値を読む。

3 TemplateはComponentではなく編集可能なゲーム側C++雛形である。Engine ManagerへScore式、Combo式、Rank境界を固定しない。現行C++ Script Templateは24件である。

## FFT Ocean Gameplay Query API

### 高水準Wrapper

`EditorNativeScript.h`では`Physics::SampleOceanSurface`を互換維持し、新しい線分・Ray・遮蔽処理を`Ocean`へまとめる。

```cpp
EditorScriptOceanSegmentHit hit{};

if (Ocean::SegmentCast(
	owner,
	previousPosition,
	currentPosition,
	hit,
	0.0f)) {
	// hit.point、hit.normal、hit.surfaceVelocityを着水処理へ使う。
}
```

```cpp
EditorScriptOceanOcclusion occlusion{};

if (Ocean::IsOccluded(owner, cameraPosition, targetPosition, occlusion, 0.2f)) {
	if (occlusion.blocked) {
		// Lockを失うかはゲーム側が決める。
	}
}
```

`queryGameObject`はSample Request Keyを分離するために使う。`oceanGameObject`を省略すると-1を渡し、各Sample位置を覆う有効Oceanを検索する。明示Oceanを渡した場合は他のOceanへFallbackしない。

### 出力構造体

| Struct | Field | 意味 |
| --- | --- | --- |
| `EditorScriptOceanSegmentHit` | `oceanGameObjectId` | 命中Ocean ID。 |
| 同上 | `point` / `normal` | FFT水面上の交点と法線。 |
| 同上 | `surfaceVelocity` | 交点の水面速度。 |
| 同上 | `distance` | Segment/Ray開始点からのWorld距離。 |
| 同上 | `normalizedDistance` | Segment全長に対する0～1位置。 |
| `EditorScriptOceanOcclusion` | `blocked` | Clearanceを含む線分が波面へ入ったか。 |
| 同上 | `minimumClearance` | Query中で最小の波面法線方向距離。 |
| 同上 | `maximumSurfaceHeight` | Query中で観測した最大水面Y。 |
| 同上 | `intersection` | 最初の遮蔽交点。未遮蔽時は無効値として扱う。 |
| `EditorScriptWaterSurfaceState` | `state` | 0 Above、1 Entering、2 Underwater、3 Leaving。 |
| 同上 | `signedDistance` | 判定点と水面の符号付き距離。 |
| 同上 | `oceanGameObjectId` / `position` / `normal` / `velocity` | 現在Sample。 |
| `EditorScriptOceanProbeSample` | `distance` / `valid` | 設定距離とSample成否。 |
| 同上 | `position` / `normal` / `velocity` | 各距離の水面情報。 |
| 同上 | `relativeHeight` | Probe原点を基準にした水面Y差。 |

### Component Runtime Wrapper

```cpp
WaterSurfaceState waterState(owner);
EditorScriptWaterSurfaceState state{};

if (waterState.Get(state) && state.state == 2) {
	// 水中用の挙動へ切り替える。
}

float waterFoam = 0.0f;
waterState.GetFoam(waterFoam);

OceanProbeSet probes(owner);
EditorScriptOceanProbeSample sample{};

if (probes.GetSample(0, sample) && sample.valid) {
	// relativeHeightやnormalからゲーム側の判断を行う。
}

float probeFoam = 0.0f;
probes.GetFoam(0, probeFoam);
```

Wrapperは状態を更新しない。Component Managerが1 Frameに一度更新したSnapshotを返す。多数のScriptが同じProbeを必要とする場合は各ScriptからOcean Raycastを重複発行せず、OceanProbeSetを共有する。

### 追加した低水準Entry

| Entry | Signature要約 | false条件 |
| --- | --- | --- |
| `OceanSegmentCast` | `(queryId, oceanId, start*, end*, clearance, hit*)` | API/出力null、ゼロ長線分、有効Oceanなし、交点なし。 |
| `OceanRaycast` | `(queryId, oceanId, ray*, maxDistance, clearance, hit*)` | API/出力null、ゼロDirection、距離0以下、有効Oceanなし、交点なし。 |
| `QueryOceanOcclusion` | `(queryId, oceanId, start*, end*, clearance, result*)` | API/出力null、ゼロ長線分、有効Oceanなし。未遮蔽自体は成功で`blocked=false`。 |
| `GetWaterSurfaceState` | `(gameObjectId, state*)` | Component不在、Play前、出力null。 |
| `GetOceanProbeSample` | `(gameObjectId, index, sample*)` | Component不在、Index範囲外、出力null。Sample失敗は成功Snapshot内の`valid=false`で表す。 |
| `SampleOceanSurfaceDetailed` | `(queryId, position*, hit*, foam*)` | Scene/API/出力null、有効Oceanなし。既存Hit構造体を変えず泡率を追加取得する。 |
| `GetWaterSurfaceFoam` | `(gameObjectId, foam*)` | Component不在、現在Oceanなし、出力null。 |
| `GetOceanProbeFoam` | `(gameObjectId, index, foam*)` | Component不在、Index範囲外、Sample無効、出力null。 |

Ocean Query 5 Entryと今回の詳細3 Entryは既存Entryの順序を変えず`EditorScriptRuntimeApi`末尾へ追加する。現行Runtime API Entryは211件である。ABI Version確認、関数Pointer null確認、出力Pointer確認をWrapperとBridgeの両側で行う。

### Runtime Property

TargetSelectorのOcean遮蔽は汎用Property APIから変更できる。

```cpp
RuntimeProperty::SetInt(selector, "TargetSelector", "OcclusionMode", 3);
RuntimeProperty::SetFloat(selector, "TargetSelector", "OceanClearance", 0.2f);
```

`OcclusionMode`は0～3へClampし、`OceanClearance`は0以上へClampする。対応するGetInt/GetFloatも同名Propertyを使う。

### 性能と失敗時の扱い

Segment Castは多数の点Sampleを行うため、弾丸では前Frameから現在Frameの短い区間、Targetでは候補までの区間へ限定する。WeaponはPhysics HitとOcean Hitを両方求めても近い方だけを採用する。Ocean SampleはGPU Readbackを優先し、まだ利用できないFrameは同じOcean設定のCPU波面へFallbackするため、未取得を理由に弾を無条件ですり抜けさせない。

APIは`IsBigWave`、`IsTargetHidden`、`BecomeTorpedo`を提供しない。これらは返された高さ、法線、速度、遮蔽結果、状態を使ってゲーム側Scriptが決定する。

## 艦砲運用 Runtime APIと高水準Wrapper

### WeaponLoadout弾薬変更

`WeaponLoadout`は従来の選択、発射、Reload、選択Slot弾薬取得に加え、任意Slotの弾薬を取得・変更できる。`slotIndex=-1`は現在選択中のSlot、0以上はHierarchy順に収集された`WeaponLoadoutSlot` Indexである。

```cpp
WeaponLoadout loadout{playerObject};
int32_t currentAmmo = 0;
int32_t reserveAmmo = 0;
int32_t maximumAmmo = 0;

if (loadout.GetAmmoAtSlot(1, currentAmmo, reserveAmmo, maximumAmmo)) {
	// 補給箱から予備弾を30追加する。
	loadout.AddReserveAmmo(1, 30);
}

// UpgradeでMagazine上限を増やし、現在Magazineだけ満たす。
loadout.SetMaximumAmmo(1, maximumAmmo + 4);
loadout.RefillMagazine(1);
```

| Wrapper | 動作 | Clampと特殊値 |
| --- | --- | --- |
| `GetAmmoAtSlot(index,current,reserve,max)` | 任意Slotの3値を一括取得する。 | 無効IndexまたはPlay中に収集されていないSlotはfalse。 |
| `AddMagazineAmmo(index,amount)` | 現在Magazineへ加算する。負数で減算できる。 | 0～Maximum。 |
| `AddReserveAmmo(index,amount)` | Reserveへ加算する。負数で減算できる。 | 0～INT32_MAX。Reserve=-1なら無限弾を維持してtrue。 |
| `SetMagazineAmmo(index,amount)` | 現在Magazineを直接設定する。 | 0～Maximum。 |
| `SetReserveAmmo(index,amount)` | Reserveを直接設定する。 | -1～INT32_MAX。-1は無限弾。 |
| `SetMaximumAmmo(index,amount)` | Magazine上限を変更する。 | 0以上。現在値が新上限を超えた場合だけ切り下げる。 |
| `RefillMagazine(index)` | Reserveを消費せずMagazineをMaximumへ設定する。 | Stage補給、Debug、Upgrade適用向け。通常Reloadは`Reload()`を使う。 |

弾薬PickupでReserveを増やす場合は`AddReserveAmmo`、MagazineとReserveの間で時間付き補充を行う場合は既存`Reload`を使う。`RefillMagazine`はReserve移送規則を持たないため、通常のReload演出を置き換えない。

### WeaponGroup

```cpp
WeaponGroup mainBattery{batteryObject};
TurretAim turret{batteryObject};
EditorScriptTurretAimState aimState{};

if (turret.GetState(aimState) && aimState.canReachTarget && aimState.isAimed) {
	if (!mainBattery.IsFiring()) {
		mainBattery.Fire();
	}
}
```

`WeaponGroup::Fire`はGroup Modeに従って発射要求を開始する。SimultaneousとRoundRobinは要求をその場で処理し、Sequentialは予約Shotが残っている間`IsFiring()`がtrueになる。`Require All Ready`が有効で一つでも準備未完了なら、部分発射せずfalseを返す。

Groupから参照する各Weaponは独立したCooldown、WeaponFirePattern、Accuracy、Recoil、AttackCollisionFilterを持てる。Group WrapperはTarget、弾薬、Damage、砲塔回転を変更しない。

### TurretAim状態

`TurretAim::GetState`はManagerがそのFrameに更新したSnapshotを返す。

| Field | 意味 |
| --- | --- |
| `targetGameObjectId` | 明示TargetまたはTargetSelectorから解決した現在Target。未検出は-1。 |
| `canReachTarget` | 目標Yaw/Pitchが両方の可動範囲内ならtrue。 |
| `isAimed` | 到達可能かつYaw/Pitch誤差が許容角以内ならtrue。 |
| `yawErrorDegrees` | Clamp後の目標角と現在Yaw Pivot角の絶対誤差。 |
| `pitchErrorDegrees` | Clamp後の目標角と現在Pitch Pivot角の絶対誤差。 |

Scriptは`isAimed`を発射条件に使えるが、Engine側TurretAimは自動発射しない。Target変更時の警告音、発射禁止規則、Boss攻撃Pattern等はScriptまたはAction構成へ残す。

### 追加した低水準Entry

| Entry | Signature要約 | falseになる主条件 |
| --- | --- | --- |
| `LoadoutGetAmmoAtSlot` | `(loadoutId,index,current*,reserve*,max*)` | Manager/Slot不在、出力Pointer null。 |
| `LoadoutAddMagazineAmmo` | `(loadoutId,index,amount)` | Manager/Slot不在。Clamp自体は成功。 |
| `LoadoutAddReserveAmmo` | `(loadoutId,index,amount)` | Manager/Slot不在。無限弾への加算は状態維持で成功。 |
| `LoadoutSetMagazineAmmo` | `(loadoutId,index,amount)` | Manager/Slot不在。 |
| `LoadoutSetReserveAmmo` | `(loadoutId,index,amount)` | Manager/Slot不在。 |
| `LoadoutSetMaximumAmmo` | `(loadoutId,index,amount)` | Manager/Slot不在。 |
| `LoadoutRefillMagazine` | `(loadoutId,index)` | Manager/Slot不在。 |
| `FireWeaponGroup` | `(groupId)` | Component不在、Play前、有効Weaponなし、Require All Ready失敗。 |
| `IsWeaponGroupFiring` | `(groupId)` | Component不在時false。falseは正常な待機状態も表す。 |
| `GetTurretAimState` | `(turretId,state*)` | Component不在/無効、出力Pointer null。 |

10 Entryは既存191 Entryの順序を変えず`EditorScriptRuntimeApi`末尾へ追加する。現行Runtime API Entryは211件である。共有構造体`EditorScriptTurretAimState`はbool 2個の後へ明示Paddingを置き、Engine本体とDLLで同じ配置を維持する。

### 既存の名前付き値・Counter・複数Lock Entry

全201 Entryの名称照合で省略しないため、関連する高水準Wrapperと低水準名を明記する。

| Runtime API | 高水準Wrapper | 動作 | falseになる主条件 |
| --- | --- | --- | --- |
| `SetNamedAttributeValue` | `AttributeSet::Set` | 名前一致AttributeのCurrentを設定し、EntryのMin/Maxへ収める。 | Component不在、名前空、Entry不在。 |
| `SetCounterValue` | `GenericCounter::Set` | Generic Counterの値を直接設定し、閾値Action判定を更新する。 | Component不在。 |
| `AddCounterValue` | `GenericCounter::Add` | 現在値へDeltaを加え、Clampと閾値Action判定を行う。 | Component不在。 |
| `GetMultiTargetLockTarget` | `MultiTargetLock::GetEntries` | IndexごとのTarget ID、進行率、Lock完了Flagを返す。 | Component不在、Index範囲外、出力Pointer null。 |

`AttributeSet::Add`は`GetNamedAttributeValue`で現在値を読み、`SetNamedAttributeValue`へ明示的に加算結果を渡す。`MultiTargetLock::GetEntries`は先に`GetMultiTargetLockCount`で件数を取得し、各Indexへ`GetMultiTargetLockTarget`を呼ぶため、途中で削除されたEntryは結果配列へ追加しない。

### Script側の受入確認

1. `GetAmmoAtSlot(-1)`と明示Indexが別Slotを正しく返す。
2. Magazine/Reserve/Maximumの上下限とReserve=-1が維持される。
3. Turretが可動端外Targetを追っても`canReachTarget=false`になり、NaNを返さない。
4. Sequential Group中だけ`IsFiring=true`となり、完了後falseへ戻る。
5. API Pointerがnull、Componentが無効、参照Objectが削除済みでもWrapperがfalseを返しCrashしない。
6. Debug/Release双方で同じDLL ABIを読み込める。

## 発射前射線・状態効果・移動母体弾道 Runtime API

### BallisticPrediction出力の追加

`EditorScriptBallisticPrediction`は既存の`valid`、`launchDirection`、`impactPosition`、`flightTime`、`trajectoryPointCount`に加え、次を返す。

| Field | 単位 | 意味 |
| --- | --- | --- |
| `launchVelocity` | World unit/s | 発射元速度を含むProjectileの初期World速度。 |
| `sourceVelocity` | World unit/s | Rigidbody並進速度と砲口作用点の角速度成分を合成した速度。 |

`launchDirection`は砲口から見た射出方向で、`launchVelocity`の正規化方向とは移動母体が横速度を持つ場合に一致しない。弾道UIへ砲身方向を出す時は`launchDirection`、World軌道や速度表示には`launchVelocity`を使う。

Projectile Emitterの照準Sourceを`弾道予測`に設定すればScriptで方向を転送する必要はない。Scriptは発射許可条件や解なし時のTarget変更だけを担当する。

### FireLineCheck Wrapper

```cpp
FireLineCheck fireLine{turretObject};
EditorScriptFireLineState state{};

if (!fireLine.GetState(state)) {
	Debug::Log("FireLineCheck Component is missing.");
	return;
}

if (!state.isClear) {
	// state.blockingGameObjectIdとstate.blockingDistanceを警告UIへ使える。
	return;
}

Weapon weapon{turretObject};
weapon.FireProjectile();
```

| Field | 意味 |
| --- | --- |
| `isClear` | 現Frameの砲口前方が安全ならtrue。 |
| `blockingGameObjectId` | Blockerなしは-1。削除済み参照の再利用前にGameObject有効性を確認する。 |
| `blockingDistance` | 砲口から最初のBlockerまでのWorld距離。 |

Wrapperは検査を実行せず、Weapon Managerが更新したSnapshotを返す。発射API自体も同じFireLineCheckを再確認するため、Scriptが状態取得後にBlockerが変化しても危険な発射を成立させない。

### StatusEffectSet Wrapper

```cpp
StatusEffectSet status{targetObject};

// 攻撃元を記録してFireを適用する。
if (!status.Apply("Fire", attackerObject)) {
	Debug::Log("Fire definition is missing or already ignored.");
}

for (const EditorScriptStatusEffectEntry& entry : status.GetEntries()) {
	if (std::strcmp(entry.effectId, "Fire") == 0) {
		// HUDは残り秒とStack数だけを読み、Damage式はAction側に置く。
	}
}
```

| Wrapper | 動作 | false/空になる主条件 |
| --- | --- | --- |
| `Apply(effectId, source)` | Definitionを検索し、Refresh/Stack/Ignore規則で適用する。 | Component不在、空ID、Definition不在、Ignore中の重複。 |
| `Remove(effectId)` | 一致Entryを削除し終了Actionを通知する。 | Component不在、空ID、Active Entry不在。 |
| `Clear()` | 全Runtime Entryを削除する。 | Component不在。Entryが0でもComponentがあれば成功。 |
| `Has(effectId)` | 同じIDのRuntime Entry有無を返す。 | Component不在、空ID、未適用。 |
| `GetEntries()` | Count取得後、各Snapshotを`std::vector`へ複製する。 | API/Componentなしは空配列。 |

`EditorScriptStatusEffectEntry`は`effectId[64]`、`sourceGameObjectId`、`remainingSeconds`、`tickRemainingSeconds`、`stackCount`を持つ。Source ObjectはEffect中にDestroyされる可能性があるため、IDを使用する直前に参照を検証する。

開始/Tick/終了ActionのString PayloadはEffect IDである。Action受信側は文字列を固定Enumへ強制変換せず、ゲームデータまたはScriptの分岐で意味を決める。Engine Wrapperは`ApplyFireDamage`等のゲーム固有APIを提供しない。

### 追加した低水準Entry

| Entry | Signature要約 | falseになる主条件 |
| --- | --- | --- |
| `GetFireLineState` | `(gameObjectId, state*)` | Component不在、出力Pointer null。 |
| `ApplyStatusEffect` | `(gameObjectId,effectId,sourceId)` | Component/Definition不在、空ID、Ignore中の重複。 |
| `RemoveStatusEffect` | `(gameObjectId,effectId)` | Component不在、空ID、Active Entry不在。 |
| `ClearStatusEffects` | `(gameObjectId)` | Component不在。 |
| `HasStatusEffect` | `(gameObjectId,effectId)` | Component不在、空ID、未適用。 |
| `GetStatusEffectCount` | `(gameObjectId,count*)` | Component不在、出力Pointer null。 |
| `GetStatusEffectEntry` | `(gameObjectId,index,entry*)` | Component不在、Index範囲外、出力Pointer null。 |

7 Entryは既存201 Entryの順序を変えず`EditorScriptRuntimeApi`末尾へ追加する。現行Runtime API Entryは211件である。`EditorScriptFireLineState`はbool後のPaddingを明示し、`EditorScriptStatusEffectEntry`は固定長文字列を使ってEngine本体とDLLのABIを維持する。

### Script側の受入確認

1. FireLineCheckなし、Blocked、Clearを戻り値とStateで区別する。
2. Block中の`FireProjectile()`がfalseとなり、弾薬やPool状態を変更しない。
3. StatusEffectのDefinition不在、Ignore中重複、Active Entry不在をfalseとして処理する。
4. Refresh/Stack/Ignoreで残り秒、Tick待ち、Stack数がInspector設定どおりになる。
5. Source GameObject削除後もSnapshot取得でCrashしない。
6. BallisticPredictionの`launchVelocity`と実Projectile初速が一致する。
7. Debug/Release双方で208 Entryの末尾Pointerと共有構造体配置が一致する。

## Particle / VisualEffect Billboard Runtime Property

BillboardはParticleSystem / VisualEffect既存Componentの公開Propertyとして操作する。専用WrapperやRuntime API Entryを追加せず、表示演出から共通Runtime Property経路で変更する。

| Component内部名 | Property | 型 | 有効範囲 | 動作 |
| --- | --- | --- | --- | --- |
| `ParticleSystem` | `BillboardMode` | int | 0～3 | 板の向きをCamera Facing、Y軸固定、Velocity Facing、World XY固定から選ぶ。 |
| `VisualEffect` | `BillboardMode` | int | 0～3 | ParticleSystemと同じ。範囲外値は0～3へ制限する。 |
| `ParticleSystem` | `BillboardStretch` | float | 0.01以上 | Velocity Facingの速度方向軸だけを伸ばす。 |
| `VisualEffect` | `BillboardStretch` | float | 0.01以上 | ParticleSystemと同じ。0以下は0.01へ制限する。 |

```cpp
GameObject tracerEffect = GameObject::Find("TracerEffect");

if (!RuntimeProperty::SetInt(
		tracerEffect,
		"ParticleSystem",
		"BillboardMode",
		2)) {
	Debug::Log("ParticleSystem BillboardMode was not found.");
}

RuntimeProperty::SetFloat(
	tracerEffect,
	"ParticleSystem",
	"BillboardStretch",
	4.0f);
```

`SetInt`へStretchを渡す、`SetFloat`へModeを渡す、内部Component名を日本語表示名で渡す、Property名の大文字小文字を変える、対象Componentがない場合はfalseを返す。Getterも同じ型契約を使う。

Billboard Modeは描画時の向きだけを変更する。発生数、速度、寿命、Collision、Damage、Effect再生状態は変更しない。FBX / OBJ Render Assetを使うModel Particleは3D Transformを維持するため、このPropertyを設定しても見た目は変わらない。

Script側の受入確認:

1. Mode 0～3を順に設定し、Inspector値と描画結果が一致する。
2. 範囲外Modeと0以下Stretchが安全な範囲へ制限される。
3. ParticleSystemとVisualEffectの同名Propertyを別々に変更できる。
4. Scene ViewとGame Viewが別Cameraでも正対結果が混ざらない。
5. Model ParticleではProperty変更後もMesh姿勢が変化しない。
6. Debug/Release双方で既存208 EntryのABI順序が変わらない。

この章追加後のRuntime API Entryは214件である。

## Rail制作支援C++ API

追加APIは`EditorScriptRuntimeApi`末尾へ配置し、既存Entry順を変更しない。通常は関数ポインタを直接呼ばず、`RailFollower` Wrapperを使う。

```cpp
GameObject playerShip = GameObject::Find("Player Ship");
RailFollower railFollower{playerShip};

railFollower.SetSpeedProfileEnabled(true);

float currentSpeedMultiplier = 1.0f;

if (railFollower.GetSpeedMultiplier(currentSpeedMultiplier)) {
	Debug::Log("Rail speed multiplier updated.");
}

const std::string activeZoneId = railFollower.GetActiveZone();

if (activeZoneId == "BossApproach") {
	railFollower.SetMoveInput({0.0f, 0.0f});
}
```

### `bool SetSpeedProfileEnabled(bool isEnabled)`

- 対象GameObjectに`RailSpeedProfile`がある場合だけtrueを返す。
- `RailMovement`の基準速度、Key配列、Zone倍率は変更しない。
- false中の現在倍率はSpeedProfile分だけ1.0となり、RailZone倍率は残る。

### `bool GetSpeedMultiplier(float& speedMultiplier)`

- Play中にRailMovement Managerが計算した`SpeedProfile x RailZone`を返す。
- 対象のRuntime Stateがまだない場合はfalseを返す。
- 逆走の符号は含めず倍率は0以上である。符号付き目標速度は`GetState().targetSpeed`を使う。

### `std::string GetActiveZone()`

- 現在進入中の`RailZone::Zone ID`を返す。
- 区間外、Componentなし、参照失敗時は空文字列を返す。
- Zone進入/退出を確実に一度処理したい場合は、毎Frame文字列比較する代わりにInspectorのActionをC++ Script関数へ接続する。

```cpp
void OnRailZoneEntered(const EditorScriptInputActionContext& context) {
	const std::string zoneId = context.payloadString;

	if (zoneId == "HighSpeed") {
		Debug::Log("Entered high speed zone.");
	}
}
```

`OnRailZoneEntered`等は固定関数名ではない。Inspectorの進入Action/退出Actionで任意の公開Action名を指定する。Payload TypeはStringである。ゲーム固有のBoss開始、敵生成、BGM切替は受信Scriptに置き、RailZoneへ埋め込まない。

### Component連携例

プレイヤー艇は`RailMovement + RailSpeedProfile + RailZone + SpeedFeedback`、Main Cameraは`Camera + CameraFollowComposer`として構成する。敵Wave Rootは`WaveSpawner + SpawnedObjectSetup + WaveMotionProfile`とし、生成Templateへ`RailMovement + Team + Health`を付ける。

ScriptはZone Action、Wave Spawned Action、RailFollower APIを接着するだけでよい。毎Frame全敵を名前検索したり、生成個体ごとにRail PathやTeamを設定する処理は書かない。

### エラー確認

1. APIがfalseなら対象GameObject IDと必要Componentの有無を`HasComponent`で確認する。
2. `GetActiveZone()`が空ならPlay中か、Rail Pathが2点以上か、現在進行率がZone範囲内か確認する。
3. 速度が変化しない場合はRailSpeedProfile Component Active、`プロファイルを使用`、Key順、RailZone倍率を確認する。
4. 古いScript DLLは末尾追加前のAPIだけを使える。新APIを使うScriptは現在の`EditorScriptApi.h`で再ビルドする。

## Camera追従・RailMovement船体推進 Runtime Property

Camera追従方式と船体推進設定は専用ABI Entryを増やさず、既存Runtime Property APIで変更する。Component内部名、Property名、値型は大文字小文字を含めて一致させる。

| Component内部名 | Property | 型 | 範囲 | 動作 |
| --- | --- | --- | --- | --- |
| `Camera` / `CinemachineCamera` | `FollowPositionSpace` | int | 0～1 | 0=World固定Offset、1=Target Local Offset。 |
| `Camera` / `CinemachineCamera` | `FollowRotationMode` | int | 0～2 | 0=Camera角度固定、1=Target回転継承、2=Targetを見る。 |
| `RailMovement` | `MovementMode` | int | 0～2 | 0=Transform、1=物理サーボ、2=船体推進。 |
| `RailMovement` | `LocalForwardAxis` | int | 0～3 | 0=+Z、1=-Z、2=+X、3=-X。 |
| `RailMovement` | `ShipHorizontalThrust` | bool | true/false | 船首推力をWorld水平面へ射影する。 |
| `RailMovement` | `ShipLateralAssist` | float | 0～1 | Rail横方向PD補助率。 |

```cpp
GameObject gameCamera = GameObject::Find("Main Camera");
GameObject playerShip = GameObject::Find("Player Ship");

RuntimeProperty::SetInt(gameCamera, "Camera", "FollowPositionSpace", 1);
RuntimeProperty::SetInt(gameCamera, "Camera", "FollowRotationMode", 1);

RuntimeProperty::SetInt(playerShip, "RailMovement", "MovementMode", 2);
RuntimeProperty::SetInt(playerShip, "RailMovement", "LocalForwardAxis", 0);
RuntimeProperty::SetBool(playerShip, "RailMovement", "ShipHorizontalThrust", true);
RuntimeProperty::SetFloat(playerShip, "RailMovement", "ShipLateralAssist", 0.2f);
```

値範囲外はSetter側でClampする。Getterは現在のComponent設定を返す。GameObjectに対象Componentがない、内部名が日本語表示名、Property名または値型が異なる場合はfalseになる。追従対象参照とCamera Transform Offsetは既存Scene/Transform操作で設定し、このRuntime Property群は追従計算方式だけを切り替える。

船体推進へ切り替える前に、対象へDynamicかつ非KinematicなRigidbody、RailMovement、2点以上のRail Pathがあることを確認する。Buoyancyと併用する場合、Runtime Propertyだけで軸分担を変更するより、Inspectorの`浮力併用プリセット`で初期設定してからゲーム中にModeや補助率を変更する方が安全である。

Script側の受入確認:

1. Follow Position/Rotationを全Modeへ切り替え、Game Viewの採用Cameraだけが変わる。
2. MovementMode 0/1/2でTransform直書き、Spline Force、船首Forceが切り替わる。
3. LocalForwardAxis 0～3でModel船首と実Force方向が一致する。
4. ShipLateralAssist 0と1で横滑り量とRail復帰力に差が出る。
5. 不正な型・名前・Componentなしはfalseで、他Componentを変更しない。
6. Debug/Release双方で既存208 EntryのABI順序が変わらない。

現行Runtime API Entryは211件のままである。

## 距離LOD・レールMarker Runtime API

今回追加した低水準Entryは`EditorScriptRuntimeApi`末尾の`RearmRailEventMarkers`と`GetSimulationLodLevel`である。既存Entryの順序を変更しない。通常のゲームC++ Scriptは関数Pointerを直接呼ばず、`RailFollower`と`SimulationLod`の型付きWrapperを使う。

### RailFollower::RearmEventMarkers

```cpp
const GameObject owner{gameObjectId};
RailFollower railFollower{owner};

const bool markerFound = railFollower.RearmEventMarkers("WaveA");
const bool anyMarkerFound = railFollower.RearmEventMarkers();
```

| 項目 | 契約 |
| --- | --- |
| 対象 | `gameObjectId`上のRailEventMarker Component。RailMovement Ownerと同一Objectへ置く。 |
| 引数 | nullは禁止。空文字は全Entry、非空文字はMarker ID完全一致。 |
| 戻り値true | 1件以上の一致Entryを再通知可能にした。既に未通知でもID一致Entryがあればtrue。 |
| 戻り値false | Runtime API未接続、Object不正、RailEventMarker不在、指定ID不在。 |
| 変更範囲 | Runtime通知済みFlagだけ。Marker設定、進行率、Action名、Scene保存値を変更しない。 |

再Armした瞬間にActionは発生しない。次に進行率がMarkerを横切った時だけ発生する。Loopの各周回で通知したいだけならInspectorの`Play中1回だけ=false`を使い、ゲーム条件成立後だけ再許可したい場合にAPIを使う。

### SimulationLod::GetLevel

```cpp
const GameObject owner{gameObjectId};
int32_t lodLevel = 0;

if (!SimulationLod{owner}.GetLevel(lodLevel)) {
	return;
}

if (lodLevel >= 2) {
	// Far用の低頻度処理へ切り替える。
}
```

| 値 | 名前 | Engine側の意味 | Script側の典型用途 |
| --- | --- | --- | --- |
| 0 | Near | 全ComponentをPlay開始時Activeへ復元する。 | 毎Frameの高精度処理。 |
| 1 | Medium | 現行は分類値のみ。Engine側でComponent停止しない。 | 2～4Frameごとの判断、低品質Query。 |
| 2 | Far | Inspectorで選択したPhysics / Script / AI / Animation / Effectを停止する。 | 停止対象外Scriptから粗い状態だけ管理する。 |
| 3 | Culled | Ownerまたは階層をInactiveにする。 | 通常はScript自体が呼ばれないため、外部Manager側の状態確認用。 |

`GetLevel`は値取得だけで距離やActive状態を変更しない。Component不在、Runtime API未接続、不正Object、出力Pointer nullではfalseである。`FarでScript停止=true`の場合、そのOwnerのScriptはFarへ入った後にUpdateされないため、自分自身で復帰を監視する設計にしない。復帰判定はEngine側が担当する。

### RailEventMarker Action Payload

RailEventMarkerはMarker通過時に`phase=Performed`、`payloadType=String`、`payloadString=Marker ID`としてActionをQueueする。受信側は固定Action名をConstructorで登録する。

```cpp
BindAction("OnRailMarker", [this](const EditorScriptInputActionContext& inputContext) {
	if (inputContext.phase != EditorScriptInputPhasePerformed ||
		inputContext.payloadType != EditorScriptActionPayloadTypeString ||
		inputContext.payloadString == nullptr) {
		return;
	}

	const std::string markerId = inputContext.payloadString;
	// markerIdの意味はゲーム固有Scriptで決める。
});
```

Action対象にScriptがない、Action名が未登録、ComponentまたはScriptがInactive、Action名の大文字小文字が違う場合は受信されない。Marker側は敵、Boss、BGM等の意味を解釈しない。

## 追加C++ Script Template

### レールイベント受信

`移動・イベント / レールイベント受信`は`OnRailMarker`をBindし、String PayloadからMarker IDを取得してログへ出す開始コードを生成する。RailEventMarkerのAction名を`OnRailMarker`へ合わせる。生成コードの`markerId`分岐へWave開始、Camera Blend、ActionRelay等のゲーム固有接続を書く。Updateは処理を持たず、Marker通過時だけ動く。

推奨Componentは`RailMovement / RailEventMarker / ActionRelay`である。Marker位置と通過判定はComponent、Marker IDの意味と処理は生成後のC++ Scriptへ分離する。

### シミュレーションLOD参照

`最適化 / シミュレーションLOD参照`はUpdateで`SimulationLod::GetLevel`を呼び、0～3の段階を取得する開始コードを生成する。Engineが自動停止する系統以外にも、ゲーム固有Query頻度、探索半径、演出品質などを段階化したい時に使う。

このTemplateは距離計算、Object Active変更、Physics停止を重複実装しない。`FarでScript停止=true`にした同一ObjectではTemplate自身も停止するため、Far中も独自処理が必要ならScript停止をfalseにして、取得したLevelで処理頻度を落とす。

### ABI・受入条件

2 EntryはRuntime API末尾へ追加し、WrapperはAPI本体、Function Pointer、GameObject ID、出力値を確認する。API Version 7と既存Entry順序は維持するため、古いDLLは既存Prefix APIを継続利用できる。新Wrapperを使うScript DLLだけ、新Headerを使ってDebug / Releaseを再Buildする。

| 試験 | 合格条件 |
| --- | --- |
| 全Marker再Arm | 空文字で全Entryが次回通過時に再通知可能になる。 |
| 指定Marker再Arm | ID一致Entryだけが再通知され、不一致IDではfalse。 |
| LOD取得 | Inspector Runtime表示と`GetLevel`の0～3が一致する。 |
| Action受信 | Template生成ScriptがMarker IDをStringとして受ける。 |
| Template Build | 2 Templateから生成したDLLがDebug / ReleaseでCompileできる。 |

この章追加後の現行基準はRuntime API Entry 216件、C++ Script Template 26件、Component 277件である。

## WaveSpawner外部制御とSimulationLOD Update契約

### WaveSpawner Wrapper

```cpp
const GameObject waveObject = GameObject::Find("Wave A");
WaveSpawner waveSpawner{waveObject};

if (!waveSpawner.Start()) {
	// Component不在、未初期化、または前回Waveがまだ実行中。
}

const bool allReturned = waveSpawner.IsComplete(true);
const bool allSpawned = waveSpawner.IsComplete(false);
```

| Wrapper | 低水準API | 動作 | falseの意味 |
| --- | --- | --- | --- |
| `WaveSpawner::Start()` | `StartWaveSpawner` | 外部開始Waveを先頭から開始する。 | Object不正、Manager未開始、WaveSpawnerなし、実行中個体あり。 |
| `WaveSpawner::IsComplete(false)` | `IsWaveSpawnerComplete(..., false)` | 全予定個体を生成済みならtrue。 | 未完了または対象不正。 |
| `WaveSpawner::IsComplete(true)` | `IsWaveSpawnerComplete(..., true)` | 全個体が撃破、Inactive、Pool返却済みならtrue。 | 未完了または対象不正。 |

完了falseはエラーと未完了を区別しないため、対象GameObjectとComponent存在はScene設計時に確定させる。Start成功は1Frame内の全生成成功を意味しない。生成は`1Frame最大生成数`とPool空きに従って後続Updateへ分散する。

### SimulationLOD中のC++ Update

EngineはScript Componentごとに、次回Updateまでの残り秒と累積deltaTimeを保持する。Medium/FarでUpdateを省略した場合、次回呼出しには省略期間の合計秒を渡す。

```cpp
void EnemyController::Update(int32_t gameObjectId, float deltaTime) {
	// deltaTimeは常に描画1Frame分とは限らない。
	decisionTimer_ += deltaTime;

	int32_t lodLevel = 0;
	SimulationLod{GameObject{gameObjectId}}.GetLevel(lodLevel);
}
```

Frame数前提の`timer++`ではなく秒単位で計算する。入力開始、終了、クリック、Marker Action等はUpdate待ち行列へ入れず、そのイベントFrameにAction関数へ通知する。FixedUpdateはPhysics周期を維持する。Farで`Script停止=true`ならScript Component自体がInactiveになるため、Update、Action、FixedUpdateを実行しない。

### ABI

`StartWaveSpawner`と`IsWaveSpawnerComplete`はRuntime API構造体末尾へ追加する。API Version 7と既存Entry順序は維持する。新Wrapperを使用するDLLは現行`EditorNativeScript.h`でDebug / Releaseを再Buildする。

現行の機械照合基準はRuntime API Entry 218件、C++ Script Template 26件、Component 277件である。
