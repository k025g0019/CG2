# CG2 README

この README は、**コードを読む前に「どこで何をしているか」をつかむための説明**です。  
全部の処理を同じ重さで並べるのではなく、**実際に触ることが多いエディタ機能と描画処理の流れ**を中心にまとめています。

---

## 全体像

このプログラムは、**DirectX12 と ImGui で作った簡易ゲームエディタの原型**です。

- DirectX12 でモデルとスプライトを描画する
- ImGui で Hierarchy / Scene / Inspector / Project / Console を表示する
- Scene 上でカメラ操作、アセットのドラッグ追加、移動 / 回転 / 拡縮ギズモ操作を行う
- GameObject と Component を `EditorScene` に持たせる
- Scene 保存 / 読み込み、Prefab 保存 / 生成、Undo / Redo を行う

大きな流れは次のとおりです。

```text
WinMain()
  ↓
DirectInput / XAudio2 / DirectX12 を初期化
  ↓
Texture / Model / SRV / ImGui を準備
  ↓
毎フレーム入力を読む
  ↓
ImGui でエディタ UI を作る
  ↓
行列と定数バッファを更新する
  ↓
DirectX12 で Scene を描画する
  ↓
ImGui を重ねて表示する
```

---

## 起動と毎フレームの入口

<details>
<summary><strong>main.cpp:479: WinMain()</strong></summary>

### コード

```cpp
// [Large] WinMain entry
int WINAPI WinMain(_In_ HINSTANCE instanceHandle, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {
	CrashHandler crashHandler;
	std::ofstream logStream("logs/application.log", std::ios::app);
	Log(logStream, "application started");
```

### ここでやっていること

- Windows アプリの入口です。
- クラッシュハンドラとログ出力を準備します。
- このあと DirectInput、XAudio2、DirectX12、ImGui の順で初期化します。

### なぜここが重要か

このファイルは `main.cpp` に処理が多いので、まず `WinMain()` の大きな区切りを見ると流れを追いやすくなります。  
現在はエディタ UI、描画、入力、音声準備までこの関数内に集まっています。

</details>

<details>
<summary><strong>main.cpp:1336: メインループ</strong></summary>

### コード

```cpp
while (message.message != WM_QUIT) {
	if (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
		TranslateMessage(&message);
		DispatchMessage(&message);
	}
	else {
		memcpy(preKey, key, sizeof(key));
		hr = keyboardDevice->Acquire();
		hr = keyboardDevice->GetDeviceState(sizeof(key), key);
```

### ここでやっていること

- Windows メッセージがあれば先に処理します。
- メッセージがなければ 1 フレーム分の更新と描画を行います。
- DirectInput でキーボード状態を読みます。

### 処理の流れ

```text
OS メッセージ処理
  ↓
キーボード入力取得
  ↓
カメラ更新
  ↓
ImGui UI 作成
  ↓
行列更新
  ↓
DirectX12 描画
  ↓
Present
```

</details>

---

## リソースとテクスチャ

<details>
<summary><strong>main.cpp:1124: 読み込むテクスチャ一覧</strong></summary>

### コード

```cpp
std::wstring textureFilePaths[] = {
	L"resources/uvChecker.png",
	L"resources/monsterBall.png",
	ConvertString(modelData.material.textureFilePath),
	L"resources/ball.png",
};
```

### ここでやっていること

- エディタで使う画像を配列で管理します。
- `uvChecker.png`、`monsterBall.png`、OBJ の material が指定する画像、`ball.png` を読み込み対象にしています。
- Project パネルの PNG サムネイルや、Scene にドラッグした Sprite の表示に使われます。

</details>

<details>
<summary><strong>main.cpp:1137: Texture 読み込みと Resource 作成</strong></summary>

### コード

```cpp
for (uint32_t textureIndex = 0; textureIndex < _countof(textureFilePaths); ++textureIndex) {
	mipImages[textureIndex] = LoadTexture(textureFilePaths[textureIndex]);
	textureMetadatas[textureIndex] = mipImages[textureIndex].GetMetadata();
	textureResources[textureIndex] = CreateTextureResource(device.Get(), textureMetadatas[textureIndex]);
}
```

### ここでやっていること

- `LoadTexture()` で画像を読み込みます。
- mipmap つきの `ScratchImage` を作ります。
- DirectX12 の `ID3D12Resource` として GPU 用テクスチャを作ります。

</details>

<details>
<summary><strong>main.cpp:1285: SRV の作成</strong></summary>

