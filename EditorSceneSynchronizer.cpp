#include "EditorSceneSynchronizer.h"

#include "EditorAssetUtility.h"
#include "EditorComponentUtility.h"

#include <cstddef>

#pragma warning(disable : 5045)

namespace {
	void ApplyRendererMaterial(
		EditorSceneObject& sceneObject,
		const EditorComponent* rendererComponent,
		bool isModelRenderer) {
		if (sceneObject.materialData == nullptr) {
			return;
		}

		Vector3 rendererColor = {1.0f, 1.0f, 1.0f};  // Renderer が未設定でも Mesh は白色で表示する。
		float rendererIntensity = 1.0f;  // 強さは色へ掛ける簡易 Material パラメータ。

		if (rendererComponent != nullptr) {
			rendererColor = rendererComponent->color;
			rendererIntensity = rendererComponent->intensity;
		}

		sceneObject.materialData->color = {
			rendererColor.x * rendererIntensity,
			rendererColor.y * rendererIntensity,
			rendererColor.z * rendererIntensity,
			1.0f};
		sceneObject.materialData->enableLighting = isModelRenderer ? TRUE : FALSE;
		sceneObject.materialData->useTexture = isModelRenderer ? FALSE : TRUE;  // Mesh は初期状態を白い面、Sprite は画像表示にする。
		sceneObject.materialData->padding[0] = 0.0f;
		sceneObject.materialData->padding[1] = 0.0f;
	}
}

void EditorSceneSynchronizer::Initialize(EditorScene* editorScene, EditorSceneObjectManager* sceneObjectManager) {
	editorScene_ = editorScene;  // GameObject を正として、描画用 SceneObject を作るため保持する
	sceneObjectManager_ = sceneObjectManager;
}

