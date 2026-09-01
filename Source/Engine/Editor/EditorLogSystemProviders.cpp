#include "EditorLogSystemProviders.h"

#include "EditorProfilerManager.h"
#include "EditorRuntimePropertyManager.h"
#include "EditorScene.h"
#include "EditorSharedState.h"
#include "EditorWeaponManager.h"

#include <functional>
#include <iomanip>
#include <sstream>

namespace {
	std::string FormatFloat(float value) {
		std::ostringstream stream;
		stream << std::fixed << std::setprecision(3) << value;
		return stream.str();
	}

	struct FixedSystemFieldProvider {
		const char* category;
		const char* name;
		bool (*resolve)(const LogSystemFieldContext& context, std::string& outValue);
	};

	constexpr char kPhysicsCategory[] = "Physics";
	constexpr char kRenderingCategory[] = "Rendering";
	constexpr char kWeaponCategory[] = "Weapon";
	constexpr char kGameStateCategory[] = "GameState";

	const FixedSystemFieldProvider kFixedSystemFieldProviders[] = {
		{kPhysicsCategory, "Gravity", [](const LogSystemFieldContext& context, std::string& outValue) {
			if (context.editorScene == nullptr) {
				return false;
			}
			const Vector3& gravity = context.editorScene->GetPhysicsSettings().gravity;
			outValue = "(" + FormatFloat(gravity.x) + ", " + FormatFloat(gravity.y) + ", " + FormatFloat(gravity.z) + ")";
			return true;
		}},
		{kPhysicsCategory, "FixedTimeStep", [](const LogSystemFieldContext& context, std::string& outValue) {
			if (context.editorScene == nullptr) {
				return false;
			}
			outValue = FormatFloat(context.editorScene->GetPhysicsSettings().fixedTimeStep);
			return true;
		}},
		{kPhysicsCategory, "PhysicsBodyFailureCount", [](const LogSystemFieldContext&, std::string& outValue) {
			outValue = std::to_string(EditorSharedState::g_physicsBodyFailureCount);
			return true;
		}},
		{kPhysicsCategory, "LastPhysicsBodyFailure", [](const LogSystemFieldContext&, std::string& outValue) {
			outValue = EditorSharedState::g_lastPhysicsBodyFailure;
			return true;
		}},
		{kRenderingCategory, "GpuFrameMilliseconds", [](const LogSystemFieldContext&, std::string& outValue) {
			outValue = FormatFloat(EditorSharedState::g_renderProfile.gpuFrameMilliseconds);
			return true;
		}},
		{kRenderingCategory, "SceneObjectCount", [](const LogSystemFieldContext&, std::string& outValue) {
			outValue = std::to_string(EditorSharedState::g_renderProfile.sceneObjectCount);
			return true;
		}},
		{kRenderingCategory, "InstanceCount", [](const LogSystemFieldContext&, std::string& outValue) {
			outValue = std::to_string(EditorSharedState::g_renderProfile.instanceCount);
			return true;
		}},
		{kRenderingCategory, "LocalVideoMemoryUsage", [](const LogSystemFieldContext&, std::string& outValue) {
			outValue = std::to_string(EditorSharedState::g_renderProfile.localVideoMemoryUsage);
			return true;
		}},
		{kRenderingCategory, "LocalVideoMemoryBudget", [](const LogSystemFieldContext&, std::string& outValue) {
			outValue = std::to_string(EditorSharedState::g_renderProfile.localVideoMemoryBudget);
			return true;
		}},
		{kWeaponCategory, "ActiveProjectileCount", [](const LogSystemFieldContext& context, std::string& outValue) {
			if (context.weaponManager == nullptr) {
				return false;
			}
			outValue = std::to_string(context.weaponManager->GetActiveProjectileCount());
			return true;
		}},
		{kWeaponCategory, "ProjectileCollisionSequence", [](const LogSystemFieldContext& context, std::string& outValue) {
			if (context.weaponManager == nullptr) {
				return false;
			}
			outValue = std::to_string(context.weaponManager->GetProjectileCollisionSequence());
			return true;
		}},
		{kWeaponCategory, "LastProjectileCollisionResult", [](const LogSystemFieldContext& context, std::string& outValue) {
			if (context.weaponManager == nullptr) {
				return false;
			}
			outValue = context.weaponManager->GetLastProjectileCollisionResult();
			return true;
		}},
		{kWeaponCategory, "LastProjectileGameObjectId", [](const LogSystemFieldContext& context, std::string& outValue) {
			if (context.weaponManager == nullptr) {
				return false;
			}
			outValue = std::to_string(context.weaponManager->GetLastProjectileGameObjectId());
			return true;
		}},
		{kWeaponCategory, "LastProjectileRawHitGameObjectId", [](const LogSystemFieldContext& context, std::string& outValue) {
			if (context.weaponManager == nullptr) {
				return false;
			}
			outValue = std::to_string(context.weaponManager->GetLastProjectileRawHitGameObjectId());
			return true;
		}},
		{kWeaponCategory, "LastProjectileRawHitGameObjectName", [](const LogSystemFieldContext& context, std::string& outValue) {
			if (context.weaponManager == nullptr) {
				return false;
			}
			outValue = context.weaponManager->GetLastProjectileRawHitGameObjectName();
			return true;
		}},
		{kWeaponCategory, "LastProjectileDamageTargetGameObjectId", [](const LogSystemFieldContext& context, std::string& outValue) {
			if (context.weaponManager == nullptr) {
				return false;
			}
			outValue = std::to_string(context.weaponManager->GetLastProjectileDamageTargetGameObjectId());
			return true;
		}},
		{kWeaponCategory, "LastProjectileHitDistance", [](const LogSystemFieldContext& context, std::string& outValue) {
			if (context.weaponManager == nullptr) {
				return false;
			}
			outValue = FormatFloat(context.weaponManager->GetLastProjectileHitDistance());
			return true;
		}},
		{kWeaponCategory, "LastProjectileAppliedDamage", [](const LogSystemFieldContext& context, std::string& outValue) {
			if (context.weaponManager == nullptr) {
				return false;
			}
			outValue = FormatFloat(context.weaponManager->GetLastProjectileAppliedDamage());
			return true;
		}},
		{kWeaponCategory, "LastProjectileHealthBefore", [](const LogSystemFieldContext& context, std::string& outValue) {
			if (context.weaponManager == nullptr) {
				return false;
			}
			outValue = FormatFloat(context.weaponManager->GetLastProjectileHealthBefore());
			return true;
		}},
		{kWeaponCategory, "LastProjectileHealthAfter", [](const LogSystemFieldContext& context, std::string& outValue) {
			if (context.weaponManager == nullptr) {
				return false;
			}
			outValue = FormatFloat(context.weaponManager->GetLastProjectileHealthAfter());
			return true;
		}},
		{kWeaponCategory, "LastProjectileCastOrigin", [](const LogSystemFieldContext& context, std::string& outValue) {
			if (context.weaponManager == nullptr) {
				return false;
			}
			outValue = context.weaponManager->GetLastProjectileCastOrigin();
			return true;
		}},
		{kWeaponCategory, "LastProjectileRadius", [](const LogSystemFieldContext& context, std::string& outValue) {
			if (context.weaponManager == nullptr) {
				return false;
			}
			outValue = FormatFloat(context.weaponManager->GetLastProjectileRadius());
			return true;
		}},
		{kWeaponCategory, "LastProjectileNearestCandidateName", [](const LogSystemFieldContext& context, std::string& outValue) {
			if (context.weaponManager == nullptr) {
				return false;
			}
			outValue = context.weaponManager->GetLastProjectileNearestCandidateName();
			return true;
		}},
		{kWeaponCategory, "LastProjectileNearestCandidateDistance", [](const LogSystemFieldContext& context, std::string& outValue) {
			if (context.weaponManager == nullptr) {
				return false;
			}
			outValue = FormatFloat(context.weaponManager->GetLastProjectileNearestCandidateDistance());
			return true;
		}},
		{kWeaponCategory, "LastProjectileNearestCandidateWorldPosition", [](const LogSystemFieldContext& context, std::string& outValue) {
			if (context.weaponManager == nullptr) {
				return false;
			}
			outValue = context.weaponManager->GetLastProjectileNearestCandidateWorldPosition();
			return true;
		}},
		{kWeaponCategory, "LastProjectileNearestCandidateColliderCenter", [](const LogSystemFieldContext& context, std::string& outValue) {
			if (context.weaponManager == nullptr) {
				return false;
			}
			outValue = context.weaponManager->GetLastProjectileNearestCandidateColliderCenter();
			return true;
		}},
		{kWeaponCategory, "LastProjectileNearestCandidateColliderSize", [](const LogSystemFieldContext& context, std::string& outValue) {
			if (context.weaponManager == nullptr) {
				return false;
			}
			outValue = context.weaponManager->GetLastProjectileNearestCandidateColliderSize();
			return true;
		}},
		{kWeaponCategory, "LastProjectileNearestCandidateBodyPosition", [](const LogSystemFieldContext& context, std::string& outValue) {
			if (context.weaponManager == nullptr) {
				return false;
			}
			outValue = context.weaponManager->GetLastProjectileNearestCandidateBodyPosition();
			return true;
		}},
		{kWeaponCategory, "LastProjectileNearestCandidateBodyInWorld", [](const LogSystemFieldContext& context, std::string& outValue) {
			if (context.weaponManager == nullptr) {
				return false;
			}
			outValue = context.weaponManager->GetLastProjectileNearestCandidateBodyInWorld();
			return true;
		}},
		{kWeaponCategory, "LastProjectileNearestCandidateWasIgnored", [](const LogSystemFieldContext& context, std::string& outValue) {
			if (context.weaponManager == nullptr) {
				return false;
			}
			outValue = context.weaponManager->GetLastProjectileNearestCandidateWasIgnored() ? "true" : "false";
			return true;
		}},
		{kWeaponCategory, "HitscanCollisionSequence", [](const LogSystemFieldContext& context, std::string& outValue) {
			if (context.weaponManager == nullptr) {
				return false;
			}
			outValue = std::to_string(context.weaponManager->GetHitscanCollisionSequence());
			return true;
		}},
		{kWeaponCategory, "LastHitscanCollisionResult", [](const LogSystemFieldContext& context, std::string& outValue) {
			if (context.weaponManager == nullptr) {
				return false;
			}
			outValue = context.weaponManager->GetLastHitscanCollisionResult();
			return true;
		}},
		{kWeaponCategory, "LastHitscanRawHitGameObjectId", [](const LogSystemFieldContext& context, std::string& outValue) {
			if (context.weaponManager == nullptr) {
				return false;
			}
			outValue = std::to_string(context.weaponManager->GetLastHitscanRawHitGameObjectId());
			return true;
		}},
		{kWeaponCategory, "LastHitscanDamageTargetGameObjectId", [](const LogSystemFieldContext& context, std::string& outValue) {
			if (context.weaponManager == nullptr) {
				return false;
			}
			outValue = std::to_string(context.weaponManager->GetLastHitscanDamageTargetGameObjectId());
			return true;
		}},
		{kWeaponCategory, "LastHitscanHitDistance", [](const LogSystemFieldContext& context, std::string& outValue) {
			if (context.weaponManager == nullptr) {
				return false;
			}
			outValue = FormatFloat(context.weaponManager->GetLastHitscanHitDistance());
			return true;
		}},
		{kWeaponCategory, "LastHitscanAppliedDamage", [](const LogSystemFieldContext& context, std::string& outValue) {
			if (context.weaponManager == nullptr) {
				return false;
			}
			outValue = FormatFloat(context.weaponManager->GetLastHitscanAppliedDamage());
			return true;
		}},
		{kWeaponCategory, "LastHitscanHealthBefore", [](const LogSystemFieldContext& context, std::string& outValue) {
			if (context.weaponManager == nullptr) {
				return false;
			}
			outValue = FormatFloat(context.weaponManager->GetLastHitscanHealthBefore());
			return true;
		}},
		{kWeaponCategory, "LastHitscanHealthAfter", [](const LogSystemFieldContext& context, std::string& outValue) {
			if (context.weaponManager == nullptr) {
				return false;
			}
			outValue = FormatFloat(context.weaponManager->GetLastHitscanHealthAfter());
			return true;
		}},
		{kGameStateCategory, "IsGamePaused", [](const LogSystemFieldContext& context, std::string& outValue) {
			if (context.runtimePropertyManager == nullptr) {
				return false;
			}
			outValue = context.runtimePropertyManager->IsGamePaused() ? "true" : "false";
			return true;
		}},
		{kGameStateCategory, "TimeScale", [](const LogSystemFieldContext& context, std::string& outValue) {
			if (context.runtimePropertyManager == nullptr) {
				return false;
			}
			outValue = FormatFloat(context.runtimePropertyManager->GetTimeScale());
			return true;
		}},
	};