### コード

```cpp
UINT srvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandlesCPU[_countof(textureFilePaths)];
D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandlesGPU[_countof(textureFilePaths)];
for (uint32_t textureIndex = 0; textureIndex < _countof(textureFilePaths); ++textureIndex) {
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = textureMetadatas[textureIndex].format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = static_cast<UINT>(textureMetadatas[textureIndex].mipLevels);

	textureSrvHandlesCPU[textureIndex] = GetCPUDescriptorHandle(srvDescriptorHeap, srvDescriptorSize,
	                                                            textureIndex + 1);
	textureSrvHandlesGPU[textureIndex] = GetGPUDescriptorHandle(srvDescriptorHeap, srvDescriptorSize,
	                                                            textureIndex + 1);
	device->CreateShaderResourceView(textureResources[textureIndex], &srvDesc, textureSrvHandlesCPU[textureIndex]);
}
```

### ここでやっていること

- テクスチャごとに SRV を作ります。
- `textureIndex + 1` にしているのは、0 番を ImGui 用に空けるためです。
- GPU ハンドルを `textureSrvHandlesGPU` に保存し、描画時と ImGui のサムネイル表示で使います。

</details>

---

## エディタ画面のレイアウト

<details>
<summary><strong>main.cpp:1083: パネル幅と高さ</strong></summary>

### コード

```cpp
float editorLeftWidth = 250.0f;
float editorRightWidth = 320.0f;
float editorBottomHeight = 190.0f;
float editorProjectWidth = 570.0f;
```

### ここでやっていること

- 左の Hierarchy 幅を `editorLeftWidth` で持ちます。
- 右の Inspector 幅を `editorRightWidth` で持ちます。
- 下の Project / Console 高さを `editorBottomHeight` で持ちます。
- Project と Console の境界を `editorProjectWidth` で持ちます。

</details>

<details>
<summary><strong>main.cpp:1093: レイアウト更新</strong></summary>

### コード

```cpp
auto updateEditorLayout = [&]() {
	editorLeftWidth = (std::clamp)(editorLeftWidth, 160.0f, 420.0f);
	editorRightWidth = (std::clamp)(editorRightWidth, 220.0f, 520.0f);
	editorBottomHeight = (std::clamp)(editorBottomHeight, 120.0f, 320.0f);
	editorSceneX = editorLeftWidth;
	editorSceneY = editorMenuHeight + editorSceneHeaderHeight;
	editorSceneWidth = editorWindowWidth - editorLeftWidth - editorRightWidth;
	editorSceneHeight = editorWindowHeight - editorSceneY - editorBottomHeight;
	editorSceneWidth = (std::max)(editorSceneWidth, 240.0f);
	editorSceneHeight = (std::max)(editorSceneHeight, 180.0f);
	float bottomPanelWidth = editorWindowWidth - editorRightWidth;
	float maxProjectWidth = (std::max)(240.0f, bottomPanelWidth - 240.0f);
	editorProjectWidth = (std::clamp)(editorProjectWidth, 240.0f, maxProjectWidth);
};
```

### ここでやっていること

- パネルの最小 / 最大サイズを制限します。
- Scene の表示領域を現在のウィンドウサイズから計算します。
- Project / Console の幅も、ウィンドウに収まるように制限します。

### なぜ必要か

フルスクリーンやウィンドウリサイズ時に、Scene の当たり判定と描画範囲がずれないようにするためです。  
Scene の矩形はこの関数で毎フレーム作り直されます。

</details>

<details>
<summary><strong>main.cpp:2592: パネルのリサイズ用 Splitter</strong></summary>

### コード

```cpp
ImGui::SetNextWindowPos(ImVec2(projectWidth - 3.0f, bottomY), ImGuiCond_Always);
ImGui::SetNextWindowSize(ImVec2(splitterHitWidth, editorBottomHeight), ImGuiCond_Always);
ImGui::Begin("ProjectConsoleSplitter", nullptr, splitterFlags);
ImGui::GetWindowDrawList()->AddRectFilled(
	ImVec2(projectWidth - 1.0f, bottomY),
	ImVec2(projectWidth + 1.0f, bottomY + editorBottomHeight),
	IM_COL32(95, 115, 140, 255));
ImGui::InvisibleButton("ProjectConsoleSplitterButton", ImGui::GetContentRegionAvail());
if (ImGui::IsItemActive()) {
	editorProjectWidth += ImGui::GetIO().MouseDelta.x;
}
ImGui::End();
```