void EditorSceneSynchronizer::Update(
	const std::vector<std::string>& textureFilePaths,
	int32_t& selectedPlacedSceneObjectIndex) {
	if (editorScene_ == nullptr || sceneObjectManager_ == nullptr) {
		return;
	}

	std::vector<EditorSceneObject>& sceneObjects = sceneObjectManager_->GetSceneObjects();  // 描画用 SceneObject 配列を直接編集する

	// 後ろから削除することで erase 後の index ずれを避ける
	for (int32_t sceneObjectIndex = static_cast<int32_t>(sceneObjects.size()) - 1;
	     sceneObjectIndex >= 0;
	     sceneObjectIndex--) {
		const EditorSceneObject& sceneObject =
			sceneObjects[static_cast<size_t>(sceneObjectIndex)];
		// 紐づく GameObject が消えていないか確認する
		const EditorGameObject* gameObject =
			editorScene_->FindGameObject(sceneObject.gameObjectId);
		bool shouldRemove = gameObject == nullptr;

		// Renderer Component が外された SceneObject は描画対象から消す
		if (!shouldRemove && gameObject != nullptr) {
			if (sceneObject.type == EditorSceneObjectType::Model) {
				shouldRemove =
					!editorScene_->HasComponent(gameObject->id, EditorComponentType::ModelRenderer);
			}
			else {
				shouldRemove =
					!editorScene_->HasComponent(gameObject->id, EditorComponentType::SpriteRenderer);
			}
		}

		// 消す必要がない SceneObject は残す
		if (!shouldRemove) {
			continue;
		}

		sceneObjectManager_->ReleaseObject(sceneObjectIndex);  // GPU Resource を解放して配列から消す
		sceneObjects.erase(sceneObjects.begin() + sceneObjectIndex);

		// 選択中の SceneObject が消えた場合は選択解除する
		if (selectedPlacedSceneObjectIndex == sceneObjectIndex) {
			selectedPlacedSceneObjectIndex = -1;
		}
		else if (selectedPlacedSceneObjectIndex > sceneObjectIndex) {
			selectedPlacedSceneObjectIndex--;
		}
	}

	// GameObject 側に Renderer があれば、対応する SceneObject を作る / 更新する
	for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		bool hasModelRenderer =
			editorScene_->HasComponent(gameObject.id, EditorComponentType::ModelRenderer);
		bool hasSpriteRenderer =
			editorScene_->HasComponent(gameObject.id, EditorComponentType::SpriteRenderer);
		if (!hasModelRenderer && !hasSpriteRenderer) {
			continue;
		}

		// ModelRenderer を優先し、なければ SpriteRenderer として扱う
		EditorSceneObjectType sceneObjectType =
			hasModelRenderer ? EditorSceneObjectType::Model : EditorSceneObjectType::Sprite;
		int32_t sceneObjectIndex = -1;

		// 既に GameObject と紐づく SceneObject があるか探す
		for (int32_t findIndex = 0;
		     findIndex < static_cast<int32_t>(sceneObjects.size());
		     findIndex++) {
			if (sceneObjects[static_cast<size_t>(findIndex)].gameObjectId == gameObject.id) {
				sceneObjectIndex = findIndex;
				break;
			}
		}

		if (sceneObjectIndex < 0) {
			int32_t textureIndex = 2;  // モデルは checker texture、スプライトは SpriteRenderer の texture を使う
			EditorModelMeshType meshType = EditorModelMeshType::Plane;  // ModelRenderer の assetPath から基本形を選ぶ
			if (sceneObjectType == EditorSceneObjectType::Sprite) {
				textureIndex = 0;
				const EditorComponent* spriteRenderer =
					EditorComponentUtility::FindComponent(gameObject, EditorComponentType::SpriteRenderer);
				if (spriteRenderer != nullptr && !spriteRenderer->assetPath.empty()) {
					// assetPath が登録済み texture にあればその番号を使う
					int32_t foundTextureIndex =
						EditorAssetUtility::GetTextureIndex(textureFilePaths, spriteRenderer->assetPath);
					if (foundTextureIndex >= 0) {
						textureIndex = foundTextureIndex;
					}
				}
			}
			else {
				const EditorComponent* modelRenderer =
					EditorComponentUtility::FindComponent(gameObject, EditorComponentType::ModelRenderer);
				const EditorComponent* meshFilter =
					EditorComponentUtility::FindComponent(gameObject, EditorComponentType::MeshFilter);
				if (modelRenderer != nullptr && !modelRenderer->assetPath.empty()) {
					meshType = EditorAssetUtility::GetModelMeshType(modelRenderer->assetPath);
				}
				else if (meshFilter != nullptr && !meshFilter->assetPath.empty()) {
					meshType = EditorAssetUtility::GetModelMeshType(meshFilter->assetPath);
				}
			}

			sceneObjectIndex = sceneObjectManager_->CreateObject(
				sceneObjectType,
				textureIndex,
				Transforms{gameObject.scale, gameObject.rotate, gameObject.translate},
				gameObject.name);
			if (sceneObjectIndex < 0) {
				continue;
			}

			sceneObjects[static_cast<size_t>(sceneObjectIndex)].gameObjectId = gameObject.id;  // 作成した SceneObject と GameObject を ID で紐づける
			sceneObjects[static_cast<size_t>(sceneObjectIndex)].meshType = meshType;
		}

		// GameObject の Transform を描画用 SceneObject へコピーする
		EditorSceneObject& sceneObject =
			sceneObjects[static_cast<size_t>(sceneObjectIndex)];
		sceneObject.transform.translate = gameObject.translate;
		sceneObject.transform.rotate = gameObject.rotate;
		sceneObject.transform.scale = gameObject.scale;
		sceneObject.name = gameObject.name;
		if (sceneObject.type == EditorSceneObjectType::Sprite) {
			// Sprite は SpriteRenderer の assetPath に合わせて textureIndex を更新する
			const EditorComponent* spriteRenderer =
				EditorComponentUtility::FindComponent(gameObject, EditorComponentType::SpriteRenderer);
			ApplyRendererMaterial(sceneObject, spriteRenderer, false);
			if (spriteRenderer != nullptr && !spriteRenderer->assetPath.empty()) {
				int32_t foundTextureIndex =
					EditorAssetUtility::GetTextureIndex(textureFilePaths, spriteRenderer->assetPath);
				if (foundTextureIndex >= 0) {
					sceneObject.textureIndex = foundTextureIndex;
				}
			}
		}
		else {
			const EditorComponent* modelRenderer =
				EditorComponentUtility::FindComponent(gameObject, EditorComponentType::ModelRenderer);
			const EditorComponent* meshFilter =
				EditorComponentUtility::FindComponent(gameObject, EditorComponentType::MeshFilter);
			std::string modelAssetPath;  // 実メッシュ描画に使うモデルパス。ModelRenderer を優先し、なければ MeshFilter を使う。
			ApplyRendererMaterial(sceneObject, modelRenderer, true);
			sceneObject.textureIndex = 2;  // Shader には Texture SRV が必要なので渡すが、Model Material 側では Texture を無効化する。
			if (modelRenderer != nullptr && !modelRenderer->assetPath.empty()) {
				modelAssetPath = modelRenderer->assetPath;
				sceneObject.meshType = EditorAssetUtility::GetModelMeshType(modelRenderer->assetPath);
			}
			else if (meshFilter != nullptr && !meshFilter->assetPath.empty()) {
				modelAssetPath = meshFilter->assetPath;
				sceneObject.meshType = EditorAssetUtility::GetModelMeshType(meshFilter->assetPath);
			}
			else {
				sceneObject.meshType = EditorModelMeshType::Plane;
			}

			if (!modelAssetPath.empty() &&
				!EditorAssetUtility::IsBuiltInPrimitiveAssetPath(modelAssetPath)) {
				ModelData modelData{};
				if (sceneObject.assetPath != modelAssetPath || !sceneObject.usesCustomMesh) {
					if (EditorAssetUtility::LoadModelAsset(modelAssetPath, modelData)) {
						sceneObjectManager_->SetCustomModelMesh(sceneObjectIndex, modelAssetPath, modelData);
					}
					else {
						sceneObjectManager_->ClearCustomModelMesh(sceneObjectIndex);  // 読み込み失敗時に前回メッシュを残すと見た目だけ古いモデルが残る。
					}
				}
			}
			else if (sceneObject.usesCustomMesh) {
				sceneObjectManager_->ClearCustomModelMesh(sceneObjectIndex);
			}
		}
	}
}

void EditorSceneSynchronizer::Draw() {
}
