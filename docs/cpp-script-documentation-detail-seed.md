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
| C++ ソース | 使用者がゲーム処理だけを書く `.cpp`。ABIコードは置かない。 |
| Header | `Script`を継承し、使用するライフサイクルとActionだけを宣言する `.h`。 |
| Generated Source | Engineが作る `.Generated.cpp`。DLL ABI、実体生成、Field、Action転送を保持し、使用者は編集しない。 |
| Build Script | ユーザー `.cpp` と `.Generated.cpp` からDebug / Release DLLを作る `.bat`。 |
| DLL | Play 中に Engine が読み込む Script 本体。 |
| DLL Path | Inspector の Script Component に設定する DLL の場所。 |

## ユーザーが書くライフサイクル

新規Scriptは`Script`を継承する。使用者は必要な関数だけ宣言・実装し、書かなかった関数は基底クラスのno-opになる。

| 関数 | 呼ばれるタイミング | 主な用途 |
| --- | --- | --- |
| `Start()` | Inspector値反映後に1回。 | 初期化、ログ、初期値設定。 |
| `Update(float deltaTime)` | ActiveなComponentへ毎フレーム。 | 入力、Transform操作、通常更新。 |
| `FixedUpdate(float fixedDeltaTime)` | ActiveなComponentへ固定時間更新。 | 物理Force、Impulse、Torque。 |
| `OnCollisionEnter(...)`など | 所有ObjectのCollision / Trigger発生時。 | 当たり判定イベント処理。 |
| `OnAnimationEvent(...)` | Animation ClipのEvent時刻通過時。 | Event名、文字列、数値を受けて演出や処理を起動。 |
| `Stop()` | Play停止またはScript停止時。 | ユーザー状態の終了処理。 |

Engineは`.Generated.cpp`内でDLL読込、Instance生成、ライフサイクル、Physics Event、Animation Event、Field、ActionのABI転送を生成する。使用者が`extern "C"`や`EditorScript_*`関数を書くことは禁止する。

## Inspector 公開変数

公開変数はユーザー`.cpp`先頭の`SCRIPT_FIELD_*`で登録できる。値はScriptインスタンスごとに保持され、`FieldFloat("moveSpeed")`などで読む。`.h`へメンバーやConstructorを追加する必要はない。従来の`ExposeBool`、`ExposeFloat`も互換APIとして使用できる。InspectorとのABI転送はGenerated側が行う。

```cpp
#include "PlayerMove.h"

SCRIPT_FIELD_FLOAT(moveSpeed, "移動速度", 6.0f, 0.0f, 30.0f, 0.1f)

void PlayerMove::Update(float deltaTime) {
	const float speed = FieldFloat("moveSpeed");
	// ゲーム処理だけを書く。
}
```

Componentは`GameObject::AddComponent<T>()`、`GetOrAddComponent<T>()`、`RemoveComponent<T>()`から実行時に変更できる。`Instantiate`、`Destroy`、`FindAllWithComponent`、World/Local座標変換もGameObject APIへ統合されている。

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

高水準基底クラス`Script`では、次の関数で登録する。

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

### 複数接続Wire API

`Wire`は`RopeConstraint`の事前配置を必要としない実行時Handleである。1つのGameObjectへ複数本を接続でき、各Wireを個別に生成、破棄、収縮、長さ変更、修復できる。

- 接続点は`HookPoint`で明示する。互換Component名は`WireConnectable`だが、物体全体ではなく専用Hookへ配置する。
- 子Hookの`力を伝えるRigidbody`へ親物体を指定すれば、選択・表示はHook、Forceは親Rigidbodyへ分離できる。
- `Physics::FindBestHook`は選択距離、選択角度、Colliderによる遮蔽を検証してHook候補を返す。接続可否は別途`HookPoint::CanConnect()`で確認する。
- Anchorは`GameObject::WorldToLocalPoint`で回転・Scale・親Hierarchy込みのローカル座標へ変換する。
- `HookPoint::SetVisualState`で通常、照準中、選択中、接続中の色と発光を切り替える。
- `WireRenderer`は通常色、高張力色、破断色、ワールド半径、発光、たるみ、長手方向の分割数を持つ。現行描画は画面へ投影したImGui線であり、立体メッシュではない。詳細な制約はComponent仕様書のWireRenderer節を参照する。
- `Renderer::SetColor`と`SetEmission`により、質量を正規化した連続色やHookの視認性をScriptから変更できる。
- `GameObject::Create`、`SetParent`、`GetParent`、`GetChildCount`、`GetChild`で実行時Hierarchyを構築できる。
- `SceneManager::Reload`で現在のPrimary Sceneを初期状態から読み直せる。
- `OnWireConnected`、`OnWireTensionChanged`、`OnWireBroken`、`OnWireTargetLost`、`OnWireDestroyed`を必要なScriptだけoverrideする。

```cpp
// first / secondは接続先Hook、firstAnchor / secondAnchorは各Hookのローカル座標。
// この例は関数内に置き、<cmath>をincludeする。
EditorScriptVector3 firstWorldAnchor{};
EditorScriptVector3 secondWorldAnchor{};

if (!first.LocalToWorldPoint(firstAnchor, firstWorldAnchor) ||
	!second.LocalToWorldPoint(secondAnchor, secondWorldAnchor)) {
	return;
}

const float anchorDeltaX = secondWorldAnchor.x - firstWorldAnchor.x;
const float anchorDeltaY = secondWorldAnchor.y - firstWorldAnchor.y;
const float anchorDeltaZ = secondWorldAnchor.z - firstWorldAnchor.z;

EditorScriptWireDesc desc{};
desc.firstGameObjectId = first.GetInstanceId();
desc.secondGameObjectId = second.GetInstanceId();
desc.firstLocalAnchor = firstAnchor;
desc.secondLocalAnchor = secondAnchor;
// 異なるObjectのローカル座標同士を引かず、World座標で初期ロープ長を求める。
desc.maximumLength = std::sqrt(
	anchorDeltaX * anchorDeltaX + anchorDeltaY * anchorDeltaY + anchorDeltaZ * anchorDeltaZ);
desc.minimumLength = 0.5f;
desc.requireConnectable = true;

Wire wire = Wire::Create(desc);

if (wire.IsValid()) {
	wire.SetShrinkSpeed(3.0f);
}
```

### 追加Wrapperの契約（2026-09-02照合）

この節は後半のWrapper署名台帳を補完する。引数の省略値も現行`EditorNativeScript.h`に合わせる。APIが存在することと、各ステージでPlay検証が済んでいることは別である。

#### HookPoint・Renderer

| 署名 | 動作・戻り値・注意 |
| --- | --- |
| `bool HookPoint::IsValid() const` | 対象に`WireConnectable`があるか。接続可能・Activeを保証するものではない。 |
| `bool HookPoint::CanConnect() const` | ObjectとComponentがActive、選択可能、最大接続本数未満ならtrue。本数0以下は無制限。 |
| `bool HookPoint::SetVisualState(HookVisualState visualState) const` | `Normal / Targeted / Selected / Connected`の設定色と発光を対象自身のRendererへ反映。Hookまたは対応Rendererなし等はfalse。親子Rendererを自動検索しない。 |
| `std::vector<Wire> HookPoint::GetWires() const` | そのHookを端点とするActiveかつ未破断のWireを列挙する。空なら未接続または取得不可。作成順は保証しない。 |
| `bool Renderer::SetColor(const EditorScriptVector3& color) const` | 対象自身のModelRenderer、なければSkinnedMeshRendererへRGBを設定。対応Rendererなし等はfalse。 |
| `bool Renderer::SetEmission(const EditorScriptVector3& color, float strength) const` | 同じRendererの発光色と強度を設定。対応Rendererなし等はfalse。 |
| `bool Renderer::SetColorFromMass(const GameObject& rigidBodyGameObject, float minimumMass, float maximumMass, const EditorScriptVector3& lightColor, const EditorScriptVector3& heavyColor) const` | 指定Objectの`RigidBody.mass`を読み、正規化して0～1へClampし、2色を線形補間してSetColorする。質量取得または色設定失敗でfalse。質量範囲の差の絶対値が0.0001以下ならlightColor。通常はminimumMass < maximumMassで指定する。 |

`HookPoint`と`WireConnectable`は同じComponentを参照する。文字列APIでは`"WireConnectable"`を使う。`Renderer`は描画Componentを作る型ではなく操作用Wrapperである。`SetColorFromMass`は呼び出した時点の色だけを更新し、質量変化を自動監視しない。Hookの状態色もEngineが自動遷移させるのではなくScriptから切り替える。

#### Wireの生成・個別操作・状態取得

| 署名 | 動作・戻り値・注意 |
| --- | --- |
| `static Wire Wire::Create(const Desc& wireDesc)` | 記述子から独立したRuntime Wireを作る。失敗は無効Wire。異なる有効な端点、非負の長さが必要。既定のrequireConnectable=trueでは両端のCanConnectも検証する。 |
| `static Wire Wire::Create(const GameObject& firstGameObject, const GameObject& secondGameObject, const EditorScriptVector3& firstLocalAnchor, const EditorScriptVector3& secondLocalAnchor, float maximumLength, const GameObject& ownerGameObject = GameObject{})` | 簡易版。ownerを通知先および描画設定Objectとして使い、その他はDesc既定値。 |
| `Handle Wire::GetHandle() const` / `bool Wire::IsValid() const` | Handle取得／状態が取得できるか。IsValidは未破断を保証しない。 |
| `bool Wire::Destroy() const` | Wireを削除。不存在でfalse。WrapperのHandle値自体は書き換えない。 |
| `bool Wire::SetLength(float maximumLength) const` | 長さ制約を変更。負値・不存在はfalse。minimumLengthより短い指定はminimumLengthへ補正。 |
| `bool Wire::SetShrinkSpeed(float shrinkSpeed) const` | m/sで収縮速度を設定。0で停止、負値・不存在はfalse。操作を離した際の停止もScript側で指定する。 |
| `bool Wire::Repair() const` | 存在するWireをActive・未破断・張力0へ戻す。削除済みWireの再生成や失われた端点の復元ではない。 |
| `bool Wire::GetState(State& wireState) const` | 成功時に状態を出力、取得不可はfalse。false時の出力値を使わない。 |
| `static int32_t Wire::GetCount(const GameObject& gameObject)` | 指定Objectを端点とするActive・未破断Wireの本数。所有者としての全Wire数ではない。 |
| `static std::vector<Wire> Wire::GetAll(const GameObject& gameObject)` | 同じ条件で列挙。`HookPoint::GetWires()`の実体。 |

`Wire::Desc / State / Handle`はそれぞれ`EditorScriptWireDesc / EditorScriptWireState / EditorScriptWireHandle`の別名。

| Desc Field | 初期値 | 意味 |
| --- | --- | --- |
| `firstGameObjectId / secondGameObjectId` | -1 / -1 | 両端HookのID。同一Object同士は不可。 |
| `ownerGameObjectId` | -1 | 通知等の所有者。 |
| `rendererSettingsGameObjectId` | -1 | WireRenderer設定元。描画時に負値ならownerを参照し、そこにも有効な設定がなければEngine既定表示。 |
| `firstLocalAnchor / secondLocalAnchor` | (0,0,0) | 各HookのローカルAnchor。親・回転・Scaleを含めてWorldへ変換される。 |
| `maximumLength / minimumLength` | 1.0 / 0.1 m | 現在の長さ制約／収縮下限。maximumLengthは接続対象を選ぶ射程ではない。 |
| `stiffness / damping` | 1200.0 N/m / 80.0 Ns/m | 張力係数／減衰。 |
| `maximumTension / breakingTension` | 0.0 / 0.0 N | 張力上限／破断閾値。0はそれぞれ無制限／Wire側の破断閾値なし。Hookの正の破断強度も適用され、複数の正の閾値では最小を採用する。 |
| `shrinkSpeed` | 0.0 m/s | 自動収縮速度。 |
| `applyReaction / requireConnectable` | true / true | 接続先への反作用／Hook接続可否検証。このゲームではrequireConnectableをtrueにする。 |

Stateには`handle`、両端・ownerのID、`isActive / isBroken`、両端World Anchor、`maximumLength / minimumLength / currentLength / currentTension`がある。Wire Eventは`type / handle / firstGameObjectId / secondGameObjectId / ownerGameObjectId / tension`を持つ。Scriptの`OnWireConnected`、`OnWireTensionChanged`、`OnWireBroken`、`OnWireTargetLost`、`OnWireDestroyed`は`const EditorScriptWireEvent&`を受け取る。DLL側のイベント配送コードはGeneratedが担当する。

#### GameObject生成・Component構成・階層・座標変換

| 署名 | 動作・戻り値・注意 |
| --- | --- |
| `static GameObject GameObject::Create(const char* gameObjectName = "GameObject")` | 空のGameObjectを生成。失敗は負IDの参照。HookのモデルやColliderを自動生成するAPIではない。 |
| `GameObject GameObject::Instantiate(const EditorScriptVector3& position, const EditorScriptVector3& rotation = EditorScriptVector3{}) const` | Scene内の元Objectを階層複製し、複製Rootを親なし・指定位置回転にする。Asset Pathから直接ロードするAPIではない。 |
| `bool GameObject::Destroy() const` | 現行実装は階層を非Active化し物理を停止する。即時のメモリ解放・Scene配列からのeraseではない。不存在はfalse。 |
| `bool GameObject::AddComponent(const char* componentTypeName) const` / `bool GameObject::RemoveComponent(const char* componentTypeName) const` | Component構成を変更。無効名・対象なし・追加削除不可ならfalse。RigidBody/Colliderには物理登録・停止処理があるが、全Subsystemの動的再構築を保証するものではない。 |
| `T GameObject::AddComponent<T>() const` / `T GameObject::GetOrAddComponent<T>() const` / `bool GameObject::RemoveComponent<T>() const` | TypeNameを持つWrapper用。追加系はWrapperを返すため、追加成功はIsValid等で確認する。GetOrAddは存在時に再追加しない。 |
| `static std::vector<GameObject> GameObject::FindAllWithComponent(const char* componentTypeName)` | 対象Componentを持つActive Objectを列挙。Component自身のActiveや接続可否は別確認。空は該当なしまたは取得不可。 |
| `GameObject GameObject::GetParent() const` | 親参照。親なし・取得不可は負ID。 |
| `bool GameObject::SetParent(const GameObject& parent, bool preserveWorldTransform = false) const` | 親を変更。World変換保持は明示的にtrueを渡す。`GameObject{}`を親にすると親なしを指定できる。 |
| `int32_t GameObject::GetChildCount() const` | 直下の子の数。孫以下の総数ではない。取得不可は0。非Activeの子も階層に残る。 |
| `GameObject GameObject::GetChild(int32_t childIndex) const` | 0始まりで直下の子を取得。負数・範囲外・取得不可は負ID。名前検索ではない。 |
| `bool GameObject::WorldToLocalPoint(const EditorScriptVector3& worldPoint, EditorScriptVector3& localPoint) const` | World点をこのObjectのLocal点へ変換。失敗false。 |
| `bool GameObject::LocalToWorldPoint(const EditorScriptVector3& localPoint, EditorScriptVector3& worldPoint) const` | Local点をWorld点へ変換。失敗false。 |
| `bool GameObject::WorldToLocalDirection(const EditorScriptVector3& worldDirection, EditorScriptVector3& localDirection) const` | 平行移動を除く方向変換。失敗false。 |
| `bool GameObject::LocalToWorldDirection(const EditorScriptVector3& localDirection, EditorScriptVector3& worldDirection) const` | Local方向をWorld方向へ変換。失敗false。 |
| `static bool SceneManager::Reload()` | 現在のPrimary Scene Pathへの読込要求。trueは要求受付であり読込完了ではない。Primaryなし等はfalse。Area Snapshot/Restoreではない。 |

`HasReference()`はIDが非負かだけを確認する。Destroy後の生存確認にはならない。方向変換にもScaleは含まれ、結果は自動正規化されない。物理Bodyを動かす場合、Transform変更だけで物理状態も意図どおり変わると決めつけず、Rigidbody APIと対象Subsystemの契約を確認する。

#### 選択・マウス・カーソル

| 署名 | 動作・戻り値・注意 |
| --- | --- |
| `static bool Physics::FindBestHook(const EditorScriptRay& aimRay, float maximumDistance, float maximumAngleDegrees, EditorScriptPhysicsHit& hit)` | 距離m・角度degree内のHook候補を検索。通常Raycastの最初のHitがそのHook自身である必要がある。ゼロ方向、非正の距離、該当なしでfalse。候補比較は角度と距離を併用し、厳密な最小角度順は保証しない。 |
| `static bool Physics::RaycastFiltered(const EditorScriptRay& ray, float distance, uint32_t physicsLayerMask, bool includeTriggers, const char* requiredComponentTypeName, EditorScriptPhysicsHit& hit)` | Layer Mask、Trigger包含、必須Component名を指定するRaycast。命中時true、未命中等false。 |
| `static bool Physics::RaycastConnectable(const EditorScriptRay& ray, float distance, EditorScriptPhysicsHit& hit, uint32_t physicsLayerMask = 0xFFFFFFFFU)` | 必須ComponentをWireConnectable、Triggerを除外に固定したFiltered版。円錐選択とは異なる。 |
| `static EditorScriptVector2 Input::GetMouseDelta()` | マウス移動差分。APIなしはゼロ。 |
| `static bool Input::GetMouseButton(MouseButton mouseButton)` | ボタン保持。APIなしはfalse。 |
| `static bool Input::GetMouseButtonDown(MouseButton mouseButton)` / `static bool Input::GetMouseButtonUp(MouseButton mouseButton)` | ボタン押下／解放の遷移。APIなしはfalse。 |
| `static void Input::SetCursorLocked(bool isLocked)` / `static bool Input::IsCursorLocked()` | カーソル固定の設定／取得。APIなしでは設定は何もせず、取得はfalse。 |
| `static void Input::SetCursorVisible(bool isVisible)` / `static bool Input::IsCursorVisible()` | カーソル表示の設定／取得。APIなしでは設定は何もせず、取得はtrue。 |

`FindBestHook`単体では選択可能フラグ・Component Active・接続本数を除外しない。`HookPoint::CanConnect()`と生成結果を確認する。選択距離は照準探索範囲であり、Hook同士の接続距離制限とは別物。

Filtered Raycastは条件不一致のHitを飛ばして探すため、必須Componentのない壁を遮蔽物として扱う選択用途では通常Raycastによる遮蔽確認も必要。FindBestHookは通常Raycastを使う。APIを置き換えるだけで同じ遮蔽挙動になるとは限らない。

現在の操作割り当て、サンプルScriptの公開値、実装と未実装の境界は[3Dワイヤーパズル エンジン要件](3DWirePuzzle_EngineRequirements.md)にまとめる。

## Cameraの標準操作と独自Script操作

Cameraの新規入力設定18項目を含む公開Field 30件を後半のCamera表に列挙する。Inspectorでの設定手順・既定値・操作条件は[Component資料のCamera](component-documentation-detail-seed.md#ゲーム中の視点操作freelook--orbit)を参照。ここではScriptからの使い分けを説明する。

### 標準操作の設定をScriptから変更する

`GameObject::GetComponent("Camera")`で既存Componentを取得し、型付きSetterを使う。Object参照は`SetGameObject`へ渡す。特定のPlayer名に依存するAPIではない。

```cpp
// カメラと追従対象は、呼出側で取得したGameObjectを渡す。
bool ConfigureOrbitCamera(const GameObject& cameraObject, const GameObject& targetObject) {
	const Component camera = cameraObject.GetComponent("Camera");

	if (!camera.IsValid()) {
		return false;
	}

	// 初期距離を変更する場合は、標準操作を事前にOFFにしておく。
	return camera.SetInt("cameraInputStyle", 1) &&
		camera.SetInt("cameraInputActivation", 0) &&
		camera.SetGameObject("cameraInputTargetGameObjectId", targetObject) &&
		camera.SetVector3("cameraInputPivotOffset", {0.0f, 1.5f, 0.0f}) &&
		camera.SetFloat("cameraInputOrbitDistance", 6.0f) &&
		camera.SetFloat("cameraInputLookSensitivity", 0.003f) &&
		camera.SetBool("cameraInputEnabled", true);
}
```

Setterは成功時true。連続設定はトランザクションではなく、途中で失敗しても先行した設定は残る。`GetFloat`等のGetterは取得先変数を参照引数で受け取り、成功をboolで返す。

感度・速度・中心Objectなどは標準操作の更新で読まれるが、`cameraInputOrbitDistance`は操作初期化時に読む初期値である。動作中のZoom距離へ直接代入するAPIではない。初期距離を再適用するならOFF状態でCamera更新を1回通してからONにする。同じScript呼出内でOFF→ONとしても再初期化を保証しない。

### マウス入力とカーソルの契約

- `MouseButton::Left` / `Right` / `Middle`はそれぞれ0 / 1 / 2。`GetMouseButton`は保持、`GetMouseButtonDown`と`GetMouseButtonUp`は押下・解放したFrameを取得する。
- `Input::GetMouseDelta()`は当該FrameのDirectInput相対移動量。同じFrameに何度読んでも同じ入力で、読出しによって消費しない。角度へ変換するときは感度を掛け、さらにDeltaTimeを掛けない。
- `Input::GetMousePosition()`は現在の前面Windowのクライアント座標へ変換した位置で、Game View内座標や0〜1正規化座標ではない。前面Windowが取得できなければスクリーン座標のままになる。画面端判定には対象Viewportの矩形・座標系を別途合わせる。
- `SetCursorLocked`はカーソルの領域制限と中央への移動を要求する。`SetCursorVisible`は表示状態を要求する。`IsCursorLocked` / `IsCursorVisible`で要求状態を取得できる。
- カーソルは共有状態。標準Cameraと複数Scriptから同時に書き換えず、制御担当を1つにする。Script操作終了・無効化時は自分が変更した固定／表示を戻す。ScriptManagerのPlay停止時にも解放処理がある。

### 独自の姿勢制御へ切り替える

`camera.SetBool("cameraInputEnabled", false)`で標準操作だけをOFFにし、`Input`と`GameObject::GetTransform/SetTransform`で独自挙動を作る。Camera Component自体は有効のままにする。下記は親なし・接続先なしのCameraを専用Scriptで制御する補助関数例で、Camera BlendやFollowComposerなど他の姿勢制御も使用しない前提。

```cpp
#include <algorithm>

// Updateから呼ぶ。cameraRollRadiansはゲーム側で決めた傾き。
bool UpdateCustomCamera(const GameObject& cameraObject, float cameraRollRadians) {
	const Component camera = cameraObject.GetComponent("Camera");

	if (!camera.IsValid() || !camera.SetBool("cameraInputEnabled", false)) {
		return false;
	}

	EditorScriptTransform cameraTransform = cameraObject.GetTransform();
	const bool isRotationActive = Input::GetMouseButton(MouseButton::Right);

	if (isRotationActive) {
		const EditorScriptVector2 mouseDelta = Input::GetMouseDelta();
		const float cameraLookSensitivity = 0.003f;
		cameraTransform.rotation.y += mouseDelta.x * cameraLookSensitivity;
		cameraTransform.rotation.x = (std::clamp)(
			cameraTransform.rotation.x + mouseDelta.y * cameraLookSensitivity,
			-1.48353f,
			1.48353f);
	}

	// 標準操作では固定されるRollも、独自Scriptなら制御できる。
	cameraTransform.rotation.z = cameraRollRadians;
	return cameraObject.SetTransform(cameraTransform);
}
```

この例はマウス回転と独自Rollだけを示し、移動・カーソル固定・Game View内判定を自動では行わない。必要なら同じScript側で入力受付条件とカーソルの取得・解放を追加する。Transformの回転単位はrad、InspectorのPitch制限値は度なので区別する。

標準操作の視点はGameObjectのTransformへ書き戻されないため、標準操作をOFFにしただけで直前の表示姿勢がScriptへ引き継がれるとは限らない。滑らかに切り替えるにはゲーム側で姿勢の受け渡しを設計する。現在の標準操作は初期化時に接続先への追従を反映した姿勢から開始するが、これとScriptへの姿勢引継ぎは別の話である。

## Runtime API 関数一覧

通常のゲームScriptでは`Input`、`GameObject`、`Rigidbody`などの高水準Wrapperを使う。低水準Runtime APIはEngine内部またはWrapper未提供機能の保守用途に限定する。

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
- **重要: ローカル座標であってワールド座標ではない。** `EditorScriptManager::GetTransformInternal`はGameObjectの`translate`/`rotate`/`scale`をそのまま返す素通しであり、親階層による変換を合成したワールド座標には変換しない。子GameObject（親を持つGameObject）に対して呼ぶと、返る値は親からの相対位置になる。

### SetTransform

```cpp
runtimeApi->SetTransform(gameObjectId, &transform);
```

- 用途: GameObject の Transform を書き換える。
- 呼ぶ場所: `Update`。
- 注意: Rigidbody と併用すると物理結果を上書きすることがある。
- **重要: ローカル座標をそのまま書き込む。** `SetTransformInternal`も`GetTransform`と対になる素通しの実装で、渡した値はGameObjectの`translate`/`rotate`/`scale`へそのまま入る（親からの相対値として解釈される）。このため、あるGameObjectの`GetTransform()`で読んだ値を、階層上の位置関係が異なる別のGameObjectへそのまま`SetTransform()`すると、意図しない位置へ飛ぶことがある。ただし、このプロジェクトのScene構成では主要なGameplay用GameObject（Player、敵、Effect用GameObjectなど）が親を持たないTop-Level（Identity親）で配置されている場合が多く、その条件下ではローカル座標＝ワールド座標として扱えるため、Top-Level同士でのTransformコピー（例: 敵撃破位置へEffect用GameObjectの`SetTransform`で移動させる）は実用上そのまま成立する。親を持つ階層（Weaponの子Muzzleなど）を跨いでコピーする場合は、必ず親階層まで遡ってワールド座標を合成するか、対象を揃えてから使う。
  根拠: `EditorScriptManager.cpp` `GetTransformInternal`/`SetTransformInternal`。

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

**Componentベースの発生パターン（`.effectdef`等の外部Asset文字列参照を使わない場合の標準手順）:** このプロジェクトの規約では、敵撃破・被弾・爆発などのGameplay VFXは、Scene上へ実体として置いたParticleSystem付きGameObject（Inspectorで直接編集できる）を使い、文字列キーで外部`.effectdef`を検索して動的生成する方式は使わない。手順は次の通り。

1. Effect用GameObject（例: `FX Explosion Large`）をSceneへ配置し、ParticleSystemを設定する。常時再生させたくない場合は`isActive=false`かつ`particleLooping=false`で待機させる。
2. 発生させたい瞬間に`Find("FX Explosion Large")`などでGameObjectを取得し、`SetTransform`で発生位置へ移動する（本章前半の`GetTransform`/`SetTransform`と同じくローカル座標の素通しなので、Top-Level同士でのコピーが前提）。
3. `.SetActive(true)`を呼ぶ。
4. **`SetActive(true)`だけではParticleは発生しない。** ParticleSystemの実際のEmission開始は`runtimeApi->PlayEffect(gameObjectId)`（`EditorEffectManager`側で`EmitterRuntime.isPlaying=true`を立てる処理）が担っており、SetActiveは単にGameObjectの描画・Update対象への出し入れをするだけである。SetActiveのみでParticleSystemの初期状態（Loop設定など）に応じて発生する場合とまったく発生しない場合があるため、Component方式で確実に発生させたいときは必ず`PlayEffect`を明示的に呼ぶ。
5. 効果を止めるときは`runtimeApi->StopEffect(gameObjectId)`を呼ぶ。StopEffectは新規Particleの発生だけを止め、既に生成済みのParticleは自身の寿命で自然に消えるまで描画され続ける（即座に消したい場合は別途`.SetActive(false)`まで行う）。
6. Burst系（`particleBurstCount>0`かつ`particleLooping=false`）のワンショットEffectは、一定時間後に`StopEffect`→`SetActive(false)`で待機状態へ戻すタイマー管理をScript側（`SharedGameState`の残り秒数フィールドなど）で持つ。毎Frame`Update`から減算し、0以下になったら停止処理を呼ぶ。

この方式は、Scene起動時から`isActive=true`のまま放置されたEFFECTS系GameObjectがPlay中ずっとUpdate/描画コストを払い続ける、という別の不具合（無関係なEffectが常時重い）にもつながるため、使わないEffectGameObjectは既定で`isActive=false`にしておくこと。
根拠: `EditorEffectManager`/`EditorVfxManager`の`PlayEffect`/`StopEffect`実装（`EmitterRuntime.isPlaying`）、`resources/scripts/WaterRailShooter0817/WaterRailShooter0817.cpp`の`PlayEnemyDestroyedEffect`/`StopEnemyDestroyedEffect`/`UpdateEnemyDestroyedEffects`実装例。

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

void MyScript::Update(float deltaTime) {
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

void MyScript::Update(float deltaTime) {
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
void GrappleScript::Update(float deltaTime) {
	(void)deltaTime;

	if (!Input::GetKeyDown(KeyCode::E)) {
		return;
	}

	RopeConstraint rope{GetGameObject()};
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

### Runtime SpringJoint API

`Physics::CreateSpringJoint`は、Play中に任意の2つのRigidbody間へJoltのSpringJointを生成する。事前にSpringJoint Componentを追加する必要はない。生成結果は`JointHandle`で返り、同じ2物体間に複数のJointがあっても個別に更新・破棄できる。`0`は無効Handleである。

Spring以外は`Physics::CreateJoint(JointType, ownerId, connectedId, JointDesc)`を使う。`JointType::Fixed`、`Hinge`、`Spring`、`Configurable`、`Character`が実行時生成に対応する。全種類が同じ`DestroyJoint`、`IsJointValid`を使い、汎用設定更新は`SetJointSettings`で行う。

| API | 用途 |
| --- | --- |
| `CreateSpringJoint(ownerId, connectedId, desc)` | 2つのGameObject IDを実Bodyへ解決し、Runtime SpringJointを生成する。 |
| `SetSpringJointSettings(handle, desc)` | 同じHandleを維持したままAnchor、距離、周波数、減衰を更新する。 |
| `DestroyJoint(handle)` | 指定HandleのRuntime Jointだけを破棄する。 |
| `IsJointValid(handle)` | Handleが現在のPlay物理Worldで有効か確認する。 |

Playerへ触れた箱を末尾へ追加し、`Player -> Box1 -> Box2 -> Box3`の順につなぐ例は次の通り。衝突相手IDは`OnCollisionEnter`の`otherGameObjectId`を使う。

```cpp
std::vector<int32_t> linkedGameObjectIds_;
std::vector<JointHandle> springJointHandles_;

void TrailLinkScript::Start(int32_t gameObjectId) {
	linkedGameObjectIds_.push_back(gameObjectId);
}

void TrailLinkScript::OnCollisionEnter(
	const EditorScriptPhysicsEvent& physicsEvent) {
	const int32_t newGameObjectId = physicsEvent.otherGameObjectId;
	if (newGameObjectId < 0 || linkedGameObjectIds_.empty()) {
		return;
	}

	if (std::find(
		linkedGameObjectIds_.begin(),
		linkedGameObjectIds_.end(),
		newGameObjectId) != linkedGameObjectIds_.end()) {
		return;
	}

	SpringJointDesc springJointDesc{};
	springJointDesc.minDistance = 0.8f;
	springJointDesc.maxDistance = 1.2f;
	springJointDesc.frequency = 5.0f;
	springJointDesc.damping = 0.7f;

	const int32_t lastGameObjectId = linkedGameObjectIds_.back();
	const JointHandle jointHandle = Physics::CreateSpringJoint(
		newGameObjectId,
		lastGameObjectId,
		springJointDesc);

	if (!Physics::IsJointValid(jointHandle)) {
		return;
	}

	linkedGameObjectIds_.push_back(newGameObjectId);
	springJointHandles_.push_back(jointHandle);
}
```

両方のGameObjectにPlay中の3D物理Bodyが必要で、同一ID同士、負値、`maxDistance < minDistance`、負の周波数・減衰は生成失敗になる。AnchorはそれぞれのGameObjectローカル座標である。Play停止時には全Runtime Jointが破棄され、保持していたHandleは無効になる。

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

新規生成したC++ Scriptは`Script`を継承する。Constructorで`BindAction`する。

```cpp
class StageEventReceiver final : public Script {
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

使用者がExport本体を手書きする必要はない。Action登録を変更したらScriptのbuild batでDLLを再Buildし、Engineが生成した`.Generated.cpp`から候補を公開する。

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
5. `.Generated.cpp`が存在し、build batのコンパイル対象に入っているか確認する。
6. Action名の大文字小文字と空白を確認する。
7. ConsoleのDLL Load、API Version、未登録Actionを確認する。

## 基本テンプレート

テンプレートは最小にする。
全 API を最初から詰め込むと読みにくくなるため、詳細例は後続のサンプルへ分ける。

```cpp
#include "NewNativeScript.h"

void NewNativeScript::Update(float deltaTime) {
	(void)deltaTime;

	// ゲーム処理だけをここへ記述する。
}
```

新規作成時は`NewNativeScript.Generated.cpp`も同時に生成される。利用者はGeneratedファイルを編集せず、必要なライフサイクルだけをHeaderへ宣言してユーザー`.cpp`へ実装する。

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
void MoveScript::Update(float deltaTime) {
	GameObject gameObject = GetGameObject();
	EditorScriptTransform transform = gameObject.GetTransform();

	if (Input::GetKey(KeyCode::W)) {
		transform.position.z += 3.0f * deltaTime;
	}

	gameObject.SetTransform(transform);
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
void PlayerScript::Update(float deltaTime) {
	const int32_t gameObjectId = GetGameObjectId();
	const EditorScriptRuntimeApi* runtimeApi =
		EditorNativeScriptRuntime::GetRuntimeApi();

	if (runtimeApi == nullptr) {
		return;
	}

	GameObject gameObject = GetGameObject();
	EditorScriptTransform transform = gameObject.GetTransform();
	EditorScriptVector2 move =
		runtimeApi->GetActionVector2(gameObjectId, "Player", "Move");

	transform.position.x += move.x * 3.0f * deltaTime;
	transform.position.z += move.y * 3.0f * deltaTime;

	gameObject.SetTransform(transform);
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
void PlayerScript::Update(float deltaTime) {
	(void)deltaTime;

	if (Input::GetKeyDown(KeyCode::Space)) {
		jumpRequested_ = true;
	}
}

void PlayerScript::FixedUpdate(float fixedDeltaTime) {
	(void)fixedDeltaTime;

	if (jumpRequested_) {
		EditorScriptVector3 impulse{0.0f, 6.0f, 0.0f};
		GetComponent<Rigidbody>().AddImpulse(impulse);
		jumpRequested_ = false;
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
void BallScript::FixedUpdate(float fixedDeltaTime) {
	(void)fixedDeltaTime;

	EditorScriptVector3 torque{};

	if (Input::GetKey(KeyCode::W)) {
		torque.x += 10.0f;
	}

	if (Input::GetKey(KeyCode::S)) {
		torque.x -= 10.0f;
	}

	if (Input::GetKey(KeyCode::A)) {
		torque.z += 10.0f;
	}

	if (Input::GetKey(KeyCode::D)) {
		torque.z -= 10.0f;
	}

	GetComponent<Rigidbody>().AddTorque(torque);
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
void HitScript::OnCollisionEnter(
	const EditorScriptPhysicsEvent& physicsEvent) {

	GameObject otherObject{physicsEvent.otherGameObjectId};
	(void)otherObject;
}

void HitScript::OnTriggerEnter(
	const EditorScriptPhysicsEvent& physicsEvent) {

	GameObject otherObject{physicsEvent.otherGameObjectId};
	(void)otherObject;
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
void MaterialReaderScript::Start() {
	const int32_t gameObjectId = GetGameObjectId();
	const EditorScriptRuntimeApi* runtimeApi =
		EditorNativeScriptRuntime::GetRuntimeApi();

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
void AiReaderScript::Update(float deltaTime) {
	(void)deltaTime;
	const int32_t gameObjectId = GetGameObjectId();
	const EditorScriptRuntimeApi* runtimeApi =
		EditorNativeScriptRuntime::GetRuntimeApi();

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
void AnimationReaderScript::Update(float deltaTime) {
	(void)deltaTime;
	const int32_t gameObjectId = GetGameObjectId();
	const EditorScriptRuntimeApi* runtimeApi =
		EditorNativeScriptRuntime::GetRuntimeApi();

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
InputReceiverScript::InputReceiverScript() {
	BindAction("OnSubmit", [this](const EditorScriptInputActionContext& context) {
		OnSubmit(context);
	});

	BindAction("OnMove", [this](const EditorScriptInputActionContext& context) {
		OnMove(context);
	});
}

void InputReceiverScript::OnSubmit(
	const EditorScriptInputActionContext& inputContext) {

	(void)inputContext;
}

void InputReceiverScript::OnMove(
	const EditorScriptInputActionContext& inputContext) {

	moveInput_ = inputContext.vector2Value;
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
| 近くにいるのに弾/Rayが当たらない | 下記「当たり判定が反応しない時の確認手順」を参照。 |

### 当たり判定が反応しない時の確認手順

見た目上は近距離・命中コース上にいるのに弾やHitscanが反応しない不具合は、原因が複数レイヤーに分かれているため、Console表示だけを見ても原因を特定できないことが多い（Consoleは他の大量のログに埋もれて可視性が低い）。切り分けは次の順で行う。

1. **RuntimeLogの鮮度を必ず確認する。** `logs/RuntimeLog.log`はPlay開始のたびにリセットされる。古いPlayのログを見て「当たっていない」と判断しないよう、ファイルの更新時刻が直近のPlayと一致しているかを先に確認する。
2. **ログ監視Windowで`Physics`カテゴリの`PhysicsBodyFailureCount`/`LastPhysicsBodyFailure`をWatchする。** `EditorSharedState::g_lastPhysicsBodyFailure`/`g_physicsBodyFailureCount`はConsoleに埋もれず参照できるシステム項目で、`RegisterRuntimeGameObject`が「非Activeで登録スキップ」、`AddGameObjectBody`が「Body生成失敗」を記録した際に更新される。カウントが増えていれば、その個体はそもそもJolt Physics Bodyが存在しない。
3. **Weaponの`LastProjectileNearestCandidate*`系フィールドを確認する。** `EditorWeaponManager`は`RecordProjectileCollision`で発射のたびに最も近いHealth所持Objectの名前・距離・World位置・Collider中心/サイズ・Body位置・Body World登録有無・無視判定(`WasIgnored`)を記録する。命中しなかった相手の名前がここに出ているか、`BodyInWorld`がfalseになっていないか、`WasIgnored`がtrueになっていないか（＝AttackCollisionFilterの誤除外）を確認する。
4. **Collider設定そのものを疑う。** BoxColliderはモデル/Scale変更後も自動追従しない（Component詳細ドキュメントのBoxCollider節を参照）。AutoConvexCollisionへ一時的に切り替えて自動検出される`Center`/`Size`と現在のBoxColliderの数値を比較する。
5. **ObjectPool経由の敵/弾なら、命中する個体としない個体を区別する。** Pool複製個体（`_Pool_N`という名前が付く）だけが当たらずTemplate本体だけ当たる場合は、Template複製時の非Active継承バグ（Component詳細ドキュメントのObjectPool節を参照）を疑う。
6. **高速な弾・小さい的では貫通/裏面判定を疑う。** `ShapeCast`の開始点が既に対象Shape内部にある場合（1Frameの移動量が的のサイズを超える等）、既定のBack-Face無視設定では命中を返さないことがある。このプロジェクトでは`CollideWithBackFaces`+`mUseShrunkenShapeAndConvexRadius`+`mReturnDeepestPoint`を設定済みだが、新しい高速Projectileを追加する際は同じ設定が使われているか確認する。

いずれの段階でも、まず疑わしい仮説（距離、Layer、Teamなど）を先に決め打ちせず、上記の順でRuntimeLogの実測値から機械的に絞り込むこと。
根拠: `EditorJoltPhysicsManager.cpp`（`RecordBodyCreationFailure`、`RegisterRuntimeGameObject`、`AddGameObjectBody`、`ShapeCast`）、`EditorWeaponManager.cpp`（`RecordProjectileCollision`、`FindNearestColliderCandidate`）、`EditorSharedState.h`（`g_lastPhysicsBodyFailure`、`g_physicsBodyFailureCount`）、`EditorLogSystemProviders.cpp`（`Physics`/`Weapon`カテゴリのフィールド登録）。

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
void WeaponScript::Update(float deltaTime) {
	(void)deltaTime;
	WeaponLoadout loadout{GetGameObject()};

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
| `Input::GetMousePosition` | なし | 前面WindowのクライアントMouse座標。 | Game View内座標ではない。Viewportの原点・サイズを別途考慮する。 |

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

3 Entryは既存順序を変えず`EditorScriptRuntimeApi`末尾へ追加している。この章の追加時点は172件であり、この章の完了時点ではRuntime API Entryは211件だった。

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

この章の追加開始時点ではRuntime API Entry 179件である。`GetInterceptPrediction`から`GetDamageDirection`までの7 Entryが既存172 Entryの後へ連続し、順序を変更しないことを機械照合する。この章の完了時点では総数211件だった。

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

7 Entryは既存179 Entryの順序を変えず`EditorScriptRuntimeApi`末尾へ追加する。この章の完了時点ではRuntime API Entryは211件だった。

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

Ocean Query 5 Entryと今回の詳細3 Entryは既存Entryの順序を変えず`EditorScriptRuntimeApi`末尾へ追加する。この章の完了時点ではRuntime API Entryは211件だった。ABI Version確認、関数Pointer null確認、出力Pointer確認をWrapperとBridgeの両側で行う。

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

10 Entryは既存191 Entryの順序を変えず`EditorScriptRuntimeApi`末尾へ追加する。この章の完了時点ではRuntime API Entryは211件だった。共有構造体`EditorScriptTurretAimState`はbool 2個の後へ明示Paddingを置き、Engine本体とDLLで同じ配置を維持する。

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

7 Entryは既存201 Entryの順序を変えず`EditorScriptRuntimeApi`末尾へ追加する。この章の完了時点ではRuntime API Entryは211件だった。`EditorScriptFireLineState`はbool後のPaddingを明示し、`EditorScriptStatusEffectEntry`は固定長文字列を使ってEngine本体とDLLのABIを維持する。

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

この章の追加時点ではRuntime API Entryは211件のままだった。

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

この章の追加時点の基準はRuntime API Entry 216件、C++ Script Template 26件、Component 277件だった。

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
void EnemyController::Update(float deltaTime) {
	// deltaTimeは常に描画1Frame分とは限らない。
	decisionTimer_ += deltaTime;

	int32_t lodLevel = 0;
	SimulationLod{GetGameObject()}.GetLevel(lodLevel);
}
```

Frame数前提の`timer++`ではなく秒単位で計算する。入力開始、終了、クリック、Marker Action等はUpdate待ち行列へ入れず、そのイベントFrameにAction関数へ通知する。FixedUpdateはPhysics周期を維持する。Farで`Script停止=true`ならScript Component自体がInactiveになるため、Update、Action、FixedUpdateを実行しない。

### ABI

`StartWaveSpawner`と`IsWaveSpawnerComplete`はRuntime API構造体末尾へ追加する。API Version 7と既存Entry順序は維持する。新Wrapperを使用するDLLは現行`EditorNativeScript.h`でDebug / Releaseを再Buildする。

この章の追加時点の機械照合基準はRuntime API Entry 218件、C++ Script Template 26件、Component 277件だった。

## Runtime API・型・Wrapper 完全リファレンス（現行コード照合版）

### この章の位置づけ

この章より前の各章は「何をしたいときにどのAPIを使うか」を用途別に説明している。この章はそれとは役割が違い、**現在のコードに実在するRuntime API Entry全件、共有型全件、高水準Wrapper全件を、1つも省かずに列挙した一次資料**である。

用途から探すときは上の章、シグネチャ・引数の型・構造体のFieldを確かめたいときはこの章を見る。

抽出元は次の2ファイルであり、これ以外を実装状態の根拠にしない。

| 情報 | 抽出元 |
| --- | --- |
| 低水準Entry（関数ポインタ）と型 | `Source/Engine/Core/EditorScriptApi.h` |
| 高水準Wrapper Class | `Source/Engine/Core/EditorNativeScript.h` |
| 各Entryの実処理 | `Source/Engine/Editor/EditorScriptManager.cpp` の `Script???Bridge` |

### 版数と機械照合基準（2026-09-02）

| 項目 | 値 |
| --- | --- |
| `kEditorScriptApiVersion` | **8** |
| `EditorScriptRuntimeApi` のEntry数 | **271**（宣言と索引を照合） |
| Wrapperの追加分 | 本文「追加Wrapperの契約」に署名と注意点を記載。従来の61 Class集計は現行総数として扱わない。 |
| Template / Component総数 | 後半の旧集計は過去の照合記録。今回のWire関連更新で全種類の再集計はしていない。 |

旧索引235件に、マウス・カーソル8件、Wire9件、Component構成・生成検索5件、座標変換4件、Filtered Raycast1件、Renderer・Hook・Hierarchy・Reload9件の計36件を追加した。Wrapperだけの補助関数（GetOrAddComponent、SetColorFromMass、FindBestHook等）はRuntime Entry数には加算しない。

### ABI互換の原則

`EditorScriptRuntimeApi` は**必ず構造体末尾へ追記する**。既存Entryの順序・型・引数は変更しない。このため、古いDLLを新しいEditorで動かしても、そのDLLが知っているEntryの位置はずれない。

以下は対応するRuntimeApi構造体が渡された後のnull確認である。末尾追記だけでは「新しいDLLを古いEditorで動かす」互換性は保証しない。古い短い構造体の末尾を読む前に、Load時点のABI Versionと対応範囲を一致させる必要がある。

```cpp
const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

if (runtimeApi == nullptr || runtimeApi->SomeEntry == nullptr) {
	return;  // APIが無い環境。機能を諦めて安全に抜ける。
}
```

高水準Wrapperを使う場合、この2段階はWrapper内部で必ず行われている。**Wrapperの `false` は「APIが無い」「参照が不正」「Componentが無い」「処理が成立しなかった」を区別しない**ので、区別が要る場面だけ低水準Entryを直接呼ぶ。

Version 7以降で追加されたEntryのうち、Wrapper側で `apiVersion >= 7` を明示的に確認しているのは型付きAction Payload（`InvokeScriptActionPayload`）と、`Timer` / `GenericStateMachine` / `Attribute` / `TargetLock` 系である。それ以外は関数ポインタのnull確認だけで判定している。

### 低水準Entry 全271件 索引

`EditorScriptRuntimeApi` の宣言順に全件を並べる。番号は宣言順であって、ABI上のオフセットを保証する識別子ではない。

「高水準Wrapper」列が「なし（低水準のみ）」のEntryは、`EditorNativeScript.h` にラッパーが無く、`EditorNativeScriptRuntime::GetRuntimeApi()` から直接呼ぶしかないものである。

| # | 戻り値 | Entry名 | 引数 | 高水準Wrapper |
| --- | --- | --- | --- | --- |
| 1 | `void` | `Log` | `const char* message` | なし（低水準のみ） |
| 2 | `bool` | `IsKeyDown` | `int32_t keyCode` | `Input::GetKey` |
| 3 | `bool` | `IsKeyPressed` | `int32_t keyCode` | `Input::GetKeyDown` |
| 4 | `EditorScriptVector2` | `GetActionVector2` | `int32_t gameObjectId, const char* actionMapName, const char* actionName` | なし（低水準のみ） |
| 5 | `bool` | `IsActionPressed` | `int32_t gameObjectId, const char* actionMapName, const char* actionName` | なし（低水準のみ） |
| 6 | `bool` | `WasActionJustPressed` | `int32_t gameObjectId, const char* actionMapName, const char* actionName` | なし（低水準のみ） |
| 7 | `EditorScriptVector2` | `GetMousePosition` | `（なし）` | `Input::GetMousePosition` |
| 8 | `EditorScriptTransform` | `GetTransform` | `int32_t gameObjectId` | `GameObject::GetTransform` |
| 9 | `void` | `SetTransform` | `int32_t gameObjectId, const EditorScriptTransform* transform` | `GameObject::SetTransform` |
| 10 | `EditorScriptVector3` | `GetVelocity` | `int32_t gameObjectId` | `Rigidbody::GetVelocity` |
| 11 | `void` | `SetVelocity` | `int32_t gameObjectId, const EditorScriptVector3* velocity` | `Rigidbody::SetVelocity` |
| 12 | `EditorScriptVector3` | `GetAngularVelocity` | `int32_t gameObjectId` | なし（低水準のみ） |
| 13 | `void` | `SetAngularVelocity` | `int32_t gameObjectId, const EditorScriptVector3* angularVelocity` | なし（低水準のみ） |
| 14 | `bool` | `AddForce` | `int32_t gameObjectId, const EditorScriptVector3* force` | `Rigidbody::AddForce` |
| 15 | `bool` | `AddImpulse` | `int32_t gameObjectId, const EditorScriptVector3* impulse` | `Rigidbody::AddImpulse` |
| 16 | `bool` | `AddTorque` | `int32_t gameObjectId, const EditorScriptVector3* torque` | `Rigidbody::AddTorque` |
| 17 | `EditorScriptAiSensorState` | `GetAiSensorState` | `int32_t gameObjectId, int32_t sensorKind` | なし（低水準のみ） |
| 18 | `EditorScriptMaterialState` | `GetMaterialState` | `int32_t gameObjectId` | なし（低水準のみ） |
| 19 | `EditorScriptAnimationState` | `GetAnimationState` | `int32_t gameObjectId` | なし（低水準のみ） |
| 20 | `bool` | `SetAnimatorFloat` | `int32_t gameObjectId, const char* parameterName, float value` | なし（低水準のみ） |
| 21 | `bool` | `SetAnimatorInt` | `int32_t gameObjectId, const char* parameterName, int32_t value` | なし（低水準のみ） |
| 22 | `bool` | `SetAnimatorBool` | `int32_t gameObjectId, const char* parameterName, bool value` | なし（低水準のみ） |
| 23 | `bool` | `SetAnimatorTrigger` | `int32_t gameObjectId, const char* parameterName` | なし（低水準のみ） |
| 24 | `bool` | `SetAnimatorVector2` | `int32_t gameObjectId, const char* parameterName, const EditorScriptVector2* value` | なし（低水準のみ） |
| 25 | `bool` | `SetAnimatorVector3` | `int32_t gameObjectId, const char* parameterName, const EditorScriptVector3* value` | なし（低水準のみ） |
| 26 | `bool` | `PlayAnimationAction` | `int32_t gameObjectId, int32_t clipIndex, float blendIn, float blendOut, float playbackSpeed, int32_t priority, bool loop` | なし（低水準のみ） |
| 27 | `bool` | `PlayEffect` | `int32_t gameObjectId` | なし（低水準のみ） |
| 28 | `bool` | `PlayEffectAt` | `int32_t gameObjectId, const char* effectAssetPath, const EditorScriptVector3* localOffset` | なし（低水準のみ） |
| 29 | `void` | `StopEffect` | `int32_t gameObjectId` | なし（低水準のみ） |
| 30 | `bool` | `PlayAudio` | `int32_t gameObjectId` | `Audio::Play` |
| 31 | `void` | `StopAudio` | `int32_t gameObjectId` | `Audio::Stop` |
| 32 | `void` | `SetAudioBusVolume` | `int32_t audioBus, float volume` | `Audio::SetBusVolume` |
| 33 | `float` | `GetAudioBusVolume` | `int32_t audioBus` | `Audio::GetBusVolume` |
| 34 | `void` | `SetAudioMasterVolume` | `float volume` | `Audio::SetMasterVolume` |
| 35 | `float` | `GetAudioMasterVolume` | `（なし）` | `Audio::GetMasterVolume` |
| 36 | `int32_t` | `GetAliveParticleCount` | `int32_t gameObjectId` | なし（低水準のみ） |
| 37 | `bool` | `GetAnimatorFloat` | `int32_t gameObjectId, const char* parameterName, float* value` | なし（低水準のみ） |
| 38 | `bool` | `GetAnimatorInt` | `int32_t gameObjectId, const char* parameterName, int32_t* value` | なし（低水準のみ） |
| 39 | `bool` | `GetAnimatorBool` | `int32_t gameObjectId, const char* parameterName, bool* value` | なし（低水準のみ） |
| 40 | `bool` | `GetAnimatorVector2` | `int32_t gameObjectId, const char* parameterName, EditorScriptVector2* value` | なし（低水準のみ） |
| 41 | `bool` | `GetAnimatorVector3` | `int32_t gameObjectId, const char* parameterName, EditorScriptVector3* value` | なし（低水準のみ） |
| 42 | `bool` | `ResetAnimatorTrigger` | `int32_t gameObjectId, const char* parameterName` | なし（低水準のみ） |
| 43 | `bool` | `PlayAnimation` | `int32_t gameObjectId` | なし（低水準のみ） |
| 44 | `bool` | `StopAnimation` | `int32_t gameObjectId` | なし（低水準のみ） |
| 45 | `bool` | `IsAnimationPlaying` | `int32_t gameObjectId` | なし（低水準のみ） |
| 46 | `float` | `GetAnimationTime` | `int32_t gameObjectId` | なし（低水準のみ） |
| 47 | `bool` | `SetAnimationTime` | `int32_t gameObjectId, float playbackTime` | なし（低水準のみ） |
| 48 | `bool` | `SetAnimationSpeed` | `int32_t gameObjectId, float playbackSpeed` | なし（低水準のみ） |
| 49 | `bool` | `GetAnimatorStateName` | `int32_t gameObjectId, char* stateName, int32_t stateNameCapacity` | なし（低水準のみ） |
| 50 | `bool` | `IsEffectPlaying` | `int32_t gameObjectId` | なし（低水準のみ） |
| 51 | `int32_t` | `FindGameObjectByName` | `const char* gameObjectName` | `GameObject::Find` |
| 52 | `bool` | `SetGameObjectActive` | `int32_t gameObjectId, bool isActive` | `GameObject::SetActive` |
| 53 | `bool` | `IsGameObjectActive` | `int32_t gameObjectId` | `GameObject::IsActive` |
| 54 | `bool` | `LoadScene` | `const char* scenePath` | `SceneManager::LoadScene` |
| 55 | `bool` | `LoadSceneByBuildIndex` | `int32_t sceneIndex` | `SceneManager::LoadScene` |
| 56 | `bool` | `SetRailPaused` | `int32_t gameObjectId, bool isPaused` | `RailFollower::SetPaused` |
| 57 | `bool` | `IsRailPaused` | `int32_t gameObjectId` | `RailFollower::IsPaused` |
| 58 | `bool` | `SetRailSpeed` | `int32_t gameObjectId, float speed` | `RailFollower::SetSpeed` |
| 59 | `bool` | `SetRailReverse` | `int32_t gameObjectId, bool isReversed` | `RailFollower::SetReverse` |
| 60 | `bool` | `SetRailNormalizedProgress` | `int32_t gameObjectId, float normalizedProgress` | `RailFollower::JumpTo` |
| 61 | `bool` | `SetRailPath` | `int32_t gameObjectId, int32_t railPathGameObjectId, bool preservesProgress` | `RailFollower::SwitchRail` |
| 62 | `bool` | `GetRailNormalizedProgress` | `int32_t gameObjectId, float* normalizedProgress` | `RailFollower::GetNormalizedProgress` |
| 63 | `bool` | `GetRailLength` | `int32_t gameObjectId, float* railLength` | `RailFollower::GetLength` |
| 64 | `bool` | `GetRailPosition` | `int32_t gameObjectId, float normalizedProgress, EditorScriptVector3* position` | `RailFollower::GetPosition` |
| 65 | `bool` | `GetRailDirection` | `int32_t gameObjectId, float normalizedProgress, EditorScriptVector3* direction` | `RailFollower::GetDirection` |
| 66 | `bool` | `ConsumeRailEndReached` | `int32_t gameObjectId` | `RailFollower::ConsumeEndReached` |
| 67 | `bool` | `ViewportPointToRay` | `const EditorScriptVector2* normalizedPosition, EditorScriptRay* ray` | `Physics::ViewportPointToRay` |
| 68 | `bool` | `GetAimRay` | `int32_t screenAimGameObjectId, EditorScriptRay* ray` | `Physics::GetAimRay` |
| 69 | `bool` | `PhysicsRaycast` | `const EditorScriptRay* ray, float distance, EditorScriptPhysicsHit* hit` | `Physics::Raycast` |
| 70 | `bool` | `PhysicsSphereCast` | `const EditorScriptRay* ray, float radius, float distance, EditorScriptPhysicsHit* hit` | `Physics::SphereCast` |
| 71 | `bool` | `PhysicsCapsuleCast` | `const EditorScriptRay* ray, float radius, float height, float distance, EditorScriptPhysicsHit* hit` | `Physics::CapsuleCast` |
| 72 | `bool` | `ApplyDamage` | `int32_t targetGameObjectId, float damage, int32_t sourceGameObjectId` | `Health::Damage` |
| 73 | `bool` | `GetHealth` | `int32_t gameObjectId, float* currentHealth, float* maximumHealth` | `Health::Get` |
| 74 | `bool` | `SetHealth` | `int32_t gameObjectId, float currentHealth` | `Health::Set` |
| 75 | `int32_t` | `SpawnFromPool` | `int32_t poolGameObjectId, const EditorScriptVector3* position, const EditorScriptVector3* rotation` | `ObjectPool::Spawn` |
| 76 | `int32_t` | `SpawnFromSpawner` | `int32_t spawnerGameObjectId` | `Spawner::Spawn` |
| 77 | `bool` | `ReleaseToPool` | `int32_t gameObjectId` | `ObjectPool::Release` |
| 78 | `bool` | `FireHitscan` | `int32_t weaponGameObjectId` | `Weapon::FireHitscan` |
| 79 | `bool` | `FireProjectile` | `int32_t emitterGameObjectId` | `Weapon::FireProjectile` |
| 80 | `bool` | `PlayCameraBlend` | `int32_t componentOwnerGameObjectId` | `CameraEffects::PlayBlend` |
| 81 | `bool` | `PlayCameraShake` | `int32_t componentOwnerGameObjectId` | `CameraEffects::PlayShake` |
| 82 | `bool` | `TriggerRailBranch` | `int32_t componentOwnerGameObjectId` | `RailBranch::Trigger` |
| 83 | `bool` | `LoadSceneAsync` | `const char* scenePath, bool isAdditive` | `SceneManager::LoadSceneAdditiveAsync`, `SceneManager::LoadSceneAsync` |
| 84 | `bool` | `UnloadScene` | `const char* scenePath` | `SceneManager::UnloadScene` |
| 85 | `float` | `GetSceneLoadProgress` | `（なし）` | `SceneManager::GetLoadProgress` |
| 86 | `bool` | `IsSceneLoading` | `（なし）` | `SceneManager::IsLoading` |
| 87 | `bool` | `IsSceneLoaded` | `const char* scenePath` | `SceneManager::IsLoaded` |
| 88 | `void` | `SetSceneFloat` | `const char* key, float value` | `SceneManager::SetFloat` |
| 89 | `bool` | `GetSceneFloat` | `const char* key, float* value` | `SceneManager::GetFloat` |
| 90 | `void` | `SetSceneString` | `const char* key, const char* value` | `SceneManager::SetString` |
| 91 | `bool` | `GetSceneString` | `const char* key, char* value, int32_t valueCapacity` | `SceneManager::GetString` |
| 92 | `bool` | `PlayActionSequence` | `int32_t sequenceGameObjectId` | `ActionSequence::Play` |
| 93 | `bool` | `PauseActionSequence` | `int32_t sequenceGameObjectId, bool isPaused` | `ActionSequence::Pause` |
| 94 | `bool` | `StopActionSequence` | `int32_t sequenceGameObjectId` | `ActionSequence::Stop` |
| 95 | `bool` | `SignalActionSequence` | `int32_t sequenceGameObjectId, const char* signalName` | `ActionSequence::Signal` |
| 96 | `bool` | `IsActionSequencePlaying` | `int32_t sequenceGameObjectId` | `ActionSequence::IsPlaying` |
| 97 | `bool` | `SaveSlot` | `const char* slotName` | `SaveSystem::Save` |
| 98 | `bool` | `LoadSlot` | `const char* slotName` | `SaveSystem::Load` |
| 99 | `bool` | `DeleteSlot` | `const char* slotName` | `SaveSystem::Delete` |
| 100 | `bool` | `HasSlot` | `const char* slotName` | `SaveSystem::Exists` |
| 101 | `bool` | `ActivateCheckpoint` | `int32_t checkpointGameObjectId, bool shouldLoad` | `Checkpoint::Activate` |
| 102 | `void` | `SetSaveFloat` | `const char* key, float value` | `SaveSystem::SetFloat` |
| 103 | `bool` | `GetSaveFloat` | `const char* key, float* value` | `SaveSystem::GetFloat` |
| 104 | `void` | `SetSaveString` | `const char* key, const char* value` | `SaveSystem::SetString` |
| 105 | `bool` | `GetSaveString` | `const char* key, char* value, int32_t valueCapacity` | `SaveSystem::GetString` |
| 106 | `bool` | `SampleOceanSurface` | `int32_t queryGameObjectId, const EditorScriptVector3* worldPosition, EditorScriptOceanSurfaceHit* hit` | `Physics::SampleOceanSurface` |
| 107 | `bool` | `SetComponentActive` | `int32_t gameObjectId, const char* componentTypeName, bool isActive` | `GameObject::SetComponentActive` |
| 108 | `bool` | `IsComponentActive` | `int32_t gameObjectId, const char* componentTypeName` | `GameObject::IsComponentActive`, `Component::IsActive` |
| 109 | `bool` | `AddForceAtPosition` | `int32_t gameObjectId, const EditorScriptVector3* force, const EditorScriptVector3* worldPosition` | `Rigidbody::AddForceAtPosition` |
| 110 | `int32_t` | `AddExplosionImpulse` | `const EditorScriptVector3* center, float radius, float impulseStrength, float upwardModifier` | `Physics::AddExplosionImpulse` |
| 111 | `bool` | `AttachRope` | `int32_t ownerGameObjectId, int32_t targetGameObjectId, const EditorScriptVector3* ownerLocalAnchor, const EditorScriptVector3* targetAnchor, float maximumLength` | `RopeConstraint::Attach`, `RopeConstraint::AttachToWorld` |
| 112 | `bool` | `DetachRope` | `int32_t ownerGameObjectId` | `RopeConstraint::Detach` |
| 113 | `bool` | `SetRopeLength` | `int32_t ownerGameObjectId, float maximumLength` | `RopeConstraint::SetLength` |
| 114 | `bool` | `RepairRope` | `int32_t ownerGameObjectId` | `RopeConstraint::Repair` |
| 115 | `EditorScriptRopeState` | `GetRopeState` | `int32_t ownerGameObjectId` | `RopeConstraint::GetState` |
| 116 | `bool` | `SetRailMoveInput` | `int32_t gameObjectId, const EditorScriptVector2* moveInput` | `RailFollower::SetMoveInput` |
| 117 | `bool` | `SetRailOffset` | `int32_t gameObjectId, const EditorScriptVector2* offset` | `RailFollower::SetOffset` |
| 118 | `bool` | `GetRailOffset` | `int32_t gameObjectId, EditorScriptVector2* offset` | `RailFollower::GetOffset` |
| 119 | `bool` | `LoadoutSelectSlot` | `int32_t gameObjectId, int32_t slotIndex` | `WeaponLoadout::Select` |
| 120 | `bool` | `LoadoutSelectNext` | `int32_t gameObjectId` | `WeaponLoadout::Next` |
| 121 | `bool` | `LoadoutSelectPrevious` | `int32_t gameObjectId` | `WeaponLoadout::Previous` |
| 122 | `bool` | `LoadoutFire` | `int32_t gameObjectId` | `WeaponLoadout::Fire` |
| 123 | `bool` | `LoadoutReload` | `int32_t gameObjectId` | `WeaponLoadout::Reload` |
| 124 | `bool` | `LoadoutGetAmmo` | `int32_t gameObjectId, int32_t* currentAmmo, int32_t* reserveAmmo` | `WeaponLoadout::GetAmmo` |
| 125 | `bool` | `GetCurrentTarget` | `int32_t gameObjectId, int32_t* targetGameObjectId` | `Targeting::GetCurrentTarget` |
| 126 | `bool` | `SetExplicitTarget` | `int32_t gameObjectId, int32_t targetGameObjectId` | `Targeting::SetTarget` |
| 127 | `bool` | `SetRuntimeFloat` | `int32_t gameObjectId, const char* componentName, const char* propertyName, float value` | `Component::SetFloat`, `RuntimeProperty::SetFloat` |
| 128 | `bool` | `GetRuntimeFloat` | `int32_t gameObjectId, const char* componentName, const char* propertyName, float* value` | `Component::GetFloat`, `RuntimeProperty::GetFloat` |
| 129 | `bool` | `SetRuntimeInt` | `int32_t gameObjectId, const char* componentName, const char* propertyName, int32_t value` | `Component::SetInt`, `Component::SetGameObject`, `RuntimeProperty::SetInt` |
| 130 | `bool` | `GetRuntimeInt` | `int32_t gameObjectId, const char* componentName, const char* propertyName, int32_t* value` | `Component::GetInt`, `Component::GetGameObject`, `RuntimeProperty::GetInt` |
| 131 | `bool` | `SetRuntimeBool` | `int32_t gameObjectId, const char* componentName, const char* propertyName, bool value` | `Component::SetBool`, `RuntimeProperty::SetBool` |
| 132 | `bool` | `GetRuntimeBool` | `int32_t gameObjectId, const char* componentName, const char* propertyName, bool* value` | `Component::GetBool`, `RuntimeProperty::GetBool` |
| 133 | `bool` | `SetRuntimeVector3` | `int32_t gameObjectId, const char* componentName, const char* propertyName, const EditorScriptVector3* value` | `Component::SetVector3`, `RuntimeProperty::SetVector3` |
| 134 | `bool` | `GetRuntimeVector3` | `int32_t gameObjectId, const char* componentName, const char* propertyName, EditorScriptVector3* value` | `Component::GetVector3`, `RuntimeProperty::GetVector3` |
| 135 | `bool` | `PlayPropertyTween` | `int32_t gameObjectId` | `PropertyTween::Play` |
| 136 | `bool` | `StopPropertyTween` | `int32_t gameObjectId` | `PropertyTween::Stop` |
| 137 | `bool` | `IsPropertyTweenPlaying` | `int32_t gameObjectId` | `PropertyTween::IsPlaying` |
| 138 | `bool` | `RelayAction` | `int32_t gameObjectId` | `ActionRelay::Relay` |
| 139 | `bool` | `HasComponent` | `int32_t gameObjectId, const char* componentTypeName` | `GameObject::HasComponent`, `Component::IsValid` |
| 140 | `bool` | `InvokeScriptAction` | `int32_t gameObjectId, const char* functionName` | `GameObject::InvokeAction` |
| 141 | `bool` | `SetRuntimeVector2` | `int32_t gameObjectId, const char* componentName, const char* propertyName, const EditorScriptVector2* value` | `Component::SetVector2`, `RuntimeProperty::SetVector2` |
| 142 | `bool` | `GetRuntimeVector2` | `int32_t gameObjectId, const char* componentName, const char* propertyName, EditorScriptVector2* value` | `Component::GetVector2`, `RuntimeProperty::GetVector2` |
| 143 | `bool` | `GetRailState` | `int32_t gameObjectId, EditorScriptRailState* state` | `RailFollower::GetState` |
| 144 | `bool` | `SetRailDistance` | `int32_t gameObjectId, float distance` | `RailFollower::SetDistance` |
| 145 | `bool` | `GetRailClosestProgress` | `int32_t gameObjectId, const EditorScriptVector3* worldPosition, float* normalizedProgress` | `RailFollower::GetClosestProgress` |
| 146 | `bool` | `GetRailFrame` | `int32_t gameObjectId, float normalizedProgress, EditorScriptRailFrame* frame` | `RailFollower::GetFrame` |
| 147 | `bool` | `ApplyDamageContext` | `EditorScriptDamageContext* damageContext` | `Health::Damage` |
| 148 | `bool` | `GetLastDamageContext` | `int32_t targetGameObjectId, EditorScriptDamageContext* damageContext` | `Health::GetLastDamageContext` |
| 149 | `bool` | `InvokeScriptActionPayload` | `int32_t gameObjectId, const char* functionName, const EditorScriptActionPayload* payload` | `GameObject::InvokeAction`（Payload overload） |
| 150 | `bool` | `StartTimer` | `int32_t gameObjectId` | `Timer::Start` |
| 151 | `bool` | `PauseTimer` | `int32_t gameObjectId, bool isPaused` | `Timer::Pause` |
| 152 | `bool` | `GetTimerRemaining` | `int32_t gameObjectId, float* remainingSeconds` | `Timer::GetRemaining` |
| 153 | `bool` | `ChangeGenericState` | `int32_t gameObjectId, const char* stateName` | `GenericStateMachine::ChangeState` |
| 154 | `bool` | `GetGenericState` | `int32_t gameObjectId, char* stateName, int32_t stateNameCapacity` | `GenericStateMachine::GetState` |
| 155 | `bool` | `SetAttributeValue` | `int32_t gameObjectId, float value` | `Attribute::Set` |
| 156 | `bool` | `GetAttributeValue` | `int32_t gameObjectId, float* current, float* maximum` | `Attribute::Get` |
| 157 | `bool` | `GetTargetLockState` | `int32_t gameObjectId, float* progress, bool* isLocked, int32_t* targetGameObjectId` | `TargetLock::GetState` |
| 158 | `bool` | `SetNamedAttributeValue` | `int32_t gameObjectId, const char* attributeName, float value` | `AttributeSet::Set` |
| 159 | `bool` | `GetNamedAttributeValue` | `int32_t gameObjectId, const char* attributeName, float* current, float* maximum` | `AttributeSet::Get` |
| 160 | `bool` | `SetCounterValue` | `int32_t gameObjectId, float value` | `GenericCounter::Set` |
| 161 | `bool` | `AddCounterValue` | `int32_t gameObjectId, float deltaValue` | `GenericCounter::Add` |
| 162 | `bool` | `GetCounterValue` | `int32_t gameObjectId, float* value` | `GenericCounter::Get` |
| 163 | `bool` | `EvaluateGenericCondition` | `int32_t gameObjectId, bool* result` | `GenericCondition::Evaluate` |
| 164 | `bool` | `GetMultiTargetLockCount` | `int32_t gameObjectId, int32_t* targetCount` | `MultiTargetLock::GetEntries` |
| 165 | `bool` | `GetMultiTargetLockTarget` | `int32_t gameObjectId, int32_t targetIndex, int32_t* targetGameObjectId, float* progress, bool* isLocked` | `MultiTargetLock::GetEntries` |
| 166 | `bool` | `GetGameplayDataValue` | `int32_t gameObjectId, const char* key, int32_t* valueType, char* value, int32_t valueCapacity` | `GameplayData::GetRaw` |
| 167 | `int32_t` | `HashDamageTag` | `const char* damageTag` | `DamageTag::Id` |
| 168 | `int32_t` | `ApplyAreaDamage` | `int32_t areaDamageGameObjectId, int32_t instigatorGameObjectId` | `AreaDamage::Apply` |
| 169 | `bool` | `DetonateProjectile` | `int32_t projectileGameObjectId` | `ProjectileDetonator::Detonate` |
| 170 | `bool` | `GetThreatTrackerCount` | `int32_t gameObjectId, int32_t* threatCount` | `ThreatTracker::GetEntries` |
| 171 | `bool` | `GetThreatTrackerEntry` | `int32_t gameObjectId, int32_t threatIndex, EditorScriptThreatInfo* threatInfo` | `ThreatTracker::GetEntries` |
| 172 | `bool` | `StartNamedCooldown` | `int32_t gameObjectId, const char* cooldownName, float durationOverride` | `CooldownSet::Start` |
| 173 | `bool` | `ResetNamedCooldown` | `int32_t gameObjectId, const char* cooldownName` | `CooldownSet::Reset` |
| 174 | `bool` | `GetNamedCooldown` | `int32_t gameObjectId, const char* cooldownName, float* remainingSeconds, bool* isReady` | `CooldownSet::Get` |
| 175 | `bool` | `ResetRuntimeState` | `int32_t gameObjectId` | `RuntimeStateReset::Reset` |
| 176 | `bool` | `GetWeaponAccuracySpread` | `int32_t gameObjectId, float* spreadDegrees` | `Weapon::GetAccuracySpread` |
| 177 | `bool` | `PlayTimeScale` | `int32_t gameObjectId, float scaleOverride, float durationOverride` | `TimeScale::Play` |
| 178 | `float` | `GetTimeScale` | `（なし）` | `TimeScale::GetCurrent` |
| 179 | `bool` | `GetInterceptPrediction` | `int32_t gameObjectId, EditorScriptVector3* position, float* timeSeconds` | `Targeting::GetInterceptPrediction` |
| 180 | `bool` | `SetObjective` | `int32_t gameObjectId, const char* objectiveId, int32_t state, float currentValue` | `ObjectiveTracker::Set` |
| 181 | `bool` | `GetObjective` | `int32_t gameObjectId, const char* objectiveId, int32_t* state, float* currentValue, float* targetValue` | `ObjectiveTracker::Get` |
| 182 | `bool` | `StartEncounter` | `int32_t gameObjectId` | `EncounterController::Start` |
| 183 | `bool` | `ResolveSpawnPoint` | `int32_t gameObjectId, EditorScriptVector3* position, EditorScriptVector3* rotation` | `SpawnPointSet::Resolve` |
| 184 | `bool` | `ApplyDifficulty` | `int32_t gameObjectId, int32_t difficultyIndex` | `DifficultyParameterSet::Apply` |
| 185 | `bool` | `GetDamageDirection` | `int32_t gameObjectId, EditorScriptVector2* direction, float* alpha, int32_t* sourceGameObjectId` | `DamageDirectionIndicator::Get` |
| 186 | `bool` | `GetBallisticPrediction` | `int32_t gameObjectId, EditorScriptBallisticPrediction* prediction` | `BallisticPrediction::Get` |
| 187 | `bool` | `GetBallisticTrajectoryPoint` | `int32_t gameObjectId, int32_t pointIndex, EditorScriptVector3* point` | `BallisticPrediction::GetTrajectoryPoints` |
| 188 | `bool` | `GetDamageEventBufferCount` | `int32_t gameObjectId, int32_t* eventCount` | `DamageEventBuffer::GetEntries` |
| 189 | `bool` | `GetDamageEventBufferEntry` | `int32_t gameObjectId, int32_t eventIndex, EditorScriptDamageEvent* damageEvent` | `DamageEventBuffer::GetEntries` |
| 190 | `bool` | `SetGamePaused` | `int32_t gameObjectId, bool isPaused` | `GamePause::Set` |
| 191 | `bool` | `IsGamePaused` | `（なし）` | `GamePause::IsPaused` |
| 192 | `bool` | `GetSurfaceWakeState` | `int32_t gameObjectId, float* speed, float* intensity` | `SurfaceWakeEmitter::GetState` |
| 193 | `bool` | `OceanSegmentCast` | `int32_t queryGameObjectId, int32_t oceanGameObjectId, const EditorScriptVector3* startPosition, const EditorScriptVector3* endPosition, float clearance, EditorScriptOceanSegmentHit* hit` | `Ocean::SegmentCast` |
| 194 | `bool` | `OceanRaycast` | `int32_t queryGameObjectId, int32_t oceanGameObjectId, const EditorScriptRay* ray, float maximumDistance, float clearance, EditorScriptOceanSegmentHit* hit` | `Ocean::Raycast` |
| 195 | `bool` | `QueryOceanOcclusion` | `int32_t queryGameObjectId, int32_t oceanGameObjectId, const EditorScriptVector3* startPosition, const EditorScriptVector3* endPosition, float clearance, EditorScriptOceanOcclusion* occlusion` | `Ocean::IsOccluded` |
| 196 | `bool` | `GetWaterSurfaceState` | `int32_t gameObjectId, EditorScriptWaterSurfaceState* state` | `WaterSurfaceState::Get` |
| 197 | `bool` | `GetOceanProbeSample` | `int32_t gameObjectId, int32_t probeIndex, EditorScriptOceanProbeSample* sample` | `OceanProbeSet::GetSample` |
| 198 | `bool` | `LoadoutGetAmmoAtSlot` | `int32_t gameObjectId, int32_t slotIndex, int32_t* currentAmmo, int32_t* reserveAmmo, int32_t* maximumAmmo` | `WeaponLoadout::GetAmmoAtSlot` |
| 199 | `bool` | `LoadoutAddMagazineAmmo` | `int32_t gameObjectId, int32_t slotIndex, int32_t amount` | `WeaponLoadout::AddMagazineAmmo` |
| 200 | `bool` | `LoadoutAddReserveAmmo` | `int32_t gameObjectId, int32_t slotIndex, int32_t amount` | `WeaponLoadout::AddReserveAmmo` |
| 201 | `bool` | `LoadoutSetMagazineAmmo` | `int32_t gameObjectId, int32_t slotIndex, int32_t amount` | `WeaponLoadout::SetMagazineAmmo` |
| 202 | `bool` | `LoadoutSetReserveAmmo` | `int32_t gameObjectId, int32_t slotIndex, int32_t amount` | `WeaponLoadout::SetReserveAmmo` |
| 203 | `bool` | `LoadoutSetMaximumAmmo` | `int32_t gameObjectId, int32_t slotIndex, int32_t amount` | `WeaponLoadout::SetMaximumAmmo` |
| 204 | `bool` | `LoadoutRefillMagazine` | `int32_t gameObjectId, int32_t slotIndex` | `WeaponLoadout::RefillMagazine` |
| 205 | `bool` | `FireWeaponGroup` | `int32_t gameObjectId` | `WeaponGroup::Fire` |
| 206 | `bool` | `IsWeaponGroupFiring` | `int32_t gameObjectId` | `WeaponGroup::IsFiring` |
| 207 | `bool` | `GetTurretAimState` | `int32_t gameObjectId, EditorScriptTurretAimState* state` | `TurretAim::GetState` |
| 208 | `bool` | `GetFireLineState` | `int32_t gameObjectId, EditorScriptFireLineState* state` | `FireLineCheck::GetState` |
| 209 | `bool` | `ApplyStatusEffect` | `int32_t gameObjectId, const char* effectId, int32_t sourceGameObjectId` | `StatusEffectSet::Apply` |
| 210 | `bool` | `RemoveStatusEffect` | `int32_t gameObjectId, const char* effectId` | `StatusEffectSet::Remove` |
| 211 | `bool` | `ClearStatusEffects` | `int32_t gameObjectId` | `StatusEffectSet::Clear` |
| 212 | `bool` | `HasStatusEffect` | `int32_t gameObjectId, const char* effectId` | `StatusEffectSet::Has` |
| 213 | `bool` | `GetStatusEffectCount` | `int32_t gameObjectId, int32_t* effectCount` | `StatusEffectSet::GetEntries` |
| 214 | `bool` | `GetStatusEffectEntry` | `int32_t gameObjectId, int32_t effectIndex, EditorScriptStatusEffectEntry* effectEntry` | `StatusEffectSet::GetEntries` |
| 215 | `bool` | `SampleOceanSurfaceDetailed` | `int32_t queryGameObjectId, const EditorScriptVector3* worldPosition, EditorScriptOceanSurfaceHit* hit, float* foam` | `Ocean::SampleDetailed` |
| 216 | `bool` | `GetWaterSurfaceFoam` | `int32_t gameObjectId, float* foam` | `WaterSurfaceState::GetFoam` |
| 217 | `bool` | `GetOceanProbeFoam` | `int32_t gameObjectId, int32_t probeIndex, float* foam` | `OceanProbeSet::GetFoam` |
| 218 | `bool` | `SetRailSpeedProfileEnabled` | `int32_t gameObjectId, bool isEnabled` | `RailFollower::SetSpeedProfileEnabled` |
| 219 | `bool` | `GetRailSpeedMultiplier` | `int32_t gameObjectId, float* speedMultiplier` | `RailFollower::GetSpeedMultiplier` |
| 220 | `bool` | `GetRailActiveZone` | `int32_t gameObjectId, char* zoneId, int32_t zoneIdCapacity` | `RailFollower::GetActiveZone` |
| 221 | `bool` | `RearmRailEventMarkers` | `int32_t gameObjectId, const char* markerId` | `RailFollower::RearmEventMarkers` |
| 222 | `bool` | `GetSimulationLodLevel` | `int32_t gameObjectId, int32_t* lodLevel` | `SimulationLod::GetLevel` |
| 223 | `bool` | `StartWaveSpawner` | `int32_t gameObjectId` | `WaveSpawner::Start` |
| 224 | `bool` | `IsWaveSpawnerComplete` | `int32_t gameObjectId, bool waitsForAllDefeated` | `WaveSpawner::IsComplete` |
| 225 | `bool` | `PhysicsRaycastIgnoringHierarchy` | `const EditorScriptRay* ray, float distance, int32_t ignoreHierarchyRootGameObjectId, EditorScriptPhysicsHit* hit` | `Physics::RaycastIgnoringHierarchy` |
| 226 | `int32_t` | `PlayEffekseerAtPosition` | `const char* effectAssetPath, const EditorScriptVector3* position, const EditorScriptVector3* rotationEuler` | `EffectManager::PlayEffekseer` |
| 227 | `bool` | `SetEffekseerEffectPosition` | `int32_t effekseerPlaybackHandle, const EditorScriptVector3* position` | `EffectManager::SetPosition` |
| 228 | `void` | `StopEffekseerEffectAtPosition` | `int32_t effekseerPlaybackHandle` | `EffectManager::Stop` |
| 229 | `bool` | `PlayVfxAtPosition` | `const char* effectId, const EditorScriptVector3* position` | なし（低水準のみ） |
| 230 | `EditorScriptJointHandle` | `CreateSpringJoint` | `int32_t ownerGameObjectId, int32_t connectedGameObjectId, const EditorScriptSpringJointDesc* springJointDesc` | `Physics::CreateSpringJoint` |
| 231 | `bool` | `DestroyJoint` | `EditorScriptJointHandle jointHandle` | `Physics::DestroyJoint` |
| 232 | `bool` | `SetSpringJointSettings` | `EditorScriptJointHandle jointHandle, const EditorScriptSpringJointDesc* springJointDesc` | `Physics::SetSpringJointSettings` |
| 233 | `bool` | `IsJointValid` | `EditorScriptJointHandle jointHandle` | `Physics::IsJointValid` |
| 234 | `EditorScriptJointHandle` | `CreateJoint` | `EditorScriptJointType jointType, int32_t ownerGameObjectId, int32_t connectedGameObjectId, const EditorScriptJointDesc* jointDesc` | `Physics::CreateJoint` |
| 235 | `bool` | `SetJointSettings` | `EditorScriptJointHandle jointHandle, const EditorScriptJointDesc* jointDesc` | `Physics::SetJointSettings` |
| 236 | `EditorScriptVector2` | `GetMouseDelta` | `()` | `Input::GetMouseDelta` |
| 237 | `bool` | `IsMouseButtonDown` | `int32_t mouseButton` | `Input::GetMouseButton` |
| 238 | `bool` | `WasMouseButtonPressed` | `int32_t mouseButton` | `Input::GetMouseButtonDown` |
| 239 | `bool` | `WasMouseButtonReleased` | `int32_t mouseButton` | `Input::GetMouseButtonUp` |
| 240 | `void` | `SetCursorLocked` | `bool isLocked` | `Input::SetCursorLocked` |
| 241 | `bool` | `IsCursorLocked` | `()` | `Input::IsCursorLocked` |
| 242 | `void` | `SetCursorVisible` | `bool isVisible` | `Input::SetCursorVisible` |
| 243 | `bool` | `IsCursorVisible` | `()` | `Input::IsCursorVisible` |
| 244 | `EditorScriptWireHandle` | `CreateWire` | `const EditorScriptWireDesc* wireDesc` | `Wire::Create` |
| 245 | `bool` | `DestroyWire` | `EditorScriptWireHandle wireHandle` | `Wire::Destroy` |
| 246 | `bool` | `SetWireLengthByHandle` | `EditorScriptWireHandle wireHandle, float maximumLength` | `Wire::SetLength` |
| 247 | `bool` | `SetWireShrinkSpeed` | `EditorScriptWireHandle wireHandle, float shrinkSpeed` | `Wire::SetShrinkSpeed` |
| 248 | `bool` | `RepairWire` | `EditorScriptWireHandle wireHandle` | `Wire::Repair` |
| 249 | `bool` | `GetWireStateByHandle` | `EditorScriptWireHandle wireHandle, EditorScriptWireState* wireState` | `Wire::GetState` |
| 250 | `int32_t` | `GetWireCountForGameObject` | `int32_t gameObjectId` | `Wire::GetCount` |
| 251 | `bool` | `GetWireForGameObject` | `int32_t gameObjectId, int32_t wireIndex, EditorScriptWireState* wireState` | `Wire::GetAll / HookPoint::GetWires` |
| 252 | `bool` | `CanConnectWire` | `int32_t gameObjectId` | `HookPoint::CanConnect / WireConnectable::CanConnect` |
| 253 | `bool` | `AddComponent` | `int32_t gameObjectId, const char* componentTypeName` | `GameObject::AddComponent` |
| 254 | `bool` | `RemoveComponent` | `int32_t gameObjectId, const char* componentTypeName` | `GameObject::RemoveComponent` |
| 255 | `int32_t` | `FindGameObjectsWithComponent` | `const char* componentTypeName, int32_t* gameObjectIds, int32_t capacity` | `GameObject::FindAllWithComponent` |
| 256 | `int32_t` | `InstantiateGameObject` | `int32_t sourceGameObjectId, const EditorScriptVector3* position, const EditorScriptVector3* rotation` | `GameObject::Instantiate` |
| 257 | `bool` | `DestroyGameObject` | `int32_t gameObjectId` | `GameObject::Destroy` |
| 258 | `bool` | `WorldToLocalPoint` | `int32_t gameObjectId, const EditorScriptVector3* worldPoint, EditorScriptVector3* localPoint` | `GameObject::WorldToLocalPoint` |
| 259 | `bool` | `LocalToWorldPoint` | `int32_t gameObjectId, const EditorScriptVector3* localPoint, EditorScriptVector3* worldPoint` | `GameObject::LocalToWorldPoint` |
| 260 | `bool` | `WorldToLocalDirection` | `int32_t gameObjectId, const EditorScriptVector3* worldDirection, EditorScriptVector3* localDirection` | `GameObject::WorldToLocalDirection` |
| 261 | `bool` | `LocalToWorldDirection` | `int32_t gameObjectId, const EditorScriptVector3* localDirection, EditorScriptVector3* worldDirection` | `GameObject::LocalToWorldDirection` |
| 262 | `bool` | `PhysicsRaycastFiltered` | `const EditorScriptRay* ray, float distance, uint32_t physicsLayerMask, bool includeTriggers, const char* requiredComponentTypeName, EditorScriptPhysicsHit* hit` | `Physics::RaycastFiltered / RaycastConnectable` |
| 263 | `bool` | `SetRendererColor` | `int32_t gameObjectId, const EditorScriptVector3* color` | `Renderer::SetColor` |
| 264 | `bool` | `SetRendererEmission` | `int32_t gameObjectId, const EditorScriptVector3* color, float strength` | `Renderer::SetEmission` |
| 265 | `bool` | `SetHookVisualState` | `int32_t hookGameObjectId, int32_t visualState` | `HookPoint::SetVisualState` |
| 266 | `int32_t` | `CreateGameObject` | `const char* name` | `GameObject::Create` |
| 267 | `int32_t` | `GetParentGameObject` | `int32_t gameObjectId` | `GameObject::GetParent` |
| 268 | `bool` | `SetParentGameObject` | `int32_t childGameObjectId, int32_t parentGameObjectId, bool preserveWorldTransform` | `GameObject::SetParent` |
| 269 | `int32_t` | `GetChildGameObjectCount` | `int32_t gameObjectId` | `GameObject::GetChildCount` |
| 270 | `int32_t` | `GetChildGameObject` | `int32_t gameObjectId, int32_t childIndex` | `GameObject::GetChild` |
| 271 | `bool` | `ReloadPrimaryScene` | `()` | `SceneManager::Reload` |

### Wrapperが無く低水準でしか呼べないEntry（36件）

上の索引で「なし（低水準のみ）」になっているEntryは次の36件である。分類ごとにまとめる。**Wrapperが無いことは未実装を意味しない。** どれも `EditorScriptManager` に実処理があり、Play中に動く。

| 分類 | Entry | 直接呼ぶときの注意 |
| --- | --- | --- |
| Log | `Log` | Consoleへ1行出す。`const char*` なので `std::string` は `.c_str()` を付ける。null文字列を渡さない。 |
| 入力Action（旧索引の補足） | `GetActionVector2` / `IsActionPressed` / `WasActionJustPressed` | Ownerに `PlayerInput` が必要。ActionMap名とAction名は Input Actions Asset の綴りと完全一致させる。現在はInputのAction Wrapperを使える。`GetMousePosition`も`Input::GetMousePosition`で取得可能で、低水準専用ではない。座標系は前述のマウス入力の契約を参照。 |
| 物理（角速度） | `GetAngularVelocity` / `SetAngularVelocity` | 単位はラジアン毎秒。`Rigidbody` Wrapperは線形速度と力の系だけを包んでいる。 |
| 状態取得 | `GetAiSensorState` / `GetMaterialState` / `GetAnimationState` | 戻り値が構造体そのもの。**失敗してもfalseを返さず、`hasComponent` がfalseの既定構造体が返る**。必ず `hasComponent` を先に見る。 |
| Animator Parameter設定 | `SetAnimatorFloat` / `SetAnimatorInt` / `SetAnimatorBool` / `SetAnimatorTrigger` / `SetAnimatorVector2` / `SetAnimatorVector3` | 対象に `Animator` が必要。Parameter名が一致しないとfalse。 |
| Animator Parameter取得 | `GetAnimatorFloat` / `GetAnimatorInt` / `GetAnimatorBool` / `GetAnimatorVector2` / `GetAnimatorVector3` / `ResetAnimatorTrigger` / `GetAnimatorStateName` | 出力はポインタ引数。`GetAnimatorStateName` は容量を渡す文字列取得なので、64バイト以上のバッファを用意する。 |
| Animation再生 | `PlayAnimationAction` / `PlayAnimation` / `StopAnimation` / `IsAnimationPlaying` / `GetAnimationTime` / `SetAnimationTime` / `SetAnimationSpeed` | 対象に `Animation` が必要。`GetAnimationTime` は失敗時も `0.0f` を返すため、`IsAnimationPlaying` と併用して区別する。 |
| Effect | `PlayEffect` / `PlayEffectAt` / `StopEffect` / `IsEffectPlaying` / `GetAliveParticleCount` / `PlayVfxAtPosition` | 前4つはGameObject単位。`GetAliveParticleCount` は失敗時も `0` を返す。 |

### 追加された11 Entryの個別詳細

前回の基準（218件）より後に追加されたEntryを個別に説明する。すべて構造体末尾へ追記されており、既存Entryの位置は変わっていない。

#### Audio 6 Entry — `class Audio`

`AudioSource` を持つGameObjectを鳴らす経路と、Bus・Masterの音量を触る経路を分ける。

```cpp
bool (*PlayAudio)(int32_t gameObjectId);
void (*StopAudio)(int32_t gameObjectId);
void (*SetAudioBusVolume)(int32_t audioBus, float volume);
float (*GetAudioBusVolume)(int32_t audioBus);
void (*SetAudioMasterVolume)(float volume);
float (*GetAudioMasterVolume)();
```

高水準Wrapperは `class Audio` である。**インスタンス側とstatic側で意味が違う**ので、混同しないこと。

| Wrapper | 対象 | 戻り値 | falseまたは0の意味 |
| --- | --- | --- | --- |
| `Audio{gameObject}.Play()` | そのObjectの `AudioSource` | `bool` | Object ID不正、API無し、AudioSourceなし、または再生開始に失敗。 |
| `Audio{gameObject}.Stop()` | 同上 | `void` | 成否を返さない。停止したかを知る手段は無い。 |
| `Audio::SetBusVolume(bus, volume)` | Bus全体 | `void` | 成否を返さない。 |
| `Audio::GetBusVolume(bus)` | Bus全体 | `float` | API無し・Audio Manager無しなら `0.0f`。「音量0」と区別できない。 |
| `Audio::SetMasterVolume(volume)` | 全体 | `void` | 成否を返さない。 |
| `Audio::GetMasterVolume()` | 全体 | `float` | 同上、失敗時 `0.0f`。 |

**Bus番号。** `0=SFX` / `1=BGM` / `2=Ambience` / `3=UI`。**範囲外の番号はエラーにならず `0`（SFX）へ丸められる**。番号を計算で作る場合、意図しないBusを操作しないよう呼び出し側で範囲を確認する。

**呼び出しTiming。** どれもMain Threadのみ。`Play()` はUpdate、Action、Collisionのいずれからでも呼べる。同じAudioSourceを短時間に何度も鳴らす場合、Component側の `audioMaxVoices`（同時発音数）と `audioRetriggerInterval`（再発音までの秒数）が上限になるため、Script側で連打しても実際の発音数はComponent設定を超えない。

**副作用。** Bus音量とMaster音量は**Runtime全体の状態**で、GameObject単位ではない。Scene遷移で自動的に戻らないので、BGMダッキングのように一時的に下げた場合はScript側で戻す責任がある。Sceneへも保存されない。

```cpp
void BgmDucker::Start(int32_t gameObjectId) {
	(void)gameObjectId;
	originalBgmVolume_ = Audio::GetBusVolume(1);  // 1 = BGM
}

void BgmDucker::OnDialogueStarted(int32_t gameObjectId, const EditorScriptInputActionContext& context) {
	(void)gameObjectId;
	(void)context;
	Audio::SetBusVolume(1, originalBgmVolume_ * 0.3f);
}

void BgmDucker::Stop(int32_t gameObjectId) {
	(void)gameObjectId;
	Audio::SetBusVolume(1, originalBgmVolume_);  // Play停止時に必ず戻す。
}
```

#### `GetHealth` — `Health::Get`

```cpp
bool (*GetHealth)(int32_t gameObjectId, float* currentHealth, float* maximumHealth);
```

| 項目 | 内容 |
| --- | --- |
| 高水準入口 | `bool Health::Get(float& currentHealth, float& maximumHealth) const` |
| 必須Component | 対象GameObjectに `Health`。 |
| 引数 | 出力ポインタ2つ。**どちらか一方でもnullならfalseを返し、何も書かない。** |
| 戻り値 | 成功でtrue。falseのとき出力先は変更されないので、呼び出し前に初期化しておく。 |
| falseの条件 | Object ID不正、API無し、Damage Manager未初期化、出力ポインタnull、対象に `Health` が無い、**または `Health` の有効チェックが外れている**。 |
| 呼出Timing | Start / Update / FixedUpdate / Action のどこでも可。Main Threadのみ。 |
| 副作用 | なし。読み取り専用。 |
| 参照寿命 | GameObjectがDestroyまたはPool返却された後のIDでは false になる。IDを保持し続けず、必要な時点で取り直す。 |

`SetHealth` は対になる書き込みで、`Health::Set(float)` が包む。**`Set` はDamage処理を通さずに現在値を直接置き換える**ので、`DamageReceiver` の倍率、無敵時間、Damage通知Actionは一切走らない。回復やデバッグ用の直接代入に使い、被弾処理には `Health::Damage()` か `ApplyDamageContext` を使う。

`Set` の追加仕様が2つある。

1. **値は `0` から `最大体力` の範囲へClampされる。** 最大体力を超える値を渡しても超えない。最大体力そのものを上げたい場合は、Component汎用Propertyアクセスで `SetFloat("Health", "healthMaximum", value)` を使う（短い別名の `"Maximum"` は読み取り専用で、書き込みは台帳のFieldキー `healthMaximum` 経由になる）。詳細は「Component 汎用Propertyアクセス完全リファレンス」を参照する。
2. **結果が0より大きければ、そのObjectの撃破済み状態と無効化待ち状態が解除される。** つまり `Set` は撃破済みObjectの復活にも使える。Pool返却済みの個体を復活させたい場合は、`RuntimeStateReset::Reset()` の方が体力以外のRuntime状態もまとめて初期化するので適している。

```cpp
float currentHealth = 0.0f;
float maximumHealth = 0.0f;
const GameObject self{gameObjectId};

if (Health{self}.Get(currentHealth, maximumHealth) && maximumHealth > 0.0f) {
	const float ratio = currentHealth / maximumHealth;
	// ratio を HUD や フェーズ判定へ使う。
}
else {
	// Healthが無い、または対象が既に破棄されている。
}
```

#### Rail速度プロファイル 3 Entry — `class RailFollower`

```cpp
bool (*SetRailSpeedProfileEnabled)(int32_t gameObjectId, bool isEnabled);
bool (*GetRailSpeedMultiplier)(int32_t gameObjectId, float* speedMultiplier);
bool (*GetRailActiveZone)(int32_t gameObjectId, char* zoneId, int32_t zoneIdCapacity);
```

| Wrapper | 必須Component | 戻り値 | falseの意味 |
| --- | --- | --- | --- |
| `RailFollower::SetSpeedProfileEnabled(bool)` | 同じObjectの `RailSpeedProfile` | `bool` | Object不正、API無し、`RailSpeedProfile` が無い。 |
| `RailFollower::GetSpeedMultiplier(float&)` | `RailMovement` | `bool` | Object不正、API無し、Rail Manager未初期化、Rail未確立。 |
| `RailFollower::GetActiveZone()` | 同じObjectの `RailZone` | `std::string` | 空文字列。`RailZone` が無い、または現在どのZoneにも入っていない。 |

**`SetSpeedProfileEnabled` はComponentの `railSpeedProfileEnabled` を直接書き換える**。Componentの `isActive` とは別のフラグで、Inspectorの有効チェックには反映されない。falseにしている間、レール速度は `RailSpeedProfile` のキーを参照せず基礎速度のままになる。ボス戦で速度演出を一時停止する、といった用途に使う。

`GetActiveZone()` はWrapper内部で128バイトのバッファを使うので、Zone IDが長すぎると切り詰められる。**Zone IDは短く付ける。**

`GetSpeedMultiplier` の出力は `RailMovement` の基礎速度へ掛かる倍率であり、実速度（m/s）ではない。実速度は `RailFollower::GetState()` の `currentSpeed` を見る。

```cpp
RailFollower rail{GameObject{gameObjectId}};
float speedMultiplier = 1.0f;

if (rail.GetSpeedMultiplier(speedMultiplier) && speedMultiplier < 0.5f) {
	// 減速区間。カメラFOVを絞る等の演出へ使う。
}

const std::string zoneId = rail.GetActiveZone();

if (zoneId == "BossApproach") {
	rail.SetSpeedProfileEnabled(false);  // 速度プロファイルを止め、Script側で速度を握る。
}
```

#### `PhysicsRaycastIgnoringHierarchy` — `Physics::RaycastIgnoringHierarchy`

```cpp
bool (*PhysicsRaycastIgnoringHierarchy)(
	const EditorScriptRay* ray,
	float distance,
	int32_t ignoreHierarchyRootGameObjectId,
	EditorScriptPhysicsHit* hit);
```

| 項目 | 内容 |
| --- | --- |
| 高水準入口 | `static bool Physics::RaycastIgnoringHierarchy(const EditorScriptRay& ray, float distance, const GameObject& ignoreHierarchyRoot, EditorScriptPhysicsHit& hit)` |
| 目的 | 指定Rootと**その子孫すべて**を無視してRaycastする。自機と自機に載っている武器・部品を狙点判定で誤検出しないために使う。 |
| 除外の決まり方 | Scene内の全GameObjectについて親を辿り、途中に `ignoreHierarchyRoot` が現れたものを除外する。Root自身も除外される。 |
| `ignoreHierarchyRootGameObjectId` が負のとき | 除外リストが空になり、通常の `PhysicsRaycast` と同じ結果になる。 |
| 戻り値 | 命中でtrue。**falseは「命中しなかった」と「引数不正・Physics未初期化」を区別しない。** |
| 出力 | 命中時のみ `hit` を書く。falseのとき `hit` は変更されない。 |
| 呼出Timing | Main Threadのみ。物理位置が確定した後に読みたい場合はFixedUpdateではなくUpdateから呼ぶ。 |
| 副作用 | なし。ただしScene内の全GameObjectを走査して親を辿るため、**通常の `PhysicsRaycast` より重い**。毎フレーム多数呼ぶ用途には向かない。 |

`distance` はWorld単位の最大距離で、`ray.direction` は正規化済みを前提とする。

```cpp
EditorScriptRay aimRay{};
const GameObject self{gameObjectId};

if (!Physics::GetAimRay(GameObject::Find("ScreenAim"), aimRay)) {
	return;
}

EditorScriptPhysicsHit hit{};

if (Physics::RaycastIgnoringHierarchy(aimRay, 3000.0f, self, hit)) {
	// hit.gameObjectId は自機階層の外にある最初の命中Object。
	targetGameObjectId_ = hit.gameObjectId;
}
else {
	targetGameObjectId_ = -1;  // 何にも当たっていない。
}
```

#### Effekseer 3 Entry — `class EffectManager`

```cpp
int32_t (*PlayEffekseerAtPosition)(const char* effectAssetPath, const EditorScriptVector3* position, const EditorScriptVector3* rotationEuler);
bool (*SetEffekseerEffectPosition)(int32_t effekseerPlaybackHandle, const EditorScriptVector3* position);
void (*StopEffekseerEffectAtPosition)(int32_t effekseerPlaybackHandle);
```

**GameObjectを介さず、任意のWorld座標へ `.efk` / `.efkefc` を再生する。** 爆発・着弾・水しぶきのように、あらかじめEffect専用GameObjectを置けない使い捨て演出のためのAPIである。Effect用のGameObjectを毎回生成・破棄しなくてよい。

| Wrapper | 戻り値 | 失敗時 |
| --- | --- | --- |
| `EffectManager::PlayEffekseer(path, position, rotationEuler = {})` | 再生Handle（`int32_t`） | **`-1`。** パスが空、API無し、Effekseer Manager未初期化。 |
| `EffectManager::SetPosition(handle, position)` | `bool` | Handleが負、API無し、位置ポインタnull。 |
| `EffectManager::Stop(handle)` | `void` | 成否を返さない。 |

**Handleの扱い。** 追従が不要な使い捨て演出なら戻り値を捨ててよい。移動する対象へ貼り付ける場合だけHandleを保持し、毎フレーム `SetPosition` を呼ぶ。**Handleの寿命はEffect本体の再生が終わるまでで、終了後のHandleへ `SetPosition` を呼んでも安全に無視される（クラッシュしない）が、位置は反映されない。**

`rotationEuler` は省略可能で、省略すると `{0,0,0}` になる。単位は度ではなくEngine側のEuler表現に従う。

```cpp
const EditorScriptVector3 impactPosition{hit.point.x, hit.point.y, hit.point.z};

// 使い捨て: Handleを捨てる。
EffectManager::PlayEffekseer("Assets/Effects/Impact.efkefc", impactPosition);

// 追従させる: Handleを保持する。
thrusterEffectHandle_ = EffectManager::PlayEffekseer("Assets/Effects/Thruster.efkefc", position);

// 以後のUpdateで
if (thrusterEffectHandle_ >= 0) {
	EffectManager::SetPosition(thrusterEffectHandle_, currentPosition);
}

// Stop時に必ず止める。
void ThrusterController::Stop(int32_t gameObjectId) {
	(void)gameObjectId;
	EffectManager::Stop(thrusterEffectHandle_);
	thrusterEffectHandle_ = -1;
}
```

#### `PlayVfxAtPosition`（Wrapper無し）

```cpp
bool (*PlayVfxAtPosition)(const char* effectId, const EditorScriptVector3* position);
```

`.effectdef`（EffectDefinition Asset）に登録したEffect IDを、任意のWorld座標へ再生する。**Effekseerの `PlayEffekseerAtPosition` とは経路が別**で、こちらはVFX Managerを通り、Handleを返さない。位置追従も停止もできない、完全な使い捨て再生である。

| 項目 | 内容 |
| --- | --- |
| 高水準入口 | **なし。低水準のみ。** |
| 引数 | `effectId` は `.effectdef` のID文字列。`position` はWorld座標。**どちらかがnullならfalse。** |
| 戻り値 | 再生ハンドルが有効ならtrue。 |
| falseの条件 | API無し、VFX Manager未初期化、引数null、または該当IDのEffectが登録されていない。 |
| 副作用 | Effectを1回再生する。呼び出し元GameObjectとは無関係に、World座標へ出る。 |
| 使い分け | Effect Assetを `.effectdef` で管理しているならこちら。`.efk` / `.efkefc` を直接指定して位置を追従させたいなら `EffectManager::PlayEffekseer`。GameObjectのEffect Componentを鳴らすなら `PlayEffect`。 |

```cpp
const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

if (runtimeApi != nullptr && runtimeApi->PlayVfxAtPosition != nullptr) {
	const EditorScriptVector3 position{0.0f, 2.0f, 30.0f};

	if (!runtimeApi->PlayVfxAtPosition("Explosion_Small", &position)) {
		// IDが .effectdef に無いか、VFX Managerが動いていない。
	}
}
```

### 共有型 完全リファレンス

`EditorScriptApi.h` が定義する全enumと全structを列挙する。Script側とEditor側はこの定義をバイト単位で共有するため、**Field順・型・配列長を変えてはいけない**。`reservedPadding` は構造体の並びを揃えるためだけの領域で、読み書きしない。

固定長 `char` 配列（`char name[64]` など）は**必ずヌル終端されている前提で読める**。書き込む側はEditorであり、Scriptからは読むだけである。

#### `enum EditorScriptPhysicsEventType`

| 値 | 定数名 |
| --- | --- |
| `0` | `EditorScriptPhysicsEventTypeCollisionEnter` |
| `1` | `EditorScriptPhysicsEventTypeCollisionStay` |
| `2` | `EditorScriptPhysicsEventTypeCollisionExit` |
| `3` | `EditorScriptPhysicsEventTypeTriggerEnter` |
| `4` | `EditorScriptPhysicsEventTypeTriggerStay` |
| `5` | `EditorScriptPhysicsEventTypeTriggerExit` |

#### `enum EditorScriptKeyCode`

| 値 | 定数名 |
| --- | --- |
| `17` | `EditorScriptKeyCodeW` |
| `30` | `EditorScriptKeyCodeA` |
| `31` | `EditorScriptKeyCodeS` |
| `32` | `EditorScriptKeyCodeD` |
| `16` | `EditorScriptKeyCodeQ` |
| `18` | `EditorScriptKeyCodeE` |
| `19` | `EditorScriptKeyCodeR` |
| `33` | `EditorScriptKeyCodeF` |
| `57` | `EditorScriptKeyCodeSpace` |
| `42` | `EditorScriptKeyCodeLeftShift` |
| `29` | `EditorScriptKeyCodeLeftCtrl` |
| `200` | `EditorScriptKeyCodeUp` |
| `208` | `EditorScriptKeyCodeDown` |
| `203` | `EditorScriptKeyCodeLeft` |
| `205` | `EditorScriptKeyCodeRight` |

#### `enum EditorScriptAiSensorKind`

| 値 | 定数名 |
| --- | --- |
| `0` | `EditorScriptAiSensorKindVision` |
| `1` | `EditorScriptAiSensorKindObjectDetection` |
| `2` | `EditorScriptAiSensorKindColorTracking` |
| `3` | `EditorScriptAiSensorKindMotionDetection` |
| `4` | `EditorScriptAiSensorKindWhisperSpeech` |
| `5` | `EditorScriptAiSensorKindVoiceCommand` |

#### `enum EditorScriptFieldType`

| 値 | 定数名 |
| --- | --- |
| `0` | `EditorScriptFieldTypeBool` |
| `1` | `EditorScriptFieldTypeInt32` |
| `2` | `EditorScriptFieldTypeFloat` |
| `3` | `EditorScriptFieldTypeVector2` |
| `4` | `EditorScriptFieldTypeVector3` |
| `5` | `EditorScriptFieldTypeString` |
| `6` | `EditorScriptFieldTypeGameObject` |
| `7` | `EditorScriptFieldTypeSceneAsset` |

#### `enum EditorScriptInputPhase`

| 値 | 定数名 |
| --- | --- |
| `0` | `EditorScriptInputPhaseStarted` |
| `1` | `EditorScriptInputPhasePerformed` |
| `2` | `EditorScriptInputPhaseCanceled` |

#### `enum EditorScriptInputValueType`

| 値 | 定数名 |
| --- | --- |
| `0` | `EditorScriptInputValueTypeButton` |
| `1` | `EditorScriptInputValueTypeVector2` |

#### `enum EditorScriptActionPayloadType`

| 値 | 定数名 |
| --- | --- |
| `0` | `EditorScriptActionPayloadTypeNone` |
| `1` | `EditorScriptActionPayloadTypeGameObject` |
| `2` | `EditorScriptActionPayloadTypeInt` |
| `3` | `EditorScriptActionPayloadTypeFloat` |
| `4` | `EditorScriptActionPayloadTypeBool` |
| `5` | `EditorScriptActionPayloadTypeVector3` |
| `6` | `EditorScriptActionPayloadTypeString` |

#### `struct EditorScriptVector2`

| Field | 型 | 既定値 | 意味 |
| --- | --- | --- | --- |
| `x` | `float` | — | — |
| `y` | `float` | — | — |

#### `struct EditorScriptVector3`

| Field | 型 | 既定値 | 意味 |
| --- | --- | --- | --- |
| `x` | `float` | — | — |
| `y` | `float` | — | — |
| `z` | `float` | — | — |

#### `struct EditorScriptTransform`

| Field | 型 | 既定値 | 意味 |
| --- | --- | --- | --- |
| `position` | `EditorScriptVector3` | — | — |
| `rotation` | `EditorScriptVector3` | — | — |
| `scale` | `EditorScriptVector3` | — | — |

#### `struct EditorScriptFieldValue`

| Field | 型 | 既定値 | 意味 |
| --- | --- | --- | --- |
| `type` | `int32_t` | — | — |
| `boolValue` | `bool` | — | — |
| `reservedPadding[3]` | `uint8_t[3]` | — | ABIそろえ用の未使用領域。読まない。 |
| `intValue` | `int32_t` | — | — |
| `floatValue` | `float` | — | — |
| `vector2Value` | `EditorScriptVector2` | — | — |
| `vector3Value` | `EditorScriptVector3` | — | — |
| `stringValue[256]` | `char[256]` | — | — |

#### `struct EditorScriptFieldDescriptor`

| Field | 型 | 既定値 | 意味 |
| --- | --- | --- | --- |
| `name[64]` | `char[64]` | — | — |
| `displayName[64]` | `char[64]` | — | — |
| `defaultValue` | `EditorScriptFieldValue` | — | — |
| `minValue` | `float` | — | — |
| `maxValue` | `float` | — | — |
| `step` | `float` | — | — |
| `hasRange` | `bool` | — | — |
| `reservedPadding[3]` | `uint8_t[3]` | — | ABIそろえ用の未使用領域。読まない。 |

#### `struct EditorScriptInputActionContext`

| Field | 型 | 既定値 | 意味 |
| --- | --- | --- | --- |
| `gameObjectId` | `int32_t` | — | — |
| `phase` | `int32_t` | — | — |
| `valueType` | `int32_t` | — | — |
| `buttonValue` | `float` | — | — |
| `vector2Value` | `EditorScriptVector2` | — | — |
| `actionMapName[64]` | `char[64]` | — | — |
| `actionName[64]` | `char[64]` | — | — |
| `bindingPath[128]` | `char[128]` | — | — |
| `payloadType` | `int32_t` | — | EditorScriptActionPayloadType |
| `payloadGameObjectId` | `int32_t` | — | — |
| `payloadInt` | `int32_t` | — | — |
| `payloadFloat` | `float` | — | — |
| `payloadBool` | `bool` | — | — |
| `payloadPadding[3]` | `uint8_t[3]` | — | ABIそろえ用の未使用領域。読まない。 |
| `payloadVector3` | `EditorScriptVector3` | — | — |
| `payloadString[256]` | `char[256]` | — | — |

#### `struct EditorScriptActionPayload`

| Field | 型 | 既定値 | 意味 |
| --- | --- | --- | --- |
| `type` | `int32_t` | — | — |
| `gameObjectId` | `int32_t` | — | — |
| `intValue` | `int32_t` | — | — |
| `floatValue` | `float` | — | — |
| `boolValue` | `bool` | — | — |
| `reservedPadding[3]` | `uint8_t[3]` | — | ABIそろえ用の未使用領域。読まない。 |
| `vector3Value` | `EditorScriptVector3` | — | — |
| `stringValue[256]` | `char[256]` | — | — |

#### `struct EditorScriptPhysicsEvent`

| Field | 型 | 既定値 | 意味 |
| --- | --- | --- | --- |
| `type` | `int32_t` | — | — |
| `selfGameObjectId` | `int32_t` | — | — |
| `otherGameObjectId` | `int32_t` | — | — |
| `point` | `EditorScriptVector3` | — | — |
| `normal` | `EditorScriptVector3` | — | — |
| `relativeVelocity` | `EditorScriptVector3` | — | — |
| `separation` | `float` | — | — |
| `isTrigger` | `bool` | — | — |
| `reservedPadding[3]` | `uint8_t[3]` | — | ABIそろえ用の未使用領域。読まない。 |

#### `struct EditorScriptRay`

| Field | 型 | 既定値 | 意味 |
| --- | --- | --- | --- |
| `origin` | `EditorScriptVector3` | — | — |
| `direction` | `EditorScriptVector3` | — | — |

#### `struct EditorScriptPhysicsHit`

| Field | 型 | 既定値 | 意味 |
| --- | --- | --- | --- |
| `gameObjectId` | `int32_t` | — | — |
| `point` | `EditorScriptVector3` | — | — |
| `normal` | `EditorScriptVector3` | — | — |
| `distance` | `float` | — | — |
| `isTrigger` | `bool` | — | — |
| `reservedPadding[3]` | `uint8_t[3]` | — | ABIそろえ用の未使用領域。読まない。 |

#### `struct EditorScriptOceanSurfaceHit`

| Field | 型 | 既定値 | 意味 |
| --- | --- | --- | --- |
| `oceanGameObjectId` | `int32_t` | — | — |
| `point` | `EditorScriptVector3` | — | — |
| `normal` | `EditorScriptVector3` | — | — |
| `velocity` | `EditorScriptVector3` | — | — |
| `signedDistance` | `float` | — | — |

#### `struct EditorScriptOceanSegmentHit`

| Field | 型 | 既定値 | 意味 |
| --- | --- | --- | --- |
| `oceanGameObjectId` | `int32_t` | — | — |
| `point` | `EditorScriptVector3` | — | — |
| `normal` | `EditorScriptVector3` | — | — |
| `surfaceVelocity` | `EditorScriptVector3` | — | — |
| `distance` | `float` | — | — |
| `normalizedDistance` | `float` | — | — |

#### `struct EditorScriptOceanOcclusion`

| Field | 型 | 既定値 | 意味 |
| --- | --- | --- | --- |
| `blocked` | `bool` | — | — |
| `reservedPadding[3]` | `uint8_t[3]` | — | ABIそろえ用の未使用領域。読まない。 |
| `minimumClearance` | `float` | — | — |
| `maximumSurfaceHeight` | `float` | — | — |
| `intersection` | `EditorScriptOceanSegmentHit` | — | — |

#### `struct EditorScriptWaterSurfaceState`

| Field | 型 | 既定値 | 意味 |
| --- | --- | --- | --- |
| `state` | `int32_t` | — | 0=Above、1=Entering、2=Underwater、3=Leaving |
| `signedDistance` | `float` | — | — |
| `oceanGameObjectId` | `int32_t` | — | — |
| `surfacePosition` | `EditorScriptVector3` | — | — |
| `surfaceNormal` | `EditorScriptVector3` | — | — |
| `surfaceVelocity` | `EditorScriptVector3` | — | — |

#### `struct EditorScriptOceanProbeSample`

| Field | 型 | 既定値 | 意味 |
| --- | --- | --- | --- |
| `valid` | `bool` | — | — |
| `reservedPadding[3]` | `uint8_t[3]` | — | ABIそろえ用の未使用領域。読まない。 |
| `distance` | `float` | — | — |
| `position` | `EditorScriptVector3` | — | — |
| `normal` | `EditorScriptVector3` | — | — |
| `velocity` | `EditorScriptVector3` | — | — |
| `relativeHeight` | `float` | — | — |

#### `struct EditorScriptDamageContext`

| Field | 型 | 既定値 | 意味 |
| --- | --- | --- | --- |
| `targetGameObjectId` | `int32_t` | `-1` | DamageReceiver / Healthを持つ被弾対象 |
| `sourceGameObjectId` | `int32_t` | `-1` | ProjectileやWeaponなど、直接Damageを発生させたObject |
| `instigatorGameObjectId` | `int32_t` | `-1` | 発射者や攻撃者など、Damageの責任主体 |
| `hitPosition` | `EditorScriptVector3` | `{}` | World空間の命中位置 |
| `hitNormal` | `EditorScriptVector3` | `{0.0f, 1.0f, 0.0f}` | World空間の命中面法線 |
| `impulse` | `EditorScriptVector3` | `{}` | 対象Rigidbodyへ加える瞬間力 |
| `baseDamage` | `float` | `0.0f` | DamageReceiver倍率を適用する前の値 |
| `appliedDamage` | `float` | `0.0f` | Runtimeが実際にHealthから減らした値 |
| `userTag` | `int32_t` | `0` | ゲーム側が任意用途へ使う識別値。固定Enumの意味は持たない |

#### `struct EditorScriptThreatInfo`

| Field | 型 | 既定値 | 意味 |
| --- | --- | --- | --- |
| `projectileGameObjectId` | `int32_t` | `-1` | 接近中のProjectile実体 |
| `sourceGameObjectId` | `int32_t` | `-1` | Projectileを発射したGameObject |
| `distance` | `float` | `0.0f` | Tracker対象までのWorld距離 |
| `closingSpeed` | `float` | `0.0f` | 対象へ近づく相対速度 |
| `estimatedArrivalSeconds` | `float` | `0.0f` | 最接近までの予測秒数 |

#### `struct EditorScriptBallisticPrediction`

| Field | 型 | 既定値 | 意味 |
| --- | --- | --- | --- |
| `valid` | `bool` | — | — |
| `reservedPadding[3]` | `uint8_t[3]` | — | ABIそろえ用の未使用領域。読まない。 |
| `launchDirection` | `EditorScriptVector3` | — | — |
| `impactPosition` | `EditorScriptVector3` | — | — |
| `flightTime` | `float` | — | — |
| `trajectoryPointCount` | `int32_t` | — | — |
| `launchVelocity` | `EditorScriptVector3` | — | 発射元速度を含む初期World速度 |
| `sourceVelocity` | `EditorScriptVector3` | — | 発射母体から継承した作用点速度 |

#### `struct EditorScriptFireLineState`

| Field | 型 | 既定値 | 意味 |
| --- | --- | --- | --- |
| `isClear` | `bool` | — | — |
| `reservedPadding[3]` | `uint8_t[3]` | — | ABIそろえ用の未使用領域。読まない。 |
| `blockingGameObjectId` | `int32_t` | — | — |
| `blockingDistance` | `float` | — | — |

#### `struct EditorScriptStatusEffectEntry`

| Field | 型 | 既定値 | 意味 |
| --- | --- | --- | --- |
| `effectId[64]` | `char[64]` | — | — |
| `sourceGameObjectId` | `int32_t` | — | — |
| `remainingSeconds` | `float` | — | — |
| `tickRemainingSeconds` | `float` | — | — |
| `stackCount` | `int32_t` | — | — |

#### `struct EditorScriptDamageEvent`

| Field | 型 | 既定値 | 意味 |
| --- | --- | --- | --- |
| `sourceGameObjectId` | `int32_t` | — | — |
| `worldDirection` | `EditorScriptVector3` | — | — |
| `damage` | `float` | — | — |
| `damageTagId` | `int32_t` | — | — |
| `remainingSeconds` | `float` | — | — |

#### `struct EditorScriptRailState`

| Field | 型 | 既定値 | 意味 |
| --- | --- | --- | --- |
| `hasComponent` | `bool` | — | — |
| `isReady` | `bool` | — | — |
| `isPaused` | `bool` | — | — |
| `isReversed` | `bool` | — | — |
| `endReached` | `bool` | — | — |
| `reservedPadding[3]` | `uint8_t[3]` | — | ABIそろえ用の未使用領域。読まない。 |
| `normalizedProgress` | `float` | — | — |
| `traveledDistance` | `float` | — | — |
| `totalDistance` | `float` | — | — |
| `currentSpeed` | `float` | — | — |
| `targetSpeed` | `float` | — | — |
| `offset` | `EditorScriptVector2` | — | — |

#### `struct EditorScriptRailFrame`

| Field | 型 | 既定値 | 意味 |
| --- | --- | --- | --- |
| `position` | `EditorScriptVector3` | — | — |
| `forward` | `EditorScriptVector3` | — | — |
| `right` | `EditorScriptVector3` | — | — |
| `up` | `EditorScriptVector3` | — | — |

#### `struct EditorScriptAnimationEvent`

| Field | 型 | 既定値 | 意味 |
| --- | --- | --- | --- |
| `name` | `const char*` | — | .animgraph の Event に設定した任意のイベント名。 |
| `effectAssetPath` | `const char*` | — | Event と同時に再生する .effect。未設定なら空文字列。 |
| `time` | `float` | — | Clip 先頭からイベント位置までの秒数。 |
| `localOffset` | `EditorScriptVector3` | — | GameObject 基準で Effect を発生させるローカル位置。 |

#### `struct EditorScriptAiSensorState`

| Field | 型 | 既定値 | 意味 |
| --- | --- | --- | --- |
| `hasComponent` | `bool` | — | — |
| `isActive` | `bool` | — | — |
| `isDetected` | `bool` | — | — |
| `hasDetails` | `bool` | — | — |
| `connectedGameObjectId` | `int32_t` | — | — |
| `detectedGameObjectId` | `int32_t` | — | — |
| `commandId` | `int32_t` | — | — |
| `range` | `float` | — | — |
| `angleDegrees` | `float` | — | — |
| `confidence` | `float` | — | — |
| `distance` | `float` | — | — |
| `direction` | `EditorScriptVector3` | — | — |
| `screenPosition` | `EditorScriptVector2` | — | — |
| `boundsPosition` | `EditorScriptVector2` | — | — |
| `boundsSize` | `EditorScriptVector2` | — | — |
| `motion` | `EditorScriptVector2` | — | — |
| `motionMagnitude` | `float` | — | — |
| `label[64]` | `char[64]` | — | — |
| `text[256]` | `char[256]` | — | — |
| `command[64]` | `char[64]` | — | — |

#### `struct EditorScriptMaterialState`

| Field | 型 | 既定値 | 意味 |
| --- | --- | --- | --- |
| `hasComponent` | `bool` | — | — |
| `hasTexture` | `bool` | — | — |
| `hasUvLayoutTexture` | `bool` | — | — |
| `useLighting` | `bool` | — | — |
| `reservedPadding[3]` | `bool[3]` | — | ABIそろえ用の未使用領域。読まない。 |
| `intensity` | `float` | — | — |
| `metallic` | `float` | — | — |
| `roughness` | `float` | — | — |
| `ior` | `float` | — | — |
| `alpha` | `float` | — | — |
| `reflectionStrength` | `float` | — | — |
| `color` | `EditorScriptVector3` | — | — |
| `rendererAssetPath[260]` | `char[260]` | — | — |
| `materialName[64]` | `char[64]` | — | — |
| `texturePath[260]` | `char[260]` | — | — |
| `uvLayoutTexturePath[260]` | `char[260]` | — | — |

#### `struct EditorScriptAnimationState`

| Field | 型 | 既定値 | 意味 |
| --- | --- | --- | --- |
| `hasComponent` | `bool` | — | — |
| `isPlaying` | `bool` | — | — |
| `isLoop` | `bool` | — | — |
| `playOnAwake` | `bool` | — | — |
| `animationType` | `int32_t` | — | — |
| `clipCount` | `int32_t` | — | — |
| `animationSpeed` | `float` | — | — |
| `animationAmplitude` | `float` | — | — |
| `currentTime` | `float` | — | — |
| `currentClipDuration` | `float` | — | — |
| `assetPath[260]` | `char[260]` | — | — |
| `currentClipName[64]` | `char[64]` | — | — |

#### `struct EditorScriptRopeState`

| Field | 型 | 既定値 | 意味 |
| --- | --- | --- | --- |
| `hasComponent` | `bool` | — | — |
| `isActive` | `bool` | — | — |
| `isBroken` | `bool` | — | — |
| `reservedPadding` | `bool` | — | ABIそろえ用の未使用領域。読まない。 |
| `targetGameObjectId` | `int32_t` | — | — |
| `maximumLength` | `float` | — | — |
| `currentLength` | `float` | — | — |
| `currentTension` | `float` | — | — |

#### `struct EditorScriptSpringJointDesc`

`JointHandle`は`EditorScriptJointHandle`の高水準別名で、実体は`uint64_t`である。`0`は`kInvalidEditorScriptJointHandle`として予約される。

| Field | 型 | 既定値 | 意味 |
| --- | --- | --- | --- |
| `ownerAnchor` | `EditorScriptVector3` | `{0.0f, 0.0f, 0.0f}` | Owner GameObjectのローカルアンカー。 |
| `connectedAnchor` | `EditorScriptVector3` | `{0.0f, 0.0f, 0.0f}` | Connected GameObjectのローカルアンカー。 |
| `minDistance` | `float` | `0.0f` | Constraintの最小距離。 |
| `maxDistance` | `float` | `1.0f` | Constraintの最大距離。 |
| `frequency` | `float` | `5.0f` | Jolt Springの周波数。 |
| `damping` | `float` | `0.7f` | Jolt Springの減衰。 |

#### `enum class EditorScriptJointType`

| 値 | Joint |
| --- | --- |
| `Fixed` | 2つのBodyの位置と回転を固定する。 |
| `Hinge` | 指定Axis周りの回転だけを許可する。 |
| `Spring` | 最小・最大距離とばね設定で接続する。 |
| `Configurable` | 軸ごとの移動・回転固定と制限を設定する。 |
| `Character` | Hingeと同じ実装経路でCharacter向け角度制限を作る。 |

#### `struct EditorScriptJointDesc`

`ownerAnchor`と`connectedAnchor`は各GameObjectのローカル座標、`axis`はJoint基準軸、角度はラジアンである。距離・角度・ばね設定に加え、Configurable Joint用の`freezePositionX/Y/Z`と`freezeRotationX/Y/Z`を持つ。

#### `struct EditorScriptTurretAimState`

| Field | 型 | 既定値 | 意味 |
| --- | --- | --- | --- |
| `targetGameObjectId` | `int32_t` | — | — |
| `canReachTarget` | `bool` | — | — |
| `isAimed` | `bool` | — | — |
| `reservedPadding[2]` | `uint8_t[2]` | — | ABIそろえ用の未使用領域。読まない。 |
| `yawErrorDegrees` | `float` | — | — |
| `pitchErrorDegrees` | `float` | — | — |


#### 型を読むときの共通ルール

1. **`hasComponent` を持つ構造体は、失敗をfalseで返さない。** `GetAiSensorState` / `GetMaterialState` / `GetAnimationState` / `GetRopeState` は戻り値が構造体そのもので、対象にComponentが無くても既定構造体が返る。必ず `hasComponent` を最初に確認する。
2. **`valid` / `isClear` / `blocked` は成功可否ではなく結果である。** `EditorScriptBallisticPrediction::valid` は「弾道が求まったか」、`EditorScriptFireLineState::isClear` は「射線が通っているか」、`EditorScriptOceanOcclusion::blocked` は「遮られているか」を表す。API呼び出し自体の成否は関数の戻り値 `bool` の方である。
3. **ID系Fieldの `-1` は「未設定」。** `targetGameObjectId`、`sourceGameObjectId`、`instigatorGameObjectId`、`oceanGameObjectId` などは、対象が無いとき `-1` になる。`0` は有効なGameObject IDになり得るので、`> 0` ではなく `>= 0` で判定する。
4. **基本の座標系はWorld。** `EditorScriptTransform`、`EditorScriptPhysicsHit::point`、`EditorScriptRailFrame`、Ocean系の `position` / `normal` / `velocity` はWorld空間である。例外は `EditorScriptAnimationEvent::localOffset`と`EditorScriptSpringJointDesc`の2つのAnchorで、各GameObject基準のローカル位置である。
5. **角度の単位。** `EditorScriptTransform::rotation` と `EditorScriptAiSensorState::angleDegrees`、`EditorScriptTurretAimState::yawErrorDegrees` / `pitchErrorDegrees` は用途がラベル名に出ている。`Degrees` が付くものだけが度で、それ以外の回転値はEngine内部のEuler表現に従う。
6. **時間の単位は秒。** `remainingSeconds`、`tickRemainingSeconds`、`estimatedArrivalSeconds`、`flightTime`、`currentTime`、`currentClipDuration` はすべて秒である。ミリ秒はComponent側の `hapticDurationMs` だけである。
7. **`EditorScriptDamageContext` の `baseDamage` と `appliedDamage` は別物。** `baseDamage` は `DamageReceiver` の倍率と無敵時間を適用する**前**の申告値、`appliedDamage` は実際にHealthから減った値である。`ApplyDamageContext` が `true` を返しても、無敵時間中なら `appliedDamage` は0になり得る。「trueが返った＝ダメージが入った」ではない。
8. **`EditorScriptInputActionContext` はInput ActionとComponent Actionを兼ねる。** `phase` / `valueType` / `buttonValue` / `vector2Value` / `actionMapName` / `actionName` / `bindingPath` はInput System由来、`payloadType` 以降はComponentからの型付きAction Payload由来である。Component経由で来たActionは、実際のInput Actionが無くても次の固定値が入る。この値でどちらの経路から来たかを見分けられる。

| 経路 | `phase` | `actionMapName` | `actionName` | `bindingPath` |
| --- | --- | --- | --- | --- |
| Input System（PlayerInput） | Started / Performed / Canceled | Input Actions Assetの実名 | Input Actions Assetの実名 | 実際のBinding Path |
| UI Component（Button等） | 常に Performed | `"UI"` | `"Button"` | `"UI/Button"` |
| その他のComponent Action | 常に Performed | `"Event"` | `"Trigger"` | `"Event/Trigger"` |

Component経由のActionは `phase` が常に `Performed` なので、**押した瞬間と離した瞬間をこのcontextで区別することはできない**。区別が要る場合はInput Actionを使う。

### 高水準Wrapper 全61クラス 索引

`EditorNativeScript.h` が定義する全Class と、その公開メンバーを宣言順に列挙する。`private:` 以降のメンバー（多くは `gameObjectId_`）は除いている。

**Wrapperの共通契約。**

- ほぼすべてのWrapperは `explicit Wrapper(const GameObject&)` で作り、内部にGameObject IDだけを持つ。**Componentへのポインタは持たないので、Wrapper Objectをメンバー変数として保持し続けても安全**である。ただし対象GameObjectがDestroyまたはPool返却されると、以後の呼び出しはfalseを返すようになる。
- `static` メンバーしか持たないClass（`Input` / `Physics` / `Ocean` / `SceneManager` / `SaveSystem` / `EffectManager` / `DamageTag` / `TimeScale::GetCurrent` など）はGameObjectと無関係のRuntime全体の操作である。
- 戻り値 `bool` はすべて「その呼び出しが成立したか」であり、**ゲーム的な成功（命中した、破壊した、到達した）ではない**。
- 戻り値が値型（`float` / `int32_t` / `std::string`）のメンバーは、失敗時に既定値（`0.0f` / `-1` / 空文字列）を返す。有効値と区別できない場合があるので、区別が要る場面では対になる `bool` 版か低水準Entryを使う。

#### `enum class KeyCode`

| 値 | 定数名 |
| --- | --- |
| `0x01` | `Escape` |
| `0x02` | `Digit1` |
| `0x03` | `Digit2` |
| `0x04` | `Digit3` |
| `0x05` | `Digit4` |
| `0x06` | `Digit5` |
| `0x07` | `Digit6` |
| `0x08` | `Digit7` |
| `0x09` | `Digit8` |
| `0x0A` | `Digit9` |
| `0x0B` | `Digit0` |
| `0x0F` | `Tab` |
| `0x10` | `Q` |
| `0x11` | `W` |
| `0x12` | `E` |
| `0x13` | `R` |
| `0x14` | `T` |
| `0x15` | `Y` |
| `0x16` | `U` |
| `0x17` | `I` |
| `0x18` | `O` |
| `0x19` | `P` |
| `0x1C` | `Enter` |
| `0x1D` | `LeftControl` |
| `0x1E` | `A` |
| `0x1F` | `S` |
| `0x20` | `D` |
| `0x21` | `F` |
| `0x22` | `G` |
| `0x23` | `H` |
| `0x24` | `J` |
| `0x25` | `K` |
| `0x26` | `L` |
| `0x2A` | `LeftShift` |
| `0x2C` | `Z` |
| `0x2D` | `X` |
| `0x2E` | `C` |
| `0x2F` | `V` |
| `0x30` | `B` |
| `0x31` | `N` |
| `0x32` | `M` |
| `0x39` | `Space` |
| `0xC8` | `UpArrow` |
| `0xCB` | `LeftArrow` |
| `0xCD` | `RightArrow` |
| `0xD0` | `DownArrow` |

#### `enum class ObjectiveState`

| 値 | 定数名 |
| --- | --- |
| `0` | `Inactive` |
| `1` | `Active` |
| `2` | `Completed` |
| `3` | `Failed` |

#### `class EditorNativeScriptRuntime`

| メンバー |
| --- |
| `static void SetRuntimeApi(const EditorScriptRuntimeApi* runtimeApi)` |
| `static const EditorScriptRuntimeApi* GetRuntimeApi()` |

#### `class Input`

| メンバー |
| --- |
| `static bool GetKey(KeyCode keyCode)` |
| `static bool GetKeyDown(KeyCode keyCode)` |

#### `class SceneManager`

| メンバー |
| --- |
| `static bool LoadScene(const char* scenePath)` |
| `static bool LoadScene(const std::string& scenePath)` |
| `static bool LoadScene(int32_t sceneBuildIndex)` |
| `static bool LoadSceneAsync(const std::string& scenePath)` |
| `static bool LoadSceneAdditiveAsync(const std::string& scenePath)` |
| `static bool UnloadScene(const std::string& scenePath)` |
| `static float GetLoadProgress()` |
| `static bool IsLoading()` |
| `static bool IsLoaded(const std::string& scenePath)` |
| `static void SetFloat(const std::string& key, float value)` |
| `static bool GetFloat(const std::string& key, float& value)` |
| `static void SetString(const std::string& key, const std::string& value)` |
| `static bool GetString(const std::string& key, std::string& value)` |

#### `class GameObject`

| メンバー |
| --- |
| `GameObject(int32_t gameObjectId = -1)` — コンストラクタ |
| `static GameObject Find(const char* gameObjectName)` |
| `int32_t GetInstanceId() const` |
| `bool HasReference() const` |
| `bool IsActive() const` |
| `bool SetActive(bool isActive) const` |
| `bool SetComponentActive(const char* componentTypeName, bool isActive) const` |
| `bool IsComponentActive(const char* componentTypeName) const` |
| `bool HasComponent(const char* componentTypeName) const` |
| `bool InvokeAction(const char* functionName) const` |
| `bool InvokeAction(const char* functionName, const EditorScriptActionPayload& payload) const` |
| `EditorScriptTransform GetTransform() const` |
| `bool SetTransform(const EditorScriptTransform& transform) const` |

#### `class Component`

| メンバー |
| --- |
| `Component(const GameObject& ownerGameObject, const char* componentTypeName)` — コンストラクタ |
| `bool IsValid() const` |
| `bool SetActive(bool isActive) const` |
| `bool IsActive() const` |
| `bool SetFloat(const char* propertyName, float value) const` |
| `bool GetFloat(const char* propertyName, float& value) const` |
| `bool SetInt(const char* propertyName, int32_t value) const` |
| `bool GetInt(const char* propertyName, int32_t& value) const` |
| `bool SetBool(const char* propertyName, bool value) const` |
| `bool GetBool(const char* propertyName, bool& value) const` |
| `bool SetVector2(const char* propertyName, const EditorScriptVector2& value) const` |
| `bool GetVector2(const char* propertyName, EditorScriptVector2& value) const` |
| `bool SetVector3(const char* propertyName, const EditorScriptVector3& value) const` |
| `bool GetVector3(const char* propertyName, EditorScriptVector3& value) const` |
| `bool SetGameObject(const char* propertyName, const GameObject& value) const` |
| `bool GetGameObject(const char* propertyName, GameObject& value) const` |

#### `class RailFollower`

| メンバー |
| --- |
| `RailFollower(const GameObject& gameObject)` — コンストラクタ |
| `RailFollower(int32_t gameObjectId)` — コンストラクタ |
| `bool Pause() const` |
| `bool Resume() const` |
| `bool SetPaused(bool isPaused) const` |
| `bool IsPaused() const` |
| `bool SetSpeed(float speed) const` |
| `bool SetReverse(bool isReversed) const` |
| `bool JumpTo(float normalizedProgress) const` |
| `bool SwitchRail(const GameObject& railPathGameObject, bool preservesProgress = true) const` |
| `bool SetMoveInput(const EditorScriptVector2& moveInput) const` |
| `bool SetOffset(const EditorScriptVector2& offset) const` |
| `bool ClearMoveInput() const` |
| `bool GetOffset(EditorScriptVector2& offset) const` |
| `bool GetNormalizedProgress(float& normalizedProgress) const` |
| `bool GetLength(float& railLength) const` |
| `bool GetPosition(float normalizedProgress, EditorScriptVector3& position) const` |
| `bool GetDirection(float normalizedProgress, EditorScriptVector3& direction) const` |
| `bool SetDistance(float distance) const` |
| `bool GetState(EditorScriptRailState& state) const` |
| `bool SetSpeedProfileEnabled(bool isEnabled) const` |
| `bool GetSpeedMultiplier(float& speedMultiplier) const` |
| `std::string GetActiveZone() const` |
| `bool RearmEventMarkers(const char* markerId = "") const` |
| `bool GetClosestProgress(const EditorScriptVector3& worldPosition, float& normalizedProgress) const` |
| `bool GetFrame(float normalizedProgress, EditorScriptRailFrame& frame) const` |
| `bool ConsumeEndReached() const` |

#### `class SimulationLod`

| メンバー |
| --- |
| `SimulationLod(const GameObject& gameObject)` — コンストラクタ |
| `bool GetLevel(int32_t& lodLevel) const` |

#### `class Physics`

| メンバー |
| --- |
| `static bool ViewportPointToRay(const EditorScriptVector2& normalizedPosition, EditorScriptRay& ray)` |
| `static bool GetAimRay(const GameObject& screenAimGameObject, EditorScriptRay& ray)` |
| `static bool Raycast(const EditorScriptRay& ray, float distance, EditorScriptPhysicsHit& hit)` |
| `static bool RaycastIgnoringHierarchy(const EditorScriptRay& ray, float distance, const GameObject& ignoreHierarchyRoot, EditorScriptPhysicsHit& hit)` |
| `static bool SphereCast(const EditorScriptRay& ray, float radius, float distance, EditorScriptPhysicsHit& hit)` |
| `static bool CapsuleCast(const EditorScriptRay& ray, float radius, float height, float distance, EditorScriptPhysicsHit& hit)` |
| `static bool SampleOceanSurface(const GameObject& queryGameObject, const EditorScriptVector3& worldPosition, EditorScriptOceanSurfaceHit& hit)` |
| `static int32_t AddExplosionImpulse(const EditorScriptVector3& center, float radius, float impulseStrength, float upwardModifier = 0.0f)` |
| `static JointHandle CreateSpringJoint(const GameObject& ownerGameObject, const GameObject& connectedGameObject, const SpringJointDesc& springJointDesc)` |
| `static JointHandle CreateSpringJoint(int32_t ownerGameObjectId, int32_t connectedGameObjectId, const SpringJointDesc& springJointDesc)` |
| `static bool DestroyJoint(JointHandle jointHandle)` |
| `static bool SetSpringJointSettings(JointHandle jointHandle, const SpringJointDesc& springJointDesc)` |
| `static bool IsJointValid(JointHandle jointHandle)` |
| `static JointHandle CreateJoint(JointType jointType, int32_t ownerGameObjectId, int32_t connectedGameObjectId, const JointDesc& jointDesc)` |
| `static JointHandle CreateJoint(JointType jointType, const GameObject& ownerGameObject, const GameObject& connectedGameObject, const JointDesc& jointDesc)` |
| `static bool SetJointSettings(JointHandle jointHandle, const JointDesc& jointDesc)` |

#### `class Ocean`

| メンバー |
| --- |
| `static bool Sample(const GameObject& queryGameObject, const EditorScriptVector3& worldPosition, EditorScriptOceanSurfaceHit& hit)` |
| `static bool SampleDetailed(const GameObject& queryGameObject, const EditorScriptVector3& worldPosition, EditorScriptOceanSurfaceHit& hit, float& foam)` |

#### `class Health`

| メンバー |
| --- |
| `Health(const GameObject& gameObject)` — コンストラクタ |
| `bool Damage(EditorScriptDamageContext& damageContext) const` |
| `bool GetLastDamageContext(EditorScriptDamageContext& damageContext) const` |
| `bool Get(float& currentHealth, float& maximumHealth) const` |
| `bool Set(float currentHealth) const` |

#### `class ObjectPool`

| メンバー |
| --- |
| `ObjectPool(const GameObject& gameObject)` — コンストラクタ |
| `static bool Release(const GameObject& gameObject)` |

#### `class Spawner`

| メンバー |
| --- |
| `Spawner(const GameObject& gameObject)` — コンストラクタ |
| `GameObject Spawn() const` |

#### `class WaveSpawner`

| メンバー |
| --- |
| `WaveSpawner(const GameObject& gameObject)` — コンストラクタ |
| `bool Start() const` |
| `bool IsComplete(bool waitsForAllDefeated = true) const` |

#### `class Weapon`

| メンバー |
| --- |
| `Weapon(const GameObject& gameObject)` — コンストラクタ |
| `bool FireHitscan() const` |
| `bool FireProjectile() const` |
| `float GetAccuracySpread() const` |

#### `class WeaponLoadout`

| メンバー |
| --- |
| `WeaponLoadout(const GameObject& gameObject)` — コンストラクタ |
| `bool Select(int32_t slotIndex) const` |
| `bool Next() const` |
| `bool Previous() const` |
| `bool Fire() const` |
| `bool Reload() const` |
| `bool GetAmmo(int32_t& currentAmmo, int32_t& reserveAmmo) const` |
| `bool GetAmmoAtSlot(int32_t slotIndex, int32_t& currentAmmo, int32_t& reserveAmmo, int32_t& maximumAmmo) const` |
| `bool AddMagazineAmmo(int32_t slotIndex, int32_t amount) const` |
| `bool AddReserveAmmo(int32_t slotIndex, int32_t amount) const` |
| `bool SetMagazineAmmo(int32_t slotIndex, int32_t amount) const` |
| `bool SetReserveAmmo(int32_t slotIndex, int32_t amount) const` |
| `bool SetMaximumAmmo(int32_t slotIndex, int32_t amount) const` |
| `bool RefillMagazine(int32_t slotIndex = -1) const` |

#### `class WeaponGroup`

| メンバー |
| --- |
| `WeaponGroup(const GameObject& gameObject)` — コンストラクタ |
| `bool Fire() const` |
| `bool IsFiring() const` |

#### `class TurretAim`

| メンバー |
| --- |
| `TurretAim(const GameObject& gameObject)` — コンストラクタ |
| `bool GetState(EditorScriptTurretAimState& state) const` |

#### `class FireLineCheck`

| メンバー |
| --- |
| `FireLineCheck(const GameObject& gameObject)` — コンストラクタ |
| `bool GetState(EditorScriptFireLineState& state) const` |

#### `class StatusEffectSet`

| メンバー |
| --- |
| `StatusEffectSet(const GameObject& gameObject)` — コンストラクタ |
| `bool Remove(const char* effectId) const` |
| `bool Clear() const` |
| `bool Has(const char* effectId) const` |
| `std::vector<EditorScriptStatusEffectEntry> GetEntries() const` |

#### `class Targeting`

| メンバー |
| --- |
| `Targeting(const GameObject& gameObject)` — コンストラクタ |
| `GameObject GetCurrentTarget() const` |
| `bool SetTarget(const GameObject& targetGameObject) const` |
| `bool GetInterceptPrediction(EditorScriptVector3& position, float& timeSeconds) const` |

#### `class RuntimeProperty`

| メンバー |
| --- |
| `static bool SetVector2(const GameObject& gameObject, const std::string& componentName, const std::string& propertyName, const EditorScriptVector2& value)` |
| `static bool GetVector2(const GameObject& gameObject, const std::string& componentName, const std::string& propertyName, EditorScriptVector2& value)` |
| `static bool SetFloat(const GameObject& gameObject, const std::string& componentName, const std::string& propertyName, float value)` |
| `static bool GetFloat(const GameObject& gameObject, const std::string& componentName, const std::string& propertyName, float& value)` |
| `static bool SetInt(const GameObject& gameObject, const std::string& componentName, const std::string& propertyName, int32_t value)` |
| `static bool GetInt(const GameObject& gameObject, const std::string& componentName, const std::string& propertyName, int32_t& value)` |
| `static bool SetBool(const GameObject& gameObject, const std::string& componentName, const std::string& propertyName, bool value)` |
| `static bool GetBool(const GameObject& gameObject, const std::string& componentName, const std::string& propertyName, bool& value)` |
| `static bool SetVector3(const GameObject& gameObject, const std::string& componentName, const std::string& propertyName, const EditorScriptVector3& value)` |
| `static bool GetVector3(const GameObject& gameObject, const std::string& componentName, const std::string& propertyName, EditorScriptVector3& value)` |

#### `class ActionPayload`

| メンバー |
| --- |
| `static EditorScriptActionPayload GameObjectValue(const GameObject& gameObject)` |
| `static EditorScriptActionPayload Int(int32_t value)` |
| `static EditorScriptActionPayload Float(float value)` |
| `static EditorScriptActionPayload Bool(bool value)` |
| `static EditorScriptActionPayload Vector3(const EditorScriptVector3& value)` |
| `static EditorScriptActionPayload String(const std::string& value)` |

#### `class Timer`

| メンバー |
| --- |
| `Timer(const GameObject& gameObject)` — コンストラクタ |
| `bool Start() const` |
| `bool Pause(bool isPaused = true) const` |
| `bool Resume() const` |
| `bool GetRemaining(float& seconds) const` |

#### `class GenericStateMachine`

| メンバー |
| --- |
| `GenericStateMachine(const GameObject& gameObject)` — コンストラクタ |
| `bool ChangeState(const std::string& stateName) const` |
| `bool GetState(std::string& stateName) const` |

#### `class Attribute`

| メンバー |
| --- |
| `Attribute(const GameObject& gameObject)` — コンストラクタ |
| `bool Set(float value) const` |
| `bool Get(float& current, float& maximum) const` |

#### `class TargetLock`

| メンバー |
| --- |
| `TargetLock(const GameObject& gameObject)` — コンストラクタ |
| `bool GetState(float& progress, bool& isLocked, GameObject& target) const` |

#### `class MultiTargetLock`

| メンバー |
| --- |
| `MultiTargetLock(const GameObject& gameObject)` — コンストラクタ |
| `std::vector<Entry> GetEntries() const` |

#### `class AttributeSet`

| メンバー |
| --- |
| `AttributeSet(const GameObject& gameObject)` — コンストラクタ |
| `bool Set(const std::string& name, float value) const` |
| `bool Get(const std::string& name, float& current, float& maximum) const` |
| `bool Add(const std::string& name, float deltaValue) const` |

#### `class GenericCounter`

| メンバー |
| --- |
| `GenericCounter(const GameObject& gameObject)` — コンストラクタ |
| `bool Set(float value) const` |
| `bool Add(float deltaValue = 1.0f) const` |
| `bool Get(float& value) const` |

#### `class GenericCondition`

| メンバー |
| --- |
| `GenericCondition(const GameObject& gameObject)` — コンストラクタ |
| `bool Evaluate(bool& result) const` |

#### `class GameplayData`

| メンバー |
| --- |
| `GameplayData(const GameObject& gameObject)` — コンストラクタ |
| `bool GetString(const std::string& key, std::string& value) const` |
| `bool GetInt(const std::string& key, int32_t& value) const` |
| `bool GetFloat(const std::string& key, float& value) const` |
| `bool GetBool(const std::string& key, bool& value) const` |

#### `class DamageTag`

| メンバー |
| --- |
| `static int32_t Id(const std::string& damageTag)` |

#### `class AreaDamage`

| メンバー |
| --- |
| `AreaDamage(const GameObject& gameObject)` — コンストラクタ |

#### `class ProjectileDetonator`

| メンバー |
| --- |
| `ProjectileDetonator(const GameObject& projectileGameObject)` — コンストラクタ |
| `bool Detonate() const` |

#### `class ThreatTracker`

| メンバー |
| --- |
| `ThreatTracker(const GameObject& gameObject)` — コンストラクタ |
| `std::vector<Entry> GetEntries() const` |

#### `class CooldownSet`

| メンバー |
| --- |
| `CooldownSet(const GameObject& gameObject)` — コンストラクタ |
| `bool Start(const std::string& cooldownName, float durationOverride = -1.0f) const` |
| `bool Reset(const std::string& cooldownName) const` |
| `bool Get(const std::string& cooldownName, float& remainingSeconds, bool& isReady) const` |
| `bool IsReady(const std::string& cooldownName) const` |

#### `class RuntimeStateReset`

| メンバー |
| --- |
| `RuntimeStateReset(const GameObject& gameObject)` — コンストラクタ |
| `bool Reset() const` |

#### `class Audio`

| メンバー |
| --- |
| `Audio(const GameObject& gameObject)` — コンストラクタ |
| `bool Play() const` |
| `void Stop() const` |
| `static void SetBusVolume(int32_t audioBus, float volume)` |
| `static float GetBusVolume(int32_t audioBus)` |
| `static void SetMasterVolume(float volume)` |
| `static float GetMasterVolume()` |

#### `class EffectManager`

| メンバー |
| --- |
| `static bool SetPosition(int32_t effekseerPlaybackHandle, const EditorScriptVector3& position)` |
| `static void Stop(int32_t effekseerPlaybackHandle)` |

#### `class TimeScale`

| メンバー |
| --- |
| `TimeScale(const GameObject& gameObject)` — コンストラクタ |
| `bool Play(float scaleOverride = -1.0f, float durationOverride = -1.0f) const` |
| `bool HitStop(float durationSeconds) const` |
| `static float GetCurrent()` |

#### `class ObjectiveTracker`

| メンバー |
| --- |
| `ObjectiveTracker(const GameObject& gameObject)` — コンストラクタ |
| `bool Set(const std::string& objectiveId, ObjectiveState state, float currentValue) const` |
| `bool Get(const std::string& objectiveId, ObjectiveState& state, float& currentValue, float& targetValue) const` |

#### `class EncounterController`

| メンバー |
| --- |
| `EncounterController(const GameObject& gameObject)` — コンストラクタ |
| `bool Start() const` |

#### `class SpawnPointSet`

| メンバー |
| --- |
| `SpawnPointSet(const GameObject& gameObject)` — コンストラクタ |
| `bool Resolve(EditorScriptVector3& position, EditorScriptVector3& rotation) const` |

#### `class DifficultyParameterSet`

| メンバー |
| --- |
| `DifficultyParameterSet(const GameObject& gameObject)` — コンストラクタ |
| `bool Apply(int32_t difficultyIndex) const` |

#### `class DamageDirectionIndicator`

| メンバー |
| --- |
| `DamageDirectionIndicator(const GameObject& gameObject)` — コンストラクタ |
| `bool Get(EditorScriptVector2& direction, float& alpha, GameObject& sourceGameObject) const` |

#### `class BallisticPrediction`

| メンバー |
| --- |
| `BallisticPrediction(const GameObject& gameObject)` — コンストラクタ |
| `bool Get(EditorScriptBallisticPrediction& prediction) const` |
| `std::vector<EditorScriptVector3> GetTrajectoryPoints() const` |

#### `class DamageEventBuffer`

| メンバー |
| --- |
| `DamageEventBuffer(const GameObject& gameObject)` — コンストラクタ |
| `std::vector<EditorScriptDamageEvent> GetEntries() const` |

#### `class GamePause`

| メンバー |
| --- |
| `GamePause(const GameObject& gameObject)` — コンストラクタ |
| `bool Pause() const` |
| `bool Resume() const` |
| `bool Set(bool isPaused) const` |
| `static bool IsPaused()` |

#### `class SurfaceWakeEmitter`

| メンバー |
| --- |
| `SurfaceWakeEmitter(const GameObject& gameObject)` — コンストラクタ |
| `bool GetState(float& speed, float& intensity) const` |

#### `class WaterSurfaceState`

| メンバー |
| --- |
| `WaterSurfaceState(const GameObject& gameObject)` — コンストラクタ |
| `bool Get(EditorScriptWaterSurfaceState& state) const` |
| `bool GetFoam(float& foam) const` |

#### `class OceanProbeSet`

| メンバー |
| --- |
| `OceanProbeSet(const GameObject& gameObject)` — コンストラクタ |
| `bool GetSample(int32_t probeIndex, EditorScriptOceanProbeSample& sample) const` |
| `bool GetFoam(int32_t probeIndex, float& foam) const` |

#### `class PropertyTween`

| メンバー |
| --- |
| `PropertyTween(const GameObject& gameObject)` — コンストラクタ |
| `bool Play() const` |
| `bool Stop() const` |
| `bool IsPlaying() const` |

#### `class ActionRelay`

| メンバー |
| --- |
| `ActionRelay(const GameObject& gameObject)` — コンストラクタ |
| `bool Relay() const` |

#### `class CameraEffects`

| メンバー |
| --- |
| `CameraEffects(const GameObject& gameObject)` — コンストラクタ |
| `bool PlayBlend() const` |
| `bool PlayShake() const` |

#### `class RailBranch`

| メンバー |
| --- |
| `RailBranch(const GameObject& gameObject)` — コンストラクタ |
| `bool Trigger() const` |

#### `class ActionSequence`

| メンバー |
| --- |
| `ActionSequence(const GameObject& gameObject)` — コンストラクタ |
| `bool Play() const` |
| `bool Pause(bool isPaused = true) const` |
| `bool Stop() const` |
| `bool Signal(const std::string& signalName) const` |
| `bool IsPlaying() const` |

#### `class SaveSystem`

| メンバー |
| --- |
| `static bool Save(const std::string& slotName)` |
| `static bool Load(const std::string& slotName)` |
| `static bool Delete(const std::string& slotName)` |
| `static bool Exists(const std::string& slotName)` |
| `static void SetFloat(const std::string& key, float value)` |
| `static bool GetFloat(const std::string& key, float& value)` |
| `static void SetString(const std::string& key, const std::string& value)` |
| `static bool GetString(const std::string& key, std::string& value)` |

#### `class Checkpoint`

| メンバー |
| --- |
| `Checkpoint(const GameObject& gameObject)` — コンストラクタ |
| `bool Save() const` |
| `bool Load() const` |

#### `class Rigidbody`

| メンバー |
| --- |
| `Rigidbody(const GameObject& gameObject)` — コンストラクタ |
| `Rigidbody(int32_t gameObjectId)` — コンストラクタ |
| `EditorScriptVector3 GetVelocity() const` |
| `bool SetVelocity(const EditorScriptVector3& velocity) const` |
| `bool AddForce(const EditorScriptVector3& force) const` |
| `bool AddForceAtPosition(const EditorScriptVector3& force, const EditorScriptVector3& worldPosition) const` |
| `bool AddImpulse(const EditorScriptVector3& impulse) const` |
| `bool AddTorque(const EditorScriptVector3& torque) const` |

#### `class RopeConstraint`

| メンバー |
| --- |
| `RopeConstraint(const GameObject& gameObject)` — コンストラクタ |
| `RopeConstraint(int32_t gameObjectId)` — コンストラクタ |
| `bool Attach(const GameObject& targetGameObject, const EditorScriptVector3& ownerLocalAnchor, const EditorScriptVector3& targetLocalAnchor, float maximumLength) const` |
| `bool AttachToWorld(const EditorScriptVector3& ownerLocalAnchor, const EditorScriptVector3& worldAnchor, float maximumLength) const` |
| `bool Detach() const` |
| `bool SetLength(float maximumLength) const` |
| `bool Repair() const` |
| `EditorScriptRopeState GetState() const` |

#### `class EditorNativeScript`

旧ScriptとのABI互換とGeneratedブリッジ内部で使用する基底クラス。新規ユーザーScriptは直接継承せず、次の`Script`を継承する。

| メンバー |
| --- |
| `virtual void Start(int32_t gameObjectId)` |
| `virtual void Update(int32_t gameObjectId, float deltaTime)` |
| `virtual void FixedUpdate(int32_t gameObjectId, float fixedDeltaTime)` |
| `virtual void Stop(int32_t gameObjectId)` |
| `virtual void OnCollisionEnter(const EditorScriptPhysicsEvent& physicsEvent)` |
| `virtual void OnCollisionStay(const EditorScriptPhysicsEvent& physicsEvent)` |
| `virtual void OnCollisionExit(const EditorScriptPhysicsEvent& physicsEvent)` |
| `virtual void OnTriggerEnter(const EditorScriptPhysicsEvent& physicsEvent)` |
| `virtual void OnTriggerStay(const EditorScriptPhysicsEvent& physicsEvent)` |
| `virtual void OnTriggerExit(const EditorScriptPhysicsEvent& physicsEvent)` |
| `void DispatchPhysicsEvent(const EditorScriptPhysicsEvent& physicsEvent)` |
| `virtual void OnAnimationEvent(const EditorScriptAnimationEvent& animationEvent)` |
| `int32_t GetFieldCount() const` |
| `bool GetFieldDescriptor(int32_t fieldIndex, EditorScriptFieldDescriptor& fieldDescriptor) const` |
| `bool GetFieldValue(const char* fieldName, EditorScriptFieldValue& fieldValue) const` |
| `bool SetFieldValue(const char* fieldName, const EditorScriptFieldValue& fieldValue)` |
| `bool InvokeAction(const char* functionName, const EditorScriptInputActionContext& inputContext)` |
| `int32_t GetActionCount() const` |
| `bool GetActionName(int32_t actionIndex, char* actionName, int32_t actionNameCapacity) const` |
| `void ExposeBool(const char* name, const char* displayName, bool& value)` |
| `AddField(name, displayName, defaultValue, false, 0.0f, 0.0f, 1.0f, [&value](EditorScriptFieldValue& fieldValue)` |
| `void ExposeInt32(const char* name, const char* displayName, int32_t& value, int32_t minValue, int32_t maxValue, int32_t step)` |
| `AddField(name, displayName, defaultValue, true, static_cast<float>(minValue), static_cast<float>(maxValue), static_cast<float>((std::max)(step, 1)), [&value](EditorScriptFieldValue& fieldValue)` |
| `void ExposeFloat(const char* name, const char* displayName, float& value, float minValue, float maxValue, float step)` |
| `AddField(name, displayName, defaultValue, true, minValue, maxValue, (std::max)(step, 0.001f), [&value](EditorScriptFieldValue& fieldValue)` |
| `void ExposeVector2(const char* name, const char* displayName, EditorScriptVector2& value, float minValue, float maxValue, float step)` |
| `AddField(name, displayName, defaultValue, true, minValue, maxValue, (std::max)(step, 0.001f), [&value](EditorScriptFieldValue& fieldValue)` |
| `void ExposeVector3(const char* name, const char* displayName, EditorScriptVector3& value, float minValue, float maxValue, float step)` |
| `AddField(name, displayName, defaultValue, true, minValue, maxValue, (std::max)(step, 0.001f), [&value](EditorScriptFieldValue& fieldValue)` |
| `void ExposeString(const char* name, const char* displayName, std::string& value)` |
| `AddField(name, displayName, defaultValue, false, 0.0f, 0.0f, 1.0f, [&value](EditorScriptFieldValue& fieldValue)` |
| `void ExposeGameObject(const char* name, const char* displayName, int32_t& gameObjectId)` |
| `AddField(name, displayName, defaultValue, false, 0.0f, 0.0f, 1.0f, [&gameObjectId](EditorScriptFieldValue& fieldValue)` |
| `void ExposeGameObject(const char* name, const char* displayName, GameObject& gameObject)` |
| `AddField(name, displayName, defaultValue, false, 0.0f, 0.0f, 1.0f, [&gameObject](EditorScriptFieldValue& fieldValue)` |
| `void ExposeScene(const char* name, const char* displayName, std::string& scenePath)` |
| `AddField(name, displayName, defaultValue, false, 0.0f, 0.0f, 1.0f, [&scenePath](EditorScriptFieldValue& fieldValue)` |
| `void BindAction(const char* functionName, ActionFunction actionFunction)` |

#### `class Script`

| メンバー | 用途 |
| --- | --- |
| `virtual void Start()` | 必要なScriptだけoverrideする開始処理。 |
| `virtual void Update(float deltaTime)` | 毎フレームのゲーム処理。 |
| `virtual void FixedUpdate(float fixedDeltaTime)` | 固定時間の物理処理。 |
| `virtual void Stop()` | 必要なScriptだけoverrideする終了処理。 |
| `GameObject GetGameObject() const` | このScriptを所有するGameObjectを取得する。 |
| `template <typename ComponentType> ComponentType GetComponent() const` | 所有GameObjectの型付きWrapperを取得する。 |
| `int32_t GetGameObjectId() const` | IDが必要な低水準処理との互換用。通常は`GetGameObject()`を優先する。 |


### C++ Script Template 全26件

Script作成時に選ぶ雛形の全件である。`Source/Engine/Editor/EditorNativeScriptAssetManager.cpp` の `kTemplateInfos` が一次情報で、選択UIにはこのカテゴリ・表示名・説明・推奨Componentがそのまま出る。

| # | 内部名 | カテゴリ | 表示名 | 生成される処理 | 推奨Component |
| --- | --- | --- | --- | --- | --- |
| 1 | `Empty` | 基本 | 空のスクリプト | 最小構成から独自処理を書きます。 | Script / MonoBehaviour |
| 2 | `PlayerController` | 移動・入力 | プレイヤー移動 | Vector2入力でTransformを移動します。 | PlayerInput / Input / FreeTransform |
| 3 | `RailPlayer` | 移動・入力 | レール移動操作 | 入力をRailMovementの左右・上下Offsetへ渡します。 | RailMovement / PlayerInput / MovementModifier |
| 4 | `EnemyController` | 戦闘・AI | 敵の基本制御 | TargetSelectorの結果を使う敵処理の開始コードです。 | TargetSelector / Health / HitscanWeapon / ProjectileEmitter |
| 5 | `TurretController` | 戦闘・AI | 砲塔制御 | 選択TargetへYaw/Pitchを向けて射撃する開始コードです。 | TargetSelector / HitscanWeapon / ProjectileEmitter |
| 6 | `HomingController` | 戦闘・AI | 追尾制御 | TargetSteeringへ追尾開始・終了条件を追加します。 | TargetSelector / TargetSteering / Rigidbody |
| 7 | `BossController` | 戦闘・AI | 体力フェーズ制御 | Health比率からフェーズを切り替える開始コードです。 | Health / ThresholdState / ActionSequence |
| 8 | `StageController` | Scene・進行 | Scene進行 | 入力やゲーム条件からSceneを切り替えます。 | TimelineEvent / ActionSequence / Scene Asset |
| 9 | `LoadoutController` | 戦闘・入力 | 武器切替 | WeaponLoadoutの切替・射撃・リロードを入力へ接続します。 | WeaponLoadout / WeaponLoadoutSlot / PlayerInput |
| 10 | `PhysicsController` | 物理 | Rigidbody移動 | FixedUpdateで入力方向へ力を加えます。 | RigidBody / Collider / ConstantForce |
| 11 | `HealthDamageController` | 戦闘 | 体力・破壊 | Healthを監視し0以下の終了処理を書く開始コードです。 | Health / DamageReceiver / Collider |
| 12 | `SpawnPoolController` | 生成 | 生成・Pool | PrefabSpawnerまたはObjectPoolからObjectを生成します。 | PrefabSpawner / ObjectPool / WaveSpawner |
| 13 | `CameraEffectsController` | カメラ | カメラ演出 | Camera BlendとShakeを入力・イベントから再生します。 | Camera / CameraBlend / CameraShake |
| 14 | `AnimationEffectController` | Animation・VFX | Animation・Effect | AnimationとParticle/VFXを同時に起動する開始コードです。 | Animator / Animation / ParticleSystem / VisualEffect |
| 15 | `AudioController` | Audio | 音量・音響制御 | AudioSourceの公開Propertyをゲーム中に変更します。 | AudioSource / AudioReverbZone / Audio Filter |
| 16 | `UiController` | UI | UIイベント | Button等からBindActionを呼ぶUI処理の開始コードです。 | Canvas / Button / Text / Image / UIValueBinding |
| 17 | `ActionEventController` | イベント | Action・Sequence | ActionRelayとActionSequenceをゲーム条件へ接続します。 | ActionRelay / ActionSequence / TimelineEvent / PropertyTween |
| 18 | `SaveCheckpointController` | 保存 | Save・Checkpoint | Save SlotとCheckpointを入力・イベントへ接続します。 | Saveable / Checkpoint |
| 19 | `OceanBuoyancyController` | 海・物理 | 海面問い合わせ | 描画と浮力が共有するOcean表面情報を取得します。 | Ocean / Buoyancy / RigidBody |
| 20 | `NavigationAiController` | Navigation・AI | Target・経路AI | Target取得後のNavigation/Steering条件を書く開始コードです。 | NavigationAgent / AIPathRequest / TargetSelector / AISteeringAgent |
| 21 | `RuntimePropertyController` | Component連携 | Component Property操作 | Component存在確認と公開Property変更を行います。 | 任意Component / PropertyTween / ActionRelay |
| 22 | `ScoreController` | ゲーム進行 | スコア制御 | 型付きAction PayloadをGenericCounterへ加算する開始コードです。 | GenericCounter / ActionRelay / UIValueBinding |
| 23 | `ComboController` | ゲーム進行 | コンボ制御 | 命中ActionでComboを加算しTimer満了Actionでリセットします。 | GenericCounter / Timer / ActionRelay |
| 24 | `StageResultController` | Scene・進行 | ステージ結果 | ScoreからRankを決定しScene間データへ保存します。 | GenericCounter / GameplayData / SceneButton |
| 25 | `RailEventController` | 移動・イベント | レールイベント受信 | Rail進行率MarkerのIDを型付きAction Payloadとして受け取ります。 | RailMovement / RailEventMarker / ActionRelay |
| 26 | `SimulationLodController` | 最適化 | シミュレーションLOD参照 | 距離別のRuntime LOD段階をゲーム固有処理から参照します。 | SimulationLOD / Script |

Templateはどれも**開始コードであって完成品ではない**。生成される `.h` / `.cpp` は、そのカテゴリで最初に必要になるAPI呼び出しと、値を埋める場所のコメントだけを含む。推奨Componentは「これが無いと動かない」ではなく「これと組み合わせる前提で書かれている」という意味である。

### Engine内部: Generated DLL Exportの全契約

`EditorScriptApi.h` の`extern "C"`ブロックが、EditorがDLLから探す関数の型を定義している。これはEngine実装者がGeneratedコードを保守するための内部資料であり、使用者はExportを手書き・追加・修正しない。

**モジュール単位（DLL 1つにつき1回）。**

| 関数型 | 役割 | 返さないとどうなるか |
| --- | --- | --- |
| `EditorScriptLoadFn` = `bool(uint32_t apiVersion, const EditorScriptRuntimeApi*)` | DLL読込直後に1回。ここで `EditorNativeScriptRuntime::SetRuntimeApi()` を呼ぶ。`apiVersion` が自分の想定より小さければfalseを返して安全に読込を拒否する。 | falseを返すとそのDLLは使われない。 |
| `EditorScriptUnloadFn` = `void()` | DLL解放直前に1回。static状態をリセットする。 | 解放されないstatic状態が次のPlayへ持ち越される。 |
| `EditorScriptGetFieldCountFn` = `int32_t()` | Inspectorへ出す公開変数の個数。 | 0扱いになり、Inspectorに公開変数が出ない。 |
| `EditorScriptGetFieldDescriptorFn` = `bool(int32_t, EditorScriptFieldDescriptor*)` | 公開変数1件の名前・表示名・型・既定値・範囲。 | その番号の変数はInspectorに出ない。 |
| `EditorScriptGetActionCountFn` = `int32_t()` | Inspectorの「Action」候補として出す関数の個数。 | 0扱いになり、他ComponentのAction欄に候補が出ない。 |
| `EditorScriptGetActionNameFn` = `bool(int32_t, char*, int32_t)` | Action候補1件の関数名。容量付き文字列出力。 | その番号の候補が出ない。 |

**GameObject単位（旧ABI・`gameObjectId` を毎回渡す方式）。**

`EditorScriptStartFn` / `EditorScriptUpdateFn` / `EditorScriptFixedUpdateFn` / `EditorScriptPhysicsEventFn` / `EditorScriptAnimationEventFn` / `EditorScriptStopFn` / `EditorScriptGetFieldValueFn` / `EditorScriptSetFieldValueFn` / `EditorScriptInvokeActionFn`。

**Instance単位（新ABI・Componentごとに状態をEngineが持つ方式）。**

`EditorScriptCreateInstanceFn` / `EditorScriptDestroyInstanceFn` と、`void*` を第1引数に取る `...InstanceFn` 群。

**どちらのABIで動くかの判定。** Engineは **`Create` と `Destroy` の両方があるDLLだけ**をInstance APIとして扱う。片方しか無いDLLは旧ABIで実行される。**同じDLLに新旧を混在させると、意図せず旧ABIで動く場合がある**ので、新ABIへ移行するときは `CreateInstance` と `DestroyInstance` を必ず対で書く。

Instance APIを使うと、同じScriptを複数GameObjectへ付けたときの状態が完全に分離される。旧ABIで状態をstatic変数へ持つと、複数Objectで共有されてしまう。

### 実行順とThread契約

| 呼び出し | いつ | Thread | 使ってよいAPI |
| --- | --- | --- | --- |
| `Load` | DLL読込直後、Play開始前 | Main | `SetRuntimeApi` のみ。Scene照会はまだしない。 |
| `Start` | そのScript ComponentがPlay中に初めて有効になったフレーム | Main | 全API。ただしScene内の他Objectがまだ `Start` を終えていない場合がある。 |
| `FixedUpdate` | 物理周期ごと | Main | 力・Impulse・Torque・速度設定。`AddForce` 系はここへ書く。 |
| `Update` | 描画フレームごと | Main | 全API。**`SimulationLOD` のMedium / Far段階では呼び出しが間引かれ、`deltaTime` に省略期間の合計が入る。** |
| Action関数 | 通知されたフレーム | Main | 全API。Update間引きの影響を受けず、そのフレームに届く。 |
| Collision / Trigger callback | 物理更新後 | Main | 全API。`EditorScriptPhysicsEvent::type` でEnter / Stay / Exit を区別する。 |
| Animation Event | Clip再生中の該当時刻 | Main | 全API。 |
| `Stop` | Play停止、Scene切替、Component無効化 | Main | 全API。ここで開いたHandle（Effekseer等）とBus音量を必ず戻す。 |
| `Unload` | DLL解放直前 | Main | なし。static状態のリセットのみ。 |

**すべてMain Thread**である。非同期Scene読込（`LoadSceneAsync`）だけがEngine内部で別Threadを使うが、結果の受け取りは `IsSceneLoading()` / `GetLoadProgress()` / `IsSceneLoaded()` のポーリングであり、Scriptが別Threadで呼ばれることはない。

### 失敗診断の順番

APIが期待通り動かないとき、次の順に切り分ける。上から順に確認すると、原因の切り分けが最短になる。

1. **`GetRuntimeApi()` が null ではないか。** nullなら `Load` で `SetRuntimeApi` を呼び忘れているか、DLLがまだ読み込まれていない。
2. **その関数ポインタが null ではないか。** nullならEditor側が古く、そのEntryがまだ無い。API Versionの問題である。
3. **`gameObjectId` が `>= 0` か。** `GameObject::Find` の失敗（名前違い、Inactive、Scene未読込）で `-1` になっていないか。
4. **対象GameObjectに必要なComponentがあるか。** `GameObject::HasComponent("Health")` のように、内部型名の文字列で確認できる。
5. **そのComponentが有効か。** `IsComponentActive` で確認する。GameObject自体がInactiveでも同じくfalseになる。
6. **引数が範囲内か。** 距離0、半径0、空文字列、負のIndexはfalseになる。
7. **戻り値の意味を取り違えていないか。** trueは「呼び出しが成立した」であって「命中した」「入った」「完了した」ではない。
8. **Consoleを見る。** `Log()` で自分のScriptの通過点を出す。Scene読込・遷移演出などEngine側の処理はEditorがConsoleへ出す。


## Component 汎用Propertyアクセス完全リファレンス

### この章の位置づけ

`GameObject::GetComponent()` が返す `Component` Wrapper は、**Componentごとの専用Wrapperを増やさずに、Inspectorの値をScriptから読み書きする**ための汎用入口である。`Timer` や `TargetLock` のような専用Wrapperは「そのComponent固有の操作（開始する、Lockする）」を提供するのに対し、`Component` Wrapper は「Inspectorに出ている値そのもの」を型付きで触る。

この経路が入ったことで、**専用WrapperもRuntime API Entryも無いComponentが、Scriptから設定変更できるようになった**。`SpringForce`、`Aerodynamics`、`FluidVolume`、`TurretAim` のように専用Wrapperを持たないComponentが対象である。

Runtime API Entry は増えていない。既存の `SetRuntimeFloat` / `GetRuntimeFloat` / `SetRuntimeInt` / `GetRuntimeInt` / `SetRuntimeBool` / `GetRuntimeBool` / `SetRuntimeVector2` / `GetRuntimeVector2` / `SetRuntimeVector3` / `GetRuntimeVector3` の10 Entryをそのまま使うため、**API Versionは7のままでABIも変わっていない**。古いDLLをそのまま動かせる。

| 情報 | 抽出元 |
| --- | --- |
| `Component` Wrapper | `Source/Engine/Core/EditorNativeScript.h` |
| Component名の解決とProperty解決 | `Source/Engine/Editor/EditorRuntimePropertyManager.cpp` |
| 公開Field台帳（自動生成） | `Source/Engine/Editor/EditorLogFieldRegistry.generated.cpp` |
| 台帳の生成元 | `Tools/generate_log_field_registry.py` が `EditorInspectorPanel.cpp` から抽出 |

### 対応範囲

| 区分 | 件数 |
| --- | --- |
| 台帳へ載っているComponent種類 | **257** |
| 台帳へ載っているField総数 | **1,474** |
| 台帳に1件も載っていないComponent種類 | 23 |
| `EditorComponentType` 総数 | 280 |

種別ごとの内訳は次のとおり。

| 種別 | 件数 | 使うアクセサ |
| --- | --- | --- |
| `Float` | 791 | `SetFloat` / `GetFloat` |
| `Bool` | 235 | `SetBool` / `GetBool` |
| `GameObjectReference` | 182 | `SetGameObject` / `GetGameObject` |
| `Vector3` | 180 | `SetVector3` / `GetVector3` |
| `Int` | 86 | `SetInt` / `GetInt` |

**`Vector2` は台帳に存在しない。** `SetVector2` / `GetVector2` は後述の「短い別名」経路でしか動かず、`uvTiling` のような未登録Vector2 Fieldへは汎用経路で届かない。`movementModifierInputRange` は短い別名 `"InputRange"` で操作できる。その他のVector2値はInspectorで設定するか、既存の専用APIを使うか、台帳生成器をVector2対応へ拡張する必要がある。

### `Component` Wrapper の全メンバー

```cpp
Component(const GameObject& ownerGameObject, const char* componentTypeName);

bool IsValid() const;
bool SetActive(bool isActive) const;
bool IsActive() const;

bool SetFloat     (const char* propertyName, float value) const;
bool GetFloat     (const char* propertyName, float& value) const;
bool SetInt       (const char* propertyName, int32_t value) const;
bool GetInt       (const char* propertyName, int32_t& value) const;
bool SetBool      (const char* propertyName, bool value) const;
bool GetBool      (const char* propertyName, bool& value) const;
bool SetVector2   (const char* propertyName, const EditorScriptVector2& value) const;
bool GetVector2   (const char* propertyName, EditorScriptVector2& value) const;
bool SetVector3   (const char* propertyName, const EditorScriptVector3& value) const;
bool GetVector3   (const char* propertyName, EditorScriptVector3& value) const;
bool SetGameObject(const char* propertyName, const GameObject& value) const;
bool GetGameObject(const char* propertyName, GameObject& value) const;
```

取得は `GameObject::GetComponent(const char* componentTypeName)` で行う。

**`Component` はComponentへのポインタを持たない。** 内部に持つのは所有GameObjectのIDと型名文字列だけで、呼び出しのたびにSceneから引き直す。したがって**メンバー変数として保持し続けても安全**であり、対象GameObjectが破棄されれば以後の呼び出しがfalseになるだけである。ダングリングポインタにはならない。

#### `IsValid()` と `IsActive()` は意味が違う

| メンバー | true になる条件 |
| --- | --- |
| `IsValid()` | 所有GameObjectにその型名のComponentが**存在する**。**有効チェックが外れていてもtrue**。 |
| `IsActive()` | そのComponentの有効チェックが入っている。所有GameObjectのActive状態は判定しない。 |

`IsValid()` は「Inspectorに枠があるか」、`IsActive()` は「Component自身の有効チェックが入っているか」である。**`IsActive()` がtrueでも所有GameObjectが非Activeなら、多くのRuntime処理は実行されない。** GameObject側は別に `GameObject::IsActive()` で確認する。値を書き換える前の存在確認には `IsValid()` を使う。無効なComponentへ値を書き込むこと自体は成功する（Set系はtrueを返す）が、Runtimeがその値を読まないので効果は出ない。

#### `SetGameObject` / `GetGameObject` の実体

この2つは**専用のRuntime API Entryではなく、`SetInt` / `GetInt` の薄いラッパー**である。

```cpp
bool SetGameObject(const char* propertyName, const GameObject& value) const {
	return SetInt(propertyName, value.GetInstanceId());
}
```

したがって次が成り立つ。

1. `GameObjectReference` 種別のFieldは `SetInt` / `GetInt` でも同じことができる。`SetGameObject` は読みやすさのための別名である。
2. **既定構築した `GameObject{}` を渡すと `-1` が書かれる。** これは「参照を外す」であって「Componentを止める」ではない。多くのComponentは参照が `-1` のとき別のFallback動作（World固定点を使う、所有者自身を使う、など）へ切り替わる。何が起きるかはComponentごとに違うので、`component-documentation-detail-seed.md` の該当Componentの「未設定は◯◯」欄を確認する。
3. `GetGameObject` は取得したIDから `GameObject` を作り直すだけで、**そのIDのGameObjectが実在するかは確認しない**。返ってきた `GameObject` は `HasReference()` が true でも Scene から消えている場合がある。
4. `SetGameObject` / `GetGameObject` 自身はField種別が `GameObjectReference` かを追加検証しない。下層の `SetInt` / `GetInt` は通常の `Int` Fieldも受け付けるため、通常の整数Fieldへ誤って使っても成功する。必ずこの章の台帳で種別を確認する。

### Component名の指定方法

`GetComponent()` へ渡す文字列は、**`EditorComponentType` の内部名**である。Inspectorの日本語表示名ではない。

```cpp
player.GetComponent("SpringForce");   // 正しい
player.GetComponent("ばね力");         // 動かない
```

内部名は `component-documentation-detail-seed.md` の「全280 Component Inspector値表」の見出しと、この章の台帳の見出しに出ている名前と同じである。

#### 内部名と列挙子名が食い違う1件

`kEditorComponentTypeNames` の文字列は基本的に `EditorComponentType` の列挙子名と同じだが、**1件だけ違う**。

| 列挙子名 | `GetComponent()` へ渡す文字列 |
| --- | --- |
| `NavigationAgent` | **`"NavMeshAgent"`** |

これ以外の279件は列挙子名と文字列が一致する。

#### 旧別名について

`EditorRuntimePropertyManager` には、汎用解決が入る前から使われていた27件の別名表（`Ocean`、`Light`、`Camera`、`PostProcess`、`AudioSource`、`ParticleSystem`、`VisualEffect`、`RailMovement`、`Health`、`WeaponLoadout`、`TargetSteering`、`TargetPoint`、`Team`、`TargetSelector`、`MovementModifier`、`Timer`、`GenericStateMachine`、`Attribute`、`DestructiblePart`、`FormationFollower`、`TargetLock`、`MultiTargetLock`、`GenericCounter`、`GenericCondition`、`GameplayData`、`WeaponAccuracy`、`TimeScale`）が残っている。**この27件はすべて内部名と綴りが同じ**なので、実際には内部名解決だけで足りる。既存Scriptを書き換える必要はない。

解決の順番は「① 所有GameObjectのComponentを走査して内部名一致 → ② 旧別名表」である。

### Property名の指定方法 — 2つの名前空間

Property名には**2系統**があり、**短い別名が先に評価される**。

| 系統 | 名前の形 | 例 | 出どころ |
| --- | --- | --- | --- |
| ① 短い別名（先に評価） | 意味ベースの短い名前 | `"Current"`、`"Maximum"`、`"Scale"`、`"Duration"`、`"PositionOffset"` | `EditorRuntimePropertyManager` の手書き分岐 |
| ② 台帳のFieldキー | `EditorComponent` のメンバー名そのもの | `"healthMaximum"`、`"springForceStiffness"`、`"timeScaleValue"` | 自動生成台帳（この章の一覧） |

**①に一致しなかった名前だけが②へ落ちる。** 両方に載っている値は、①の名前で呼んでも②の名前で呼んでも同じFieldへ届く。

②の名前は `EditorInspectorPanel.cpp` から自動抽出されるため、**Inspectorに新しい行を足して台帳を再生成すれば、Script側は何も足さずにその値へ届く**。これがこの仕組みの狙いである。

#### 台帳に載るのは「Inspectorで編集できる値」だけ

台帳は主に `EditorInspectorPanel.cpp` の編集行（`DrawFloatRow` などの入力ウィジェット）から抽出される。加えて、生成器が明示対応している単一Field形式の `DrawReadOnlyFloatRow` / `DrawReadOnlyVector3Row` は監視用途として台帳へ載る。`DrawTextRow`、複合式、配列要素、その他の読み取り専用形式は台帳に載らない。

| 値 | 台帳のキー | ②経路 | ①の短い別名 |
| --- | --- | --- | --- |
| Health 最大体力（編集値） | `healthMaximum` | 使える | なし。②で読み書きする |
| Health 現在体力（Runtime値） | — | 使えない | `"Current"` で読み書き。または `Health` Wrapper |
| GenericCounter 現在値（Runtime値） | — | 使えない | `"Current"` |
| WeaponAccuracy 現在拡散（Runtime値） | — | 使えない | `"CurrentSpread"`。読むだけなら `GetWeaponAccuracySpread` |
| FireLineCheck Blocking距離（Runtime値） | — | 使えない | なし。`GetFireLineState` で読む |

**「Inspectorに数字が出ているのに②経路で届かない」ときは、その行が編集欄ではなく状態表示である可能性が高い。** `component-documentation-detail-seed.md` の「全280 Component Inspector値表」に出ていない行は状態表示であり、台帳にも載らない。

#### ①と②で挙動が違う点（重要）

**①の短い別名はClampや副作用を伴うことがあり、②の台帳経路は生の代入である。**

| Property | ①の名前 | ①の挙動 | ②の名前 | ②の挙動 |
| --- | --- | --- | --- | --- |
| TimeScale 時間倍率 | `"Scale"` | `0.0`〜`4.0` へClamp | `"timeScaleValue"` | **Clampなし。範囲外の値がそのまま入る** |
| TimeScale 継続秒 | `"Duration"` | 最低 `0.001` へ補正 | `"timeScaleDuration"` | 補正なし |
| WeaponAccuracy 拡散 | `"CurrentSpread"` | 負値を `0` へ補正 | — | — |
| RailMovement Offset速度 | `"OffsetMoveSpeed"` | 負値を `0` へ補正 | `"railOffsetMoveSpeed"` | 補正なし |
| GenericCounter 現在値 | `"Current"` | Min/Maxへ Clamp し、変化があれば `counterChangedActionName` のActionをFloat Payload付きで通知する | — | **台帳に無い。②経路では触れない** |

**Runtimeへ通知が要る値、範囲の意味がある値は、短い別名がある限りそちらを使う。** 台帳経路は「専用の入口が用意されていない値へ届くための最後の手段」と考える。どの値に短い別名があるかは、`EditorRuntimePropertyManager.cpp` の `SetFloat` / `GetFloat` / `SetInt` / `SetBool` / `SetVector2` / `SetVector3` の分岐を見るのが確実である。

#### 値が書き換わることと、Runtime資源が再構築されることは別

台帳経路のSet系は `EditorComponent` のFieldを直接書き換える。毎Frameまたは物理固定更新ごとにFieldを読み直す処理（`SpringForce`、`DistanceActivation`、多くのGameplay判定など）は次の更新から反映される。一方、Play開始時にNative資源やキャッシュを組み立てる処理は、Fieldだけ変えてもその資源を自動再構築しない場合がある。

| 変更対象 | 台帳Set後の扱い |
| --- | --- |
| 毎更新でFieldを読むGameplay値 | 通常は次の更新から反映される。 |
| Jolt Jointの接続先・制限 | Component値は変わるが、既に作成済みのConstraint再生成が別途必要になる場合がある。 |
| Collider形状、Body生成条件 | Scene上の値は変わるが、既存Physics BodyのShape再登録が必要になる場合がある。 |
| Texture・Model・Shader等のAsset/描画資源 | そもそも文字列・Asset参照は台帳対象外。専用の読込・再生成経路を使う。 |
| Audio、Effect、Animationの再生状態 | 設定値の変更とPlay/Stop操作は別。専用WrapperまたはRuntime APIを使う。 |

したがって、Set系がtrueを返す意味は「指定Fieldへ書き込めた」であり、「そのSubsystemが再初期化まで完了した」ではない。

### 台帳に1件も載っていない23 Component

次の23種類は自動生成台帳へ1件もFieldが出ないため、**`Component` Wrapper の値アクセスが一切効かない**。`IsValid()` / `IsActive()` / `SetActive()` は動く。

| Component | 値アクセスができない理由 |
| --- | --- |
| `Transform` | 位置・回転・拡縮は `GameObject::GetTransform` / `SetTransform` が担当する。Inspectorも専用描画である。 |
| `AudioSource` | Inspector描画がswitch文内のインラインで、抽出対象の `Draw???Component` 関数になっていない。音量等は短い別名（`AudioSource` + 各名）と `Audio` Wrapperを使う。 |
| `MeshFilter` / `CinemachineCamera` / `WheelCollider` | 同上（インライン描画）。 |
| `Script` / `MonoBehaviour` | 公開変数はScript側の `GetFieldDescriptor` / `SetFieldValue` が担当する。 |
| `PlayerInput` / `StandaloneInputModule` / `InputSystemUIInputModule` / `TouchInputModule` | 入力の設定はInput Actions Assetと `PlayerInput` のInspectorが持ち、数値Fieldを公開していない。 |
| `AudioListener` / `HapticSource` / `FlareLayer` / `AvatarMask` / `VisualEffect` | 数値Fieldを公開していない、または専用経路を持つ。 |
| `GameplayData` / `ImpactResponder` / `SurfaceType` | 設定の実体が可変長配列または文字列で、単一Fieldとして公開できない。`GameplayData` は `GameplayData` Wrapperを使う。 |
| `LegacyRailShooterEnemy` / `LegacyRailShooterShip` / `LegacyRailShooterEnemyMotion` / `LegacyRailShooterStage` | 旧Scene読み込み専用の互換スロットで、Runtime処理そのものが無い。 |

**可変長配列（`[要素]` を持つComponent）のFieldは、どのComponentでも台帳に出ない。** `StatusEffectSet` のEffect定義、`RailZone` の区間、`WeaponLoadout` のSlot、`CooldownSet` の各Cooldownのような要素側の値は、この経路では触れない。要素を名前で扱う専用API（`StartNamedCooldown`、`ApplyStatusEffect`、`SetNamedAttributeValue` など）を使う。

### 失敗条件と切り分け

Set / Get 系がfalseを返す条件を、判定される順に並べる。**falseは理由を区別しない**ので、上から順に潰す。

| # | 条件 | 確認方法 |
| --- | --- | --- |
| 1 | 所有GameObjectが未参照（ID が負） | `gameObject.HasReference()` |
| 2 | 型名が空文字列またはnull | `GetComponent()` の引数を見直す |
| 3 | Property名が空文字列またはnull | 同上 |
| 4 | Runtime APIが無い（Editorが古い） | `EditorNativeScriptRuntime::GetRuntimeApi()` と該当Entryのnull確認 |
| 5 | そのGameObjectに指定型のComponentが無い | `component.IsValid()` |
| 6 | Property名がその型のどちらの名前空間にも無い | この章の台帳でFieldキーを確認 |
| 7 | 呼んだアクセサと種別が食い違う | 台帳の「種別」列を確認。`Float` のFieldへ `SetInt` を呼んでもfalse |

**7番が最も起きやすい。** 台帳の種別は `EditorComponent` のメンバー型そのものなので、Inspectorで整数に見える値でも `Float` のことがある。逆にCombo（選択肢）は `Int` である。

`IsValid()` がtrueで `SetFloat` がfalseなら、原因は6か7に絞られる。

### 使用例 — 衝突相手をばねの接続先にする

`SpringForce` は専用Wrapperを持たないComponentの代表例である。次の例は、衝突した相手をばねの接続先へ設定し、同時にばね定数と反作用フラグを変える。

```cpp
void PlayerScript::OnCollisionEnter(
	const EditorScriptPhysicsEvent& physicsEvent) {

	const GameObject player{physicsEvent.selfGameObjectId};
	const GameObject collidedObject{physicsEvent.otherGameObjectId};

	const Component springForce = player.GetComponent("SpringForce");

	if (!springForce.IsValid()) {
		return;  // このObjectにSpringForceが無い。
	}

	springForce.SetGameObject("springForceTargetGameObjectId", collidedObject);
	springForce.SetFloat("springForceStiffness", 40.0f);
	springForce.SetBool("springForceApplyReaction", true);
}
```

**この代入が実際に力として現れる条件。** `SpringForce` の力は `EditorPhysicsManager::ApplySpringForces()` が物理の固定時間ごとに計算し、そのたびに `springForceTargetGameObjectId` を読み直す。したがって代入は次の物理更新から反映される。ただし、次のいずれかに当てはまるフレームは**その個体の計算自体が飛ばされる**ので、値を入れても何も起きない。

| 条件 | 結果 |
| --- | --- |
| 所有GameObjectが無効 | 計算しない |
| 所有GameObjectに `RigidBody` が無い、または無効 | 計算しない |
| その `RigidBody` がKinematic | 計算しない（Kinematicは力で動かない） |
| `SpringForce` 自体の有効チェックが外れている | 計算しない |
| 接続先が見つからない、または無効 | 計算しない |
| **接続先が所有者自身と同じID** | 計算しない |
| 接続先IDが `-1` | 接続先ではなく `springForceWorldAnchor`（World固定点）へ引く |

最後の2行に注意する。自分自身との衝突は通常起きないが、`SetGameObject` へ既定構築の `GameObject{}` を渡すと `-1` が入り、**ばねが止まるのではなくWorld固定点へ引く挙動へ切り替わる**。ばねを止めたいときは参照を外すのではなく `springForce.SetActive(false)` を使う。

戻り値を確認する形にすると次のようになる。

```cpp
void PlayerScript::OnCollisionEnter(
	const EditorScriptPhysicsEvent& physicsEvent) {

	const GameObject player{physicsEvent.selfGameObjectId};
	const GameObject collidedObject{physicsEvent.otherGameObjectId};
	const Component springForce = player.GetComponent("SpringForce");

	if (!springForce.IsValid()) {
		return;
	}

	if (!springForce.SetGameObject("springForceTargetGameObjectId", collidedObject) ||
		!springForce.SetFloat("springForceStiffness", 40.0f) ||
		!springForce.SetBool("springForceApplyReaction", true)) {
		const EditorScriptRuntimeApi* runtimeApi = EditorNativeScriptRuntime::GetRuntimeApi();

		if (runtimeApi != nullptr && runtimeApi->Log != nullptr) {
			runtimeApi->Log("SpringForce: Property名または種別が一致していません。");
		}

		return;
	}

	// 無効化されていると値は入るが力は出ないため、ここで有効へ戻す。
	if (!springForce.IsActive()) {
		springForce.SetActive(true);
	}
}
```

### 使用例 — 値を読んでから変える

```cpp
void TurretScript::Update(float deltaTime) {
	(void)deltaTime;

	const Component turretAim = GetGameObject().GetComponent("TurretAim");

	if (!turretAim.IsValid()) {
		return;
	}

	float yawSpeed = 0.0f;

	if (!turretAim.GetFloat("turretYawSpeedDegrees", yawSpeed)) {
		return;  // Property名か種別が違う。
	}

	// 目標が近いほど旋回を速くする、といったゲーム側の調整。
	turretAim.SetFloat("turretYawSpeedDegrees", yawSpeed * 1.5f);
}
```

**読んだ値は「今のRuntime値」であって「Sceneに保存されている値」ではない。** Play中にScriptが書き換えた値はSceneへ保存されず、Play停止時にScene読み込み時の値へ戻る。恒久的に変えたいならInspectorで編集する。

### 全257 Component 公開Field台帳（1,474件）

`EditorComponentType` の宣言順に並べる。見出しは `GetComponent()` へ渡す文字列である。「Property名」列の値をそのまま `SetFloat` などの第1引数へ渡す。

#### ModelRenderer

Inspector表示名「メッシュレンダラー」／カテゴリ「描画・レンダリング」／公開Field 7件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 強さ | `intensity` | Float | `SetFloat` / `GetFloat` |
| 透明度 | `alpha` | Float | `SetFloat` / `GetFloat` |
| 粗さ | `roughness` | Float | `SetFloat` / `GetFloat` |
| 金属度 | `metallic` | Float | `SetFloat` / `GetFloat` |
| 反射 | `reflectionStrength` | Float | `SetFloat` / `GetFloat` |
| 発光 | `emissionStrength` | Float | `SetFloat` / `GetFloat` |
| 両面 | `doubleSided` | Bool | `SetBool` / `GetBool` |

#### SpriteRenderer

Inspector表示名「スプライトレンダラー」／カテゴリ「描画・レンダリング」／公開Field 7件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 強さ | `intensity` | Float | `SetFloat` / `GetFloat` |
| 透明度 | `alpha` | Float | `SetFloat` / `GetFloat` |
| 粗さ | `roughness` | Float | `SetFloat` / `GetFloat` |
| 金属度 | `metallic` | Float | `SetFloat` / `GetFloat` |
| 反射 | `reflectionStrength` | Float | `SetFloat` / `GetFloat` |
| 発光 | `emissionStrength` | Float | `SetFloat` / `GetFloat` |
| 両面 | `doubleSided` | Bool | `SetBool` / `GetBool` |

#### Light

Inspector表示名「ライト」／カテゴリ「ライト・環境」／公開Field 8件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 強さ | `intensity` | Float | `SetFloat` / `GetFloat` |
| 半径 | `colliderRadius` | Float | `SetFloat` / `GetFloat` |
| 太陽方位角 | `sunAzimuthDegrees` | Float | `SetFloat` / `GetFloat` |
| 太陽高度 | `sunElevationDegrees` | Float | `SetFloat` / `GetFloat` |
| 太陽色温度(K) | `sunTemperatureKelvin` | Float | `SetFloat` / `GetFloat` |
| 方位角/高度を使用 | `sunUseAzimuthElevation` | Bool | `SetBool` / `GetBool` |
| 色温度を使用 | `sunUseColorTemperature` | Bool | `SetBool` / `GetBool` |
| 色温度を高度から自動推定 | `sunAutoTemperatureFromElevation` | Bool | `SetBool` / `GetBool` |

#### Camera

Inspector表示名「カメラ」／カテゴリ「カメラ」／公開Field 30件（2026-09-02、Cameraの現行台帳と照合）。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 視野角 | `cameraFieldOfView` | Float | `SetFloat` / `GetFloat` |
| ニアクリップ | `cameraNearClip` | Float | `SetFloat` / `GetFloat` |
| ファークリップ | `cameraFarClip` | Float | `SetFloat` / `GetFloat` |
| 露出補正 (EV) | `cameraExposure` | Float | `SetFloat` / `GetFloat` |
| フォーカス距離 | `cameraDofFocusDistance` | Float | `SetFloat` / `GetFloat` |
| 絞り | `cameraDofAperture` | Float | `SetFloat` / `GetFloat` |
| 焦点距離 (mm) | `cameraDofFocalLength` | Float | `SetFloat` / `GetFloat` |
| ブラー強度 | `cameraMotionBlurIntensity` | Float | `SetFloat` / `GetFloat` |
| 優先度 | `cameraPriority` | Int | `SetInt` / `GetInt` |
| 被写界深度 | `cameraDofEnabled` | Bool | `SetBool` / `GetBool` |
| モーションブラー | `cameraMotionBlurEnabled` | Bool | `SetBool` / `GetBool` |
| 投影 | `cameraProjectionMode` | Int | `SetInt` / `GetInt`（0=Perspective / 1=Orthographic） |
| 視点操作を有効化 | `cameraInputEnabled` | Bool | `SetBool` / `GetBool` |
| 操作形式 | `cameraInputStyle` | Int | `SetInt` / `GetInt`（0=FreeLook / 1=Orbit） |
| 回転入力 | `cameraInputActivation` | Int | `SetInt` / `GetInt`（0=右ボタン保持 / 1=常時） |
| マウス感度 | `cameraInputLookSensitivity` | Float | `SetFloat` / `GetFloat` |
| Y軸反転 | `cameraInputInvertY` | Bool | `SetBool` / `GetBool` |
| Pitch最小角度 | `cameraInputMinimumPitchDegrees` | Float | `SetFloat` / `GetFloat` |
| Pitch最大角度 | `cameraInputMaximumPitchDegrees` | Float | `SetFloat` / `GetFloat` |
| 操作中カーソル固定 | `cameraInputLockCursor` | Bool | `SetBool` / `GetBool` |
| 操作中カーソル非表示 | `cameraInputHideCursor` | Bool | `SetBool` / `GetBool` |
| WASD/QE移動 | `cameraInputMovementEnabled` | Bool | `SetBool` / `GetBool` |
| 移動速度 | `cameraInputMoveSpeed` | Float | `SetFloat` / `GetFloat` |
| Shift倍率 | `cameraInputFastMultiplier` | Float | `SetFloat` / `GetFloat` |
| Orbit中心 | `cameraInputTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| 中心Offset | `cameraInputPivotOffset` | Vector3 | `SetVector3` / `GetVector3` |
| 距離（操作初期化時） | `cameraInputOrbitDistance` | Float | `SetFloat` / `GetFloat` |
| 最小距離 | `cameraInputMinimumDistance` | Float | `SetFloat` / `GetFloat` |
| 最大距離 | `cameraInputMaximumDistance` | Float | `SetFloat` / `GetFloat` |
| ホイールZoom速度 | `cameraInputZoomSpeed` | Float | `SetFloat` / `GetFloat` |

#### RigidBody

Inspector表示名「リジッドボディ」／カテゴリ「3D物理」／公開Field 12件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 実質密度 kg/m3 | `bodyDensity` | Float | `SetFloat` / `GetFloat` |
| 質量 | `mass` | Float | `SetFloat` / `GetFloat` |
| 線形減衰 | `drag` | Float | `SetFloat` / `GetFloat` |
| 角度減衰 | `angularDrag` | Float | `SetFloat` / `GetFloat` |
| 慣性倍率 | `inertiaMultiplier` | Float | `SetFloat` / `GetFloat` |
| Colliderから質量を計算 | `automaticMassFromCollider` | Bool | `SetBool` / `GetBool` |
| ジャイロ効果 | `applyGyroscopicForce` | Bool | `SetBool` / `GetBool` |
| 重力を使用 | `useGravity` | Bool | `SetBool` / `GetBool` |
| キネマティックにする | `isKinematic` | Bool | `SetBool` / `GetBool` |
| 重心オフセット | `centerOfMassOffset` | Vector3 | `SetVector3` / `GetVector3` |
| 速度 | `velocity` | Vector3 | `SetVector3` / `GetVector3` |
| 角速度 | `angularVelocity` | Vector3 | `SetVector3` / `GetVector3` |

#### BoxCollider

Inspector表示名「箱の当たり判定」／カテゴリ「3D物理」／公開Field 2件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 中心 | `colliderCenter` | Vector3 | `SetVector3` / `GetVector3` |
| サイズ | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |

#### SphereCollider

Inspector表示名「球の当たり判定」／カテゴリ「3D物理」／公開Field 2件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 半径 | `colliderRadius` | Float | `SetFloat` / `GetFloat` |
| 中心 | `colliderCenter` | Vector3 | `SetVector3` / `GetVector3` |

#### Input

Inspector表示名「入力」／カテゴリ「入力・イベント」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 移動速度 | `inputMoveSpeed` | Float | `SetFloat` / `GetFloat` |

#### Animation

Inspector表示名「アニメーション」／カテゴリ「アニメーション」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 速度 | `animationSpeed` | Float | `SetFloat` / `GetFloat` |
| 振幅 | `animationAmplitude` | Float | `SetFloat` / `GetFloat` |
| ループ | `animationLoop` | Bool | `SetBool` / `GetBool` |
| 自動再生 | `animationPlayOnAwake` | Bool | `SetBool` / `GetBool` |

#### Animator

Inspector表示名「アニメーター」／カテゴリ「アニメーション」／公開Field 12件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 再生速度 | `animationSpeed` | Float | `SetFloat` / `GetFloat` |
| 既定遷移秒 | `animatorTransitionDuration` | Float | `SetFloat` / `GetFloat` |
| MoveX 左右 | `animatorMoveX` | Float | `SetFloat` / `GetFloat` |
| MoveY 前後 | `animatorMoveY` | Float | `SetFloat` / `GetFloat` |
| Speed | `animatorSpeedParameter` | Float | `SetFloat` / `GetFloat` |
| 停止 Clip | `animatorIdleClipIndex` | Int | `SetInt` / `GetInt` |
| 前 Clip | `animatorForwardClipIndex` | Int | `SetInt` / `GetInt` |
| 後 Clip | `animatorBackwardClipIndex` | Int | `SetInt` / `GetInt` |
| 左 Clip | `animatorLeftClipIndex` | Int | `SetInt` / `GetInt` |
| 右 Clip | `animatorRightClipIndex` | Int | `SetInt` / `GetInt` |
| Root Motion を適用 | `animatorApplyRootMotion` | Bool | `SetBool` / `GetBool` |
| 移動速度を自動取得 | `animatorAutoVelocity` | Bool | `SetBool` / `GetBool` |

#### ParentConstraint

Inspector表示名「親制約」／カテゴリ「アニメーション」／公開Field 3件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 重み | `constraintWeight` | Float | `SetFloat` / `GetFloat` |
| ターゲット ID | `connectedGameObjectId` | Int | `SetInt` / `GetInt` |
| 位置オフセット | `constraintPositionOffset` | Vector3 | `SetVector3` / `GetVector3` |

#### PositionConstraint

Inspector表示名「位置制約」／カテゴリ「アニメーション」／公開Field 3件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 重み | `constraintWeight` | Float | `SetFloat` / `GetFloat` |
| ターゲット ID | `connectedGameObjectId` | Int | `SetInt` / `GetInt` |
| オフセット | `constraintPositionOffset` | Vector3 | `SetVector3` / `GetVector3` |

#### RotationConstraint

Inspector表示名「回転制約」／カテゴリ「アニメーション」／公開Field 2件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 重み | `constraintWeight` | Float | `SetFloat` / `GetFloat` |
| ターゲット ID | `connectedGameObjectId` | Int | `SetInt` / `GetInt` |

#### ScaleConstraint

Inspector表示名「スケール制約」／カテゴリ「アニメーション」／公開Field 5件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 重み | `constraintWeight` | Float | `SetFloat` / `GetFloat` |
| ターゲット ID | `connectedGameObjectId` | Int | `SetInt` / `GetInt` |
| X 軸フリーズ | `constraintFreezeAxisX` | Bool | `SetBool` / `GetBool` |
| Y 軸フリーズ | `constraintFreezeAxisY` | Bool | `SetBool` / `GetBool` |
| Z 軸フリーズ | `constraintFreezeAxisZ` | Bool | `SetBool` / `GetBool` |

#### EventSystem

Inspector表示名「イベントシステム」／カテゴリ「入力・イベント」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 優先度 | `physicsLayer` | Int | `SetInt` / `GetInt` |

#### CapsuleCollider

Inspector表示名「カプセル当たり判定」／カテゴリ「3D物理」／公開Field 2件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 半径 | `colliderRadius` | Float | `SetFloat` / `GetFloat` |
| 中心 | `colliderCenter` | Vector3 | `SetVector3` / `GetVector3` |

#### MeshCollider

Inspector表示名「メッシュ当たり判定」／カテゴリ「3D物理」／公開Field 2件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 中心 | `colliderCenter` | Vector3 | `SetVector3` / `GetVector3` |
| サイズ | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |

#### CharacterController

Inspector表示名「キャラクターコントローラー」／カテゴリ「3D物理」／公開Field 2件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 半径 | `colliderRadius` | Float | `SetFloat` / `GetFloat` |
| 中心 | `colliderCenter` | Vector3 | `SetVector3` / `GetVector3` |

#### NavMeshAgent

Inspector表示名「NavMesh エージェント」／カテゴリ「ナビゲーション」／公開Field 1件／`EditorComponentType` の列挙子名は `NavigationAgent`。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 目的地 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### PlayableDirector

Inspector表示名「プレイアブルディレクター」／カテゴリ「アニメーション」／公開Field 3件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 速度 | `animationSpeed` | Float | `SetFloat` / `GetFloat` |
| 自動再生 | `animationPlayOnAwake` | Bool | `SetBool` / `GetBool` |
| ループ | `animationLoop` | Bool | `SetBool` / `GetBool` |

#### Canvas

Inspector表示名「キャンバス」／カテゴリ「基本」／公開Field 6件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 幅高さの比重 | `sliderValue` | Float | `SetFloat` / `GetFloat` |
| 透明度 | `intensity` | Float | `SetFloat` / `GetFloat` |
| 描画順 | `physicsLayer` | Int | `SetInt` / `GetInt` |
| 入力を受け取る | `buttonInteractable` | Bool | `SetBool` / `GetBool` |
| 横を内容へ合わせる | `freezePositionX` | Bool | `SetBool` / `GetBool` |
| 縦を内容へ合わせる | `freezePositionY` | Bool | `SetBool` / `GetBool` |

#### Image

Inspector表示名「イメージ」／カテゴリ「UI」／公開Field 6件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 幅高さの比重 | `sliderValue` | Float | `SetFloat` / `GetFloat` |
| 透明度 | `intensity` | Float | `SetFloat` / `GetFloat` |
| 描画順 | `physicsLayer` | Int | `SetInt` / `GetInt` |
| 入力を受け取る | `buttonInteractable` | Bool | `SetBool` / `GetBool` |
| 横を内容へ合わせる | `freezePositionX` | Bool | `SetBool` / `GetBool` |
| 縦を内容へ合わせる | `freezePositionY` | Bool | `SetBool` / `GetBool` |

#### Text

Inspector表示名「テキスト」／カテゴリ「UI」／公開Field 6件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 幅高さの比重 | `sliderValue` | Float | `SetFloat` / `GetFloat` |
| 透明度 | `intensity` | Float | `SetFloat` / `GetFloat` |
| 描画順 | `physicsLayer` | Int | `SetInt` / `GetInt` |
| 入力を受け取る | `buttonInteractable` | Bool | `SetBool` / `GetBool` |
| 横を内容へ合わせる | `freezePositionX` | Bool | `SetBool` / `GetBool` |
| 縦を内容へ合わせる | `freezePositionY` | Bool | `SetBool` / `GetBool` |

#### RectTransform

Inspector表示名「レクトトランスフォーム」／カテゴリ「基本」／公開Field 6件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 幅高さの比重 | `sliderValue` | Float | `SetFloat` / `GetFloat` |
| 透明度 | `intensity` | Float | `SetFloat` / `GetFloat` |
| 描画順 | `physicsLayer` | Int | `SetInt` / `GetInt` |
| 入力を受け取る | `buttonInteractable` | Bool | `SetBool` / `GetBool` |
| 横を内容へ合わせる | `freezePositionX` | Bool | `SetBool` / `GetBool` |
| 縦を内容へ合わせる | `freezePositionY` | Bool | `SetBool` / `GetBool` |

#### SkinnedMeshRenderer

Inspector表示名「スキンメッシュレンダラー」／カテゴリ「描画・レンダリング」／公開Field 7件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 強さ | `intensity` | Float | `SetFloat` / `GetFloat` |
| 透明度 | `alpha` | Float | `SetFloat` / `GetFloat` |
| 粗さ | `roughness` | Float | `SetFloat` / `GetFloat` |
| 金属度 | `metallic` | Float | `SetFloat` / `GetFloat` |
| 反射 | `reflectionStrength` | Float | `SetFloat` / `GetFloat` |
| 発光 | `emissionStrength` | Float | `SetFloat` / `GetFloat` |
| 両面 | `doubleSided` | Bool | `SetBool` / `GetBool` |

#### LineRenderer

Inspector表示名「ラインレンダラー」／カテゴリ「描画・レンダリング」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 表示サイズ | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |

#### TrailRenderer

Inspector表示名「トレイルレンダラー」／カテゴリ「描画・レンダリング」／公開Field 5件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 太さ | `particleSize` | Float | `SetFloat` / `GetFloat` |
| 終端の太さ | `particleEndSize` | Float | `SetFloat` / `GetFloat` |
| 残る秒数 | `particleLifetime` | Float | `SetFloat` / `GetFloat` |
| 毎秒の分割数 | `particleRate` | Float | `SetFloat` / `GetFloat` |
| 終端透明度 | `particleEndAlpha` | Float | `SetFloat` / `GetFloat` |

#### BillboardRenderer

Inspector表示名「ビルボードレンダラー」／カテゴリ「描画・レンダリング」／公開Field 7件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 強さ | `intensity` | Float | `SetFloat` / `GetFloat` |
| 透明度 | `alpha` | Float | `SetFloat` / `GetFloat` |
| 粗さ | `roughness` | Float | `SetFloat` / `GetFloat` |
| 金属度 | `metallic` | Float | `SetFloat` / `GetFloat` |
| 反射 | `reflectionStrength` | Float | `SetFloat` / `GetFloat` |
| 発光 | `emissionStrength` | Float | `SetFloat` / `GetFloat` |
| 両面 | `doubleSided` | Bool | `SetBool` / `GetBool` |

#### CanvasRenderer

Inspector表示名「キャンバスレンダラー」／カテゴリ「描画・レンダリング」／公開Field 6件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 幅高さの比重 | `sliderValue` | Float | `SetFloat` / `GetFloat` |
| 透明度 | `intensity` | Float | `SetFloat` / `GetFloat` |
| 描画順 | `physicsLayer` | Int | `SetInt` / `GetInt` |
| 入力を受け取る | `buttonInteractable` | Bool | `SetBool` / `GetBool` |
| 横を内容へ合わせる | `freezePositionX` | Bool | `SetBool` / `GetBool` |
| 縦を内容へ合わせる | `freezePositionY` | Bool | `SetBool` / `GetBool` |

#### ParticleSystemRenderer

Inspector表示名「パーティクルシステムレンダラー」／カテゴリ「描画・レンダリング」／公開Field 7件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 強さ | `intensity` | Float | `SetFloat` / `GetFloat` |
| 透明度 | `alpha` | Float | `SetFloat` / `GetFloat` |
| 粗さ | `roughness` | Float | `SetFloat` / `GetFloat` |
| 金属度 | `metallic` | Float | `SetFloat` / `GetFloat` |
| 反射 | `reflectionStrength` | Float | `SetFloat` / `GetFloat` |
| 発光 | `emissionStrength` | Float | `SetFloat` / `GetFloat` |
| 両面 | `doubleSided` | Bool | `SetBool` / `GetBool` |

#### ReflectionProbe

Inspector表示名「リフレクションプローブ」／カテゴリ「ライト・環境」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 反射像の強さ | `intensity` | Float | `SetFloat` / `GetFloat` |
| 反射の粗さ | `roughness` | Float | `SetFloat` / `GetFloat` |
| 中心 | `colliderCenter` | Vector3 | `SetVector3` / `GetVector3` |
| サイズ | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |

#### LightProbeGroup

Inspector表示名「ライトプローブグループ」／カテゴリ「ライト・環境」／公開Field 2件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 反射寄与 | `intensity` | Float | `SetFloat` / `GetFloat` |
| ぼかし | `roughness` | Float | `SetFloat` / `GetFloat` |

#### LightProbeProxyVolume

Inspector表示名「ライトプローブプロキシボリューム」／カテゴリ「ライト・環境」／公開Field 3件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 反射寄与 | `intensity` | Float | `SetFloat` / `GetFloat` |
| ぼかし | `roughness` | Float | `SetFloat` / `GetFloat` |
| サイズ | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |

#### Volume

Inspector表示名「ボリューム」／カテゴリ「ライト・環境」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 重み | `intensity` | Float | `SetFloat` / `GetFloat` |

#### TerrainCollider

Inspector表示名「地形の当たり判定」／カテゴリ「3D物理」／公開Field 2件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 中心 | `colliderCenter` | Vector3 | `SetVector3` / `GetVector3` |
| サイズ | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |

#### ConstantForce

Inspector表示名「コンスタントフォース」／カテゴリ「3D物理」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 力 | `velocity` | Vector3 | `SetVector3` / `GetVector3` |

#### HingeJoint

Inspector表示名「ヒンジジョイント」／カテゴリ「3D物理」／公開Field 7件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 最小距離 | `jointMinDistance` | Float | `SetFloat` / `GetFloat` |
| 最大距離 | `jointMaxDistance` | Float | `SetFloat` / `GetFloat` |
| ばね周波数 | `jointSpringFrequency` | Float | `SetFloat` / `GetFloat` |
| ばね減衰 | `jointSpringDamping` | Float | `SetFloat` / `GetFloat` |
| アンカー | `colliderCenter` | Vector3 | `SetVector3` / `GetVector3` |
| 回転軸 | `jointAxis` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### FixedJoint

Inspector表示名「固定ジョイント」／カテゴリ「3D物理」／公開Field 7件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 最小距離 | `jointMinDistance` | Float | `SetFloat` / `GetFloat` |
| 最大距離 | `jointMaxDistance` | Float | `SetFloat` / `GetFloat` |
| ばね周波数 | `jointSpringFrequency` | Float | `SetFloat` / `GetFloat` |
| ばね減衰 | `jointSpringDamping` | Float | `SetFloat` / `GetFloat` |
| アンカー | `colliderCenter` | Vector3 | `SetVector3` / `GetVector3` |
| 回転軸 | `jointAxis` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### SpringJoint

Inspector表示名「スプリングジョイント」／カテゴリ「3D物理」／公開Field 7件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 最小距離 | `jointMinDistance` | Float | `SetFloat` / `GetFloat` |
| 最大距離 | `jointMaxDistance` | Float | `SetFloat` / `GetFloat` |
| ばね周波数 | `jointSpringFrequency` | Float | `SetFloat` / `GetFloat` |
| ばね減衰 | `jointSpringDamping` | Float | `SetFloat` / `GetFloat` |
| アンカー | `colliderCenter` | Vector3 | `SetVector3` / `GetVector3` |
| 回転軸 | `jointAxis` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### ConfigurableJoint

Inspector表示名「コンフィギュラブルジョイント」／カテゴリ「3D物理」／公開Field 7件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 最小距離 | `jointMinDistance` | Float | `SetFloat` / `GetFloat` |
| 最大距離 | `jointMaxDistance` | Float | `SetFloat` / `GetFloat` |
| ばね周波数 | `jointSpringFrequency` | Float | `SetFloat` / `GetFloat` |
| ばね減衰 | `jointSpringDamping` | Float | `SetFloat` / `GetFloat` |
| アンカー | `colliderCenter` | Vector3 | `SetVector3` / `GetVector3` |
| 回転軸 | `jointAxis` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### CharacterJoint

Inspector表示名「キャラクタージョイント」／カテゴリ「3D物理」／公開Field 7件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 最小距離 | `jointMinDistance` | Float | `SetFloat` / `GetFloat` |
| 最大距離 | `jointMaxDistance` | Float | `SetFloat` / `GetFloat` |
| ばね周波数 | `jointSpringFrequency` | Float | `SetFloat` / `GetFloat` |
| ばね減衰 | `jointSpringDamping` | Float | `SetFloat` / `GetFloat` |
| アンカー | `colliderCenter` | Vector3 | `SetVector3` / `GetVector3` |
| 回転軸 | `jointAxis` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### RigidBody2D

Inspector表示名「リジッドボディ 2D」／カテゴリ「2D物理」／公開Field 12件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 実質密度 kg/m3 | `bodyDensity` | Float | `SetFloat` / `GetFloat` |
| 質量 | `mass` | Float | `SetFloat` / `GetFloat` |
| 線形減衰 | `drag` | Float | `SetFloat` / `GetFloat` |
| 角度減衰 | `angularDrag` | Float | `SetFloat` / `GetFloat` |
| 慣性倍率 | `inertiaMultiplier` | Float | `SetFloat` / `GetFloat` |
| Colliderから質量を計算 | `automaticMassFromCollider` | Bool | `SetBool` / `GetBool` |
| ジャイロ効果 | `applyGyroscopicForce` | Bool | `SetBool` / `GetBool` |
| 重力を使用 | `useGravity` | Bool | `SetBool` / `GetBool` |
| キネマティックにする | `isKinematic` | Bool | `SetBool` / `GetBool` |
| 重心オフセット | `centerOfMassOffset` | Vector3 | `SetVector3` / `GetVector3` |
| 速度 | `velocity` | Vector3 | `SetVector3` / `GetVector3` |
| 角速度 | `angularVelocity` | Vector3 | `SetVector3` / `GetVector3` |

#### BoxCollider2D

Inspector表示名「四角の当たり判定 2D」／カテゴリ「2D物理」／公開Field 2件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 中心 | `colliderCenter` | Vector3 | `SetVector3` / `GetVector3` |
| サイズ | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |

#### CircleCollider2D

Inspector表示名「円の当たり判定 2D」／カテゴリ「2D物理」／公開Field 2件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 半径 | `colliderRadius` | Float | `SetFloat` / `GetFloat` |
| 中心 | `colliderCenter` | Vector3 | `SetVector3` / `GetVector3` |

#### CapsuleCollider2D

Inspector表示名「カプセル当たり判定 2D」／カテゴリ「2D物理」／公開Field 2件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 半径 | `colliderRadius` | Float | `SetFloat` / `GetFloat` |
| 中心 | `colliderCenter` | Vector3 | `SetVector3` / `GetVector3` |

#### PolygonCollider2D

Inspector表示名「多角形の当たり判定 2D」／カテゴリ「2D物理」／公開Field 2件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 中心 | `colliderCenter` | Vector3 | `SetVector3` / `GetVector3` |
| サイズ | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |

#### EdgeCollider2D

Inspector表示名「線の当たり判定 2D」／カテゴリ「2D物理」／公開Field 2件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 中心 | `colliderCenter` | Vector3 | `SetVector3` / `GetVector3` |
| サイズ | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |

#### CompositeCollider2D

Inspector表示名「複合当たり判定 2D」／カテゴリ「2D物理」／公開Field 2件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 中心 | `colliderCenter` | Vector3 | `SetVector3` / `GetVector3` |
| サイズ | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |

#### TilemapCollider2D

Inspector表示名「タイルマップ当たり判定 2D」／カテゴリ「2D物理」／公開Field 2件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 中心 | `colliderCenter` | Vector3 | `SetVector3` / `GetVector3` |
| サイズ | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |

#### CustomCollider2D

Inspector表示名「カスタム当たり判定 2D」／カテゴリ「2D物理」／公開Field 2件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 中心 | `colliderCenter` | Vector3 | `SetVector3` / `GetVector3` |
| サイズ | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |

#### DistanceJoint2D

Inspector表示名「ディスタンスジョイント 2D」／カテゴリ「2D物理」／公開Field 7件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 最小距離 | `jointMinDistance` | Float | `SetFloat` / `GetFloat` |
| 最大距離 | `jointMaxDistance` | Float | `SetFloat` / `GetFloat` |
| ばね周波数 | `jointSpringFrequency` | Float | `SetFloat` / `GetFloat` |
| ばね減衰 | `jointSpringDamping` | Float | `SetFloat` / `GetFloat` |
| アンカー | `colliderCenter` | Vector3 | `SetVector3` / `GetVector3` |
| 回転軸 | `jointAxis` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### HingeJoint2D

Inspector表示名「ヒンジジョイント 2D」／カテゴリ「2D物理」／公開Field 7件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 最小距離 | `jointMinDistance` | Float | `SetFloat` / `GetFloat` |
| 最大距離 | `jointMaxDistance` | Float | `SetFloat` / `GetFloat` |
| ばね周波数 | `jointSpringFrequency` | Float | `SetFloat` / `GetFloat` |
| ばね減衰 | `jointSpringDamping` | Float | `SetFloat` / `GetFloat` |
| アンカー | `colliderCenter` | Vector3 | `SetVector3` / `GetVector3` |
| 回転軸 | `jointAxis` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### SpringJoint2D

Inspector表示名「スプリングジョイント 2D」／カテゴリ「2D物理」／公開Field 7件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 最小距離 | `jointMinDistance` | Float | `SetFloat` / `GetFloat` |
| 最大距離 | `jointMaxDistance` | Float | `SetFloat` / `GetFloat` |
| ばね周波数 | `jointSpringFrequency` | Float | `SetFloat` / `GetFloat` |
| ばね減衰 | `jointSpringDamping` | Float | `SetFloat` / `GetFloat` |
| アンカー | `colliderCenter` | Vector3 | `SetVector3` / `GetVector3` |
| 回転軸 | `jointAxis` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### FixedJoint2D

Inspector表示名「固定ジョイント 2D」／カテゴリ「2D物理」／公開Field 7件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 最小距離 | `jointMinDistance` | Float | `SetFloat` / `GetFloat` |
| 最大距離 | `jointMaxDistance` | Float | `SetFloat` / `GetFloat` |
| ばね周波数 | `jointSpringFrequency` | Float | `SetFloat` / `GetFloat` |
| ばね減衰 | `jointSpringDamping` | Float | `SetFloat` / `GetFloat` |
| アンカー | `colliderCenter` | Vector3 | `SetVector3` / `GetVector3` |
| 回転軸 | `jointAxis` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### SliderJoint2D

Inspector表示名「スライダージョイント 2D」／カテゴリ「2D物理」／公開Field 7件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 最小距離 | `jointMinDistance` | Float | `SetFloat` / `GetFloat` |
| 最大距離 | `jointMaxDistance` | Float | `SetFloat` / `GetFloat` |
| ばね周波数 | `jointSpringFrequency` | Float | `SetFloat` / `GetFloat` |
| ばね減衰 | `jointSpringDamping` | Float | `SetFloat` / `GetFloat` |
| アンカー | `colliderCenter` | Vector3 | `SetVector3` / `GetVector3` |
| 回転軸 | `jointAxis` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### WheelJoint2D

Inspector表示名「ホイールジョイント 2D」／カテゴリ「2D物理」／公開Field 7件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 最小距離 | `jointMinDistance` | Float | `SetFloat` / `GetFloat` |
| 最大距離 | `jointMaxDistance` | Float | `SetFloat` / `GetFloat` |
| ばね周波数 | `jointSpringFrequency` | Float | `SetFloat` / `GetFloat` |
| ばね減衰 | `jointSpringDamping` | Float | `SetFloat` / `GetFloat` |
| アンカー | `colliderCenter` | Vector3 | `SetVector3` / `GetVector3` |
| 回転軸 | `jointAxis` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### PlatformEffector2D

Inspector表示名「プラットフォームエフェクター 2D」／カテゴリ「2D物理」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 片面 | `isTrigger` | Bool | `SetBool` / `GetBool` |

#### SurfaceEffector2D

Inspector表示名「サーフェスエフェクター 2D」／カテゴリ「2D物理」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 力 | `intensity` | Float | `SetFloat` / `GetFloat` |

#### AreaEffector2D

Inspector表示名「エリアエフェクター 2D」／カテゴリ「2D物理」／公開Field 2件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 力 | `intensity` | Float | `SetFloat` / `GetFloat` |
| 方向 | `velocity` | Vector3 | `SetVector3` / `GetVector3` |

#### PointEffector2D

Inspector表示名「ポイントエフェクター 2D」／カテゴリ「2D物理」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 力 | `intensity` | Float | `SetFloat` / `GetFloat` |

#### BuoyancyEffector2D

Inspector表示名「浮力エフェクター 2D」／カテゴリ「2D物理」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 浮力 | `intensity` | Float | `SetFloat` / `GetFloat` |

#### AimConstraint

Inspector表示名「エイム制約」／カテゴリ「アニメーション」／公開Field 3件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 重み | `constraintWeight` | Float | `SetFloat` / `GetFloat` |
| ターゲット ID | `connectedGameObjectId` | Int | `SetInt` / `GetInt` |
| ターゲット方向軸 | `constraintAimAxis` | Int | `SetInt` / `GetInt` |

#### LookAtConstraint

Inspector表示名「ルックアット制約」／カテゴリ「アニメーション」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 重み | `constraintWeight` | Float | `SetFloat` / `GetFloat` |
| ロール角 | `constraintRoll` | Float | `SetFloat` / `GetFloat` |
| ターゲット ID | `connectedGameObjectId` | Int | `SetInt` / `GetInt` |
| 上方向軸 | `constraintUpAxis` | Int | `SetInt` / `GetInt` |

#### AudioReverbZone

Inspector表示名「オーディオリバーブゾーン」／カテゴリ「オーディオ」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 効果量 | `intensity` | Float | `SetFloat` / `GetFloat` |

#### AudioLowPassFilter

Inspector表示名「オーディオローパスフィルター」／カテゴリ「オーディオ」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 効果量 | `intensity` | Float | `SetFloat` / `GetFloat` |

#### AudioHighPassFilter

Inspector表示名「オーディオハイパスフィルター」／カテゴリ「オーディオ」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 効果量 | `intensity` | Float | `SetFloat` / `GetFloat` |

#### AudioEchoFilter

Inspector表示名「オーディオエコーフィルター」／カテゴリ「オーディオ」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 効果量 | `intensity` | Float | `SetFloat` / `GetFloat` |

#### AudioDistortionFilter

Inspector表示名「オーディオディストーションフィルター」／カテゴリ「オーディオ」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 効果量 | `intensity` | Float | `SetFloat` / `GetFloat` |

#### AudioReverbFilter

Inspector表示名「オーディオリバーブフィルター」／カテゴリ「オーディオ」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 効果量 | `intensity` | Float | `SetFloat` / `GetFloat` |

#### AudioChorusFilter

Inspector表示名「オーディオコーラスフィルター」／カテゴリ「オーディオ」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 効果量 | `intensity` | Float | `SetFloat` / `GetFloat` |

#### CanvasScaler

Inspector表示名「キャンバススケーラー」／カテゴリ「UI」／公開Field 6件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 幅高さの比重 | `sliderValue` | Float | `SetFloat` / `GetFloat` |
| 透明度 | `intensity` | Float | `SetFloat` / `GetFloat` |
| 描画順 | `physicsLayer` | Int | `SetInt` / `GetInt` |
| 入力を受け取る | `buttonInteractable` | Bool | `SetBool` / `GetBool` |
| 横を内容へ合わせる | `freezePositionX` | Bool | `SetBool` / `GetBool` |
| 縦を内容へ合わせる | `freezePositionY` | Bool | `SetBool` / `GetBool` |

#### GraphicRaycaster

Inspector表示名「グラフィックレイキャスター」／カテゴリ「UI」／公開Field 6件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 幅高さの比重 | `sliderValue` | Float | `SetFloat` / `GetFloat` |
| 透明度 | `intensity` | Float | `SetFloat` / `GetFloat` |
| 描画順 | `physicsLayer` | Int | `SetInt` / `GetInt` |
| 入力を受け取る | `buttonInteractable` | Bool | `SetBool` / `GetBool` |
| 横を内容へ合わせる | `freezePositionX` | Bool | `SetBool` / `GetBool` |
| 縦を内容へ合わせる | `freezePositionY` | Bool | `SetBool` / `GetBool` |

#### RawImage

Inspector表示名「Raw イメージ」／カテゴリ「UI」／公開Field 6件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 幅高さの比重 | `sliderValue` | Float | `SetFloat` / `GetFloat` |
| 透明度 | `intensity` | Float | `SetFloat` / `GetFloat` |
| 描画順 | `physicsLayer` | Int | `SetInt` / `GetInt` |
| 入力を受け取る | `buttonInteractable` | Bool | `SetBool` / `GetBool` |
| 横を内容へ合わせる | `freezePositionX` | Bool | `SetBool` / `GetBool` |
| 縦を内容へ合わせる | `freezePositionY` | Bool | `SetBool` / `GetBool` |

#### TextMeshProUGUI

Inspector表示名「TextMeshPro UGUI」／カテゴリ「UI」／公開Field 6件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 幅高さの比重 | `sliderValue` | Float | `SetFloat` / `GetFloat` |
| 透明度 | `intensity` | Float | `SetFloat` / `GetFloat` |
| 描画順 | `physicsLayer` | Int | `SetInt` / `GetInt` |
| 入力を受け取る | `buttonInteractable` | Bool | `SetBool` / `GetBool` |
| 横を内容へ合わせる | `freezePositionX` | Bool | `SetBool` / `GetBool` |
| 縦を内容へ合わせる | `freezePositionY` | Bool | `SetBool` / `GetBool` |

#### Button

Inspector表示名「ボタン」／カテゴリ「UI」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 操作可能 | `buttonInteractable` | Bool | `SetBool` / `GetBool` |

#### Toggle

Inspector表示名「トグル」／カテゴリ「UI」／公開Field 2件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 現在値 | `toggleValue` | Bool | `SetBool` / `GetBool` |
| 操作可能 | `buttonInteractable` | Bool | `SetBool` / `GetBool` |

#### Slider

Inspector表示名「スライダー」／カテゴリ「UI」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 最小値 | `sliderMinValue` | Float | `SetFloat` / `GetFloat` |
| 最大値 | `sliderMaxValue` | Float | `SetFloat` / `GetFloat` |
| 現在値 | `sliderValue` | Float | `SetFloat` / `GetFloat` |
| 操作可能 | `buttonInteractable` | Bool | `SetBool` / `GetBool` |

#### Scrollbar

Inspector表示名「スクロールバー」／カテゴリ「UI」／公開Field 6件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 幅高さの比重 | `sliderValue` | Float | `SetFloat` / `GetFloat` |
| 透明度 | `intensity` | Float | `SetFloat` / `GetFloat` |
| 描画順 | `physicsLayer` | Int | `SetInt` / `GetInt` |
| 入力を受け取る | `buttonInteractable` | Bool | `SetBool` / `GetBool` |
| 横を内容へ合わせる | `freezePositionX` | Bool | `SetBool` / `GetBool` |
| 縦を内容へ合わせる | `freezePositionY` | Bool | `SetBool` / `GetBool` |

#### Dropdown

Inspector表示名「ドロップダウン」／カテゴリ「UI」／公開Field 6件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 幅高さの比重 | `sliderValue` | Float | `SetFloat` / `GetFloat` |
| 透明度 | `intensity` | Float | `SetFloat` / `GetFloat` |
| 描画順 | `physicsLayer` | Int | `SetInt` / `GetInt` |
| 入力を受け取る | `buttonInteractable` | Bool | `SetBool` / `GetBool` |
| 横を内容へ合わせる | `freezePositionX` | Bool | `SetBool` / `GetBool` |
| 縦を内容へ合わせる | `freezePositionY` | Bool | `SetBool` / `GetBool` |

#### TMPDropdown

Inspector表示名「TMP ドロップダウン」／カテゴリ「UI」／公開Field 6件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 幅高さの比重 | `sliderValue` | Float | `SetFloat` / `GetFloat` |
| 透明度 | `intensity` | Float | `SetFloat` / `GetFloat` |
| 描画順 | `physicsLayer` | Int | `SetInt` / `GetInt` |
| 入力を受け取る | `buttonInteractable` | Bool | `SetBool` / `GetBool` |
| 横を内容へ合わせる | `freezePositionX` | Bool | `SetBool` / `GetBool` |
| 縦を内容へ合わせる | `freezePositionY` | Bool | `SetBool` / `GetBool` |

#### InputField

Inspector表示名「入力フィールド」／カテゴリ「UI」／公開Field 6件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 幅高さの比重 | `sliderValue` | Float | `SetFloat` / `GetFloat` |
| 透明度 | `intensity` | Float | `SetFloat` / `GetFloat` |
| 描画順 | `physicsLayer` | Int | `SetInt` / `GetInt` |
| 入力を受け取る | `buttonInteractable` | Bool | `SetBool` / `GetBool` |
| 横を内容へ合わせる | `freezePositionX` | Bool | `SetBool` / `GetBool` |
| 縦を内容へ合わせる | `freezePositionY` | Bool | `SetBool` / `GetBool` |

#### TMPInputField

Inspector表示名「TMP 入力フィールド」／カテゴリ「UI」／公開Field 6件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 幅高さの比重 | `sliderValue` | Float | `SetFloat` / `GetFloat` |
| 透明度 | `intensity` | Float | `SetFloat` / `GetFloat` |
| 描画順 | `physicsLayer` | Int | `SetInt` / `GetInt` |
| 入力を受け取る | `buttonInteractable` | Bool | `SetBool` / `GetBool` |
| 横を内容へ合わせる | `freezePositionX` | Bool | `SetBool` / `GetBool` |
| 縦を内容へ合わせる | `freezePositionY` | Bool | `SetBool` / `GetBool` |

#### ScrollRect

Inspector表示名「スクロールレクト」／カテゴリ「UI」／公開Field 6件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 幅高さの比重 | `sliderValue` | Float | `SetFloat` / `GetFloat` |
| 透明度 | `intensity` | Float | `SetFloat` / `GetFloat` |
| 描画順 | `physicsLayer` | Int | `SetInt` / `GetInt` |
| 入力を受け取る | `buttonInteractable` | Bool | `SetBool` / `GetBool` |
| 横を内容へ合わせる | `freezePositionX` | Bool | `SetBool` / `GetBool` |
| 縦を内容へ合わせる | `freezePositionY` | Bool | `SetBool` / `GetBool` |

#### Mask

Inspector表示名「マスク」／カテゴリ「UI」／公開Field 6件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 幅高さの比重 | `sliderValue` | Float | `SetFloat` / `GetFloat` |
| 透明度 | `intensity` | Float | `SetFloat` / `GetFloat` |
| 描画順 | `physicsLayer` | Int | `SetInt` / `GetInt` |
| 入力を受け取る | `buttonInteractable` | Bool | `SetBool` / `GetBool` |
| 横を内容へ合わせる | `freezePositionX` | Bool | `SetBool` / `GetBool` |
| 縦を内容へ合わせる | `freezePositionY` | Bool | `SetBool` / `GetBool` |

#### RectMask2D

Inspector表示名「レクトマスク 2D」／カテゴリ「UI」／公開Field 6件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 幅高さの比重 | `sliderValue` | Float | `SetFloat` / `GetFloat` |
| 透明度 | `intensity` | Float | `SetFloat` / `GetFloat` |
| 描画順 | `physicsLayer` | Int | `SetInt` / `GetInt` |
| 入力を受け取る | `buttonInteractable` | Bool | `SetBool` / `GetBool` |
| 横を内容へ合わせる | `freezePositionX` | Bool | `SetBool` / `GetBool` |
| 縦を内容へ合わせる | `freezePositionY` | Bool | `SetBool` / `GetBool` |

#### HorizontalLayoutGroup

Inspector表示名「水平レイアウトグループ」／カテゴリ「UI」／公開Field 6件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 幅高さの比重 | `sliderValue` | Float | `SetFloat` / `GetFloat` |
| 透明度 | `intensity` | Float | `SetFloat` / `GetFloat` |
| 描画順 | `physicsLayer` | Int | `SetInt` / `GetInt` |
| 入力を受け取る | `buttonInteractable` | Bool | `SetBool` / `GetBool` |
| 横を内容へ合わせる | `freezePositionX` | Bool | `SetBool` / `GetBool` |
| 縦を内容へ合わせる | `freezePositionY` | Bool | `SetBool` / `GetBool` |

#### VerticalLayoutGroup

Inspector表示名「垂直レイアウトグループ」／カテゴリ「UI」／公開Field 6件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 幅高さの比重 | `sliderValue` | Float | `SetFloat` / `GetFloat` |
| 透明度 | `intensity` | Float | `SetFloat` / `GetFloat` |
| 描画順 | `physicsLayer` | Int | `SetInt` / `GetInt` |
| 入力を受け取る | `buttonInteractable` | Bool | `SetBool` / `GetBool` |
| 横を内容へ合わせる | `freezePositionX` | Bool | `SetBool` / `GetBool` |
| 縦を内容へ合わせる | `freezePositionY` | Bool | `SetBool` / `GetBool` |

#### GridLayoutGroup

Inspector表示名「グリッドレイアウトグループ」／カテゴリ「UI」／公開Field 6件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 幅高さの比重 | `sliderValue` | Float | `SetFloat` / `GetFloat` |
| 透明度 | `intensity` | Float | `SetFloat` / `GetFloat` |
| 描画順 | `physicsLayer` | Int | `SetInt` / `GetInt` |
| 入力を受け取る | `buttonInteractable` | Bool | `SetBool` / `GetBool` |
| 横を内容へ合わせる | `freezePositionX` | Bool | `SetBool` / `GetBool` |
| 縦を内容へ合わせる | `freezePositionY` | Bool | `SetBool` / `GetBool` |

#### ContentSizeFitter

Inspector表示名「コンテンツサイズフィッター」／カテゴリ「UI」／公開Field 6件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 幅高さの比重 | `sliderValue` | Float | `SetFloat` / `GetFloat` |
| 透明度 | `intensity` | Float | `SetFloat` / `GetFloat` |
| 描画順 | `physicsLayer` | Int | `SetInt` / `GetInt` |
| 入力を受け取る | `buttonInteractable` | Bool | `SetBool` / `GetBool` |
| 横を内容へ合わせる | `freezePositionX` | Bool | `SetBool` / `GetBool` |
| 縦を内容へ合わせる | `freezePositionY` | Bool | `SetBool` / `GetBool` |

#### AspectRatioFitter

Inspector表示名「アスペクト比フィッター」／カテゴリ「UI」／公開Field 6件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 幅高さの比重 | `sliderValue` | Float | `SetFloat` / `GetFloat` |
| 透明度 | `intensity` | Float | `SetFloat` / `GetFloat` |
| 描画順 | `physicsLayer` | Int | `SetInt` / `GetInt` |
| 入力を受け取る | `buttonInteractable` | Bool | `SetBool` / `GetBool` |
| 横を内容へ合わせる | `freezePositionX` | Bool | `SetBool` / `GetBool` |
| 縦を内容へ合わせる | `freezePositionY` | Bool | `SetBool` / `GetBool` |

#### LayoutElement

Inspector表示名「レイアウトエレメント」／カテゴリ「UI」／公開Field 6件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 幅高さの比重 | `sliderValue` | Float | `SetFloat` / `GetFloat` |
| 透明度 | `intensity` | Float | `SetFloat` / `GetFloat` |
| 描画順 | `physicsLayer` | Int | `SetInt` / `GetInt` |
| 入力を受け取る | `buttonInteractable` | Bool | `SetBool` / `GetBool` |
| 横を内容へ合わせる | `freezePositionX` | Bool | `SetBool` / `GetBool` |
| 縦を内容へ合わせる | `freezePositionY` | Bool | `SetBool` / `GetBool` |

#### PlayerInputManager

Inspector表示名「プレイヤー入力マネージャー」／カテゴリ「入力・イベント」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 最大 Player 数 | `particleMaxCount` | Int | `SetInt` / `GetInt` |

#### NavMeshObstacle

Inspector表示名「NavMesh 障害物」／カテゴリ「ナビゲーション」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 半径 | `colliderRadius` | Float | `SetFloat` / `GetFloat` |

#### NavMeshSurface

Inspector表示名「NavMesh サーフェス」／カテゴリ「ナビゲーション」／公開Field 2件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 最大段差 | `navMaxClimb` | Float | `SetFloat` / `GetFloat` |
| レイヤーマスク | `physicsLayer` | Int | `SetInt` / `GetInt` |

#### NavMeshModifier

Inspector表示名「NavMesh モディファイア」／カテゴリ「ナビゲーション」／公開Field 3件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| Area | `navArea` | Int | `SetInt` / `GetInt` |
| Area を上書き | `navAreaOverride` | Bool | `SetBool` / `GetBool` |
| ビルドから除外 | `navIgnoreFromBuild` | Bool | `SetBool` / `GetBool` |

#### NavMeshModifierVolume

Inspector表示名「NavMesh モディファイアボリューム」／カテゴリ「ナビゲーション」／公開Field 3件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| Area | `navArea` | Int | `SetInt` / `GetInt` |
| 中心 | `colliderCenter` | Vector3 | `SetVector3` / `GetVector3` |
| サイズ | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |

#### NavMeshLink

Inspector表示名「NavMesh リンク」／カテゴリ「ナビゲーション」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| コスト倍率 | `navCostModifier` | Float | `SetFloat` / `GetFloat` |
| 幅 | `colliderRadius` | Float | `SetFloat` / `GetFloat` |
| 双方向 | `navBidirectional` | Bool | `SetBool` / `GetBool` |
| 接続先 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIBehaviorTree

Inspector表示名「行動ツリー」／カテゴリ「AI」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 対象 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIBehaviorBlackboard

Inspector表示名「共有データ（Blackboard）」／カテゴリ「AI」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| セルサイズ | `colliderRadius` | Float | `SetFloat` / `GetFloat` |
| 段差 | `navMaxClimb` | Float | `SetFloat` / `GetFloat` |
| グリッド数 | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIBehaviorSelector

Inspector表示名「条件分岐（Selector）」／カテゴリ「AI」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| セルサイズ | `colliderRadius` | Float | `SetFloat` / `GetFloat` |
| 段差 | `navMaxClimb` | Float | `SetFloat` / `GetFloat` |
| グリッド数 | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIBehaviorSequence

Inspector表示名「順番実行（Sequence）」／カテゴリ「AI」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| セルサイズ | `colliderRadius` | Float | `SetFloat` / `GetFloat` |
| 段差 | `navMaxClimb` | Float | `SetFloat` / `GetFloat` |
| グリッド数 | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIBehaviorTask

Inspector表示名「実行処理（Task）」／カテゴリ「AI」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| セルサイズ | `colliderRadius` | Float | `SetFloat` / `GetFloat` |
| 段差 | `navMaxClimb` | Float | `SetFloat` / `GetFloat` |
| グリッド数 | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIBehaviorDecorator

Inspector表示名「条件装飾（Decorator）」／カテゴリ「AI」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| セルサイズ | `colliderRadius` | Float | `SetFloat` / `GetFloat` |
| 段差 | `navMaxClimb` | Float | `SetFloat` / `GetFloat` |
| グリッド数 | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIStateMachine

Inspector表示名「状態制御」／カテゴリ「AI」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 対象 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIState

Inspector表示名「状態」／カテゴリ「AI」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| セルサイズ | `colliderRadius` | Float | `SetFloat` / `GetFloat` |
| 段差 | `navMaxClimb` | Float | `SetFloat` / `GetFloat` |
| グリッド数 | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIStateTransition

Inspector表示名「状態遷移」／カテゴリ「AI」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| セルサイズ | `colliderRadius` | Float | `SetFloat` / `GetFloat` |
| 段差 | `navMaxClimb` | Float | `SetFloat` / `GetFloat` |
| グリッド数 | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIGoapPlanner

Inspector表示名「目標計画」／カテゴリ「AI」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 対象 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIGoapGoal

Inspector表示名「目標条件」／カテゴリ「AI」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| セルサイズ | `colliderRadius` | Float | `SetFloat` / `GetFloat` |
| 段差 | `navMaxClimb` | Float | `SetFloat` / `GetFloat` |
| グリッド数 | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIGoapAction

Inspector表示名「計画行動」／カテゴリ「AI」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| セルサイズ | `colliderRadius` | Float | `SetFloat` / `GetFloat` |
| 段差 | `navMaxClimb` | Float | `SetFloat` / `GetFloat` |
| グリッド数 | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIGoapWorldState

Inspector表示名「世界状態」／カテゴリ「AI」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| セルサイズ | `colliderRadius` | Float | `SetFloat` / `GetFloat` |
| 段差 | `navMaxClimb` | Float | `SetFloat` / `GetFloat` |
| グリッド数 | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIHtnPlanner

Inspector表示名「タスク計画」／カテゴリ「AI」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 対象 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIHtnDomain

Inspector表示名「タスク領域」／カテゴリ「AI」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| セルサイズ | `colliderRadius` | Float | `SetFloat` / `GetFloat` |
| 段差 | `navMaxClimb` | Float | `SetFloat` / `GetFloat` |
| グリッド数 | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIHtnTask

Inspector表示名「タスク」／カテゴリ「AI」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| セルサイズ | `colliderRadius` | Float | `SetFloat` / `GetFloat` |
| 段差 | `navMaxClimb` | Float | `SetFloat` / `GetFloat` |
| グリッド数 | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIHtnMethod

Inspector表示名「タスク分解」／カテゴリ「AI」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| セルサイズ | `colliderRadius` | Float | `SetFloat` / `GetFloat` |
| 段差 | `navMaxClimb` | Float | `SetFloat` / `GetFloat` |
| グリッド数 | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIPathfindingAgent

Inspector表示名「経路探索エージェント」／カテゴリ「AI」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 対象 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIMicroPatherGrid

Inspector表示名「グリッド経路」／カテゴリ「AI」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| セルサイズ | `colliderRadius` | Float | `SetFloat` / `GetFloat` |
| 段差 | `navMaxClimb` | Float | `SetFloat` / `GetFloat` |
| グリッド数 | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIRecastNavMeshBuilder

Inspector表示名「ナビメッシュ生成」／カテゴリ「AI」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| セルサイズ | `colliderRadius` | Float | `SetFloat` / `GetFloat` |
| 段差 | `navMaxClimb` | Float | `SetFloat` / `GetFloat` |
| グリッド数 | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIRecastCrowdAgent

Inspector表示名「群衆エージェント」／カテゴリ「AI」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 対象 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIPathRequest

Inspector表示名「経路要求」／カテゴリ「AI」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| セルサイズ | `colliderRadius` | Float | `SetFloat` / `GetFloat` |
| 段差 | `navMaxClimb` | Float | `SetFloat` / `GetFloat` |
| グリッド数 | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIDynamicObstacle

Inspector表示名「動的障害物」／カテゴリ「AI」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| セルサイズ | `colliderRadius` | Float | `SetFloat` / `GetFloat` |
| 段差 | `navMaxClimb` | Float | `SetFloat` / `GetFloat` |
| グリッド数 | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AISteeringAgent

Inspector表示名「操舵エージェント」／カテゴリ「AI」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 対象 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AISeekSteering

Inspector表示名「接近操舵」／カテゴリ「AI」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 対象 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIFleeSteering

Inspector表示名「逃走操舵」／カテゴリ「AI」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 対象 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIArriveSteering

Inspector表示名「到着操舵」／カテゴリ「AI」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 対象 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIPursuitSteering

Inspector表示名「追跡操舵」／カテゴリ「AI」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 対象 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIWanderSteering

Inspector表示名「徘徊操舵」／カテゴリ「AI」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 対象 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIObstacleAvoidanceSteering

Inspector表示名「障害物回避操舵」／カテゴリ「AI」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 対象 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIFlockSteering

Inspector表示名「群れ操舵」／カテゴリ「AI」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 対象 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIVisionSensor

Inspector表示名「視界センサー」／カテゴリ「AI」／公開Field 2件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 視界距離 | `colliderRadius` | Float | `SetFloat` / `GetFloat` |
| 対象 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIOpenCvCamera

Inspector表示名「画像入力カメラ」／カテゴリ「AI」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| セルサイズ | `colliderRadius` | Float | `SetFloat` / `GetFloat` |
| 段差 | `navMaxClimb` | Float | `SetFloat` / `GetFloat` |
| グリッド数 | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIOpenCvObjectDetector

Inspector表示名「画像物体検出」／カテゴリ「AI」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| セルサイズ | `colliderRadius` | Float | `SetFloat` / `GetFloat` |
| 段差 | `navMaxClimb` | Float | `SetFloat` / `GetFloat` |
| グリッド数 | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIOpenCvColorTracker

Inspector表示名「画像色追跡」／カテゴリ「AI」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| セルサイズ | `colliderRadius` | Float | `SetFloat` / `GetFloat` |
| 段差 | `navMaxClimb` | Float | `SetFloat` / `GetFloat` |
| グリッド数 | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIMotionSensor

Inspector表示名「動きセンサー」／カテゴリ「AI」／公開Field 2件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 視界距離 | `colliderRadius` | Float | `SetFloat` / `GetFloat` |
| 対象 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIWhisperSpeechRecognizer

Inspector表示名「Whisper 音声認識」／カテゴリ「AI」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| セルサイズ | `colliderRadius` | Float | `SetFloat` / `GetFloat` |
| 段差 | `navMaxClimb` | Float | `SetFloat` / `GetFloat` |
| グリッド数 | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AIVoiceCommand

Inspector表示名「音声コマンド」／カテゴリ「AI」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| セルサイズ | `colliderRadius` | Float | `SetFloat` / `GetFloat` |
| 段差 | `navMaxClimb` | Float | `SetFloat` / `GetFloat` |
| グリッド数 | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先 | `connectedGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### ParticleSystem

Inspector表示名「パーティクルシステム」／カテゴリ「エフェクト」／公開Field 38件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 再生時間 | `particleDuration` | Float | `SetFloat` / `GetFloat` |
| 開始遅延 | `particleStartDelay` | Float | `SetFloat` / `GetFloat` |
| 寿命 | `particleLifetime` | Float | `SetFloat` / `GetFloat` |
| 寿命のばらつき | `particleLifetimeRandomness` | Float | `SetFloat` / `GetFloat` |
| 1秒当たり | `particleRate` | Float | `SetFloat` / `GetFloat` |
| 半径 | `particleShapeRadius` | Float | `SetFloat` / `GetFloat` |
| コーン角度 | `particleShapeAngle` | Float | `SetFloat` / `GetFloat` |
| 初速度 | `particleSpeed` | Float | `SetFloat` / `GetFloat` |
| 速度のばらつき | `particleSpeedRandomness` | Float | `SetFloat` / `GetFloat` |
| 重力 | `particleGravity` | Float | `SetFloat` / `GetFloat` |
| 空気抵抗 | `particleDrag` | Float | `SetFloat` / `GetFloat` |
| 終了速度倍率 | `particleEndSpeedMultiplier` | Float | `SetFloat` / `GetFloat` |
| 回転速度 | `particleRotationSpeed` | Float | `SetFloat` / `GetFloat` |
| 乱流の強さ | `particleNoiseStrength` | Float | `SetFloat` / `GetFloat` |
| 乱流の周波数 | `particleNoiseFrequency` | Float | `SetFloat` / `GetFloat` |
| 角速度 | `particleAngularSpeed` | Float | `SetFloat` / `GetFloat` |
| 半径方向加速度 | `particleRadialAcceleration` | Float | `SetFloat` / `GetFloat` |
| 波の振幅 | `particleWaveAmplitude` | Float | `SetFloat` / `GetFloat` |
| 波の周波数 | `particleWaveFrequency` | Float | `SetFloat` / `GetFloat` |
| 吸引力 | `particleAttractorStrength` | Float | `SetFloat` / `GetFloat` |
| 反発 | `particleCollisionBounce` | Float | `SetFloat` / `GetFloat` |
| 摩擦 | `particleCollisionFriction` | Float | `SetFloat` / `GetFloat` |
| 開始サイズ | `particleSize` | Float | `SetFloat` / `GetFloat` |
| 終了サイズ | `particleEndSize` | Float | `SetFloat` / `GetFloat` |
| サイズのばらつき | `particleSizeRandomness` | Float | `SetFloat` / `GetFloat` |
| 開始アルファ | `particleStartAlpha` | Float | `SetFloat` / `GetFloat` |
| 終了アルファ | `particleEndAlpha` | Float | `SetFloat` / `GetFloat` |
| 放射強度 | `particleEmissionStrength` | Float | `SetFloat` / `GetFloat` |
| 速度方向の長さ | `particleBillboardStretch` | Float | `SetFloat` / `GetFloat` |
| 最大数 | `particleMaxCount` | Int | `SetInt` / `GetInt` |
| 開始バースト | `particleBurstCount` | Int | `SetInt` / `GetInt` |
| 自動再生 | `animationPlayOnAwake` | Bool | `SetBool` / `GetBool` |
| ループ | `particleLooping` | Bool | `SetBool` / `GetBool` |
| プリウォーム | `particlePrewarm` | Bool | `SetBool` / `GetBool` |
| 衝突 | `particleCollision` | Bool | `SetBool` / `GetBool` |
| ボックス範囲 | `particleBoxSize` | Vector3 | `SetVector3` / `GetVector3` |
| 放出方向 | `particleDirection` | Vector3 | `SetVector3` / `GetVector3` |
| 運動中心 | `particleMotionCenter` | Vector3 | `SetVector3` / `GetVector3` |

#### LensFlare

Inspector表示名「レンズフレア」／カテゴリ「エフェクト」／公開Field 2件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 明るさ | `intensity` | Float | `SetFloat` / `GetFloat` |
| 透明度 | `alpha` | Float | `SetFloat` / `GetFloat` |

#### Projector

Inspector表示名「プロジェクター」／カテゴリ「エフェクト」／公開Field 3件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 視野角 | `intensity` | Float | `SetFloat` / `GetFloat` |
| 透明度 | `alpha` | Float | `SetFloat` / `GetFloat` |
| 投影サイズ | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |

#### DecalProjector

Inspector表示名「デカールプロジェクター」／カテゴリ「エフェクト」／公開Field 2件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 透明度 | `alpha` | Float | `SetFloat` / `GetFloat` |
| サイズ | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |

#### Terrain

Inspector表示名「テレイン」／カテゴリ「地形・タイルマップ」／公開Field 2件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 最高LOD解像度 | `oceanGridResolution` | Int | `SetInt` / `GetInt` |
| サイズ X / 高さ / Z | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |

#### Tilemap

Inspector表示名「タイルマップ」／カテゴリ「地形・タイルマップ」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| サイズ | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |

#### TilemapRenderer

Inspector表示名「タイルマップレンダラー」／カテゴリ「地形・タイルマップ」／公開Field 7件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 強さ | `intensity` | Float | `SetFloat` / `GetFloat` |
| 透明度 | `alpha` | Float | `SetFloat` / `GetFloat` |
| 粗さ | `roughness` | Float | `SetFloat` / `GetFloat` |
| 金属度 | `metallic` | Float | `SetFloat` / `GetFloat` |
| 反射 | `reflectionStrength` | Float | `SetFloat` / `GetFloat` |
| 発光 | `emissionStrength` | Float | `SetFloat` / `GetFloat` |
| 両面 | `doubleSided` | Bool | `SetBool` / `GetBool` |

#### Grid

Inspector表示名「グリッド」／カテゴリ「地形・タイルマップ」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| セルサイズ | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |

#### LocalMove

Inspector表示名「ローカル移動」／カテゴリ「ゲームプレイ」／公開Field 2件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 速度 | `inputMoveSpeed` | Float | `SetFloat` / `GetFloat` |
| ローカル方向 | `velocity` | Vector3 | `SetVector3` / `GetVector3` |

#### RollingMove

Inspector表示名「ローリング移動」／カテゴリ「ゲームプレイ」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| トルク | `rollingTorque` | Float | `SetFloat` / `GetFloat` |
| 馬力 | `rollingHorsepower` | Float | `SetFloat` / `GetFloat` |
| 半径 | `colliderRadius` | Float | `SetFloat` / `GetFloat` |
| 進行方向 | `velocity` | Vector3 | `SetVector3` / `GetVector3` |

#### PostProcess

Inspector表示名「ポストプロセス」／カテゴリ「ライト・環境」／公開Field 33件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| SMAA しきい値 | `smaaThreshold` | Float | `SetFloat` / `GetFloat` |
| SMAA 角丸め | `smaaCornerRounding` | Float | `SetFloat` / `GetFloat` |
| Temporal シャープ | `temporalSharpness` | Float | `SetFloat` / `GetFloat` |
| Temporal 履歴ブレンド | `temporalBlendRatio` | Float | `SetFloat` / `GetFloat` |
| 明部しきい値 | `bloomThreshold` | Float | `SetFloat` / `GetFloat` |
| しきい値遷移 | `bloomSoftKnee` | Float | `SetFloat` / `GetFloat` |
| 最終明るさ | `finalBrightness` | Float | `SetFloat` / `GetFloat` |
| 露出 | `compositeExposure` | Float | `SetFloat` / `GetFloat` |
| 自動露出 下限 | `compositeMinimumExposure` | Float | `SetFloat` / `GetFloat` |
| 自動露出 上限 | `compositeMaximumExposure` | Float | `SetFloat` / `GetFloat` |
| 露出追従速度 | `compositeExposureAdaptationSpeed` | Float | `SetFloat` / `GetFloat` |
| 基準輝度 | `compositeTargetLuminance` | Float | `SetFloat` / `GetFloat` |
| ホワイトポイント | `compositeWhitePoint` | Float | `SetFloat` / `GetFloat` |
| 彩度 | `compositeSaturation` | Float | `SetFloat` / `GetFloat` |
| コントラスト | `compositeContrast` | Float | `SetFloat` / `GetFloat` |
| 色温度 | `compositeTemperature` | Float | `SetFloat` / `GetFloat` |
| Tint | `compositeTint` | Float | `SetFloat` / `GetFloat` |
| Gamma | `compositeGamma` | Float | `SetFloat` / `GetFloat` |
| 局所コントラスト | `compositeLocalContrast` | Float | `SetFloat` / `GetFloat` |
| 出力ディザリング | `compositeOutputDither` | Float | `SetFloat` / `GetFloat` |
| SSGI強度 | `compositeSsgiIntensity` | Float | `SetFloat` / `GetFloat` |
| SSGI半径 | `compositeSsgiRadiusPixels` | Float | `SetFloat` / `GetFloat` |
| カラーLUT強度 | `compositeColorLutStrength` | Float | `SetFloat` / `GetFloat` |
| ビネット | `compositeVignetteStrength` | Float | `SetFloat` / `GetFloat` |
| ビネット半径 | `compositeVignetteRadius` | Float | `SetFloat` / `GetFloat` |
| フィルムグレイン | `compositeFilmGrain` | Float | `SetFloat` / `GetFloat` |
| 色収差 | `compositeChromaticAberration` | Float | `SetFloat` / `GetFloat` |
| AO強度 | `compositeAmbientOcclusionStrength` | Float | `SetFloat` / `GetFloat` |
| SSR | `ssrEnabled` | Bool | `SetBool` / `GetBool` |
| 自動露出 | `compositeAutoExposureEnabled` | Bool | `SetBool` / `GetBool` |
| SSGI | `compositeSsgiEnabled` | Bool | `SetBool` / `GetBool` |
| Lift | `compositeLift` | Vector3 | `SetVector3` / `GetVector3` |
| Gain | `compositeGain` | Vector3 | `SetVector3` / `GetVector3` |

#### Environment

Inspector表示名「環境」／カテゴリ「ライト・環境」／公開Field 22件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 露出 | `intensity` | Float | `SetFloat` / `GetFloat` |
| 地平線のぼかし | `roughness` | Float | `SetFloat` / `GetFloat` |
| 反射への寄与 | `reflectionStrength` | Float | `SetFloat` / `GetFloat` |
| 環境光 | `metallic` | Float | `SetFloat` / `GetFloat` |
| 放射の強さ | `emissionStrength` | Float | `SetFloat` / `GetFloat` |
| 環境テクスチャ回転 | `environmentTextureRotation` | Float | `SetFloat` / `GetFloat` |
| MIPバイアス | `environmentTextureMipBias` | Float | `SetFloat` / `GetFloat` |
| 雲量 | `volumetricCloudCoverage` | Float | `SetFloat` / `GetFloat` |
| 密度 | `volumetricCloudDensity` | Float | `SetFloat` / `GetFloat` |
| スケール | `volumetricCloudScale` | Float | `SetFloat` / `GetFloat` |
| 移動速度 | `volumetricCloudSpeed` | Float | `SetFloat` / `GetFloat` |
| 高度 | `volumetricCloudHeight` | Float | `SetFloat` / `GetFloat` |
| 厚さ | `volumetricCloudThickness` | Float | `SetFloat` / `GetFloat` |
| 光吸収 | `volumetricCloudLightAbsorption` | Float | `SetFloat` / `GetFloat` |
| 銀縁 | `volumetricCloudSilverLining` | Float | `SetFloat` / `GetFloat` |
| 熱気の強さ | `environmentHeatIntensity` | Float | `SetFloat` / `GetFloat` |
| 地平線中心 | `environmentHeatHorizonCenter` | Float | `SetFloat` / `GetFloat` |
| 地平線範囲 | `environmentHeatHorizonWidth` | Float | `SetFloat` / `GetFloat` |
| 太陽方向の影響 | `environmentHeatSunInfluence` | Float | `SetFloat` / `GetFloat` |
| 歪みスケール | `environmentHeatDistortionScale` | Float | `SetFloat` / `GetFloat` |
| 環境画像を使用 | `environmentTextureEnabled` | Bool | `SetBool` / `GetBool` |
| 体積雲を使用 | `volumetricCloudEnabled` | Bool | `SetBool` / `GetBool` |

#### FreeTransform

Inspector表示名「自由移動/回転」／カテゴリ「ゲームプレイ」／公開Field 5件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 移動速度 | `freeMoveSpeed` | Float | `SetFloat` / `GetFloat` |
| 回転速度 | `freeRotateSpeed` | Float | `SetFloat` / `GetFloat` |
| ローカル空間 | `freeUseLocalSpace` | Bool | `SetBool` / `GetBool` |
| 移動入力 | `velocity` | Vector3 | `SetVector3` / `GetVector3` |
| 回転入力(deg/s) | `freeRotationInput` | Vector3 | `SetVector3` / `GetVector3` |

#### AutoConvexCollision

Inspector表示名「Auto Convex Collision」／カテゴリ「3D物理」／公開Field 3件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 最大凸包数 | `autoConvexMaximumHulls` | Int | `SetInt` / `GetInt` |
| 中心 | `colliderCenter` | Vector3 | `SetVector3` / `GetVector3` |
| サイズ | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |

#### Ocean

Inspector表示名「Ocean」／カテゴリ「描画・レンダリング」／公開Field 53件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 海面サイズ | `oceanSize` | Float | `SetFloat` / `GetFloat` |
| 波の高さ | `oceanWaveHeight` | Float | `SetFloat` / `GetFloat` |
| 最大波高 | `oceanMaxWaveHeight` | Float | `SetFloat` / `GetFloat` |
| 波長 | `oceanWaveLength` | Float | `SetFloat` / `GetFloat` |
| 波の速度 | `oceanWaveSpeed` | Float | `SetFloat` / `GetFloat` |
| 時間倍率 | `oceanTimeScale` | Float | `SetFloat` / `GetFloat` |
| Choppiness | `oceanChoppiness` | Float | `SetFloat` / `GetFloat` |
| 副波の強さ | `oceanSecondaryWaveScale` | Float | `SetFloat` / `GetFloat` |
| 細波の波長比 | `oceanRippleScale` | Float | `SetFloat` / `GetFloat` |
| 細波の強さ | `oceanRippleStrength` | Float | `SetFloat` / `GetFloat` |
| 風速 | `oceanWindSpeed` | Float | `SetFloat` / `GetFloat` |
| 水深 | `oceanWaterDepth` | Float | `SetFloat` / `GetFloat` |
| 方向分散 | `oceanDirectionSpread` | Float | `SetFloat` / `GetFloat` |
| うねりの強さ | `oceanSwellStrength` | Float | `SetFloat` / `GetFloat` |
| スペクトルシード | `oceanSpectrumSeed` | Float | `SetFloat` / `GetFloat` |
| 波頭の尖り | `oceanCrestSharpness` | Float | `SetFloat` / `GetFloat` |
| 泡の強さ | `oceanFoamStrength` | Float | `SetFloat` / `GetFloat` |
| 泡の閾値 | `oceanFoamThreshold` | Float | `SetFloat` / `GetFloat` |
| 粗さ | `oceanRoughness` | Float | `SetFloat` / `GetFloat` |
| 反射 | `oceanReflectionStrength` | Float | `SetFloat` / `GetFloat` |
| 屈折 | `transmission` | Float | `SetFloat` / `GetFloat` |
| 微細法線 | `oceanDetailNormalStrength` | Float | `SetFloat` / `GetFloat` |
| 吸収距離 | `oceanAbsorptionDistance` | Float | `SetFloat` / `GetFloat` |
| 屈折の歪み | `oceanRefractionDistortion` | Float | `SetFloat` / `GetFloat` |
| 太陽Diffuse影響 | `oceanSunDiffuseInfluence` | Float | `SetFloat` / `GetFloat` |
| 太陽Diffuse下限 | `oceanDiffuseFloor` | Float | `SetFloat` / `GetFloat` |
| 太陽Specular影響 | `oceanSunSpecularInfluence` | Float | `SetFloat` / `GetFloat` |
| 太陽Glitter影響 | `oceanSunGlitterInfluence` | Float | `SetFloat` / `GetFloat` |
| Sky Reflection影響 | `oceanSkyReflectionInfluence` | Float | `SetFloat` / `GetFloat` |
| Ambient影響 | `oceanAmbientInfluence` | Float | `SetFloat` / `GetFloat` |
| 大波反射影響 | `oceanMacroReflectionInfluence` | Float | `SetFloat` / `GetFloat` |
| 曲率感度 | `oceanCurvatureInfluence` | Float | `SetFloat` / `GetFloat` |
| 谷の環境遮蔽 | `oceanTroughOcclusionStrength` | Float | `SetFloat` / `GetFloat` |
| 波頭Haze | `oceanCrestHazeStrength` | Float | `SetFloat` / `GetFloat` |
| 波頭細波増幅 | `oceanCrestDetailBoost` | Float | `SetFloat` / `GetFloat` |
| 斜面屈折影響 | `oceanSlopeRefractionInfluence` | Float | `SetFloat` / `GetFloat` |
| 中波構造 | `oceanMediumWaveStrength` | Float | `SetFloat` / `GetFloat` |
| 波形の色分離 | `oceanWaveColorSeparation` | Float | `SetFloat` / `GetFloat` |
| 形状による粗さ差 | `oceanShapeRoughnessVariation` | Float | `SetFloat` / `GetFloat` |
| 近距離Detail保持 | `oceanDetailFilterSharpness` | Float | `SetFloat` / `GetFloat` |
| 浅角度形状保持 | `oceanGrazingShapeVisibility` | Float | `SetFloat` / `GetFloat` |
| 近景Pixel変位 | `oceanPerPixelDisplacementStrength` | Float | `SetFloat` / `GetFloat` |
| Pixel変位距離 | `oceanPerPixelDisplacementDistance` | Float | `SetFloat` / `GetFloat` |
| 細分化目標Pixel | `oceanTessellationTargetPixels` | Float | `SetFloat` / `GetFloat` |
| 細分化最大係数 | `oceanTessellationMaximumFactor` | Float | `SetFloat` / `GetFloat` |
| グリッター強度 | `oceanGlitterIntensity` | Float | `SetFloat` / `GetFloat` |
| グリッター鋭さ | `oceanGlitterSharpness` | Float | `SetFloat` / `GetFloat` |
| グリッター密度 | `oceanGlitterDensity` | Float | `SetFloat` / `GetFloat` |
| グリッター開始閾値 | `oceanGlitterThreshold` | Float | `SetFloat` / `GetFloat` |
| グリッター最大輝度 | `oceanGlitterMaxClamp` | Float | `SetFloat` / `GetFloat` |
| グリッド解像度 | `oceanGridResolution` | Int | `SetInt` / `GetInt` |
| Pixel変位反復数 | `oceanPerPixelDisplacementSteps` | Int | `SetInt` / `GetInt` |
| GPU適応細分化 | `oceanGpuTessellationEnabled` | Bool | `SetBool` / `GetBool` |

#### Buoyancy

Inspector表示名「Buoyancy」／カテゴリ「物理」／公開Field 28件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 水密度 kg/m3 | `buoyancyWaterDensity` | Float | `SetFloat` / `GetFloat` |
| 目標水没率 | `buoyancyTargetSubmersionRatio` | Float | `SetFloat` / `GetFloat` |
| 浮力 | `buoyancyStrength` | Float | `SetFloat` / `GetFloat` |
| 上下減衰 | `buoyancyDamping` | Float | `SetFloat` / `GetFloat` |
| 前後の水抵抗 | `buoyancyWaterDrag` | Float | `SetFloat` / `GetFloat` |
| 横方向の水抵抗 | `buoyancyLateralDrag` | Float | `SetFloat` / `GetFloat` |
| 上下の水抵抗 | `buoyancyVerticalDrag` | Float | `SetFloat` / `GetFloat` |
| 回転抵抗 | `buoyancyAngularDrag` | Float | `SetFloat` / `GetFloat` |
| 着水衝撃 | `buoyancySlammingStrength` | Float | `SetFloat` / `GetFloat` |
| 波の横押し | `buoyancyNormalInfluence` | Float | `SetFloat` / `GetFloat` |
| 自動物理を使用 | `buoyancyAutomaticPhysicalProperties` | Bool | `SetBool` / `GetBool` |
| 浮力中心 | `buoyancyCenterOffset` | Vector3 | `SetVector3` / `GetVector3` |
| 船体サイズ | `buoyancyHullSize` | Vector3 | `SetVector3` / `GetVector3` |
| 船体重量 N | `buoyancyDebugWeightForce` | Float | `SetFloat` / `GetFloat` |
| 浮力 N | `buoyancyDebugBuoyancyForce` | Float | `SetFloat` / `GetFloat` |
| 圧力抗力 N | `buoyancyDebugPressureDragForce` | Float | `SetFloat` / `GetFloat` |
| 圧力の上向き成分 N | `buoyancyDebugPressureUpwardForce` | Float | `SetFloat` / `GetFloat` |
| 摩擦抗力 N | `buoyancyDebugSkinFrictionForce` | Float | `SetFloat` / `GetFloat` |
| 付加質量力 N | `buoyancyDebugAddedMassForce` | Float | `SetFloat` / `GetFloat` |
| 着水衝撃 N | `buoyancyDebugSlammingForce` | Float | `SetFloat` / `GetFloat` |
| 造波抵抗 N | `buoyancyDebugWaveMakingResistance` | Float | `SetFloat` / `GetFloat` |
| 水没率 | `buoyancyDebugSubmergedRatio` | Float | `SetFloat` / `GetFloat` |
| 濡れ面積 m2 | `buoyancyDebugWettedArea` | Float | `SetFloat` / `GetFloat` |
| Trim角 度 | `buoyancyDebugTrimAngleDegrees` | Float | `SetFloat` / `GetFloat` |
| 前進相対速度 m/s | `buoyancyDebugForwardSpeed` | Float | `SetFloat` / `GetFloat` |
| 斜航角 度 | `buoyancyDebugSideslipAngleDegrees` | Float | `SetFloat` / `GetFloat` |
| 付加質量Coriolis N·m | `buoyancyDebugAddedMassCoriolisTorque` | Vector3 | `SetVector3` / `GetVector3` |
| 対象 Ocean | `buoyancyOceanGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### RailMovement

Inspector表示名「レール移動」／カテゴリ「ゲームプレイ」／公開Field 139件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 速度 | `railSpeed` | Float | `SetFloat` / `GetFloat` |
| 加速度 | `railAcceleration` | Float | `SetFloat` / `GetFloat` |
| 減速度 | `railDeceleration` | Float | `SetFloat` / `GetFloat` |
| 開始位置 | `railStartNormalized` | Float | `SetFloat` / `GetFloat` |
| 向きの先読み | `railLookAheadDistance` | Float | `SetFloat` / `GetFloat` |
| 移動速度 | `railOffsetMoveSpeed` | Float | `SetFloat` / `GetFloat` |
| 位置ばね | `railPositionSpring` | Float | `SetFloat` / `GetFloat` |
| 位置減衰 | `railPositionDamping` | Float | `SetFloat` / `GetFloat` |
| 最大加速度 | `railMaximumAcceleration` | Float | `SetFloat` / `GetFloat` |
| 回転ばね | `railRotationSpring` | Float | `SetFloat` / `GetFloat` |
| 回転減衰 | `railRotationDamping` | Float | `SetFloat` / `GetFloat` |
| 最大角加速度 | `railMaximumAngularAcceleration` | Float | `SetFloat` / `GetFloat` |
| 推進速度ゲイン | `railEngineSpeedGain` | Float | `SetFloat` / `GetFloat` |
| エンジン加速応答 | `railEngineAccelResponse` | Float | `SetFloat` / `GetFloat` |
| エンジン減速応答 | `railEngineDecelResponse` | Float | `SetFloat` / `GetFloat` |
| 最大前進加速度 | `railEngineMaxAcceleration` | Float | `SetFloat` / `GetFloat` |
| 操舵基本先読み距離(m) | `railSteeringBaseLookAheadDistance` | Float | `SetFloat` / `GetFloat` |
| 操舵先読み時間(秒) | `railSteeringLookAheadTime` | Float | `SetFloat` / `GetFloat` |
| 操舵Yaw強さ | `railSteeringYawGain` | Float | `SetFloat` / `GetFloat` |
| 操舵Yawダンピング | `railSteeringYawDamping` | Float | `SetFloat` / `GetFloat` |
| 最大Yaw角加速度 | `railSteeringMaxYawAngularAcceleration` | Float | `SetFloat` / `GetFloat` |
| 横補助Dead Zone(m) | `railLateralAssistDeadZone` | Float | `SetFloat` / `GetFloat` |
| 横補助開始距離(m) | `railLateralAssistSoftRadius` | Float | `SetFloat` / `GetFloat` |
| 横補助緊急距離(m) | `railLateralAssistEmergencyRadius` | Float | `SetFloat` / `GetFloat` |
| 横補助最大倍率 | `railLateralAssistMaxMultiplier` | Float | `SetFloat` / `GetFloat` |
| 横グリップ強さ | `railHullLateralGripStrength` | Float | `SetFloat` / `GetFloat` |
| 横グリップ最大加速度 | `railHullLateralGripMaxAcceleration` | Float | `SetFloat` / `GetFloat` |
| 横グリップ開始速度(m/s) | `railHullLateralGripMinSpeed` | Float | `SetFloat` / `GetFloat` |
| 横グリップ最大速度(m/s) | `railHullLateralGripFullSpeed` | Float | `SetFloat` / `GetFloat` |
| 横滑りDead Zone速度(m/s) | `railHullLateralGripDeadZoneSpeed` | Float | `SetFloat` / `GetFloat` |
| 横滑り角補助開始角度(度) | `railHullLateralGripSlipStartDegrees` | Float | `SetFloat` / `GetFloat` |
| 横滑り角補助最大角度(度) | `railHullLateralGripSlipFullDegrees` | Float | `SetFloat` / `GetFloat` |
| Mode2 最大合成加速度 | `railMode2MaxCombinedAcceleration` | Float | `SetFloat` / `GetFloat` |
| Rail RideのYawサンプル距離(m) | `railRideYawSampleDistance` | Float | `SetFloat` / `GetFloat` |
| 最大ロール角度 | `railMaximumRollAngle` | Float | `SetFloat` / `GetFloat` |
| ロール復元力 | `railRollRestorationStrength` | Float | `SetFloat` / `GetFloat` |
| ロールダンピング | `railRollDamping` | Float | `SetFloat` / `GetFloat` |
| 最大ピッチ角度 | `railMaximumPitchAngle` | Float | `SetFloat` / `GetFloat` |
| ピッチ復元力 | `railPitchRestorationStrength` | Float | `SetFloat` / `GetFloat` |
| ピッチダンピング | `railPitchDamping` | Float | `SetFloat` / `GetFloat` |
| 最大ヨー角度 | `railMaximumYawAngle` | Float | `SetFloat` / `GetFloat` |
| ヨー復元力 | `railYawRestorationStrength` | Float | `SetFloat` / `GetFloat` |
| ヨーダンピング | `railYawDamping` | Float | `SetFloat` / `GetFloat` |
| Stage1 開始角度(度) | `railYawSafetyStage1Degrees` | Float | `SetFloat` / `GetFloat` |
| Stage2 開始角度(度) | `railYawSafetyStage2Degrees` | Float | `SetFloat` / `GetFloat` |
| Stage4 到達角度(度) | `railYawSafetyStage4Degrees` | Float | `SetFloat` / `GetFloat` |
| 最大復元倍率 | `railYawSafetyMaxRestorationScale` | Float | `SetFloat` / `GetFloat` |
| 最小速度倍率 | `railYawSafetyMinSpeedScale` | Float | `SetFloat` / `GetFloat` |
| Forward Position Error 上限(m) | `railMaxForwardRecoveryError` | Float | `SetFloat` / `GetFloat` |
| Roll Free角度(度) | `railRollFreeDegrees` | Float | `SetFloat` / `GetFloat` |
| Roll Emergency角度(度) | `railRollEmergencyDegrees` | Float | `SetFloat` / `GetFloat` |
| Pitch Free角度(度) | `railPitchFreeDegrees` | Float | `SetFloat` / `GetFloat` |
| Pitch Emergency角度(度) | `railPitchEmergencyDegrees` | Float | `SetFloat` / `GetFloat` |
| Attitude復元力 | `railAttitudeSafetyStrength` | Float | `SetFloat` / `GetFloat` |
| Attitudeダンピング | `railAttitudeSafetyDamping` | Float | `SetFloat` / `GetFloat` |
| Attitude Torque上限 | `railAttitudeSafetyMaxTorque` | Float | `SetFloat` / `GetFloat` |
| 危険時Forward最小倍率 | `railAttitudeSafetyMinForwardScale` | Float | `SetFloat` / `GetFloat` |
| Catchup倍率 | `railPhysicalCatchupSpeedMultiplier` | Float | `SetFloat` / `GetFloat` |
| Pitch最大角度(度) | `railAttitudeAngleLimitMaxPitchDegrees` | Float | `SetFloat` / `GetFloat` |
| Roll最大角度(度) | `railAttitudeAngleLimitMaxRollDegrees` | Float | `SetFloat` / `GetFloat` |
| 押し戻し強さ | `railAttitudeAngleSoftLimitStrength` | Float | `SetFloat` / `GetFloat` |
| 押し戻しダンピング | `railAttitudeAngleSoftLimitDamping` | Float | `SetFloat` / `GetFloat` |
| 押し戻しTorque上限 | `railAttitudeAngleSoftLimitMaxTorque` | Float | `SetFloat` / `GetFloat` |
| PlayerInputを使用 | `railUsePlayerInput` | Bool | `SetBool` / `GetBool` |
| 船体横滑り抑制を使用 | `railHullLateralGripEnabled` | Bool | `SetBool` / `GetBool` |
| Yaw Safety Assistを使用 | `railYawSafetyAssistEnabled` | Bool | `SetBool` / `GetBool` |
| Attitude Safety Assistを使用 | `railAttitudeSafetyAssistEnabled` | Bool | `SetBool` / `GetBool` |
| 絶対角度制限(Hard Clamp)を使用 | `railAttitudeAngleLimitEnabled` | Bool | `SetBool` / `GetBool` |
| 角度ソフト制限を使用 | `railAttitudeAngleSoftLimitEnabled` | Bool | `SetBool` / `GetBool` |
| ループ | `railLoop` | Bool | `SetBool` / `GetBool` |
| 進行方向へ回転 | `railOrientToPath` | Bool | `SetBool` / `GetBool` |
| 滑らかな曲線 | `railUseSmoothCurve` | Bool | `SetBool` / `GetBool` |
| 開始時に停止 | `railStartPaused` | Bool | `SetBool` / `GetBool` |
| 逆方向 | `railReverse` | Bool | `SetBool` / `GetBool` |
| 終端で停止 | `railStopAtEnd` | Bool | `SetBool` / `GetBool` |
| 位置追従軸 | `railPositionInfluence` | Vector3 | `SetVector3` / `GetVector3` |
| 回転追従軸 | `railRotationInfluence` | Vector3 | `SetVector3` / `GetVector3` |
| Yaw誤差 rad | `railDebugYawError` | Float | `SetFloat` / `GetFloat` |
| Rail指令速度 m/s | `railDebugCurrentSpeed` | Float | `SetFloat` / `GetFloat` |
| 実前進速度 m/s | `railDebugActualForwardSpeed` | Float | `SetFloat` / `GetFloat` |
| Gameplay Rail Progress m | `railDebugGameplayRailProgress` | Float | `SetFloat` / `GetFloat` |
| Physical Rail Progress m | `railDebugPhysicalRailProgress` | Float | `SetFloat` / `GetFloat` |
| Physical Target Speed m/s | `railDebugPhysicalTargetSpeed` | Float | `SetFloat` / `GetFloat` |
| Pitch制限中(1=制限) | `railDebugPitchAngleLimited` | Float | `SetFloat` / `GetFloat` |
| Roll制限中(1=制限) | `railDebugRollAngleLimited` | Float | `SetFloat` / `GetFloat` |
| YawSafety 速度倍率 | `railDebugYawSafetySpeedScale` | Float | `SetFloat` / `GetFloat` |
| YawSafety 復元倍率 | `railDebugYawSafetyRestorationScale` | Float | `SetFloat` / `GetFloat` |
| Forward Position Scale(合成) | `railDebugForwardPositionScale` | Float | `SetFloat` / `GetFloat` |
| Forward Position Error(m) | `railDebugForwardPositionError` | Float | `SetFloat` / `GetFloat` |
| Lateral Position Error(m) | `railDebugLateralPositionError` | Float | `SetFloat` / `GetFloat` |
| Forward補正力 N | `railDebugForwardCorrectionForce` | Float | `SetFloat` / `GetFloat` |
| Lateral補正力 N | `railDebugLateralCorrectionForce` | Float | `SetFloat` / `GetFloat` |
| 船体Pitch(度) | `railDebugBoatPitchDegrees` | Float | `SetFloat` / `GetFloat` |
| 船体Roll(度) | `railDebugBoatRollDegrees` | Float | `SetFloat` / `GetFloat` |
| Pitch Safety Factor | `railDebugPitchSafetyFactor` | Float | `SetFloat` / `GetFloat` |
| Roll Safety Factor | `railDebugRollSafetyFactor` | Float | `SetFloat` / `GetFloat` |
| Rail最近傍距離 m | `railDebugClosestRailDistance` | Float | `SetFloat` / `GetFloat` |
| Rail最近傍距離変化量 m | `railDebugClosestRailDistanceDelta` | Float | `SetFloat` / `GetFloat` |
| 操舵先読み距離 m | `railDebugSteeringLookAheadDistance` | Float | `SetFloat` / `GetFloat` |
| 操舵目標Yaw誤差 度 | `railDebugSteeringYawErrorDegrees` | Float | `SetFloat` / `GetFloat` |
| エンジン加速度 m/s2 | `railDebugEngineAcceleration` | Float | `SetFloat` / `GetFloat` |
| 横補助加速度 m/s2 | `railDebugLateralAssistAcceleration` | Float | `SetFloat` / `GetFloat` |
| 横補助倍率 | `railDebugLateralAssistScale` | Float | `SetFloat` / `GetFloat` |
| Yaw角速度 rad/s | `railDebugYawAngularVelocity` | Float | `SetFloat` / `GetFloat` |
| 船首-移動方向差 度 | `railDebugForwardVelocitySlipAngleDegrees` | Float | `SetFloat` / `GetFloat` |
| 水平速度 m/s | `railDebugHorizontalSpeed` | Float | `SetFloat` / `GetFloat` |
| 船体横方向速度 m/s | `railDebugLateralSpeed` | Float | `SetFloat` / `GetFloat` |
| 船体横グリップ加速度 m/s2 | `railDebugHullLateralGripAcceleration` | Float | `SetFloat` / `GetFloat` |
| 船体横グリップ速度倍率 | `railDebugHullLateralGripSpeedFactor` | Float | `SetFloat` / `GetFloat` |
| 船体横グリップSlip倍率 | `railDebugHullLateralGripSlipFactor` | Float | `SetFloat` / `GetFloat` |
| 船体横グリップ最終倍率 | `railDebugHullLateralGripScale` | Float | `SetFloat` / `GetFloat` |
| 合成前加速度 m/s2 | `railDebugPreClampAcceleration` | Float | `SetFloat` / `GetFloat` |
| 合成後加速度 m/s2 | `railDebugPostClampAcceleration` | Float | `SetFloat` / `GetFloat` |
| Mode2最終Clamp倍率 | `railDebugMode2ClampScale` | Float | `SetFloat` / `GetFloat` |
| Rail位置誤差XZ m | `railDebugRailRidePositionErrorXZ` | Float | `SetFloat` / `GetFloat` |
| Rail Target Yaw 度 | `railDebugRailRideTargetYawDegrees` | Float | `SetFloat` / `GetFloat` |
| PlayerShip最終Yaw 度 | `railDebugRailRideFinalYawDegrees` | Float | `SetFloat` / `GetFloat` |
| Rail-Yaw誤差 度 | `railDebugRailRideYawErrorDegrees` | Float | `SetFloat` / `GetFloat` |
| 速度方向-Rail方向差 度 | `railDebugRailRideVelocityDirectionErrorDegrees` | Float | `SetFloat` / `GetFloat` |
| Physics Y | `railDebugRailRidePhysicsY` | Float | `SetFloat` / `GetFloat` |
| 最終Y | `railDebugRailRideFinalY` | Float | `SetFloat` / `GetFloat` |
| Physics Pitch 度 | `railDebugRailRidePhysicsPitchDegrees` | Float | `SetFloat` / `GetFloat` |
| 最終Pitch 度 | `railDebugRailRideFinalPitchDegrees` | Float | `SetFloat` / `GetFloat` |
| Physics Roll 度 | `railDebugRailRidePhysicsRollDegrees` | Float | `SetFloat` / `GetFloat` |
| 最終Roll 度 | `railDebugRailRideFinalRollDegrees` | Float | `SetFloat` / `GetFloat` |
| 追従Force | `railDebugFollowForce` | Vector3 | `SetVector3` / `GetVector3` |
| 追従Torque | `railDebugFollowTorque` | Vector3 | `SetVector3` / `GetVector3` |
| 位置誤差 | `railDebugPositionError` | Vector3 | `SetVector3` / `GetVector3` |
| Attitude Recovery Torque | `railDebugAttitudeRecoveryTorque` | Vector3 | `SetVector3` / `GetVector3` |
| 船体計算上Forward | `railDebugShipForward` | Vector3 | `SetVector3` / `GetVector3` |
| 船体計算上Right | `railDebugShipRight` | Vector3 | `SetVector3` / `GetVector3` |
| Rail固定位置 | `railDebugRailRidePosition` | Vector3 | `SetVector3` / `GetVector3` |
| 実PlayerShip位置 | `railDebugRailRideActualPosition` | Vector3 | `SetVector3` / `GetVector3` |
| Rail接線Forward | `railDebugRailRideForward` | Vector3 | `SetVector3` / `GetVector3` |
| Rail Velocity XZ | `railDebugRailRideVelocityXZ` | Vector3 | `SetVector3` / `GetVector3` |
| Rigidbody Velocity XZ | `railDebugRailRideActualVelocityXZ` | Vector3 | `SetVector3` / `GetVector3` |
| 適用中 位置追従軸 | `railDebugAppliedPositionInfluence` | Vector3 | `SetVector3` / `GetVector3` |
| 適用中 回転追従軸 | `railDebugAppliedRotationInfluence` | Vector3 | `SetVector3` / `GetVector3` |
| Rail Path | `railPathGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### Health

Inspector表示名「体力」／カテゴリ「ゲームプレイ」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 最大体力 | `healthMaximum` | Float | `SetFloat` / `GetFloat` |

#### SceneButton

Inspector表示名「Scene ボタン」／カテゴリ「UI」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 操作可能 | `buttonInteractable` | Bool | `SetBool` / `GetBool` |

#### Foliage

Inspector表示名「フォリッジ」／カテゴリ「地形・タイルマップ」／公開Field 8件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 密度 | `intensity` | Float | `SetFloat` / `GetFloat` |
| LOD距離 | `colliderRadius` | Float | `SetFloat` / `GetFloat` |
| 揺れ幅 | `oceanWaveHeight` | Float | `SetFloat` / `GetFloat` |
| 風速 | `oceanWindSpeed` | Float | `SetFloat` / `GetFloat` |
| 空間周波数 | `oceanWaveLength` | Float | `SetFloat` / `GetFloat` |
| 時間倍率 | `oceanTimeScale` | Float | `SetFloat` / `GetFloat` |
| 最大Instance数 | `particleMaxCount` | Int | `SetInt` / `GetInt` |
| 配置範囲 | `colliderSize` | Vector3 | `SetVector3` / `GetVector3` |

#### WaveSpawner

Inspector表示名「ウェーブ生成」／カテゴリ「ゲームプレイ」／公開Field 14件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 編隊間隔 | `waveFormationSpacing` | Float | `SetFloat` / `GetFloat` |
| レール開始進行率 | `waveSpawnRailStartNormalized` | Float | `SetFloat` / `GetFloat` |
| 開始進行率 | `waveTriggerValue` | Float | `SetFloat` / `GetFloat` |
| 生成間隔 | `waveSpawnInterval` | Float | `SetFloat` / `GetFloat` |
| 生成数(総数) | `waveSpawnCount` | Int | `SetInt` / `GetInt` |
| 同時存在目標数(0=一括生成) | `waveTargetAliveCount` | Int | `SetInt` / `GetInt` |
| 1Frame最大生成数 | `waveSpawnMaximumPerFrame` | Int | `SetInt` / `GetInt` |
| グリッド列数 | `waveFormationColumns` | Int | `SetInt` / `GetInt` |
| 開始時に子を待機 | `waveDeactivateChildrenOnStart` | Bool | `SetBool` / `GetBool` |
| ObjectPool | `wavePoolGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| 生成基準位置 | `waveSpawnPointGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| 補充SpawnPointSet | `waveSpawnPointSetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| 進行率 Source | `waveTriggerSourceGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| Action 対象 | `waveActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### TimelineEvent

Inspector表示名「タイムラインイベント」／カテゴリ「入力・イベント」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 発火進行率 | `timelineTriggerValue` | Float | `SetFloat` / `GetFloat` |
| 一度だけ | `timelineTriggerOnce` | Bool | `SetBool` / `GetBool` |
| 進行率 Source | `timelineSourceGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| Action 対象 | `timelineTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### ThresholdState

Inspector表示名「しきい値状態」／カテゴリ「入力・イベント」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| State 2 境界 | `thresholdSecondValue` | Float | `SetFloat` / `GetFloat` |
| State 3 境界 | `thresholdThirdValue` | Float | `SetFloat` / `GetFloat` |
| Source Object | `thresholdSourceGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| Action 対象 | `thresholdTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### UIValueBinding

Inspector表示名「値バインディング」／カテゴリ「UI」／公開Field 3件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 表示倍率 | `uiBindingScale` | Float | `SetFloat` / `GetFloat` |
| 小数桁 | `uiBindingPrecision` | Int | `SetInt` / `GetInt` |
| Source Object | `uiBindingSourceGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### Aerodynamics

Inspector表示名「空気力学」／カテゴリ「3D物理」／公開Field 15件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 空気密度 kg/m3 | `aerodynamicAirDensity` | Float | `SetFloat` / `GetFloat` |
| 抗力係数 Cd | `aerodynamicDragCoefficient` | Float | `SetFloat` / `GetFloat` |
| 代表面積 m2 | `aerodynamicReferenceArea` | Float | `SetFloat` / `GetFloat` |
| 基礎揚力係数 | `aerodynamicBaseLiftCoefficient` | Float | `SetFloat` / `GetFloat` |
| 揚力傾斜 /rad | `aerodynamicLiftSlope` | Float | `SetFloat` / `GetFloat` |
| 翼面積 m2 | `aerodynamicLiftArea` | Float | `SetFloat` / `GetFloat` |
| ゼロ揚力迎角 deg | `aerodynamicZeroLiftAngleDegrees` | Float | `SetFloat` / `GetFloat` |
| 失速迎角 deg | `aerodynamicStallAngleDegrees` | Float | `SetFloat` / `GetFloat` |
| 横力係数 | `aerodynamicSideForceCoefficient` | Float | `SetFloat` / `GetFloat` |
| 側面積 m2 | `aerodynamicSideArea` | Float | `SetFloat` / `GetFloat` |
| 回転抗力係数 | `aerodynamicAngularDragCoefficient` | Float | `SetFloat` / `GetFloat` |
| Magnus 係数 | `aerodynamicMagnusCoefficient` | Float | `SetFloat` / `GetFloat` |
| 合力上限 N | `aerodynamicMaximumForce` | Float | `SetFloat` / `GetFloat` |
| 基礎風速 m/s | `aerodynamicAmbientWindVelocity` | Vector3 | `SetVector3` / `GetVector3` |
| 圧力中心 | `aerodynamicCenterOfPressure` | Vector3 | `SetVector3` / `GetVector3` |

#### WindZone

Inspector表示名「風ゾーン」／カテゴリ「3D物理」／公開Field 5件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 風速 m/s | `windZoneSpeed` | Float | `SetFloat` / `GetFloat` |
| 影響半径 m | `windZoneRadius` | Float | `SetFloat` / `GetFloat` |
| 乱流速度 m/s | `windZoneTurbulenceStrength` | Float | `SetFloat` / `GetFloat` |
| 乱流周波数 | `windZoneTurbulenceFrequency` | Float | `SetFloat` / `GetFloat` |
| 風向 | `windZoneDirection` | Vector3 | `SetVector3` / `GetVector3` |

#### GravityField

Inspector表示名「重力場」／カテゴリ「3D物理」／公開Field 5件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 引力源質量 kg | `gravityFieldSourceMass` | Float | `SetFloat` / `GetFloat` |
| 加速度 m/s2 | `gravityFieldAcceleration` | Float | `SetFloat` / `GetFloat` |
| 最小計算距離 m | `gravityFieldMinimumDistance` | Float | `SetFloat` / `GetFloat` |
| 影響半径 m | `gravityFieldInfluenceRadius` | Float | `SetFloat` / `GetFloat` |
| 加速度上限 m/s2 | `gravityFieldMaximumAcceleration` | Float | `SetFloat` / `GetFloat` |

#### RotatingFrame

Inspector表示名「回転座標系」／カテゴリ「3D物理」／公開Field 5件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 影響半径 m | `rotatingFrameRadius` | Float | `SetFloat` / `GetFloat` |
| 加速度上限 m/s2 | `rotatingFrameMaximumAcceleration` | Float | `SetFloat` / `GetFloat` |
| 角速度 rad/s | `rotatingFrameAngularVelocity` | Vector3 | `SetVector3` / `GetVector3` |
| 角加速度 rad/s2 | `rotatingFrameAngularAcceleration` | Vector3 | `SetVector3` / `GetVector3` |
| 中心の速度 m/s | `rotatingFrameLinearVelocity` | Vector3 | `SetVector3` / `GetVector3` |

#### FluidVolume

Inspector表示名「流体ボリューム」／カテゴリ「3D物理」／公開Field 7件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 密度 kg/m3 | `fluidDensity` | Float | `SetFloat` / `GetFloat` |
| 粘性 Pa*s | `fluidDynamicViscosity` | Float | `SetFloat` / `GetFloat` |
| 二次抗力係数 | `fluidDragCoefficient` | Float | `SetFloat` / `GetFloat` |
| 角粘性 | `fluidAngularViscosity` | Float | `SetFloat` / `GetFloat` |
| 合力上限 N | `fluidMaximumForce` | Float | `SetFloat` / `GetFloat` |
| サイズ m | `fluidVolumeSize` | Vector3 | `SetVector3` / `GetVector3` |
| 流速 m/s | `fluidFlowVelocity` | Vector3 | `SetVector3` / `GetVector3` |

#### SpringForce

Inspector表示名「ばね力」／カテゴリ「3D物理」／公開Field 9件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 自然長 m | `springForceRestLength` | Float | `SetFloat` / `GetFloat` |
| ばね定数 N/m | `springForceStiffness` | Float | `SetFloat` / `GetFloat` |
| 減衰 Ns/m | `springForceDamping` | Float | `SetFloat` / `GetFloat` |
| Force上限 N | `springForceMaximumForce` | Float | `SetFloat` / `GetFloat` |
| 接続先へ反作用 | `springForceApplyReaction` | Bool | `SetBool` / `GetBool` |
| 所有者Anchor | `springForceLocalAnchor` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先Anchor | `springForceTargetLocalAnchor` | Vector3 | `SetVector3` / `GetVector3` |
| World固定点 | `springForceWorldAnchor` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先 | `springForceTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### ElectromagneticBody

Inspector表示名「電磁気ボディ」／カテゴリ「3D物理」／公開Field 3件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| Force上限 N | `electromagneticMaximumForce` | Float | `SetFloat` / `GetFloat` |
| Torque上限 N*m | `electromagneticMaximumTorque` | Float | `SetFloat` / `GetFloat` |
| 磁気Moment A*m2 | `electromagneticMagneticMoment` | Vector3 | `SetVector3` / `GetVector3` |

#### ElectromagneticField

Inspector表示名「電磁場」／カテゴリ「3D物理」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 最小計算距離 m | `electromagneticMinimumDistance` | Float | `SetFloat` / `GetFloat` |
| 影響半径 m | `electromagneticInfluenceRadius` | Float | `SetFloat` / `GetFloat` |
| 電場 E N/C | `electromagneticElectricField` | Vector3 | `SetVector3` / `GetVector3` |
| 磁束密度 B T | `electromagneticMagneticField` | Vector3 | `SetVector3` / `GetVector3` |

#### ScreenAim

Inspector表示名「画面照準」／カテゴリ「入力・イベント」／公開Field 5件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 移動速度 | `screenAimSpeed` | Float | `SetFloat` / `GetFloat` |
| Y軸反転 | `screenAimInvertY` | Bool | `SetBool` / `GetBool` |
| 画面内に制限 | `screenAimClamp` | Bool | `SetBool` / `GetBool` |
| 入力Object | `screenAimInputGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| 照準UI | `screenAimReticleGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### HitscanWeapon

Inspector表示名「レイ射撃」／カテゴリ「ゲームプレイ」／公開Field 8件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 射程 | `hitscanRange` | Float | `SetFloat` / `GetFloat` |
| ダメージ | `hitscanDamage` | Float | `SetFloat` / `GetFloat` |
| 発射間隔 | `hitscanInterval` | Float | `SetFloat` / `GetFloat` |
| 押下中に連射 | `hitscanAutomatic` | Bool | `SetBool` / `GetBool` |
| FFT水面へ命中 | `hitscanOceanCollision` | Bool | `SetBool` / `GetBool` |
| 画面照準 | `hitscanAimGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| 入力Object | `hitscanInputGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| Action対象 | `hitscanActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### ProjectileEmitter

Inspector表示名「弾発射」／カテゴリ「ゲームプレイ」／公開Field 30件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 速度 | `projectileSpeed` | Float | `SetFloat` / `GetFloat` |
| ダメージ | `projectileDamage` | Float | `SetFloat` / `GetFloat` |
| 判定半径 | `projectileRadius` | Float | `SetFloat` / `GetFloat` |
| 発射位置の安全距離 | `projectileSpawnClearance` | Float | `SetFloat` / `GetFloat` |
| 寿命 | `projectileLifetime` | Float | `SetFloat` / `GetFloat` |
| 発射間隔 | `projectileInterval` | Float | `SetFloat` / `GetFloat` |
| 並進速度継承 | `projectileLinearVelocityInheritance` | Float | `SetFloat` / `GetFloat` |
| 角速度継承 | `projectileAngularVelocityInheritance` | Float | `SetFloat` / `GetFloat` |
| 可変速度: 最短飛行時間(距離依存時) | `projectileVariableSpeedMinimumFlightTime` | Float | `SetFloat` / `GetFloat` |
| 可変速度: 最長飛行時間(距離依存時) | `projectileVariableSpeedMaximumFlightTime` | Float | `SetFloat` / `GetFloat` |
| 可変速度: 距離÷この値=飛行時間(距離依存時) | `projectileVariableSpeedDistanceFactor` | Float | `SetFloat` / `GetFloat` |
| 可変速度: 固定飛行時間 | `projectileVariableSpeedFixedFlightTime` | Float | `SetFloat` / `GetFloat` |
| 可変速度: 俯角(度) | `projectileVariableSpeedDepressionAngleDegrees` | Float | `SetFloat` / `GetFloat` |
| 可変速度: 弧の高さ | `projectileVariableSpeedArcHeight` | Float | `SetFloat` / `GetFloat` |
| 曳光弾: 長さ倍率 | `projectileTracerLengthScale` | Float | `SetFloat` / `GetFloat` |
| 曳光弾: 最小長さ | `projectileTracerMinimumLength` | Float | `SetFloat` / `GetFloat` |
| 曳光弾: 太さ | `projectileTracerThickness` | Float | `SetFloat` / `GetFloat` |
| 押下中に連射 | `projectileAutomatic` | Bool | `SetBool` / `GetBool` |
| FFT水面へ命中 | `projectileOceanCollision` | Bool | `SetBool` / `GetBool` |
| 発射元速度を継承 | `projectileInheritSourceVelocity` | Bool | `SetBool` / `GetBool` |
| 親Rigidbodyを検索 | `projectileUseParentRigidBody` | Bool | `SetBool` / `GetBool` |
| 曳光弾ストレッチ表示 | `projectileTracerStretchEnabled` | Bool | `SetBool` / `GetBool` |
| Hitscanで即ダメージ解決(この弾は演出専用) | `projectileHitscanResolution` | Bool | `SetBool` / `GetBool` |
| 照準/Selector | `projectileAimGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| 弾道予測/可変速度Target | `projectileBallisticPredictionGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| 入力Object | `projectileInputGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| 弾ObjectPool | `projectilePoolGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| 発射位置 | `projectileSpawnPointGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| 速度Source | `projectileSourceVelocityGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| Action対象 | `projectileActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### DamageReceiver

Inspector表示名「ダメージ受信」／カテゴリ「ゲームプレイ」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| ダメージ倍率 | `damageMultiplier` | Float | `SetFloat` / `GetFloat` |
| 無敵時間 | `damageInvulnerabilitySeconds` | Float | `SetFloat` / `GetFloat` |
| 死亡時に無効化 | `damageDeactivateOnDeath` | Bool | `SetBool` / `GetBool` |
| Action対象 | `damageActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### ObjectPool

Inspector表示名「オブジェクトプール」／カテゴリ「ゲームプレイ」／公開Field 3件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 遅延生成容量 | `objectPoolInitialSize` | Int | `SetInt` / `GetInt` |
| 容量不足時に拡張 | `objectPoolAllowExpand` | Bool | `SetBool` / `GetBool` |
| Template | `objectPoolTemplateGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### PrefabSpawner

Inspector表示名「プレハブ生成」／カテゴリ「ゲームプレイ」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 生成間隔 | `prefabSpawnerInterval` | Float | `SetFloat` / `GetFloat` |
| ObjectPool | `prefabSpawnerPoolGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| 生成位置 | `prefabSpawnerPointGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| Action対象 | `prefabSpawnerActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### CameraBlend

Inspector表示名「カメラブレンド」／カテゴリ「カメラ」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 時間 | `cameraBlendDuration` | Float | `SetFloat` / `GetFloat` |
| Play開始時に再生 | `cameraBlendPlayOnStart` | Bool | `SetBool` / `GetBool` |
| 開始Camera | `cameraBlendSourceGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| 終了Camera | `cameraBlendTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### CameraShake

Inspector表示名「カメラシェイク」／カテゴリ「カメラ」／公開Field 6件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 周波数 | `cameraShakeFrequency` | Float | `SetFloat` / `GetFloat` |
| 時間 | `cameraShakeDuration` | Float | `SetFloat` / `GetFloat` |
| Priority | `cameraShakePriority` | Int | `SetInt` / `GetInt` |
| Play開始時に再生 | `cameraShakePlayOnStart` | Bool | `SetBool` / `GetBool` |
| 位置振幅 | `cameraShakePositionAmplitude` | Vector3 | `SetVector3` / `GetVector3` |
| 回転振幅 | `cameraShakeRotationAmplitude` | Vector3 | `SetVector3` / `GetVector3` |

#### RailBranch

Inspector表示名「レール分岐」／カテゴリ「ゲームプレイ」／公開Field 6件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 切替進行率 | `railBranchTriggerNormalized` | Float | `SetFloat` / `GetFloat` |
| 進行率を維持 | `railBranchPreserveProgress` | Bool | `SetBool` / `GetBool` |
| 一度だけ | `railBranchTriggerOnce` | Bool | `SetBool` / `GetBool` |
| RailFollower | `railBranchFollowerGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| 切替先Rail Path | `railBranchTargetPathGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| Action対象 | `railBranchActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### ActionSequence

Inspector表示名「アクションシーケンス」／カテゴリ「入力・イベント」／公開Field 2件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| Play開始時に再生 | `actionSequencePlayOnStart` | Bool | `SetBool` / `GetBool` |
| ループ | `actionSequenceLoop` | Bool | `SetBool` / `GetBool` |

#### ActionSequenceStep

Inspector表示名「シーケンスステップ」／カテゴリ「入力・イベント」／公開Field 8件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 待機秒 | `actionSequenceWaitSeconds` | Float | `SetFloat` / `GetFloat` |
| 比較値 | `actionSequenceCompareValue` | Float | `SetFloat` / `GetFloat` |
| 並列Group (-1=順次) | `actionSequenceParallelGroup` | Int | `SetInt` / `GetInt` |
| true移動先Index | `actionSequenceTrueStepIndex` | Int | `SetInt` / `GetInt` |
| false移動先Index | `actionSequenceFalseStepIndex` | Int | `SetInt` / `GetInt` |
| Active | `actionSequenceActiveValue` | Bool | `SetBool` / `GetBool` |
| Additive読込 | `actionSequenceSceneAdditive` | Bool | `SetBool` / `GetBool` |
| Action対象 | `actionSequenceTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### Saveable

Inspector表示名「保存対象」／カテゴリ「ゲームプレイ」／公開Field 5件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| Transform | `saveableTransform` | Bool | `SetBool` / `GetBool` |
| Active | `saveableActive` | Bool | `SetBool` / `GetBool` |
| Health | `saveableHealth` | Bool | `SetBool` / `GetBool` |
| Rigidbody | `saveableRigidbody` | Bool | `SetBool` / `GetBool` |
| C++ Script公開値 | `saveableScriptProperties` | Bool | `SetBool` / `GetBool` |

#### Checkpoint

Inspector表示名「チェックポイント」／カテゴリ「ゲームプレイ」／公開Field 3件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| Play開始時に保存 | `checkpointSaveOnStart` | Bool | `SetBool` / `GetBool` |
| Play開始時に読込 | `checkpointLoadOnStart` | Bool | `SetBool` / `GetBool` |
| 完了Action対象 | `checkpointActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### RopeConstraint

Inspector表示名「ロープ拘束」／カテゴリ「3D物理」／公開Field 10件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 最大長 m | `ropeMaximumLength` | Float | `SetFloat` / `GetFloat` |
| 張力係数 N/m | `ropeStiffness` | Float | `SetFloat` / `GetFloat` |
| 減衰 Ns/m | `ropeDamping` | Float | `SetFloat` / `GetFloat` |
| 張力上限 N | `ropeMaximumTension` | Float | `SetFloat` / `GetFloat` |
| 破断張力 N | `ropeBreakingTension` | Float | `SetFloat` / `GetFloat` |
| 接続先へ反作用 | `ropeApplyReaction` | Bool | `SetBool` / `GetBool` |
| 所有者Anchor | `ropeLocalAnchor` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先Anchor | `ropeTargetLocalAnchor` | Vector3 | `SetVector3` / `GetVector3` |
| World固定点 | `ropeWorldAnchor` | Vector3 | `SetVector3` / `GetVector3` |
| 接続先 | `ropeTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### TorsionSpring

Inspector表示名「ねじりばね」／カテゴリ「3D物理」／公開Field 5件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| ばね定数 N*m/rad | `torsionStiffness` | Float | `SetFloat` / `GetFloat` |
| 減衰 N*m*s/rad | `torsionDamping` | Float | `SetFloat` / `GetFloat` |
| Torque上限 N*m | `torsionMaximumTorque` | Float | `SetFloat` / `GetFloat` |
| 接続先へ反作用 | `torsionApplyReaction` | Bool | `SetBool` / `GetBool` |
| 基準Object | `torsionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### WeaponLoadout

Inspector表示名「武器ロードアウト」／カテゴリ「ゲームプレイ」／公開Field 2件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 選択Slot | `weaponLoadoutSelectedSlotIndex` | Int | `SetInt` / `GetInt` |
| Action対象 | `weaponLoadoutActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### WeaponLoadoutSlot

Inspector表示名「武器スロット」／カテゴリ「ゲームプレイ」／公開Field 7件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| Reload秒 | `weaponSlotReloadSeconds` | Float | `SetFloat` / `GetFloat` |
| 現在弾数 | `weaponSlotCurrentAmmo` | Int | `SetInt` / `GetInt` |
| 予備弾 (-1=無限) | `weaponSlotReserveAmmo` | Int | `SetInt` / `GetInt` |
| 最大弾数 | `weaponSlotMaximumAmmo` | Int | `SetInt` / `GetInt` |
| 空で自動Reload | `weaponSlotAutoReload` | Bool | `SetBool` / `GetBool` |
| Weapon Object | `weaponSlotWeaponGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| Visual Object | `weaponSlotVisualGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### TargetSelector

Inspector表示名「ターゲット選択」／カテゴリ「ゲームプレイ」／公開Field 11件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 最大距離 | `targetSelectorMaximumDistance` | Float | `SetFloat` / `GetFloat` |
| 最大角度 | `targetSelectorMaximumAngle` | Float | `SetFloat` / `GetFloat` |
| Ocean Clearance | `targetSelectorOceanClearance` | Float | `SetFloat` / `GetFloat` |
| 検索Layer (-1=全て) | `targetSelectorSearchLayer` | Int | `SetInt` / `GetInt` |
| 最大候補数 | `targetSelectorMaximumTargets` | Int | `SetInt` / `GetInt` |
| 指定Team ID | `targetSelectorSpecificTeamId` | Int | `SetInt` / `GetInt` |
| 現在Target ID | `targetSelectorCurrentTargetGameObjectId` | Int | `SetInt` / `GetInt` |
| 遮蔽判定 | `targetSelectorOcclusionCheck` | Bool | `SetBool` / `GetBool` |
| Neutralを含む | `targetSelectorIncludeNeutral` | Bool | `SetBool` / `GetBool` |
| 基準Object | `targetSelectorReferenceGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| Action対象 | `targetSelectorActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### TargetSteering

Inspector表示名「ターゲット追従」／カテゴリ「ゲームプレイ」／公開Field 17件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 旋回速度 deg/s | `targetSteeringTurnSpeed` | Float | `SetFloat` / `GetFloat` |
| 加速度 | `targetSteeringAcceleration` | Float | `SetFloat` / `GetFloat` |
| 最大速度 | `targetSteeringMaximumSpeed` | Float | `SetFloat` / `GetFloat` |
| 開始Delay | `targetSteeringStartDelay` | Float | `SetFloat` / `GetFloat` |
| 予測秒 | `targetSteeringPredictionSeconds` | Float | `SetFloat` / `GetFloat` |
| 横Offset (右+/左-) | `targetSteeringSideOffset` | Float | `SetFloat` / `GetFloat` |
| 前後Offset | `targetSteeringForwardOffset` | Float | `SetFloat` / `GetFloat` |
| 高さOffset | `targetSteeringVerticalOffset` | Float | `SetFloat` / `GetFloat` |
| 目標距離 | `targetSteeringTargetDistance` | Float | `SetFloat` / `GetFloat` |
| 距離Margin | `targetSteeringDistanceMargin` | Float | `SetFloat` / `GetFloat` |
| 相対位置追従速度 (0=最大速度) | `targetSteeringPositionLerpSpeed` | Float | `SetFloat` / `GetFloat` |
| 継続秒 (0=無期限) | `targetSteeringDuration` | Float | `SetFloat` / `GetFloat` |
| 開始Offset | `targetSteeringStartOffset` | Vector3 | `SetVector3` / `GetVector3` |
| 終了Offset | `targetSteeringEndOffset` | Vector3 | `SetVector3` / `GetVector3` |
| 明示Target | `targetSteeringTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| TargetSelector | `targetSteeringSelectorGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| 完了Action対象 | `targetSteeringActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### MovementModifier

Inspector表示名「移動補正」／カテゴリ「ゲームプレイ」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 入力追従速度 | `movementModifierInputSpeed` | Float | `SetFloat` / `GetFloat` |
| 位置Offset | `movementModifierLocalPositionOffset` | Vector3 | `SetVector3` / `GetVector3` |
| 回転Offset deg | `movementModifierLocalRotationOffset` | Vector3 | `SetVector3` / `GetVector3` |
| PlayerInput | `movementModifierInputGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### PropertyTween

Inspector表示名「プロパティ補間」／カテゴリ「入力・イベント」／公開Field 7件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 時間 | `propertyTweenDuration` | Float | `SetFloat` / `GetFloat` |
| Play開始時に再生 | `propertyTweenPlayOnStart` | Bool | `SetBool` / `GetBool` |
| ループ | `propertyTweenLoop` | Bool | `SetBool` / `GetBool` |
| 開始値 | `propertyTweenStartValue` | Vector3 | `SetVector3` / `GetVector3` |
| 終了値 | `propertyTweenEndValue` | Vector3 | `SetVector3` / `GetVector3` |
| 対象 | `propertyTweenTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| 完了Action対象 | `propertyTweenActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### ActionRelay

Inspector表示名「アクション中継」／カテゴリ「入力・イベント」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| Play開始時にRelay | `actionRelayOnStart` | Bool | `SetBool` / `GetBool` |

#### ActionRelayTarget

Inspector表示名「中継先」／カテゴリ「入力・イベント」／公開Field 2件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 有効 | `actionRelayTargetEnabled` | Bool | `SetBool` / `GetBool` |
| Action対象 | `actionRelayTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### Thruster

Inspector表示名「推進力」／カテゴリ「3D物理」／公開Field 5件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 推進力 N | `thrusterForce` | Float | `SetFloat` / `GetFloat` |
| スロットル | `thrusterThrottle` | Float | `SetFloat` / `GetFloat` |
| ローカル方向を使用 | `thrusterUseLocalDirection` | Bool | `SetBool` / `GetBool` |
| 推進方向 | `thrusterDirection` | Vector3 | `SetVector3` / `GetVector3` |
| ローカル作用点 | `thrusterLocalApplicationPoint` | Vector3 | `SetVector3` / `GetVector3` |

#### PulleyConstraint

Inspector表示名「滑車拘束」／カテゴリ「3D物理」／公開Field 11件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 全長 m | `pulleyTotalLength` | Float | `SetFloat` / `GetFloat` |
| 滑車比 | `pulleyRatio` | Float | `SetFloat` / `GetFloat` |
| 張力係数 N/m | `pulleyStiffness` | Float | `SetFloat` / `GetFloat` |
| 減衰 Ns/m | `pulleyDamping` | Float | `SetFloat` / `GetFloat` |
| 張力上限 N | `pulleyMaximumTension` | Float | `SetFloat` / `GetFloat` |
| 破断張力 N | `pulleyBreakingTension` | Float | `SetFloat` / `GetFloat` |
| 所有者Anchor | `pulleyOwnerLocalAnchor` | Vector3 | `SetVector3` / `GetVector3` |
| 反対側Anchor | `pulleyTargetLocalAnchor` | Vector3 | `SetVector3` / `GetVector3` |
| 所有者側支持点 | `pulleyOwnerWorldSupport` | Vector3 | `SetVector3` / `GetVector3` |
| 反対側支持点 | `pulleyTargetWorldSupport` | Vector3 | `SetVector3` / `GetVector3` |
| 反対側Object | `pulleyTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### PhysicsServo

Inspector表示名「物理サーボ」／カテゴリ「3D物理」／公開Field 8件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 位置ばね N/m | `servoPositionStiffness` | Float | `SetFloat` / `GetFloat` |
| 速度減衰 Ns/m | `servoPositionDamping` | Float | `SetFloat` / `GetFloat` |
| Force上限 N | `servoMaximumForce` | Float | `SetFloat` / `GetFloat` |
| 回転ばね N*m/rad | `servoRotationStiffness` | Float | `SetFloat` / `GetFloat` |
| 角速度減衰 | `servoRotationDamping` | Float | `SetFloat` / `GetFloat` |
| Torque上限 N*m | `servoMaximumTorque` | Float | `SetFloat` / `GetFloat` |
| 追従先へ反作用 | `servoApplyReaction` | Bool | `SetBool` / `GetBool` |
| 追従先 | `servoTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### VortexField

Inspector表示名「渦流場」／カテゴリ「3D物理」／公開Field 7件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 影響半径 m | `vortexRadius` | Float | `SetFloat` / `GetFloat` |
| 角速度 rad/s | `vortexAngularVelocity` | Float | `SetFloat` / `GetFloat` |
| 中心流入速度 m/s | `vortexRadialInflowVelocity` | Float | `SetFloat` / `GetFloat` |
| 軸方向速度 m/s | `vortexAxialVelocity` | Float | `SetFloat` / `GetFloat` |
| 速度結合率 1/s | `vortexVelocityCoupling` | Float | `SetFloat` / `GetFloat` |
| 加速度上限 m/s2 | `vortexMaximumAcceleration` | Float | `SetFloat` / `GetFloat` |
| ローカル渦軸 | `vortexAxis` | Vector3 | `SetVector3` / `GetVector3` |

#### PressureField

Inspector表示名「圧力場」／カテゴリ「3D物理」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 圧力 Pa | `pressureFieldPressure` | Float | `SetFloat` / `GetFloat` |
| 影響半径 m | `pressureFieldRadius` | Float | `SetFloat` / `GetFloat` |
| 減衰指数 | `pressureFieldFalloffExponent` | Float | `SetFloat` / `GetFloat` |
| Force上限 N | `pressureFieldMaximumForce` | Float | `SetFloat` / `GetFloat` |

#### Suspension

Inspector表示名「サスペンション」／カテゴリ「3D物理」／公開Field 10件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 自然長 m | `suspensionRestLength` | Float | `SetFloat` / `GetFloat` |
| 最大伸長 m | `suspensionMaximumLength` | Float | `SetFloat` / `GetFloat` |
| 車輪半径 m | `suspensionWheelRadius` | Float | `SetFloat` / `GetFloat` |
| ばね定数 N/m | `suspensionStiffness` | Float | `SetFloat` / `GetFloat` |
| 減衰 Ns/m | `suspensionDamping` | Float | `SetFloat` / `GetFloat` |
| Force上限 N | `suspensionMaximumForce` | Float | `SetFloat` / `GetFloat` |
| 接地法線へForce | `suspensionUseHitNormal` | Bool | `SetBool` / `GetBool` |
| 接地物へ反作用 | `suspensionApplyReaction` | Bool | `SetBool` / `GetBool` |
| ローカル取付点 | `suspensionLocalAnchor` | Vector3 | `SetVector3` / `GetVector3` |
| ローカル接地方向 | `suspensionLocalDirection` | Vector3 | `SetVector3` / `GetVector3` |

#### UprightStabilizer

Inspector表示名「姿勢安定化」／カテゴリ「3D物理」／公開Field 5件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 姿勢ばね N*m/rad | `uprightStiffness` | Float | `SetFloat` / `GetFloat` |
| 角速度減衰 | `uprightDamping` | Float | `SetFloat` / `GetFloat` |
| Torque上限 N*m | `uprightMaximumTorque` | Float | `SetFloat` / `GetFloat` |
| ローカル上方向 | `uprightLocalUpAxis` | Vector3 | `SetVector3` / `GetVector3` |
| 目標World上方向 | `uprightTargetWorldUp` | Vector3 | `SetVector3` / `GetVector3` |

#### TargetPoint

Inspector表示名「ターゲットポイント」／カテゴリ「ゲームプレイ」／公開Field 3件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 優先値 | `targetPointPriority` | Float | `SetFloat` / `GetFloat` |
| 注視半径 | `targetPointRadius` | Float | `SetFloat` / `GetFloat` |
| 注視Offset | `targetPointAimOffset` | Vector3 | `SetVector3` / `GetVector3` |

#### Team

Inspector表示名「チーム」／カテゴリ「ゲームプレイ」／公開Field 2件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| Team ID | `teamId` | Int | `SetInt` / `GetInt` |
| Target可能 | `teamTargetable` | Bool | `SetBool` / `GetBool` |

#### Timer

Inspector表示名「タイマー」／カテゴリ「入力・イベント」／公開Field 5件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 時間 | `timerDuration` | Float | `SetFloat` / `GetFloat` |
| 繰り返す | `timerRepeat` | Bool | `SetBool` / `GetBool` |
| Play開始時に再生 | `timerPlayOnStart` | Bool | `SetBool` / `GetBool` |
| 一時停止 | `timerPaused` | Bool | `SetBool` / `GetBool` |
| Action対象 | `timerActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### GenericStateMachine

Inspector表示名「汎用ステートマシン」／カテゴリ「ゲームプレイ」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| Action対象 | `stateMachineActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### Attribute

Inspector表示名「属性・リソース」／カテゴリ「ゲームプレイ」／公開Field 5件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 最小 | `attributeMinimum` | Float | `SetFloat` / `GetFloat` |
| 最大 | `attributeMaximum` | Float | `SetFloat` / `GetFloat` |
| 現在 | `attributeCurrent` | Float | `SetFloat` / `GetFloat` |
| 毎秒回復 | `attributeRegenerationPerSecond` | Float | `SetFloat` / `GetFloat` |
| Action対象 | `attributeActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### DestructiblePart

Inspector表示名「破壊可能部位」／カテゴリ「ゲームプレイ」／公開Field 3件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 子Objectを無効化 | `destructibleDisableChildren` | Bool | `SetBool` / `GetBool` |
| Health Source | `destructibleHealthGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| Action対象 | `destructibleActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### FormationFollower

Inspector表示名「編隊追従」／カテゴリ「ゲームプレイ」／公開Field 5件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 位置追従速度 | `formationPositionSpeed` | Float | `SetFloat` / `GetFloat` |
| 回転追従速度 deg/s | `formationRotationSpeed` | Float | `SetFloat` / `GetFloat` |
| 回転を追従 | `formationFollowRotation` | Bool | `SetBool` / `GetBool` |
| ローカルOffset | `formationLocalOffset` | Vector3 | `SetVector3` / `GetVector3` |
| Leader | `formationLeaderGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### TargetLock

Inspector表示名「ターゲットロック」／カテゴリ「ゲームプレイ」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| Lock時間 | `targetLockSeconds` | Float | `SetFloat` / `GetFloat` |
| 喪失猶予 | `targetLockLostGraceSeconds` | Float | `SetFloat` / `GetFloat` |
| TargetSelector | `targetLockSelectorGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| Action対象 | `targetLockActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### MultiTargetLock

Inspector表示名「複数ターゲットロック」／カテゴリ「ゲームプレイ」／公開Field 6件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 1体のLock時間 | `multiTargetLockSecondsPerTarget` | Float | `SetFloat` / `GetFloat` |
| 喪失猶予 | `multiTargetLockLostGraceSeconds` | Float | `SetFloat` / `GetFloat` |
| 最大Lock数 | `multiTargetLockMaximumCount` | Int | `SetInt` / `GetInt` |
| 候補を自動取得 | `multiTargetLockAutoAcquire` | Bool | `SetBool` / `GetBool` |
| TargetSelector | `multiTargetLockSelectorGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| Action対象 | `multiTargetLockActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### WorldTargetMarker

Inspector表示名「ワールドターゲットマーカー」／カテゴリ「UI」／公開Field 9件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 画面端余白 | `targetMarkerEdgePadding` | Float | `SetFloat` / `GetFloat` |
| Multi Lock番号 | `targetMarkerMultiLockIndex` | Int | `SetInt` / `GetInt` |
| カメラ後方を隠す | `targetMarkerHideBehindCamera` | Bool | `SetBool` / `GetBool` |
| Lock完了時だけ表示 | `targetMarkerOnlyWhenLocked` | Bool | `SetBool` / `GetBool` |
| Target方向へ回転 | `targetMarkerRotateToDirection` | Bool | `SetBool` / `GetBool` |
| World Offset | `targetMarkerWorldOffset` | Vector3 | `SetVector3` / `GetVector3` |
| 明示Target | `targetMarkerTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| TargetSelector | `targetMarkerSelectorGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| TargetLock | `targetMarkerLockGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### OffScreenIndicator

Inspector表示名「画面外インジケーター」／カテゴリ「UI」／公開Field 9件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 画面端余白 | `targetMarkerEdgePadding` | Float | `SetFloat` / `GetFloat` |
| Multi Lock番号 | `targetMarkerMultiLockIndex` | Int | `SetInt` / `GetInt` |
| カメラ後方を隠す | `targetMarkerHideBehindCamera` | Bool | `SetBool` / `GetBool` |
| Lock完了時だけ表示 | `targetMarkerOnlyWhenLocked` | Bool | `SetBool` / `GetBool` |
| Target方向へ回転 | `targetMarkerRotateToDirection` | Bool | `SetBool` / `GetBool` |
| World Offset | `targetMarkerWorldOffset` | Vector3 | `SetVector3` / `GetVector3` |
| 明示Target | `targetMarkerTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| TargetSelector | `targetMarkerSelectorGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| TargetLock | `targetMarkerLockGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AttributeSet

Inspector表示名「属性セット」／カテゴリ「ゲームプレイ」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| Action対象 | `attributeSetActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### GenericCounter

Inspector表示名「汎用カウンター」／カテゴリ「ゲームプレイ」／公開Field 6件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 初期値 | `counterInitialValue` | Float | `SetFloat` / `GetFloat` |
| 最小 | `counterMinimumValue` | Float | `SetFloat` / `GetFloat` |
| 最大 | `counterMaximumValue` | Float | `SetFloat` / `GetFloat` |
| 閾値 | `counterThresholdValue` | Float | `SetFloat` / `GetFloat` |
| 成立時は1回だけ | `counterFireOnce` | Bool | `SetBool` / `GetBool` |
| Action対象 | `counterActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### GenericCondition

Inspector表示名「汎用条件」／カテゴリ「ゲームプレイ」／公開Field 5件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 比較値 | `conditionCompareFloat` | Float | `SetFloat` / `GetFloat` |
| 毎Frame評価 | `conditionEvaluateEveryFrame` | Bool | `SetBool` / `GetBool` |
| 結果変化時だけ通知 | `conditionFireOnChangeOnly` | Bool | `SetBool` / `GetBool` |
| 比較元 | `conditionSourceGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| Action対象 | `conditionActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AreaDamage

Inspector表示名「範囲ダメージ」／カテゴリ「ゲームプレイ」／公開Field 13件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 半径 | `areaDamageRadius` | Float | `SetFloat` / `GetFloat` |
| 基礎Damage | `areaDamageBaseDamage` | Float | `SetFloat` / `GetFloat` |
| 端の最低倍率 | `areaDamageMinimumMultiplier` | Float | `SetFloat` / `GetFloat` |
| Impulse | `areaDamageImpulse` | Float | `SetFloat` / `GetFloat` |
| 遮蔽時倍率 | `areaDamageBlockedMultiplier` | Float | `SetFloat` / `GetFloat` |
| Layer Mask | `areaDamageLayerMask` | Int | `SetInt` / `GetInt` |
| 遮蔽Layer Mask | `areaDamageOcclusionLayerMask` | Int | `SetInt` / `GetInt` |
| 遮蔽Sample数 | `areaDamageOcclusionSamplePoints` | Int | `SetInt` / `GetInt` |
| 発生元を除外 | `areaDamageIgnoreOwner` | Bool | `SetBool` / `GetBool` |
| Neutralを無視 | `areaDamageIgnoreNeutral` | Bool | `SetBool` / `GetBool` |
| Play開始時に実行 | `areaDamagePlayOnStart` | Bool | `SetBool` / `GetBool` |
| Team Source | `areaDamageTeamSourceGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| Action対象 | `areaDamageActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### HitZone

Inspector表示名「ヒットゾーン」／カテゴリ「ゲームプレイ」／公開Field 2件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 部位Damage倍率 | `hitZoneDamageMultiplier` | Float | `SetFloat` / `GetFloat` |
| Health対象 | `hitZoneHealthGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### DamageTagModifier

Inspector表示名「ダメージタグ倍率」／カテゴリ「ゲームプレイ」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 未登録Tag倍率 | `damageTagDefaultMultiplier` | Float | `SetFloat` / `GetFloat` |

#### ProjectileDetonator

Inspector表示名「弾起爆装置」／カテゴリ「ゲームプレイ」／公開Field 7件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 近接半径 | `projectileDetonatorProximityRadius` | Float | `SetFloat` / `GetFloat` |
| 接触時起爆 | `projectileDetonateOnContact` | Bool | `SetBool` / `GetBool` |
| 近接時起爆 | `projectileDetonateOnProximity` | Bool | `SetBool` / `GetBool` |
| 寿命切れ時起爆 | `projectileDetonateOnLifetime` | Bool | `SetBool` / `GetBool` |
| 近接Target | `projectileDetonatorTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| AreaDamage | `projectileDetonatorAreaDamageGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| Action対象 | `projectileDetonatorActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### ThreatTracker

Inspector表示名「脅威トラッカー」／カテゴリ「ゲームプレイ」／公開Field 6件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 最大距離 | `threatTrackerMaximumDistance` | Float | `SetFloat` / `GetFloat` |
| 最低接近速度 | `threatTrackerMinimumClosingSpeed` | Float | `SetFloat` / `GetFloat` |
| 最大逸れ距離 | `threatTrackerMaximumMissDistance` | Float | `SetFloat` / `GetFloat` |
| 最大脅威数 | `threatTrackerMaximumCount` | Int | `SetInt` / `GetInt` |
| 監視対象 | `threatTrackerTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| Action対象 | `threatTrackerActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### RuntimeStateReset

Inspector表示名「実行状態リセット」／カテゴリ「ゲームプレイ」／公開Field 8件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| Health | `runtimeResetHealth` | Bool | `SetBool` / `GetBool` |
| State Machine | `runtimeResetStateMachine` | Bool | `SetBool` / `GetBool` |
| Attribute / Counter | `runtimeResetAttributes` | Bool | `SetBool` / `GetBool` |
| Target Lock | `runtimeResetLocks` | Bool | `SetBool` / `GetBool` |
| Timer | `runtimeResetTimers` | Bool | `SetBool` / `GetBool` |
| 破壊可能部位 | `runtimeResetDestructibleParts` | Bool | `SetBool` / `GetBool` |
| Cooldown | `runtimeResetCooldowns` | Bool | `SetBool` / `GetBool` |
| Action対象 | `runtimeResetActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### CooldownSet

Inspector表示名「クールダウンセット」／カテゴリ「ゲームプレイ」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| Action対象 | `cooldownSetActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### WeaponFirePattern

Inspector表示名「武器発射パターン」／カテゴリ「ゲームプレイ」／公開Field 5件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 発射間隔 | `weaponFirePatternInterval` | Float | `SetFloat` / `GetFloat` |
| 扇状角度 deg | `weaponFirePatternSpreadAngle` | Float | `SetFloat` / `GetFloat` |
| チャージ秒 | `weaponFirePatternChargeSeconds` | Float | `SetFloat` / `GetFloat` |
| 発射数 | `weaponFirePatternCount` | Int | `SetInt` / `GetInt` |
| Action対象 | `weaponFirePatternActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### TargetAssignment

Inspector表示名「ターゲット割り当て斉射」／カテゴリ「ゲームプレイ」／公開Field 5件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 発射間隔 | `targetAssignmentInterval` | Float | `SetFloat` / `GetFloat` |
| 最大Target数 | `targetAssignmentMaximumTargets` | Int | `SetInt` / `GetInt` |
| Lock完了Targetのみ | `targetAssignmentLockedOnly` | Bool | `SetBool` / `GetBool` |
| 複数Target Lock | `targetAssignmentMultiTargetLockGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| Action対象 | `targetAssignmentActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### WeaponAccuracy

Inspector表示名「武器命中精度」／カテゴリ「ゲームプレイ」／公開Field 5件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 基礎Spread deg | `weaponAccuracyBaseSpread` | Float | `SetFloat` / `GetFloat` |
| 最大Spread deg | `weaponAccuracyMaximumSpread` | Float | `SetFloat` / `GetFloat` |
| 1発の増加 deg | `weaponAccuracySpreadPerShot` | Float | `SetFloat` / `GetFloat` |
| 毎秒回復 deg | `weaponAccuracyRecoveryPerSecond` | Float | `SetFloat` / `GetFloat` |
| 移動Spread倍率 | `weaponAccuracyMovementSpread` | Float | `SetFloat` / `GetFloat` |

#### WeaponRecoil

Inspector表示名「武器反動」／カテゴリ「ゲームプレイ」／公開Field 8件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 回復速度 | `weaponRecoilRecoveryPerSecond` | Float | `SetFloat` / `GetFloat` |
| Body Impulse | `weaponRecoilBodyImpulse` | Vector3 | `SetVector3` / `GetVector3` |
| Body Torque | `weaponRecoilBodyTorque` | Vector3 | `SetVector3` / `GetVector3` |
| 表示位置反動 | `weaponRecoilVisualPosition` | Vector3 | `SetVector3` / `GetVector3` |
| 表示回転反動 rad | `weaponRecoilVisualRotation` | Vector3 | `SetVector3` / `GetVector3` |
| 表示反動対象 | `weaponRecoilVisualGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| Camera Shake | `weaponRecoilCameraShakeGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| Action対象 | `weaponRecoilActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### TimeScale

Inspector表示名「時間倍率・ヒットストップ」／カテゴリ「ゲームプレイ」／公開Field 5件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 時間倍率 | `timeScaleValue` | Float | `SetFloat` / `GetFloat` |
| 継続秒（実時間） | `timeScaleDuration` | Float | `SetFloat` / `GetFloat` |
| Blend秒（実時間） | `timeScaleBlendSeconds` | Float | `SetFloat` / `GetFloat` |
| Play開始時に実行 | `timeScalePlayOnStart` | Bool | `SetBool` / `GetBool` |
| Action対象 | `timeScaleActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AimAssist

Inspector表示名「照準補助」／カテゴリ「照準」／公開Field 6件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 補助半径 | `aimAssistRadius` | Float | `SetFloat` / `GetFloat` |
| 補助強度 | `aimAssistStrength` | Float | `SetFloat` / `GetFloat` |
| 追従速度 | `aimAssistFollowSpeed` | Float | `SetFloat` / `GetFloat` |
| 入力中の抑制 | `aimAssistInputSuppression` | Float | `SetFloat` / `GetFloat` |
| 画面照準 | `aimAssistScreenAimGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| Target Selector | `aimAssistTargetSelectorGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### InterceptPrediction

Inspector表示名「迎撃予測」／カテゴリ「照準」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| Projectile速度 | `interceptProjectileSpeed` | Float | `SetFloat` / `GetFloat` |
| 最大予測秒 | `interceptMaximumTime` | Float | `SetFloat` / `GetFloat` |
| 明示Target | `interceptTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| Target Selector | `interceptTargetSelectorGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### DamageDirectionIndicator

Inspector表示名「被弾方向表示」／カテゴリ「UI」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 表示秒 | `damageDirectionDuration` | Float | `SetFloat` / `GetFloat` |
| Fade秒 | `damageDirectionFadeSeconds` | Float | `SetFloat` / `GetFloat` |
| 最低Damage | `damageDirectionMinimumDamage` | Float | `SetFloat` / `GetFloat` |
| 画面端半径 | `damageDirectionEdgeRadius` | Float | `SetFloat` / `GetFloat` |

#### ObjectiveTracker

Inspector表示名「目標トラッカー」／カテゴリ「ゲームプレイ」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| Action対象 | `objectiveActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### EncounterController

Inspector表示名「エンカウンター制御」／カテゴリ「ゲームプレイ」／公開Field 2件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| Play開始時に実行 | `encounterPlayOnStart` | Bool | `SetBool` / `GetBool` |
| Action対象 | `encounterActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### SpawnPointSet

Inspector表示名「生成地点セット」／カテゴリ「ゲームプレイ」／公開Field 2件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 直前を避ける | `spawnPointAvoidImmediateRepeat` | Bool | `SetBool` / `GetBool` |
| Volume Size | `spawnPointVolumeSize` | Vector3 | `SetVector3` / `GetVector3` |

#### DifficultyParameterSet

Inspector表示名「難易度パラメーターセット」／カテゴリ「ゲームプレイ」／公開Field 3件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 選択Index | `difficultySelectedIndex` | Int | `SetInt` / `GetInt` |
| Play開始時に適用 | `difficultyApplyOnStart` | Bool | `SetBool` / `GetBool` |
| Action対象 | `difficultyActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### CameraFeedbackMixer

Inspector表示名「カメラフィードバックミキサー」／カテゴリ「カメラ」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 全体強度 | `cameraFeedbackGlobalStrength` | Float | `SetFloat` / `GetFloat` |
| 最大同時数 | `cameraFeedbackMaximumConcurrent` | Int | `SetInt` / `GetInt` |
| 最大位置振幅 | `cameraFeedbackMaximumPosition` | Vector3 | `SetVector3` / `GetVector3` |
| 最大回転振幅 | `cameraFeedbackMaximumRotation` | Vector3 | `SetVector3` / `GetVector3` |

#### BallisticPrediction

Inspector表示名「弾道予測」／カテゴリ「照準」／公開Field 16件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 初速 | `ballisticInitialSpeed` | Float | `SetFloat` / `GetFloat` |
| 線形Drag | `ballisticDrag` | Float | `SetFloat` / `GetFloat` |
| 最大飛翔秒 | `ballisticMaximumTime` | Float | `SetFloat` / `GetFloat` |
| 積分Step | `ballisticSimulationStep` | Float | `SetFloat` / `GetFloat` |
| 並進速度継承 | `ballisticLinearVelocityInheritance` | Float | `SetFloat` / `GetFloat` |
| 角速度継承 | `ballisticAngularVelocityInheritance` | Float | `SetFloat` / `GetFloat` |
| 最大Point数 | `ballisticMaximumPoints` | Int | `SetInt` / `GetInt` |
| 発射元速度を継承 | `ballisticInheritSourceVelocity` | Bool | `SetBool` / `GetBool` |
| 親Rigidbodyを検索 | `ballisticUseParentRigidBody` | Bool | `SetBool` / `GetBool` |
| 重力 | `ballisticGravity` | Vector3 | `SetVector3` / `GetVector3` |
| Target加速度 | `ballisticTargetAcceleration` | Vector3 | `SetVector3` / `GetVector3` |
| 発射元World速度 | `ballisticSourceVelocity` | Vector3 | `SetVector3` / `GetVector3` |
| 初期World速度 | `ballisticLaunchVelocity` | Vector3 | `SetVector3` / `GetVector3` |
| 明示Target | `ballisticTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| Target Selector | `ballisticTargetSelectorGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| 速度Source | `ballisticSourceVelocityGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### DamageEventBuffer

Inspector表示名「複数被弾履歴」／カテゴリ「UI」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 表示寿命 | `damageEventLifetime` | Float | `SetFloat` / `GetFloat` |
| 最低Damage | `damageEventMinimumDamage` | Float | `SetFloat` / `GetFloat` |
| 最大Entry数 | `damageEventMaximumEntries` | Int | `SetInt` / `GetInt` |
| 同じSourceを統合 | `damageEventMergeSameSource` | Bool | `SetBool` / `GetBool` |

#### GamePause

Inspector表示名「ゲーム一時停止」／カテゴリ「ゲームプレイ」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| ゲーム時間を停止 | `gamePausePauseGameTime` | Bool | `SetBool` / `GetBool` |
| 物理を停止 | `gamePausePausePhysics` | Bool | `SetBool` / `GetBool` |
| Audioを停止 | `gamePausePauseAudio` | Bool | `SetBool` / `GetBool` |
| Action対象 | `gamePauseActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### SurfaceWakeEmitter

Inspector表示名「水面航跡エミッター」／カテゴリ「海・水面」／公開Field 12件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 最低速度 | `surfaceWakeMinimumSpeed` | Float | `SetFloat` / `GetFloat` |
| 最大強度速度 | `surfaceWakeMaximumSpeed` | Float | `SetFloat` / `GetFloat` |
| 航跡幅 | `surfaceWakeWidth` | Float | `SetFloat` / `GetFloat` |
| Foam寿命 | `surfaceWakeLifetime` | Float | `SetFloat` / `GetFloat` |
| 最大発生数/秒 | `surfaceWakeMaximumEmissionRate` | Float | `SetFloat` / `GetFloat` |
| 局所波の強度 | `surfaceWakeWaveAmplitudeScale` | Float | `SetFloat` / `GetFloat` |
| 局所波の影響半径 | `surfaceWakeWaveRadiusScale` | Float | `SetFloat` / `GetFloat` |
| 水面へ局所波を与える | `surfaceWakeAffectOceanSurface` | Bool | `SetBool` / `GetBool` |
| Ocean | `surfaceWakeOceanGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| 左航跡Effect | `surfaceWakeLeftEffectGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| 右航跡Effect | `surfaceWakeRightEffectGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| 船首Spray Effect | `surfaceWakeBowEffectGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### TrajectoryRenderer

Inspector表示名「軌道プレビュー」／カテゴリ「描画・レンダリング」／公開Field 7件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 透明度 | `trajectoryAlpha` | Float | `SetFloat` / `GetFloat` |
| 太さ | `trajectoryThickness` | Float | `SetFloat` / `GetFloat` |
| 最大Point数 | `trajectoryMaximumPoints` | Int | `SetInt` / `GetInt` |
| Scene View | `trajectoryShowInSceneView` | Bool | `SetBool` / `GetBool` |
| Game View | `trajectoryShowInGameView` | Bool | `SetBool` / `GetBool` |
| 着弾点 | `trajectoryShowImpactPoint` | Bool | `SetBool` / `GetBool` |
| 弾道予測 | `trajectoryPredictionGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### WaterSurfaceState

Inspector表示名「水面出入り状態」／カテゴリ「海・水面」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| Clearance | `waterSurfaceClearance` | Float | `SetFloat` / `GetFloat` |
| ローカル判定位置 | `waterSurfaceLocalOffset` | Vector3 | `SetVector3` / `GetVector3` |
| Ocean | `waterSurfaceOceanGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| Action対象 | `waterSurfaceActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### OceanProbeSet

Inspector表示名「海面前方プローブ」／カテゴリ「海・水面」／公開Field 3件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| ローカル原点 | `oceanProbeLocalOriginOffset` | Vector3 | `SetVector3` / `GetVector3` |
| ローカル方向 | `oceanProbeLocalDirection` | Vector3 | `SetVector3` / `GetVector3` |
| Ocean | `oceanProbeOceanGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### AttackCollisionFilter

Inspector表示名「攻撃コリジョンフィルター」／カテゴリ「ゲームプレイ」／公開Field 5件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| Arming距離 | `attackFilterArmingDistance` | Float | `SetFloat` / `GetFloat` |
| 発射者を無視 | `attackFilterIgnoreInstigator` | Bool | `SetBool` / `GetBool` |
| 発射者の子も無視 | `attackFilterIgnoreInstigatorHierarchy` | Bool | `SetBool` / `GetBool` |
| Neutralを無視 | `attackFilterIgnoreNeutral` | Bool | `SetBool` / `GetBool` |
| 発射責任者 | `attackFilterInstigatorGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### TurretAim

Inspector表示名「砲塔照準」／カテゴリ「照準」／公開Field 12件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| Yaw最小 deg | `turretYawMinimumDegrees` | Float | `SetFloat` / `GetFloat` |
| Yaw最大 deg | `turretYawMaximumDegrees` | Float | `SetFloat` / `GetFloat` |
| Pitch最小 deg | `turretPitchMinimumDegrees` | Float | `SetFloat` / `GetFloat` |
| Pitch最大 deg | `turretPitchMaximumDegrees` | Float | `SetFloat` / `GetFloat` |
| Yaw速度 deg/s | `turretYawSpeedDegrees` | Float | `SetFloat` / `GetFloat` |
| Pitch速度 deg/s | `turretPitchSpeedDegrees` | Float | `SetFloat` / `GetFloat` |
| 照準許容角 deg | `turretAimToleranceDegrees` | Float | `SetFloat` / `GetFloat` |
| Target予測秒 | `turretPredictionSeconds` | Float | `SetFloat` / `GetFloat` |
| 明示Target | `turretTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| Target Selector | `turretTargetSelectorGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| Yaw Pivot | `turretYawPivotGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| Pitch Pivot | `turretPitchPivotGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### WeaponGroup

Inspector表示名「武器グループ」／カテゴリ「ゲームプレイ」／公開Field 3件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 順次間隔 | `weaponGroupInterval` | Float | `SetFloat` / `GetFloat` |
| 全武器Ready必須 | `weaponGroupRequireAllReady` | Bool | `SetBool` / `GetBool` |
| 完了Action対象 | `weaponGroupActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### ProjectileImpactPhysics

Inspector表示名「弾体貫通・跳弾」／カテゴリ「ゲームプレイ」／公開Field 7件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 初期貫通Energy | `projectileImpactPenetrationEnergy` | Float | `SetFloat` / `GetFloat` |
| 基本貫通損失 | `projectileImpactPenetrationLoss` | Float | `SetFloat` / `GetFloat` |
| 跳弾開始角 deg | `projectileImpactRicochetAngleDegrees` | Float | `SetFloat` / `GetFloat` |
| 速度保持率 | `projectileImpactEnergyRetention` | Float | `SetFloat` / `GetFloat` |
| Damage保持率 | `projectileImpactDamageRetention` | Float | `SetFloat` / `GetFloat` |
| 最大貫通回数 | `projectileImpactMaximumPenetrations` | Int | `SetInt` / `GetInt` |
| 最大跳弾回数 | `projectileImpactMaximumRicochets` | Int | `SetInt` / `GetInt` |

#### CameraHorizonStabilizer

Inspector表示名「水平線スタビライザー」／カテゴリ「カメラ」／公開Field 10件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| Pitch継承 | `horizonPitchInheritance` | Float | `SetFloat` / `GetFloat` |
| Yaw継承 | `horizonYawInheritance` | Float | `SetFloat` / `GetFloat` |
| Roll継承 | `horizonRollInheritance` | Float | `SetFloat` / `GetFloat` |
| 減衰 | `horizonDamping` | Float | `SetFloat` / `GetFloat` |
| 最大Roll deg | `horizonMaximumRollDegrees` | Float | `SetFloat` / `GetFloat` |
| 位置を追従 | `horizonFollowPosition` | Bool | `SetBool` / `GetBool` |
| ローカル位置Offset | `horizonLocalPositionOffset` | Vector3 | `SetVector3` / `GetVector3` |
| 回転Offset deg | `horizonRotationOffsetDegrees` | Vector3 | `SetVector3` / `GetVector3` |
| World Up | `horizonWorldUp` | Vector3 | `SetVector3` / `GetVector3` |
| 追従Source | `horizonSourceGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### FireLineCheck

Inspector表示名「発射前射線チェック」／カテゴリ「ゲームプレイ」／公開Field 6件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 検査距離 | `fireLineDistance` | Float | `SetFloat` / `GetFloat` |
| 検査半径 | `fireLineRadius` | Float | `SetFloat` / `GetFloat` |
| Block Layer Mask | `fireLineLayerMask` | Int | `SetInt` / `GetInt` |
| 砲口 | `fireLineMuzzleGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| 前方向Source | `fireLineDirectionGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| 許可Target | `fireLineAllowedTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### StatusEffectSet

Inspector表示名「状態効果セット」／カテゴリ「ゲームプレイ」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| Action対象 | `statusEffectActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### RailSpeedProfile

Inspector表示名「レール速度プロファイル」／カテゴリ「ゲームプレイ」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| プロファイルを使用 | `railSpeedProfileEnabled` | Bool | `SetBool` / `GetBool` |

#### RailZone

Inspector表示名「レール区間」／カテゴリ「ゲームプレイ」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| Action対象 | `railZoneActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### CameraFollowComposer

Inspector表示名「カメラ追従コンポーザー」／カテゴリ「カメラ」／公開Field 9件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 位置減衰 | `cameraComposerPositionDamping` | Float | `SetFloat` / `GetFloat` |
| 回転減衰 | `cameraComposerRotationDamping` | Float | `SetFloat` / `GetFloat` |
| 速度先読み秒 | `cameraComposerLookAheadSeconds` | Float | `SetFloat` / `GetFloat` |
| 1Frame最大追従距離 | `cameraComposerMaximumDistance` | Float | `SetFloat` / `GetFloat` |
| 対象Yawを継承 | `cameraComposerInheritTargetYaw` | Bool | `SetBool` / `GetBool` |
| Pitch/Rollを安定化 | `cameraComposerStabilizePitchRoll` | Bool | `SetBool` / `GetBool` |
| 追従Offset | `cameraComposerFollowOffset` | Vector3 | `SetVector3` / `GetVector3` |
| 注視Offset | `cameraComposerLookAtOffset` | Vector3 | `SetVector3` / `GetVector3` |
| 追従対象 | `cameraComposerTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### SpeedFeedback

Inspector表示名「速度フィードバック」／カテゴリ「カメラ」／公開Field 10件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 最小速度 | `speedFeedbackMinimumSpeed` | Float | `SetFloat` / `GetFloat` |
| 最大速度 | `speedFeedbackMaximumSpeed` | Float | `SetFloat` / `GetFloat` |
| 最小FOV | `speedFeedbackMinimumFovDegrees` | Float | `SetFloat` / `GetFloat` |
| 最大FOV | `speedFeedbackMaximumFovDegrees` | Float | `SetFloat` / `GetFloat` |
| 最小Blur | `speedFeedbackMinimumMotionBlur` | Float | `SetFloat` / `GetFloat` |
| 最大Blur | `speedFeedbackMaximumMotionBlur` | Float | `SetFloat` / `GetFloat` |
| Camera強度加算 | `speedFeedbackCameraStrength` | Float | `SetFloat` / `GetFloat` |
| 応答速度 | `speedFeedbackResponseSpeed` | Float | `SetFloat` / `GetFloat` |
| 速度Source | `speedFeedbackSourceGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| 対象Camera | `speedFeedbackCameraGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### SpawnedObjectSetup

Inspector表示名「生成オブジェクト設定」／カテゴリ「ゲームプレイ」／公開Field 8件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 開始進行率 | `spawnedSetupRailStartNormalized` | Float | `SetFloat` / `GetFloat` |
| 個体ごとの進行率差 | `spawnedSetupRailStartStep` | Float | `SetFloat` / `GetFloat` |
| Rail速度倍率 | `spawnedSetupRailSpeedMultiplier` | Float | `SetFloat` / `GetFloat` |
| Team ID | `spawnedSetupTeamId` | Int | `SetInt` / `GetInt` |
| Teamを上書き | `spawnedSetupOverrideTeam` | Bool | `SetBool` / `GetBool` |
| 生成時にRuntime状態をReset | `spawnedSetupResetRuntimeState` | Bool | `SetBool` / `GetBool` |
| Rail Path | `spawnedSetupRailPathGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |
| Action対象 | `spawnedSetupActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### WaveMotionProfile

Inspector表示名「ウェーブ移動プロファイル」／カテゴリ「ゲームプレイ」／公開Field 3件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 周波数 | `waveMotionFrequency` | Float | `SetFloat` / `GetFloat` |
| 個体ごとの位相差 | `waveMotionPhaseStep` | Float | `SetFloat` / `GetFloat` |
| Blend In秒 | `waveMotionBlendInSeconds` | Float | `SetFloat` / `GetFloat` |

#### DistanceActivation

Inspector表示名「距離アクティベーション」／カテゴリ「最適化」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 有効化距離 | `distanceActivationEnterDistance` | Float | `SetFloat` / `GetFloat` |
| 無効化距離 | `distanceActivationExitDistance` | Float | `SetFloat` / `GetFloat` |
| 子階層も対象 | `distanceActivationAffectHierarchy` | Bool | `SetBool` / `GetBool` |
| 距離基準 | `distanceActivationReferenceGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### SimulationLOD

Inspector表示名「シミュレーション LOD」／カテゴリ「最適化」／公開Field 12件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| Medium距離 | `simulationLodMediumDistance` | Float | `SetFloat` / `GetFloat` |
| Far距離 | `simulationLodFarDistance` | Float | `SetFloat` / `GetFloat` |
| Culled距離 | `simulationLodCulledDistance` | Float | `SetFloat` / `GetFloat` |
| Medium Script更新秒 | `simulationLodMediumScriptInterval` | Float | `SetFloat` / `GetFloat` |
| Far Script更新秒 | `simulationLodFarScriptInterval` | Float | `SetFloat` / `GetFloat` |
| FarでPhysics停止 | `simulationLodDisablePhysicsAtFar` | Bool | `SetBool` / `GetBool` |
| FarでScript停止 | `simulationLodDisableScriptsAtFar` | Bool | `SetBool` / `GetBool` |
| FarでAI停止 | `simulationLodDisableAiAtFar` | Bool | `SetBool` / `GetBool` |
| FarでAnimation停止 | `simulationLodDisableAnimationAtFar` | Bool | `SetBool` / `GetBool` |
| FarでEffect停止 | `simulationLodDisableEffectsAtFar` | Bool | `SetBool` / `GetBool` |
| 子階層も対象 | `simulationLodAffectHierarchy` | Bool | `SetBool` / `GetBool` |
| 距離基準 | `simulationLodReferenceGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### RailEventMarker

Inspector表示名「レールイベントマーカー」／カテゴリ「入力・イベント」／公開Field 1件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| Action対象 | `railEventMarkerActionTargetGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### SceneStreaming

Inspector表示名「Scene Streaming」／カテゴリ「最適化」／公開Field 4件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 読込距離 | `sceneStreamingLoadDistance` | Float | `SetFloat` / `GetFloat` |
| 解除距離 | `sceneStreamingUnloadDistance` | Float | `SetFloat` / `GetFloat` |
| 遠距離でSceneを破棄 | `sceneStreamingUnloadWhenFar` | Bool | `SetBool` / `GetBool` |
| 距離基準 | `sceneStreamingReferenceGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

#### TextEffect

Inspector表示名「テキストエフェクト」／カテゴリ「UI」／公開Field 7件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 継続時間(秒) | `textAppearDuration` | Float | `SetFloat` / `GetFloat` |
| 開始遅延(秒) | `textAppearDelay` | Float | `SetFloat` / `GetFloat` |
| 文字/秒 | `textAppearParamA` | Float | `SetFloat` / `GetFloat` |
| 開始遅延(秒) | `textContinuousDelay` | Float | `SetFloat` / `GetFloat` |
| 点滅間隔(秒) | `textContinuousParamA` | Float | `SetFloat` / `GetFloat` |
| 文字ごとの色ずれ | `textContinuousParamB` | Float | `SetFloat` / `GetFloat` |
| 揺れる速さ | `textContinuousParamC` | Float | `SetFloat` / `GetFloat` |

#### SceneTransition

Inspector表示名「シーン遷移」／カテゴリ「エフェクト」／公開Field 7件。

| Inspector表示 | Property名（`fieldKey`） | 種別 | 使うアクセサ |
| --- | --- | --- | --- |
| 覆うまでの時間(秒) | `sceneTransitionOutDuration` | Float | `SetFloat` / `GetFloat` |
| 静止時間(秒) | `sceneTransitionHoldSeconds` | Float | `SetFloat` / `GetFloat` |
| 見せる時間(秒) | `sceneTransitionInDuration` | Float | `SetFloat` / `GetFloat` |
| 色 | `sceneTransitionColor` | Vector3 | `SetVector3` / `GetVector3` |
| Dive開始位置 Offset | `sceneTransitionCameraDivePositionOffset` | Vector3 | `SetVector3` / `GetVector3` |
| Dive開始角度 deg | `sceneTransitionCameraDiveRotationDegrees` | Vector3 | `SetVector3` / `GetVector3` |
| Dive基準Object | `sceneTransitionCameraDiveSourceGameObjectId` | GameObjectReference | `SetGameObject` / `GetGameObject` |

### Script公開面の全体像と機械照合結果

Scriptから利用できる公開面は「Runtime APIが271個」だけではない。Wrapper、共有型、Template、名前付きComponent Fieldもある。以下は旧235 Entry時点の集計を残した履歴表であり、現行値はRuntime Entry行のみ再照合している。今回追加した署名・Wire関連Fieldは本文の追加Wrapper節とComponent仕様書を参照する。

| 層 | 件数（Runtime以外は過去の集計） | 意味 |
| --- | --- | --- |
| Runtime API Entry | 271 | DLLとEditorを接続する低水準関数ポインタ。2026-09-02再照合。 |
| 共有enum / struct | 39 | Event、Hit、Damage、Rail、Ocean、Runtime Joint等を受け渡すABI型。 |
| 高水準Wrapper Class | 61 | `GameObject`、`Rigidbody`、`Health`、`Component`等のC++入口。 |
| C++ Script Template | 26 | Script作成UIから生成できる開始コード。 |
| 汎用Component Field | 1,474（257 Component） | Runtime API 10 Entryを介して名前で読み書きするInspector Field。 |

1,474 Fieldは1,474個の関数ポインタを追加したという意味ではない。`SetRuntimeFloat`等の既存10 Entryへ「Component内部名 + Fieldキー」を渡して解決するデータ駆動の公開面である。このためAPI Versionを増やさず、多数のComponent設定へ到達できる。

照合記録は次のとおり。Runtime以外の行は当時の結果で、現在の完全一致を保証しない。

| 照合対象 | 結果 |
| --- | --- |
| `EditorScriptRuntimeApi` 宣言名と全271件索引 | 271 / 271一致、重複なし（2026-09-02）。 |
| `EditorScriptApi.h` の共有型と共有型章 | 39 / 39一致。 |
| `EditorNativeScript.h` のWrapper ClassとWrapper章 | 61 / 61一致。 |
| `kTemplateInfos` とTemplate章 | 26 / 26一致。 |
| `EditorLogFieldRegistry.generated.cpp` と全Field台帳 | 1,474 / 1,474一致、重複なし。 |
| 台帳のComponent種類 | 257 / 257一致。 |

`NavigationAgent`だけは列挙子名と公開文字列が異なり、台帳の列挙子は`NavigationAgent`、`GetComponent()`へ渡す名前は`"NavMeshAgent"`である。この1件を正規化した上でField台帳は完全一致している。

今回の271件照合はRuntime Entryの索引確認であり、全Component Field・全Wrapperの現行完全網羅や実行時検証を意味しない。文字列、Asset参照、配列、複合要素、未登録Vector2、Subsystem再構築、Component固有の実行操作まで汎用Fieldアクセスだけで完結するという意味でもない。それらは本章前半の専用Wrapper、低水準Entry、またはComponent個別のRuntime契約を使う。