### ここでやっていること

- 見た目は細い線、当たり判定は `InvisibleButton` で作っています。
- ドラッグ中の `MouseDelta` を幅へ加算します。
- 同じ考え方で、左パネル、右パネル、下パネルもリサイズしています。

</details>

---

## シーンカメラ操作

<details>
<summary><strong>main.cpp:1719: マウス中ボタン / 右ボタンのドラッグ開始</strong></summary>

### コード

```cpp
if (isSceneHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
	isSceneMiddleCameraDragging = true;
}

if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
	isSceneMiddleCameraDragging = false;
}

if (isSceneHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
	isSceneRightCameraDragging = true;
}

if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
	isSceneRightCameraDragging = false;
}
```

### ここでやっていること

- Scene 上で中ボタンを押したら平行移動モードにします。
- 中ボタンを離したら平行移動を終了します。
- Scene 上で右ボタンを押したら回転モードにします。
- 右ボタンを離したら回転を終了します。

</details>

<details>
<summary><strong>main.cpp:1741: 中ドラッグでカメラ平行移動</strong></summary>

### コード

```cpp
if (isSceneMiddleCameraDragging) {
	ImVec2 mouseDelta = ImGui::GetIO().MouseDelta;
	cameraTransform.translate.x -= cameraRight.x * mouseDelta.x * editorCameraPanSpeed;
	cameraTransform.translate.y -= cameraRight.y * mouseDelta.x * editorCameraPanSpeed;
	cameraTransform.translate.z -= cameraRight.z * mouseDelta.x * editorCameraPanSpeed;
	cameraTransform.translate.x += cameraUp.x * mouseDelta.y * editorCameraPanSpeed;
	cameraTransform.translate.y += cameraUp.y * mouseDelta.y * editorCameraPanSpeed;
	cameraTransform.translate.z += cameraUp.z * mouseDelta.y * editorCameraPanSpeed;
}
```

### ここでやっていること

- マウスの移動量をカメラの右方向と上方向へ変換します。
- 画面上のドラッグ感覚に近い形で Scene を平行移動します。

</details>

<details>
<summary><strong>main.cpp:1751: 右ドラッグでカメラ回転</strong></summary>

### コード

```cpp
if (isSceneRightCameraDragging) {
	ImVec2 mouseDelta = ImGui::GetIO().MouseDelta;
	cameraTransform.rotate.y += mouseDelta.x * editorCameraRotateSpeed;
	cameraTransform.rotate.x += mouseDelta.y * editorCameraRotateSpeed;
	cameraTransform.rotate.x =
		(std::clamp)(cameraTransform.rotate.x, -1.5f, 1.5f);
}
```

### ここでやっていること

- 横移動で Y 回転を変えます。
- 縦移動で X 回転を変えます。
- X 回転は上下に回りすぎないよう `-1.5f` から `1.5f` に制限します。

</details>

<details>
<summary><strong>main.cpp:1759: ホイールで前後移動</strong></summary>

### コード

```cpp
if (isSceneHovered && ImGui::GetIO().MouseWheel != 0.0f) {
	float pitchCos = std::cos(cameraTransform.rotate.x);
	Vector3 wheelCameraForward{
		std::sin(cameraTransform.rotate.y) * pitchCos,
		-std::sin(cameraTransform.rotate.x),
		std::cos(cameraTransform.rotate.y) * pitchCos
	};
	cameraTransform.translate.x +=
		wheelCameraForward.x * ImGui::GetIO().MouseWheel * editorCameraWheelMoveSpeed;
	cameraTransform.translate.y +=
		wheelCameraForward.y * ImGui::GetIO().MouseWheel * editorCameraWheelMoveSpeed;
	cameraTransform.translate.z +=
		wheelCameraForward.z * ImGui::GetIO().MouseWheel * editorCameraWheelMoveSpeed;
}
```

### ここでやっていること

- カメラの向いている方向を作ります。
- ホイール量に合わせて、その方向へ前後移動します。

</details>

---

## アセットの表示とドラッグ追加

<details>
<summary><strong>main.cpp:2503: Project のアセット一覧</strong></summary>

### コード

```cpp
const char* assetPaths[] = {
	"resources/ball.png",
	"resources/monsterBall.png",
	"resources/uvChecker.png",
	"resources/plane.obj",
	"resources/plane.mtl",
	"resources/sound/maou_19_12345.wav",
};
```

### ここでやっていること