	constexpr char kProfilerCategory[] = "Profiler(CPU ms)";
}

namespace {
	struct LogSystemDisplayName {
		const char* category;  // 内部キー(英語)。空文字ならカテゴリ名そのものの訳。
		const char* name;      // 内部キー(英語)
		const char* displayName;  // UI表示用の日本語
	};

	// 内部キーは変更せず表示だけ日本語化する。ここに無いものは英語のまま表示される。
	const LogSystemDisplayName kLogSystemDisplayNames[] = {
		{"Physics", "Gravity", "重力"},
		{"Physics", "FixedTimeStep", "物理の固定更新間隔"},
		{"Physics", "PhysicsBodyFailureCount", "当たり判定Body生成の失敗数"},
		{"Physics", "LastPhysicsBodyFailure", "当たり判定Body生成の失敗理由"},
		{"Rendering", "GpuFrameMilliseconds", "GPUフレーム時間 ms"},
		{"Rendering", "SceneObjectCount", "シーン内オブジェクト数"},
		{"Rendering", "InstanceCount", "描画インスタンス数"},
		{"Rendering", "LocalVideoMemoryUsage", "VRAM使用量"},
		{"Rendering", "LocalVideoMemoryBudget", "VRAM上限"},
		{"Weapon", "ActiveProjectileCount", "飛翔中の弾数"},
		{"Weapon", "ProjectileCollisionSequence", "弾の命中判定 通し番号"},
		{"Weapon", "LastProjectileCollisionResult", "弾: 判定結果"},
		{"Weapon", "LastProjectileGameObjectId", "弾: 弾のID"},
		{"Weapon", "LastProjectileRawHitGameObjectId", "弾: 当たった相手のID"},
		{"Weapon", "LastProjectileRawHitGameObjectName", "弾: 当たった相手の名前"},
		{"Weapon", "LastProjectileDamageTargetGameObjectId", "弾: ダメージ対象のID"},
		{"Weapon", "LastProjectileHitDistance", "弾: 命中までの距離"},
		{"Weapon", "LastProjectileAppliedDamage", "弾: 与えたダメージ"},
		{"Weapon", "LastProjectileHealthBefore", "弾: 相手のHP(命中前)"},
		{"Weapon", "LastProjectileHealthAfter", "弾: 相手のHP(命中後)"},
		{"Weapon", "LastProjectileCastOrigin", "弾: 判定した座標"},
		{"Weapon", "LastProjectileRadius", "弾: 判定の半径"},
		{"Weapon", "LastProjectileNearestCandidateName", "最寄りの敵/自機: 名前"},
		{"Weapon", "LastProjectileNearestCandidateDistance", "最寄りの敵/自機: 弾との距離"},
		{"Weapon", "LastProjectileNearestCandidateWorldPosition", "最寄りの敵/自機: 座標"},
		{"Weapon", "LastProjectileNearestCandidateColliderCenter", "最寄りの敵/自機: 当たり判定の中心"},
		{"Weapon", "LastProjectileNearestCandidateColliderSize", "最寄りの敵/自機: 当たり判定の大きさ"},
		{"Weapon", "LastProjectileNearestCandidateBodyPosition", "最寄りの敵/自機: 物理Bodyの座標"},
		{"Weapon", "LastProjectileNearestCandidateBodyInWorld", "最寄りの敵/自機: 物理Bodyが有効か"},
		{"Weapon", "LastProjectileNearestCandidateWasIgnored", "最寄りの敵/自機: 判定から除外されたか"},
		{"Weapon", "HitscanCollisionSequence", "レイ射撃の判定 通し番号"},
		{"Weapon", "LastHitscanCollisionResult", "レイ射撃: 判定結果"},
		{"Weapon", "LastHitscanRawHitGameObjectId", "レイ射撃: 当たった相手のID"},
		{"Weapon", "LastHitscanDamageTargetGameObjectId", "レイ射撃: ダメージ対象のID"},
		{"Weapon", "LastHitscanHitDistance", "レイ射撃: 命中までの距離"},
		{"Weapon", "LastHitscanAppliedDamage", "レイ射撃: 与えたダメージ"},
		{"Weapon", "LastHitscanHealthBefore", "レイ射撃: 相手のHP(命中前)"},
		{"Weapon", "LastHitscanHealthAfter", "レイ射撃: 相手のHP(命中後)"},
		{"GameState", "IsGamePaused", "ポーズ中か"},
		{"GameState", "TimeScale", "時間の倍率"},
	};

