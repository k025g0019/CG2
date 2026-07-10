# OpenCode に任せる作業

## 目的

Codex 側では、壊れやすい土台として以下を実装済みにする。

- 空シーン起動
- Scene ガイド線
- Component データ拡張
- RigidBody / Collider / Input の最小実行
- Play / Stop の状態管理
- Console のログ表示
- FBX リソースを GameObject として置く導線

OpenCode 側は、以下の細かい作業と UI 仕上げを進める。

## 1. 操作性

- Scene 上の選択判定を見直す。
- ヒエラルキーの複数選択を追加する。
- Delete キーで選択 GameObject を消せるようにする。
- Ctrl+D で複製できるようにする。
- W/E/R で Move / Rotate / Scale を切り替える。
- Play 中は編集操作をロックする表示を入れる。

## 2. UI 整理

- Inspector の Component UI を 2 列レイアウトにする。
- タグや短い数値項目は同じ行に並べる。
- Component の折りたたみヘッダーに有効チェックを横並びで置く。
- Console に Info / Warning / Error の色分けを入れる。
- Project の AssetGrid の列数をウィンドウ幅に合わせる。

## 3. FBX

- 現状の FBX 配置は GameObject と assetPath の登録まで。
- 実際に FBX メッシュを読む処理を追加する。
- `resources/UVCube.fbx`
- `resources/box.fbx`
- `resources/cylinder.fbx`
- `resources/cone.fbx`
- `resources/Torus.fbx`
- `resources/ICOCube.fbx`

## 4. Physics

- 現状は Play 中だけ RigidBody の重力、速度、床衝突を処理する。
- 実装済みの親子関係は `EditorRuntimeManager -> EditorPhysicsManager -> BoxCollision / SphereCollision`。
- BoxCollider 同士の AABB 判定を追加する。
- SphereCollider 同士の距離判定を追加する。
- Box と Sphere の混合判定を追加する。
- Trigger の接触ログを Console に出す。
- Collision Layer の UI を追加する。

## 5. Input

- 現状は DirectInput のキー番号をそのまま Inspector で設定する。
- キー名を ComboBox で選べるようにする。
- `Jump` 以外の Action 名を増やす。
- Input 設定を Scene 保存に含めた状態で動作確認する。

## 6. ファイル分割

- Codex 側で `main -> GameScene -> Platform / SceneLifecycle / FrameInput / ImGuiFrame / MainMenu / Docking / SceneView / Hierarchy / Inspector / BottomPanel / Render` へ分離済み。
- Codex 側で `GameScene.Initialize -> PlatformManager.Initialize -> WorkspaceManager.Initialize` の順で初期化する形へ変更済み。
- Codex 側で共有状態を `EditorSharedState` へ分離済み。
- Codex 側で `GameSceneLegacy` と `RunGameSceneLegacy` を削除済み。
- Codex 側で `EditorRuntimeManager -> EditorInputManager / EditorPhysicsManager` へ分離済み。
- Codex 側で `EditorPhysicsManager -> BoxCollision / SphereCollision` へ分離済み。
- Codex 側で `EditorSceneObjectManager / EditorSceneSynchronizer / EditorSelectionManager` へ分離済み。
- Codex 側で `EditorAssetFactory / EditorAssetUtility` へ分離済み。
- Codex 側で `EditorMainMenuBar / EditorHierarchyPanel / EditorInspectorPanel / EditorBottomPanel` へ分離済み。
- Codex 側で `EditorSceneCameraController` へカメラ操作を分離済み。
- OpenCode 側は Scene overlay の細かい見た目調整を進める。
- OpenCode 側は DirectX 初期化処理を `DirectXRenderContext` へ分離する。