- Project パネルに表示するアセットを配列で管理します。
- `.png` は画像サムネイルとして表示します。
- `.obj`、`.mtl`、`.wav` は文字アイコンで表示します。

</details>

<details>
<summary><strong>main.cpp:2521: PNG は画像として表示</strong></summary>

### コード

```cpp
if (isPng && textureIndex >= 0) {
	ImGui::Image(
		ImTextureRef(textureSrvHandlesGPU[textureIndex].ptr),
		ImVec2(48.0f, 48.0f));
	if (ImGui::IsItemClicked()) {
		selectedAssetPath = relativePath;
	}
	if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
		ImGui::SetDragDropPayload("ASSET_PATH", relativePath.c_str(), relativePath.size() + 1);
		ImGui::Text("%s", relativePath.c_str());
		ImGui::EndDragDropSource();
	}
```

### ここでやっていること

- PNG は `ImGui::Image()` でサムネイル表示します。
- クリックすると Inspector の選択アセットへ反映します。
- ドラッグすると `ASSET_PATH` として Scene へ渡せるようにします。

</details>

<details>
<summary><strong>main.cpp:2536: PNG 以外は種類別アイコン</strong></summary>

### コード

```cpp
else {
	bool isSelected = selectedAssetPath == relativePath;
	auto assetIcon = "FILE";
	if (hasExtension(relativePath, ".wav")) {
		assetIcon = "WAV";
	}
	else if (hasExtension(relativePath, ".obj")) {
		assetIcon = "OBJ";
	}
	else if (hasExtension(relativePath, ".mtl")) {
		assetIcon = "MTL";
	}
	if (ImGui::Button(assetIcon, ImVec2(58.0f, 48.0f))) {
		selectedAssetPath = relativePath;
	}
```

### ここでやっていること

- 画像化できないファイルは、拡張子に応じた文字アイコンで表示します。
- `.wav` は `WAV`、`.obj` は `OBJ`、`.mtl` は `MTL` です。
- 画像でないアセットもクリックとドラッグの対象にしています。

</details>

<details>
<summary><strong>main.cpp:1645: Scene へのドロップ受け取り</strong></summary>

### コード

```cpp
if (ImGui::BeginDragDropTarget()) {
	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
		std::string droppedAsset(
			static_cast<const char*>(payload->Data), static_cast<size_t>(payload->DataSize - 1));
		selectedAssetPath = droppedAsset;
		int32_t droppedTextureIndex = getTextureIndex(droppedAsset);
		if (droppedTextureIndex >= 0) {
```

### ここでやっていること

- Project からドラッグされたアセットパスを受け取ります。
- PNG なら TextureIndex を取って Sprite として追加します。
- `.obj` なら Model として追加します。

</details>

---

## 移動 / 回転 / 拡縮ギズモ

<details>
<summary><strong>main.cpp:1837: 移動量を Transform に反映</strong></summary>

### コード

```cpp
auto applyMoveAxis = [&](int32_t axisIndex, float amount) {
	if (axisIndex == 0) {
		selectedModelTransform->translate.x += amount;
	}
	else if (axisIndex == 1) {
		selectedModelTransform->translate.y += amount;
	}
	else {
		selectedModelTransform->translate.z += amount;
	}
	syncSelectedPlacedObjectToGameObject();
};
```

### ここでやっていること

- X / Y / Z のどの軸を動かしたかを `axisIndex` で判定します。
- 選択中のモデルの `translate` を更新します。
- 最後に `EditorScene` 側の GameObject へ同期します。

</details>

<details>
<summary><strong>main.cpp:1863: 移動ギズモの描画とドラッグ判定</strong></summary>

### コード

```cpp
auto drawMoveAxisGizmo = [&](const char* id, const ImVec2& origin, const ImVec2& end, ImU32 color,
                             int32_t axisIndex) {
	sceneDrawList->AddLine(origin, end, color, 3.0f);

	ImVec2 axisVector{end.x - origin.x, end.y - origin.y};
	float axisLengthSquared = axisVector.x * axisVector.x + axisVector.y * axisVector.y;
	if (axisLengthSquared > 0.0001f) {
		float inverseLength = 1.0f / std::sqrt(axisLengthSquared);
```

### ここでやっていること

- Scene 上に色付きの軸線を描きます。
- 軸の先端には三角形の矢印を描きます。
- `InvisibleButton` を重ねて、矢印をドラッグできるようにしています。

</details>

