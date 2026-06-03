#include "EditorAssetFactory.h"

#include "EditorAssetUtility.h"
#include "EditorComponentUtility.h"

#include <cstddef>

namespace {
	bool IsPrimitiveModelAsset(const std::string& assetPath) {
		// 自前モデルまで基本形扱いすると板表示や誤 Collider の原因になるので、内部基本形だけに限定する。
		return EditorAssetUtility::IsBuiltInPrimitiveAssetPath(assetPath);
	}

	Vector3 GetPrimitiveColliderSize(EditorModelMeshType meshType) {
		// Collider サイズは生成メッシュのおおよその外形に合わせる
		switch (meshType) {
		case EditorModelMeshType::Box:
			return Vector3{1.6f, 0.7f, 1.0f};
		case EditorModelMeshType::Torus:
			return Vector3{1.1f, 0.35f, 1.1f};
		case EditorModelMeshType::Ico:
			return Vector3{1.2f, 1.2f, 1.2f};
		case EditorModelMeshType::Sphere:
			return Vector3{1.0f, 1.0f, 1.0f};
		case EditorModelMeshType::Cube:
		case EditorModelMeshType::Cylinder:
		case EditorModelMeshType::Cone:
		case EditorModelMeshType::Plane:
		case EditorModelMeshType::Count:
		default:
			return Vector3{1.0f, 1.0f, 1.0f};
		}
	}

	bool UsesSphereCollider(EditorModelMeshType meshType) {
		// 見た目が球に近い FBX は、箱ではなく球の当たり判定を初期追加する。
		return meshType == EditorModelMeshType::Sphere ||
			meshType == EditorModelMeshType::Ico;
	}

	bool UsesMeshCollider(EditorModelMeshType meshType) {
		// トーラスは穴を塞がないように MeshCollider で初期化する。
		return meshType == EditorModelMeshType::Torus;
	}
}

void EditorAssetFactory::Initialize(
	EditorScene* editorScene,
	EditorSceneObjectManager* sceneObjectManager,
	const std::vector<std::string>* textureFilePaths,
	std::vector<std::string>* consoleMessages) {
	editorScene_ = editorScene;  // CreateModelGameObject / CreateSpriteGameObject で使う外部データを保持する
	sceneObjectManager_ = sceneObjectManager;
	textureFilePaths_ = textureFilePaths;
	consoleMessages_ = consoleMessages;
}

void EditorAssetFactory::Update() {
}

void EditorAssetFactory::Draw() {
}