	struct LogSystemCategoryDisplayName {
		const char* category;
		const char* displayName;
	};

	const LogSystemCategoryDisplayName kLogSystemCategoryDisplayNames[] = {
		{"Physics", "物理"},
		{"Rendering", "描画"},
		{"Weapon", "武器・当たり判定"},
		{"GameState", "ゲーム状態"},
		{"Profiler(CPU ms)", "処理時間 (CPU ms)"},
	};
}

const char* GetLogSystemCategoryDisplayName(const std::string& category) {
	for (const LogSystemCategoryDisplayName& entry : kLogSystemCategoryDisplayNames) {
		if (category == entry.category) {
			return entry.displayName;
		}
	}

	return category.c_str();
}

const char* GetLogSystemFieldDisplayName(const std::string& category, const std::string& name) {
	for (const LogSystemDisplayName& entry : kLogSystemDisplayNames) {
		if (category == entry.category && name == entry.name) {
			return entry.displayName;
		}
	}

	return name.c_str();
}

std::vector<std::pair<std::string, std::string>> ListLogSystemFields(const LogSystemFieldContext& context) {
	std::vector<std::pair<std::string, std::string>> fields;

	for (const FixedSystemFieldProvider& provider : kFixedSystemFieldProviders) {
		fields.emplace_back(provider.category, provider.name);
	}

	if (context.profilerManager != nullptr) {
		for (const EditorProfilerSample& sample : context.profilerManager->GetSortedSamples()) {
			fields.emplace_back(kProfilerCategory, sample.name);
		}
	}

	return fields;
}