<details>
<summary><strong>main.cpp:1944: 回転ギズモ</strong></summary>

### コード

```cpp
auto drawRotateGizmo = [&](const ImVec2& center) {
	drawEllipse(center, 58.0f, 58.0f, IM_COL32(80, 220, 90, 220));
	drawEllipse(center, 28.0f, 58.0f, IM_COL32(230, 70, 70, 220));
	drawEllipse(center, 58.0f, 28.0f, IM_COL32(80, 120, 240, 220));

	ImGui::SetCursorScreenPos(ImVec2(center.x - 66.0f, center.y - 66.0f));
	ImGui::InvisibleButton("RotateGizmo", ImVec2(132.0f, 132.0f));
```

### ここでやっていること

- 楕円を 3 つ重ねて、回転用ギズモの見た目を作ります。
- 中心付近に当たり判定を作り、ドラッグで回転値を変えます。

</details>

<details>
<summary><strong>main.cpp:1984: 現在のツールで表示を切り替える</strong></summary>

### コード

```cpp
if (activeEditorTool == 1) {
	drawMoveAxisGizmo("MoveGizmoX", gizmoOrigin, gizmoX, IM_COL32(230, 70, 70, 255), 0);
	drawMoveAxisGizmo("MoveGizmoY", gizmoOrigin, gizmoY, IM_COL32(80, 220, 90, 255), 1);
	drawMoveAxisGizmo("MoveGizmoZ", gizmoOrigin, gizmoZ, IM_COL32(80, 120, 240, 255), 2);
}
else if (activeEditorTool == 2) {
	drawRotateGizmo(gizmoOrigin);
}
else if (activeEditorTool == 3) {
	drawScaleAxisGizmo("ScaleGizmoX", gizmoOrigin, gizmoX, IM_COL32(230, 70, 70, 255), 0);
	drawScaleAxisGizmo("ScaleGizmoY", gizmoOrigin, gizmoY, IM_COL32(80, 220, 90, 255), 1);
	drawScaleAxisGizmo("ScaleGizmoZ", gizmoOrigin, gizmoZ, IM_COL32(80, 120, 240, 255), 2);
}
```

### ここでやっていること

- `activeEditorTool` が 1 なら移動ギズモを出します。
- 2 なら回転ギズモを出します。
- 3 なら拡縮ギズモを出します。

</details>

---

## Hierarchy と Inspector

<details>
<summary><strong>EditorScene.h:13: Component 種類</strong></summary>

### コード

```cpp
enum class EditorComponentType {
	Transform,
	ModelRenderer,
	SpriteRenderer,
	Light,
	Camera,
	AudioSource,
};
```

### ここでやっていること

- GameObject に付けられる Component の種類を定義しています。
- `Transform` は基本 Component です。
- `ModelRenderer`、`SpriteRenderer`、`Light`、`Camera`、`AudioSource` を追加できます。

</details>

<details>
<summary><strong>EditorScene.h:21: GameObject の構造</strong></summary>

### コード

```cpp
struct EditorGameObject {
	int32_t id;
	int32_t parentId;
	bool isActive;
	std::string name;
	Vector3 translate;
	Vector3 rotate;
	Vector3 scale;
	std::vector<int32_t> children;
	std::vector<EditorComponent> components;
};
```

### ここでやっていること

- `id` で GameObject を識別します。
- `parentId` と `children` で親子関係を持ちます。
- `translate / rotate / scale` で Transform を持ちます。
- `components` に Component 一覧を持ちます。

</details>

<details>
<summary><strong>main.cpp:2138: Hierarchy パネル</strong></summary>

### コード

```cpp
ImGui::Begin("ヒエラルキー###Hierarchy", nullptr, fixedWindowFlags);
ImGui::InputText("検索", hierarchyFilter, _countof(hierarchyFilter));
const char* objectNames[] = {
	"モデル",
	"スプライト",
	"ライト",
	"デバッグカメラ",
};
```

### ここでやっていること

- 左側の Hierarchy パネルを作ります。
- 検索欄を持ちます。
- 基本表示名として、モデル、スプライト、ライト、デバッグカメラを持っています。

</details>

<details>
<summary><strong>main.cpp:2183: Hierarchy で選択</strong></summary>

### コード

```cpp
bool isSelected = selectedEditorGameObjectId == gameObject->id;
if (ImGui::Selectable(label.c_str(), isSelected)) {
	selectedEditorGameObjectId = gameObject->id;
	selectedPlacedSceneObjectIndex = -1;
	syncLegacySelection();
}
```

