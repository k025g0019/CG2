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
					<< "latest=" << sample.latestMilliseconds
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
