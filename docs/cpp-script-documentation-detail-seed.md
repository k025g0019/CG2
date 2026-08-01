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
| `EditorScript_Unload` | DLL 解放時に 1 回。 | 全 GameObject の一時状態を破棄。 |
| `EditorScript_Start` | Play 開始時、GameObject ごとに 1 回。 | 初期化、ログ、初期値設定。 |
| `EditorScript_Update` | 毎フレーム。 | 入力、Transform 操作、通常更新。 |
| `EditorScript_FixedUpdate` | 固定時間更新。 | 物理 Force、Impulse、Torque。 |
| `EditorScript_OnPhysicsEvent` | Collision / Trigger 発生時。 | 当たり判定イベント処理。 |
| `EditorScript_OnAnimationEvent` | Animation ClipのEvent時刻通過時。 | Event名、文字列、数値を受けて演出や処理を起動。 |
| `EditorScript_Stop` | Play 停止または Script 停止時。 | GameObject ごとの状態削除。 |
| `EditorScript_InvokeAction` | UI / Input Action Event から呼ばれた時。 | 関数名による任意処理の実行。 |
| `EditorScript_GetActionCount` | InspectorがDLLのAction候補を取得する時。 | `BindAction`済み名称数を返す。 |
| `EditorScript_GetActionName` | Inspectorが候補名を列挙する時。 | Indexに対応するAction名を返す。 |

## Inspector 公開変数用関数

これらは C++ 側の変数を Inspector に出すための関数である。

| 関数 | 目的 |
| --- | --- |
| `EditorScript_GetFieldCount` | Inspector に出す公開変数の数を返す。 |
| `EditorScript_GetFieldDescriptor` | 変数名、表示名、型、初期値、範囲を返す。 |
| `EditorScript_GetFieldValue` | GameObject ごとの現在値を返す。 |
| `EditorScript_SetFieldValue` | Inspector で変更された値を C++ 側へ反映する。 |

対応する型は次である。

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

高水準API:

```cpp
GameObject target = GameObject::Find("Target");

if (target.HasReference()) {
	target.SetActive(true);
	EditorScriptTransform transform = target.GetTransform();
	transform.position.y += 1.0f;
	target.SetTransform(transform);
}
```

頻繁に使う参照は毎Frame名前検索せず、`ExposeGameObject`でInspectorから設定して保持する。名前検索は初期化や任意候補探索に限定する。

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
| `SwitchRail` | `SetRailPath` | 別Rail Pathへ切り替える。進行率を維持するか指定できる。 |
| `GetNormalizedProgress` | `GetRailNormalizedProgress` | 現在進行率を取得する。 |
| `GetLength` | `GetRailLength` | Rail全長を取得する。 |
| `GetPosition` | `GetRailPosition` | 任意進行率のWorld位置を取得する。 |
| `GetDirection` | `GetRailDirection` | 任意進行率の進行方向を取得する。 |
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

### Rigidbody 高水準API

`Rigidbody` Wrapperは`GetVelocity`、`SetVelocity`、`AddForce`、`AddImpulse`、`AddTorque`を提供する。

```cpp
Rigidbody rigidbody{gameObjectId};
rigidbody.AddImpulse(EditorScriptVector3{0.0f, 5.0f, 0.0f});
```

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

| 送信元 | Action | `buttonValue` |
| --- | --- | --- |
| WaveSpawner | 開始 | 1.0 |
| WaveSpawner | 各生成 | 有効化したGameObject IDをfloatへ変換した値。 |
| WaveSpawner | 完了 | 直下の子GameObject数。 |
| TimelineEvent | 発火 | 発火秒または発火進行率の設定値。 |
| ThresholdState | State変更 | State 1 / 2 / 3に対応する1.0 / 2.0 / 3.0。 |

受信側は`inputContext.phase == EditorScriptInputPhasePerformed`を確認する。

```cpp
void StageEventReceiver::OnWaveSpawned(
	const EditorScriptInputActionContext& inputContext) {
	if (inputContext.phase != EditorScriptInputPhasePerformed ||
		inputContext.valueType != EditorScriptInputValueTypeButton) {
		return;
	}

	const int32_t spawnedGameObjectId =
		static_cast<int32_t>(inputContext.buttonValue);
	GameObject spawnedObject{spawnedGameObjectId};

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

#include <unordered_map>

namespace {
	const EditorScriptRuntimeApi* runtimeApi = nullptr;
	std::unordered_map<int32_t, NewNativeScript> scriptStates;

	NewNativeScript& GetState(int32_t gameObjectId) {
		return scriptStates[gameObjectId];
	}
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
	scriptStates.clear();
	runtimeApi = nullptr;
}

extern "C" __declspec(dllexport) void EditorScript_Start(int32_t gameObjectId) {
	if (runtimeApi == nullptr) {
		return;
	}

	NewNativeScript& state = GetState(gameObjectId);
	state.isStarted = true;
	runtimeApi->Log("Script Start");
}

extern "C" __declspec(dllexport) void EditorScript_Update(
	int32_t gameObjectId,
	float deltaTime) {
	if (runtimeApi == nullptr) {
		return;
	}

	NewNativeScript& state = GetState(gameObjectId);
	EditorScriptTransform transform = runtimeApi->GetTransform(gameObjectId);

	transform.position.x += state.moveSpeed * deltaTime;

	runtimeApi->SetTransform(gameObjectId, &transform);
}

extern "C" __declspec(dllexport) void EditorScript_FixedUpdate(
	int32_t gameObjectId,
	float fixedDeltaTime) {
	(void)gameObjectId;
	(void)fixedDeltaTime;
}

extern "C" __declspec(dllexport) void EditorScript_OnPhysicsEvent(
	int32_t gameObjectId,
	const EditorScriptPhysicsEvent* physicsEvent) {
	if (runtimeApi == nullptr || physicsEvent == nullptr) {
		return;
	}

	(void)gameObjectId;
}

extern "C" __declspec(dllexport) void EditorScript_Stop(int32_t gameObjectId) {
	scriptStates.erase(gameObjectId);
}
```

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