### ここでやっていること

- Hierarchy の行をクリックしたら、その GameObject ID を選択中 ID に入れます。
- `syncLegacySelection()` で Scene 側の選択と合わせます。
- これにより、Inspector と Scene ギズモの対象をそろえます。

</details>

<details>
<summary><strong>main.cpp:2257: Inspector の GameObject 編集</strong></summary>

### コード

```cpp
if (selectedEditorGameObject != nullptr) {
	constexpr auto kEditorScenePath = "resources/editorScene.scene";
	constexpr auto kEditorPrefabPath = "resources/editorPrefab.prefab";

	if (ImGui::CollapsingHeader("GameObject", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("有効", &selectedEditorGameObject->isActive);
		ImGui::InputText("名前", selectedGameObjectName, _countof(selectedGameObjectName));
```

### ここでやっていること

- 選択中 GameObject の有効フラグを編集します。
- 名前を編集します。
- Transform、複製、削除、Undo / Redo、Scene / Prefab、Component 操作へ続きます。

</details>

<details>
<summary><strong>main.cpp:2356: Component 追加 / 削除</strong></summary>

### コード

```cpp
if (ImGui::CollapsingHeader("Components", ImGuiTreeNodeFlags_DefaultOpen)) {
	const char* componentNames[] = {
		"ModelRenderer",
		"SpriteRenderer",
		"Light",
		"Camera",
		"AudioSource",
	};
	ImGui::Combo("追加するComponent", &selectedAddComponentIndex, componentNames, _countof(componentNames));
	if (ImGui::Button("Component追加")) {
		editorScene.PushUndo();
		editorScene.AddComponent(selectedEditorGameObject->id,
		                         ComponentTypeFromIndex(selectedAddComponentIndex));
```

### ここでやっていること

- 追加する Component を Combo で選びます。
- ボタンを押したら Undo 用に履歴を積んでから Component を追加します。
- 既存 Component は TreeNode で表示し、`Transform` 以外は削除できます。

</details>

---

## Scene / Prefab / Undo

<details>
<summary><strong>main.cpp:2329: Scene / Prefab ボタン</strong></summary>

### コード

```cpp
if (ImGui::CollapsingHeader("Scene / Prefab", ImGuiTreeNodeFlags_DefaultOpen)) {
	if (ImGui::Button("Scene保存")) {
		editorScene.SaveScene(kEditorScenePath);
	}
	ImGui::SameLine();
	if (ImGui::Button("Scene読込")) {
		if (editorScene.LoadScene(kEditorScenePath) && !editorScene.GetGameObjects().empty()) {
			selectedEditorGameObjectId = editorScene.GetGameObjects()[0].id;
```

### ここでやっていること

- `resources/editorScene.scene` に Scene を保存します。
- 保存済み Scene を読み込みます。
- 読み込み後は先頭 GameObject を選択します。
- Prefab 保存 / Prefab 生成も同じ場所にあります。

</details>

<details>
<summary><strong>EditorScene.cpp:230: Scene 保存</strong></summary>

### コード

```cpp
bool EditorScene::SaveScene(const std::string& filePath) const {
	std::ofstream file(filePath);
	if (!file.is_open()) {
		return false;
	}

	for (const EditorGameObject& gameObject : gameObjects_) {
		file << "GameObject|"
		     << gameObject.id << "|"
		     << gameObject.parentId << "|"
		     << gameObject.name << "|"
```

### ここでやっていること

- テキスト形式で Scene を保存します。
- GameObject 行と Component 行を分けて出力します。
- `|` 区切りなので、あとで読み込み時に分解しやすい形です。

</details>

<details>
<summary><strong>EditorScene.cpp:267: Scene 読み込み</strong></summary>

### コード

```cpp
bool EditorScene::LoadScene(const std::string& filePath) {
	std::ifstream file(filePath);
	if (!file.is_open()) {
		return false;
	}

	std::vector<EditorGameObject> loadedGameObjects;
	std::string line;

	while (std::getline(file, line)) {
		std::vector<std::string> elements = SplitLine(line, '|');
```

### ここでやっていること

- 保存した Scene ファイルを 1 行ずつ読みます。
- `SplitLine()` で `|` 区切りにします。
- `GameObject` 行なら GameObject を復元し、`Component` 行なら対応する GameObject に Component を追加します。

</details>

<details>
<summary><strong>EditorScene.cpp:360: Undo / Redo</strong></summary>

