#include "EditorAssetManagerAdapters.h"

#include "EditorAssetUtility.h"
#include "EditorSharedState.h"
#include "Source/Engine/Asset/AssetManager.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
	AssetTypeHandler MakeNoOpCacheHandler() {
		// Prefab / InputActionは使用時に毎回Fileから読み直す実装で、保持するCacheが無い。
		// 「何もしない」こと自体が「既に最新」と同義なのでAppliedを返す。
		AssetTypeHandler handler;
		handler.supportsHotReload = true;
		handler.reload = [](const std::string&) { return true; };
		return handler;
	}
}

void RegisterEditorAssetManagerAdapters() {
	AssetManager& assetManager = AssetManager::Get();

	{
		AssetTypeHandler modelHandler;
		modelHandler.supportsHotReload = true;
		modelHandler.reload = [](const std::string& path) {
			EditorAssetUtility::InvalidateModelAssetCache(path);
			EditorSharedState::g_editorSceneObjectManager.InvalidateAssetResources(path);
			EditorSharedState::g_editorSceneSynchronizer.Update(
				EditorSharedState::g_editorTextureFilePaths,
				EditorSharedState::g_selectedPlacedSceneObjectIndex);
			return true;
		};
		modelHandler.invalidate = [](const std::string& path) {
			EditorAssetUtility::InvalidateModelAssetCache(path);
			EditorSharedState::g_editorSceneObjectManager.InvalidateAssetResources(path);
		};
		// FBX / OBJ はバイナリ・独自書式のためテキスト検索では参照Textureを拾えない。
		// Parse済みModelDataのMaterialから、実際に使う各Texture Pathを依存として返す。
		modelHandler.getDependencies = [](const std::string& path) {
			std::vector<std::string> dependencies;
			const ModelData* modelData = EditorAssetUtility::GetSharedModelAssetData(path, false);

			if (modelData == nullptr) {
				return dependencies;
			}

			const auto appendTexturePath = [&dependencies, &path](const std::string& texturePath) {
				if (texturePath.empty()) {
					return;
				}

				try {
					const std::string normalizedPath =
						std::filesystem::path(texturePath).lexically_normal().generic_string();

					// Assets / resources 配下のみを依存として認める(絶対Pathや外部Pathは対象外)。
					if (!normalizedPath.starts_with("Assets/") && !normalizedPath.starts_with("resources/")) {
						return;
					}

					if (std::find(dependencies.begin(), dependencies.end(), normalizedPath) == dependencies.end()) {
						dependencies.push_back(normalizedPath);
					}
				}
				catch (const std::exception& e) {
					std::cerr << "ERROR: Failed to normalize texture path in model '" << path
						<< "': '" << texturePath << "' - " << e.what() << std::endl;
				}
			};

			for (const MaterialData& material : modelData->materials) {
				appendTexturePath(material.textureFilePath);
				appendTexturePath(material.normalTextureFilePath);
				appendTexturePath(material.metallicTextureFilePath);
				appendTexturePath(material.roughnessTextureFilePath);
				appendTexturePath(material.ambientOcclusionTextureFilePath);
				appendTexturePath(material.emissionTextureFilePath);
				appendTexturePath(material.heightTextureFilePath);
				appendTexturePath(material.opacityTextureFilePath);
			}

			return dependencies;
		};
		assetManager.RegisterHandler(AssetType::Model, modelHandler);
	}

	{
		// Textureはモデル用Cacheを持たないため、SceneObjectManager側のGPU資源解放だけでよい。
		AssetTypeHandler textureHandler;
		textureHandler.supportsHotReload = true;
		textureHandler.reload = [](const std::string& path) {
			EditorSharedState::g_editorSceneObjectManager.InvalidateAssetResources(path);
			EditorSharedState::g_editorSceneSynchronizer.Update(
				EditorSharedState::g_editorTextureFilePaths,
				EditorSharedState::g_selectedPlacedSceneObjectIndex);
			return true;
		};
		textureHandler.invalidate = [](const std::string& path) {
			EditorSharedState::g_editorSceneObjectManager.InvalidateAssetResources(path);
		};
		assetManager.RegisterHandler(AssetType::Texture, textureHandler);
	}

	{
		AssetTypeHandler audioHandler;
		audioHandler.supportsHotReload = true;
		audioHandler.reload = [](const std::string& path) {
			EditorSharedState::g_editorRuntimeManager.GetAudioManager().InvalidateClip(path);
			return true;
		};
		audioHandler.invalidate = [](const std::string& path) {
			EditorSharedState::g_editorRuntimeManager.GetAudioManager().InvalidateClip(path);
		};
		assetManager.RegisterHandler(AssetType::Audio, audioHandler);
	}

	{
		// AssetType::Vfxは.effectと.effectdefをまとめた分類だが、実体のCanonical Cacheは
		// 拡張子ごとに単独で分かれている:
		//  - .effect     -> EditorEffectManager::effectAssetCache_ (旧Particle Asset)
		//  - .effectdef  -> EditorVfxManager::definitionCache_ (Stage1 VFX)
		// EditorVfxManager::ResolveDefinitionは.effectを拒否するガードを持つため、同じPathが
		// 2箇所へ重複Cacheされることはない。ここでも実際の拡張子を見て対応する側だけへ
		// Invalidateを渡し、無関係なManagerへの空振り呼び出しを避ける。
		AssetTypeHandler vfxHandler;
		vfxHandler.supportsHotReload = true;
		vfxHandler.reload = [](const std::string& path) {
			if (EditorAssetUtility::HasExtension(path, ".effect")) {
				EditorSharedState::g_editorRuntimeManager.GetEffectManager().InvalidateEffectAssetCache(path);
			}
			else {
				EditorSharedState::g_editorRuntimeManager.GetVfxManager().InvalidateEffectDefinition(path);
			}
			return true;
		};
		vfxHandler.invalidate = [](const std::string& path) {
			if (EditorAssetUtility::HasExtension(path, ".effect")) {
				EditorSharedState::g_editorRuntimeManager.GetEffectManager().InvalidateEffectAssetCache(path);
			}
			else {
				EditorSharedState::g_editorRuntimeManager.GetVfxManager().InvalidateEffectDefinition(path);
			}
		};
		assetManager.RegisterHandler(AssetType::Vfx, vfxHandler);
	}

	{
		// EditorAnimationManagerのCacheはPlay中のGameObjectIdキーでしか保持されず、
		// 元ファイルPathを記録していない。「このPathのCacheだけ安全に無効化する」手段が
		// 現状存在しないため、無理に実装して無関係なGameObjectを巻き込むより
		// 未対応であることを明示する。
		AssetTypeHandler animationHandler;
		animationHandler.supportsHotReload = false;
		animationHandler.unsupportedReason =
			"Animation CacheはPlay中のGameObjectId単位で保持され、元Pathを記録していないため、"
			"ファイル単位の安全な再読込ができません(Playをやり直すと反映されます)";
		assetManager.RegisterHandler(AssetType::Animation, animationHandler);
	}

	{
		// .material Fileを実際にLoad/CacheするEditor側実装は調査した範囲では見つからなかった
		// (EditorGameBuildManager::IsRecursiveDependencyAssetのBuild対象一覧に列挙されているのみ)。
		// OBJの.mtlはModelData読み込み時に取り込まれ、Model Cache(g_cachedModelAssets)側の
		// InvalidateModelAssetCacheで一緒に無効化されるため、Materialとして新規Cacheは増やさない。
		AssetTypeHandler materialHandler = MakeNoOpCacheHandler();
		assetManager.RegisterHandler(AssetType::Material, materialHandler);
	}

	assetManager.RegisterHandler(AssetType::Prefab, MakeNoOpCacheHandler());
	assetManager.RegisterHandler(AssetType::InputAction, MakeNoOpCacheHandler());

	{
		// 実行中DLLを勝手に差し替えると危険なため、変更検知はするが反映はユーザーの
		// 明示的な再ビルド操作に委ねる。
		AssetTypeHandler scriptHandler;
		scriptHandler.supportsHotReload = false;
		scriptHandler.unsupportedReason =
			"Scriptは実行中DLLへ自動反映されません。再ビルドしてください";
		assetManager.RegisterHandler(AssetType::Script, scriptHandler);
	}
}