bool ResolveLogSystemFieldValue(
	const LogSystemFieldContext& context,
	const std::string& category,
	const std::string& name,
	std::string& outValue) {
	if (category == kProfilerCategory) {
		if (context.profilerManager == nullptr) {
			return false;
		}

		for (const EditorProfilerSample& sample : context.profilerManager->GetSortedSamples()) {
			if (sample.name == name) {
				std::ostringstream stream;
				stream << std::fixed << std::setprecision(3)
					<< "source=" << sample.source
					<< " calls=" << sample.sampleCount
					<< " total=" << sample.totalMilliseconds
					<< " self=" << sample.selfMilliseconds
					<< " allocations=" << sample.allocationCount
					<< " allocatedBytes=" << sample.allocatedBytes
					<< " thread=" << sample.threadName
					<< " gameObjectId=" << sample.gameObjectId
					<< " latest=" << sample.latestMilliseconds
					<< " avg=" << sample.averageMilliseconds
					<< " peak=" << sample.peakMilliseconds;
				outValue = stream.str();
				return true;
			}
		}

		return false;
	}

	for (const FixedSystemFieldProvider& provider : kFixedSystemFieldProviders) {
		if (category == provider.category && name == provider.name) {
			return provider.resolve(context, outValue);
		}
	}

	return false;
}