### コード

```cpp
void EditorScene::PushUndo() {
	undoStack_.push_back(gameObjects_);
	redoStack_.clear();
}

bool EditorScene::Undo() {
	if (undoStack_.empty()) {
		return false;
	}

	redoStack_.push_back(gameObjects_);
	gameObjects_ = undoStack_.back();
```

### ここでやっていること

- `PushUndo()` は現在の GameObject 配列を保存します。
- `Undo()` は保存していた状態へ戻します。
- `Redo()` は Undo 前の状態へ戻します。

この実装は、GameObject 配列全体をスナップショットとして持つ単純な方式です。

</details>

---

## 環境、マテリアル、不可視オブジェクト表示

<details>
<summary><strong>main.cpp:2403: 背景色とギズモ表示</strong></summary>

### コード

```cpp
if (ImGui::CollapsingHeader("環境 / 背景", ImGuiTreeNodeFlags_DefaultOpen)) {
	ImGui::ColorEdit4("背景色", sceneClearColor);
	ImGui::Checkbox("ギズモ表示", &isSceneGizmoVisible);
	ImGui::Checkbox("ライトアイコン", &isLightGizmoVisible);
	ImGui::Checkbox("カメラアイコン", &isCameraGizmoVisible);
}
```

### ここでやっていること

- Scene のクリア色を Inspector から変えられます。
- ライトやカメラなど、通常は形がないものの表示を切り替えます。

</details>

<details>
<summary><strong>main.cpp:2409: モデルのマテリアル色</strong></summary>

### コード

```cpp
if (ImGui::CollapsingHeader("モデル / マテリアル", ImGuiTreeNodeFlags_DefaultOpen)) {
	bool isLighting = sphereMaterialData->enableLighting != FALSE;
	ImGui::Checkbox("ライティング", &isLighting);
	ImGui::ColorEdit4("マテリアル色", &sphereMaterialData->color.x);
	sphereMaterialData->enableLighting = isLighting ? TRUE : FALSE;
}
```

### ここでやっていること

- モデルのライティング有効 / 無効を切り替えます。
- モデルのマテリアル色を `ColorEdit4` で編集します。
- ImGui の bool を DirectX 側の `TRUE / FALSE` へ反映しています。

</details>

<details>
<summary><strong>main.cpp:2003: Light アイコン</strong></summary>

### コード

```cpp
if (isSceneGizmoVisible && isLightGizmoVisible) {
	ImVec2 lightIconPosition = projectWorldPosition(directionalLightIconPosition);
	ImU32 lightIconColor = IM_COL32(255, 220, 80, 255);
	sceneDrawList->AddCircleFilled(lightIconPosition, 8.0f, lightIconColor);
	for (int32_t rayIndex = 0; rayIndex < 8; ++rayIndex) {
```

### ここでやっていること

- ライトの位置を Scene 上にアイコンとして表示します。
- 小さい円と放射状の線で、ライトだと分かる見た目にしています。
- アイコンをクリックするとライトを選択できます。

</details>

---

## 行列更新と描画

<details>
<summary><strong>main.cpp:2653: 行列と定数バッファ更新</strong></summary>

### コード

```cpp
cameraMatrix = MakeAffineMatrix(
	cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
viewMatrix = Inverse(cameraMatrix);
Matrix4x4 spriteWorldMatrix = MakeAffineMatrix(
	spriteTransform.scale, spriteTransform.rotate, spriteTransform.translate);
Matrix4x4 spriteWorldViewProjectionMatrix = Multiply(spriteWorldMatrix, spriteProjectionMatrix);
Matrix4x4 worldMatrix = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
Matrix4x4 worldViewProjectionMatrix = Multiply(worldMatrix, Multiply(viewMatrix, projectionMatrix));
```

### ここでやっていること

- カメラ行列から View 行列を作ります。
- Sprite 用と Model 用で別々に WVP を作ります。
- UI で変えた Transform が、ここで描画用の行列へ変換されます。

</details>

<details>
<summary><strong>main.cpp:2699: 描画前の DirectX12 設定</strong></summary>

### コード

```cpp
commandList->SetGraphicsRootSignature(rootSignature.Get());
commandList->SetPipelineState(graphicsPipelineState.Get());
commandList->SetGraphicsRootConstantBufferView(2, directionalLightResource->GetGPUVirtualAddress());
ID3D12DescriptorHeap* descriptorHeaps[] = {srvDescriptorHeap};
commandList->SetDescriptorHeaps(1, descriptorHeaps);
commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], FALSE, &dsvHandle);
```