void EditorAssetFactory::CreateModelGameObject(
	const std::string& assetPath,
	const Vector3& position,
	int32_t& selectedGameObjectId,
	int32_t& selectedPlacedSceneObjectIndex,
	int32_t& selectedSceneObject) {
	if (editorScene_ == nullptr || sceneObjectManager_ == nullptr) {
		return;
	}

	// モデルは現時点で textureIndex 2 の checker texture を使う
	selectedPlacedSceneObjectIndex = sceneObjectManager_->CreateObject(
		EditorSceneObjectType::Model,
		2,
		Transforms{{1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, position},
		EditorAssetUtility::GetFilename(assetPath));
	if (selectedPlacedSceneObjectIndex < 0) {
		return;
	}

	EditorModelMeshType meshType = EditorAssetUtility::GetModelMeshType(assetPath);  // FBX 名から描画する基本形を決める
	sceneObjectManager_->GetSceneObjects()[static_cast<size_t>(selectedPlacedSceneObjectIndex)].meshType = meshType;
	selectedSceneObject = 0;  // 旧選択分類では Model が 0
	editorScene_->PushUndo();  // 生成操作を Undo 対象にする
	selectedGameObjectId = editorScene_->CreateGameObject(EditorAssetUtility::GetFilename(assetPath));  // Scene の正データとして GameObject を作る
	// 描画用 SceneObject と GameObject を ID で紐づける
	sceneObjectManager_->GetSceneObjects()[static_cast<size_t>(selectedPlacedSceneObjectIndex)].gameObjectId =
		selectedGameObjectId;
	editorScene_->AddComponent(selectedGameObjectId, EditorComponentType::MeshFilter);  // Unity の基本形と同じく MeshFilter を持たせる
	editorScene_->AddComponent(selectedGameObjectId, EditorComponentType::ModelRenderer);  // Inspector と Synchronizer がモデルとして扱えるよう ModelRenderer を付ける
	if (IsPrimitiveModelAsset(assetPath)) {
		EditorComponentType colliderType = EditorComponentType::BoxCollider;
		bool shouldAddRigidBody = true;
		if (UsesMeshCollider(meshType)) {
			colliderType = EditorComponentType::MeshCollider;
			shouldAddRigidBody = false;  // Jolt の MeshShape は静的専用なので、穴付きトーラスは初期状態を静的メッシュにする。
		}
		else if (UsesSphereCollider(meshType)) {
			colliderType = EditorComponentType::SphereCollider;
		}

		editorScene_->AddComponent(selectedGameObjectId, colliderType);
		if (shouldAddRigidBody) {
			editorScene_->AddComponent(selectedGameObjectId, EditorComponentType::RigidBody);
		}
	}

	// GameObject の Transform と Renderer の AssetPath を初期化する
	if (EditorGameObject* gameObject = editorScene_->FindGameObject(selectedGameObjectId)) {
		gameObject->translate = position;
		gameObject->scale = {1.0f, 1.0f, 1.0f};
		for (EditorComponent& component : gameObject->components) {
			if (component.type == EditorComponentType::MeshFilter ||
				component.type == EditorComponentType::ModelRenderer) {
				component.assetPath = assetPath;
			}
			else if (component.type == EditorComponentType::BoxCollider) {
				component.colliderSize = GetPrimitiveColliderSize(meshType);
			}
			else if (component.type == EditorComponentType::SphereCollider) {
				component.colliderRadius = 0.5f;  // 内部 Sphere メッシュは半径 0.5f で作る。
				component.colliderSize = GetPrimitiveColliderSize(meshType);
			}
			else if (component.type == EditorComponentType::MeshCollider) {
				component.colliderSize = GetPrimitiveColliderSize(meshType);
			}
		}
	}

	if (consoleMessages_ != nullptr) {
		consoleMessages_->push_back("Asset: Created " + assetPath);  // Console に生成結果を出す
	}
}

void EditorAssetFactory::CreateSpriteGameObject(
	const std::string& assetPath,
	const Vector3& position,
	int32_t& selectedGameObjectId,
	int32_t& selectedPlacedSceneObjectIndex,
	int32_t& selectedSceneObject) {
	if (editorScene_ == nullptr || sceneObjectManager_ == nullptr || textureFilePaths_ == nullptr) {
		return;
	}

	int32_t textureIndex = EditorAssetUtility::GetTextureIndex(*textureFilePaths_, assetPath);  // assetPath が登録済み Texture にあれば、その SRV 番号を使う
	if (textureIndex < 0) {
		textureIndex = 0;  // 見つからない場合は先頭 Texture を仮に使う
	}

	// Sprite は 128x128 の見た目で SceneObject を作る
	selectedPlacedSceneObjectIndex = sceneObjectManager_->CreateObject(
		EditorSceneObjectType::Sprite,
		textureIndex,
		Transforms{{128.0f, 128.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, position},
		EditorAssetUtility::GetFilename(assetPath));
	if (selectedPlacedSceneObjectIndex < 0) {
		return;
	}

	selectedSceneObject = 1;  // 旧選択分類では Sprite が 1
	editorScene_->PushUndo();  // 生成操作を Undo 対象にする
	selectedGameObjectId = editorScene_->CreateGameObject(EditorAssetUtility::GetFilename(assetPath));  // Scene の正データとして GameObject を作る
	// 描画用 SceneObject と GameObject を ID で紐づける
	sceneObjectManager_->GetSceneObjects()[static_cast<size_t>(selectedPlacedSceneObjectIndex)].gameObjectId =
		selectedGameObjectId;
	editorScene_->AddComponent(selectedGameObjectId, EditorComponentType::SpriteRenderer);  // Inspector と Synchronizer がスプライトとして扱えるよう SpriteRenderer を付ける

	// GameObject の Transform と Renderer の AssetPath を初期化する
	if (EditorGameObject* gameObject = editorScene_->FindGameObject(selectedGameObjectId)) {
		gameObject->translate = position;
		gameObject->scale = {128.0f, 128.0f, 1.0f};
		for (EditorComponent& component : gameObject->components) {
			if (component.type == EditorComponentType::SpriteRenderer) {
				component.assetPath = assetPath;
			}
		}
	}

	if (consoleMessages_ != nullptr) {
		consoleMessages_->push_back("Asset: Created " + assetPath);  // Console に生成結果を出す
	}
}