### ここでやっていること

- RootSignature と PipelineState をセットします。
- ライトの定数バッファを GPU へ渡します。
- SRV DescriptorHeap をセットします。
- 描画先の RTV と DSV を指定します。

</details>

<details>
<summary><strong>main.cpp:2707: 背景と DepthBuffer のクリア</strong></summary>

### コード

```cpp
commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], sceneClearColor, 0, nullptr);
commandList->ClearDepthStencilView(
	dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
```

### ここでやっていること

- `sceneClearColor` で Scene 背景を塗りつぶします。
- DepthBuffer を `1.0f` でクリアします。
- これにより、奥行きがあるモデル同士でも前後関係を判定できます。

</details>

<details>
<summary><strong>main.cpp:2719: 追加された Scene Object の描画</strong></summary>

### コード

```cpp
for (const EditorSceneObject& sceneObject : editorSceneObjects) {
	if (sceneObject.type == EditorSceneObjectType::Sprite) {
		int32_t textureIndex =
			(std::clamp)(sceneObject.textureIndex, 0, static_cast<int32_t>(_countof(textureFilePaths)) - 1);
		commandList->SetGraphicsRootConstantBufferView(
			0, spriteMaterialResource->GetGPUVirtualAddress());
```

### ここでやっていること

- Scene に追加された Sprite / Model を順番に描画します。
- Sprite は `sceneObject.textureIndex` を使って、ドラッグされた PNG のテクスチャを表示します。
- Model は OBJ 用の頂点バッファとテクスチャを使って描画します。

</details>

---

## 音声

<details>
<summary><strong>main.cpp:562: wav 読み込み</strong></summary>

### コード

```cpp
SoundData soundData = SoundLoadWave("resources/sound/maou_19_12345.wav");
IXAudio2SourceVoice* sourceVoice = nullptr;
hr = xAudio2->CreateSourceVoice(&sourceVoice, &soundData.wfex);
```

### ここでやっていること

- `resources/sound/maou_19_12345.wav` を読み込みます。
- XAudio2 の SourceVoice を作ります。

</details>

<details>
<summary><strong>main.cpp:573: wav バッファ登録</strong></summary>

### コード

```cpp
XAUDIO2_BUFFER soundBuffer{};
soundBuffer.pAudioData = soundData.pBuffer;
soundBuffer.AudioBytes = soundData.bufferSize;
soundBuffer.Flags = XAUDIO2_END_OF_STREAM;
hr = sourceVoice->SubmitSourceBuffer(&soundBuffer);
```

### ここでやっていること

- wav のバッファを SourceVoice に渡します。
- 現在のコードでは `SubmitSourceBuffer()` までで、`Start()` は呼んでいません。
- そのため、起動直後に音が自動再生される形にはしていません。

</details>

---

## ビルド

Visual Studio から `CG2.sln` を開いて、`Debug | x64` でビルドします。  
PowerShell から確認する場合は、Visual Studio 2022 の MSBuild を使います。

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" CG2.sln /p:Configuration=Debug /p:Platform=x64
```

---

## 読む順番

最初に読むなら、次の順番が分かりやすいです。

1. `main.cpp:479` の `WinMain()`
2. `main.cpp:1336` のメインループ
3. `main.cpp:1083` からのエディタレイアウト
4. `main.cpp:1645` のアセットドロップ
5. `main.cpp:1837` からのギズモ処理
6. `main.cpp:2138` からの Hierarchy / Inspector
7. `EditorScene.h` と `EditorScene.cpp` の GameObject / Component / Save / Load
8. `main.cpp:2653` からの行列更新と描画

---

## 現在できること

- Scene 上でモデルとスプライトを表示できます。
- Project から PNG / OBJ を Scene へドラッグ追加できます。
- PNG はサムネイル、PNG 以外は種類別アイコンで表示されます。
- 選択中モデルに移動 / 回転 / 拡縮ギズモを表示できます。
- 右ドラッグでカメラ回転、中ドラッグで平行移動、ホイールで前後移動できます。
- 背景色、ライト、カメラアイコン表示を Inspector から変更できます。
- GameObject の名前変更、複製、削除確認、Undo / Redo ができます。
- Component の追加 / 削除ができます。
- Hierarchy のドラッグで親子関係を作れます。
- Scene 保存 / 読み込み、Prefab 保存 / 生成ができます。

