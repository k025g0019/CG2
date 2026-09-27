#include "EditorBlastDestructionManager.h"

#include "EditorAssetUtility.h"
#include "EditorComponentUtility.h"
#include "EditorDamageManager.h"
#include "EditorPhysicsManager.h"
#include "EditorScriptManager.h"
#include "EditorSharedState.h"
#include "ProjectSettings.h"
#include "Source/Engine/Effect/EditorEffectManager.h"

#include <NvBlast.h>
#include <NvBlastExtAuthoringFractureTool.h>
#include <NvBlastExtAuthoringMesh.h>
#include <NvBlastExtAuthoringTypes.h>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <random>
#include <set>
#include <sstream>
#include <string>

namespace {
	constexpr size_t kBlastAlignment = 16U;
	constexpr uint32_t kFractureGeneratorVersion = 2U;
	constexpr const char* kFractureCacheRoot = "Library/FractureCache";
	constexpr float kMaxGpuDebrisSpeed = 40.0f;  // GPU破片の初速上限（m/s）
	constexpr float kClusterScatterDuration = 0.5f;  // Cluster内破片がばらけ終わるまでの秒数

	class DeterministicBlastRandom final : public Nv::Blast::RandomGeneratorBase {
	public:
		explicit DeterministicBlastRandom(int32_t value) { seed(value); }
		float getRandomValue() override { return distribution_(generator_); }
		void seed(int32_t value) override { generator_.seed(static_cast<uint32_t>(value)); }
	private:
		std::mt19937 generator_{};
		std::uniform_real_distribution<float> distribution_{0.0f, 1.0f};
	};

	using CreateAuthoringMeshFunction = Nv::Blast::Mesh* (*)(const NvcVec3*, const NvcVec3*, const NvcVec2*, uint32_t, const uint32_t*, uint32_t);
	using CreateSitesGeneratorFunction = Nv::Blast::VoronoiSitesGenerator* (*)(Nv::Blast::Mesh*, Nv::Blast::RandomGeneratorBase*);
	using CreateFractureToolFunction = Nv::Blast::FractureTool* (*)();

	struct AuthoringModule {
		HMODULE handle = nullptr;
		CreateAuthoringMeshFunction createMesh = nullptr;
		CreateSitesGeneratorFunction createSites = nullptr;
		CreateFractureToolFunction createTool = nullptr;
		~AuthoringModule() { if (handle != nullptr) FreeLibrary(handle); }
		bool Load(std::string& error) {
			handle = LoadLibraryW(L"NvBlastExtAuthoring.dll");
			if (handle == nullptr) {
				error = "NvBlastExtAuthoring.dllがありません。Build/PhysicsSdk/Setup.ps1でAuthoring SDKを生成してください。";
				return false;
			}
			// Bridge の Export 名は CG2 系だが、旧名(Mano系)で生成済みのSDKもそのまま使えるようにする。
			const auto resolve = [handle](const char* current, const char* legacy) {
				FARPROC address = GetProcAddress(handle, current);
				return address != nullptr ? address : GetProcAddress(handle, legacy);
			};
			createMesh = reinterpret_cast<CreateAuthoringMeshFunction>(
				resolve("CG2BlastAuthoringCreateMesh", "ManoBlastAuthoringCreateMesh"));
			createSites = reinterpret_cast<CreateSitesGeneratorFunction>(
				resolve("CG2BlastAuthoringCreateVoronoiSitesGenerator", "ManoBlastAuthoringCreateVoronoiSitesGenerator"));
			createTool = reinterpret_cast<CreateFractureToolFunction>(
				resolve("CG2BlastAuthoringCreateFractureTool", "ManoBlastAuthoringCreateFractureTool"));
			if (createMesh == nullptr || createSites == nullptr || createTool == nullptr) {
				error = "NvBlastExtAuthoring.dllのBridge APIが一致しません。Physics SDKを再生成してください。";
				return false;
			}
			return true;
		}
	};

	void HashBytes(uint64_t& hash, const void* data, size_t size) {
		const auto* bytes = static_cast<const uint8_t*>(data);
		for (size_t index = 0; index < size; ++index) {
			hash ^= bytes[index];
			hash *= 1099511628211ULL;
		}
	}

	std::string ToCachePath(const std::string& cacheKey, const std::string& fileName) {
		return (std::filesystem::path(kFractureCacheRoot) / cacheKey / fileName).generic_string();
	}

	void DeleteGeneratedChunkChildren(EditorScene& scene, int32_t ownerId) {
		const EditorGameObject* owner = scene.FindGameObject(ownerId);
		if (owner == nullptr) return;
		const std::vector<int32_t> children = owner->children;
		for (const int32_t childId : children) {
			const EditorGameObject* child = scene.FindGameObject(childId);
			if (child != nullptr && child->name.rfind("__BlastChunk_", 0U) == 0U) scene.DeleteGameObject(childId);
		}
	}

	float DistanceSquared(const Vector3& first, const Vector3& second) {
		const float x = first.x - second.x;
		const float y = first.y - second.y;
		const float z = first.z - second.z;
		return x * x + y * y + z * z;
	}

	Vector3 DirectionFromTo(const Vector3& from, const Vector3& to) {
		const Vector3 difference{to.x - from.x, to.y - from.y, to.z - from.z};
		const float length = std::sqrt(DistanceSquared(from, to));
		if (length <= 0.00001f) {
			return Vector3{0.0f, 1.0f, 0.0f};
		}
		return Vector3{difference.x / length, difference.y / length, difference.z / length};
	}

	void* AllocateBlastMemory(size_t size) {
		return size > 0U ? _aligned_malloc(size, kBlastAlignment) : nullptr;
	}

	bool IsColliderType(EditorComponentType type) {
		return type == EditorComponentType::BoxCollider ||
			type == EditorComponentType::SphereCollider ||
			type == EditorComponentType::CapsuleCollider ||
			type == EditorComponentType::MeshCollider ||
			type == EditorComponentType::TerrainCollider ||
			type == EditorComponentType::AutoConvexCollision ||
			type == EditorComponentType::CharacterController;
	}

	bool IsRendererType(EditorComponentType type) {
		return type == EditorComponentType::ModelRenderer ||
			type == EditorComponentType::SkinnedMeshRenderer ||
			type == EditorComponentType::MeshFilter;
	}
}

EditorBlastDestructionManager::~EditorBlastDestructionManager() {
	Stop();
}

void EditorBlastDestructionManager::Initialize(
	EditorScene* editorScene,
	EditorPhysicsManager* physicsManager,
	EditorScriptManager* scriptManager,
	std::vector<std::string>* consoleMessages,
	EditorDamageManager* damageManager,
	EditorEffectManager* effectManager) {
	Stop();
	editorScene_ = editorScene;
	physicsManager_ = physicsManager;
	scriptManager_ = scriptManager;
	damageManager_ = damageManager;
	effectManager_ = effectManager;
	consoleMessages_ = consoleMessages;
}

void EditorBlastDestructionManager::Start() {
	Stop();
	if (editorScene_ == nullptr) {
		return;
	}

	// 自動Chunk生成でScene配列が再確保されても参照を失わないよう、先に所有者IDだけを収集する。
	std::vector<int32_t> ownerIds;
	for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		const EditorComponent* component = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::DestructiblePart);
		if (component == nullptr || !component->isActive || !component->destructibleBlastEnabled) {
			continue;
		}
		ownerIds.push_back(gameObject.id);
	}
	for (const int32_t ownerId : ownerIds) {
		EditorGameObject* owner = editorScene_->FindGameObject(ownerId);
		EditorComponent* component = owner != nullptr
			? EditorComponentUtility::FindComponent(*owner, EditorComponentType::DestructiblePart)
			: nullptr;
		if (owner != nullptr && component != nullptr) {
			try {
				BuildDestructible(*owner, *component);
			} catch (const std::exception& exception) {
				DeleteGeneratedChunkChildren(*editorScene_, ownerId);
				owner = editorScene_->FindGameObject(ownerId);
				component = owner != nullptr ? EditorComponentUtility::FindComponent(*owner, EditorComponentType::DestructiblePart) : nullptr;
				if (component == nullptr) continue;
				SetBakeFailure(*component, "Failed", exception.what());
				PushConsoleMessage("Blast: " + owner->name + " の初期化例外: " + exception.what());
			} catch (...) {
				DeleteGeneratedChunkChildren(*editorScene_, ownerId);
				owner = editorScene_->FindGameObject(ownerId);
				component = owner != nullptr ? EditorComponentUtility::FindComponent(*owner, EditorComponentType::DestructiblePart) : nullptr;
				if (component == nullptr) continue;
				SetBakeFailure(*component, "Failed", "不明なAuthoring例外が発生しました。");
				PushConsoleMessage("Blast: " + owner->name + " の初期化中に不明な例外が発生しました。");
			}
		}
	}
}

void EditorBlastDestructionManager::Update(float deltaTime) {
	if (editorScene_ == nullptr) {
		return;
	}

	// Health=0は従来のDestructiblePartと同じ入口として扱い、全Bondへ十分なDamageを与える。
	for (auto& [ownerGameObjectId, runtime] : destructibles_) {
		// 分裂後も、物理化の解除、破片の沈下・破棄、Cluster内のばらけ演出は進める。
		if (deltaTime > 0.0f) {
			UpdateTimedPhysicsChunks(runtime, deltaTime);
			UpdateSinkingChunks(runtime, deltaTime);
			UpdateClusterFollowers(runtime, deltaTime);
		}
		if (runtime.hasSplit) {
			continue;
		}
		EditorGameObject* owner = editorScene_->FindGameObject(ownerGameObjectId);
		EditorComponent* destructible = owner != nullptr
			? EditorComponentUtility::FindComponent(*owner, EditorComponentType::DestructiblePart)
			: nullptr;
		if (destructible == nullptr || !destructible->isActive) {
			continue;
		}
		if (damageManager_ != nullptr) {
			const uint64_t damageSequence = damageManager_->GetDamageSequence(ownerGameObjectId);
			if (damageSequence > runtime.consumedDamageSequence) {
				EditorScriptDamageContext damageContext{};
				runtime.consumedDamageSequence = damageSequence;
				if (damageManager_->GetLastDamageContext(ownerGameObjectId, damageContext)) {
					runtime.receivedLocalizedDamage = true;
					const float contextImpulse = std::sqrt(
						damageContext.impulse.x * damageContext.impulse.x +
						damageContext.impulse.y * damageContext.impulse.y +
						damageContext.impulse.z * damageContext.impulse.z);
					ApplyDamage(
						ownerGameObjectId,
						Vector3{damageContext.hitPosition.x, damageContext.hitPosition.y, damageContext.hitPosition.z},
						destructible->destructibleBlastDamageRadius,
						(std::max)(damageContext.appliedDamage, damageContext.baseDamage),
						contextImpulse > 0.0001f ? contextImpulse : destructible->destructibleBlastImpulse);
				}
			}
		}
		const int32_t healthGameObjectId = destructible->destructibleHealthGameObjectId >= 0
			? destructible->destructibleHealthGameObjectId
			: ownerGameObjectId;
		EditorGameObject* healthOwner = editorScene_->FindGameObject(healthGameObjectId);
		EditorComponent* health = healthOwner != nullptr
			? EditorComponentUtility::FindComponent(*healthOwner, EditorComponentType::Health)
			: nullptr;
		if (health != nullptr && health->isActive && health->healthCurrent <= 0.0f &&
			!runtime.receivedLocalizedDamage) {
			FractureAll(ownerGameObjectId, destructible->destructibleBlastImpulse);
		}
	}
}

void EditorBlastDestructionManager::Stop() {
	std::vector<int32_t> generatedChunkIds;
	for (auto& [gameObjectId, runtime] : destructibles_) {
		(void)gameObjectId;
		for (NvBlastActor* actor : runtime.actors) {
			if (actor != nullptr) {
				NvBlastActorDeactivate(actor, nullptr);
			}
		}
		runtime.actors.clear();
		runtime.family = nullptr;
		runtime.asset = nullptr;
		ReleaseAlignedMemory(runtime.familyMemory);
		ReleaseAlignedMemory(runtime.assetMemory);
		generatedChunkIds.insert(
			generatedChunkIds.end(),
			runtime.generatedChunkGameObjectIds.begin(),
			runtime.generatedChunkGameObjectIds.end());
	}
	destructibles_.clear();
	physicsDebrisGameObjectIds_.clear();
	if (editorScene_ != nullptr) {
		for (const int32_t chunkId : generatedChunkIds) {
			editorScene_->DeleteGameObject(chunkId);
		}
	}
}

std::string EditorBlastDestructionManager::ResolveSourceMeshPath(const EditorGameObject& owner) const {
	const std::array<EditorComponentType, 3> sourceTypes{
		EditorComponentType::MeshFilter,
		EditorComponentType::ModelRenderer,
		EditorComponentType::SkinnedMeshRenderer};
	for (const EditorComponentType type : sourceTypes) {
		const EditorComponent* component = EditorComponentUtility::FindComponent(owner, type);
		if (component != nullptr && component->isActive && !component->assetPath.empty()) {
			return component->assetPath;
		}
	}
	return {};
}

std::string EditorBlastDestructionManager::ComputeCacheKey(
	const std::string& sourceAssetPath,
	const EditorComponent& component) const {
	uint64_t hash = 1469598103934665603ULL;
	const ModelData* modelData = EditorAssetUtility::GetSharedModelAssetData(sourceAssetPath, false);
	if (modelData != nullptr) {
		for (const VertexData& vertex : modelData->vertices) {
			HashBytes(hash, &vertex.position, sizeof(vertex.position));
			HashBytes(hash, &vertex.normal, sizeof(vertex.normal));
			HashBytes(hash, &vertex.texcoord, sizeof(vertex.texcoord));
		}
		if (!modelData->indices.empty()) {
			HashBytes(hash, modelData->indices.data(), modelData->indices.size() * sizeof(uint32_t));
		}
	}
	HashBytes(hash, &component.destructibleBlastFractureMethod, sizeof(component.destructibleBlastFractureMethod));
	HashBytes(hash, &component.destructibleBlastChunkCount, sizeof(component.destructibleBlastChunkCount));
	HashBytes(hash, &component.destructibleBlastRandomSeed, sizeof(component.destructibleBlastRandomSeed));
	HashBytes(hash, &component.destructibleBlastCollisionQuality, sizeof(component.destructibleBlastCollisionQuality));
	HashBytes(hash, &component.destructibleBlastBondNeighborCount, sizeof(component.destructibleBlastBondNeighborCount));
	HashBytes(hash, &kFractureGeneratorVersion, sizeof(kFractureGeneratorVersion));
	std::ostringstream stream;
	stream << std::hex << std::setw(16) << std::setfill('0') << hash;
	return stream.str();
}

void EditorBlastDestructionManager::SetBakeFailure(
	EditorComponent& component,
	const std::string& status,
	const std::string& reason) const {
	component.destructibleBlastBakeStatus = status;
	component.destructibleBlastBakeError = reason;
}

bool EditorBlastDestructionManager::LoadCacheManifest(
	const std::string& cacheKey,
	std::vector<CachedChunk>& chunks,
	std::vector<std::pair<uint32_t, uint32_t>>& bondPairs,
	std::string& error) const {
	chunks.clear();
	bondPairs.clear();
	std::ifstream stream(ToCachePath(cacheKey, "manifest.txt"), std::ios::binary);
	if (!stream) {
		error = "Cache manifestがありません。";
		return false;
	}
	std::string line;
	std::getline(stream, line);
	if (line.size() >= 3U && static_cast<uint8_t>(line[0]) == 0xEFU &&
		static_cast<uint8_t>(line[1]) == 0xBBU && static_cast<uint8_t>(line[2]) == 0xBFU) {
		line.erase(0U, 3U);
	}
	if (line != "CG2_FRACTURE_CACHE|2|" + cacheKey) {
		error = "Cache versionまたはHashが一致しません。";
		return false;
	}
	while (std::getline(stream, line)) {
		std::istringstream row(line);
		std::string tag;
		if (line.rfind("Bond|", 0U) == 0U) {
			std::string first;
			std::string second;
			std::getline(row, tag, '|');
			if (!std::getline(row, first, '|') || !std::getline(row, second)) {
				error = "Cache manifestのBond行が破損しています。";
				return false;
			}
			try {
				bondPairs.emplace_back(static_cast<uint32_t>(std::stoul(first)), static_cast<uint32_t>(std::stoul(second)));
			} catch (...) {
				error = "Cache manifestのBond Indexが破損しています。";
				return false;
			}
			continue;
		}
		CachedChunk chunk{};
		std::string x;
		std::string y;
		std::string z;
		if (!std::getline(row, tag, '|') || tag != "Chunk" ||
			!std::getline(row, chunk.assetPath, '|') ||
			!std::getline(row, x, '|') || !std::getline(row, y, '|') || !std::getline(row, z)) {
			error = "Cache manifestのChunk行が破損しています。";
			return false;
		}
		try {
			chunk.localCenter = {std::stof(x), std::stof(y), std::stof(z)};
		} catch (...) {
			error = "Cache manifestの座標が破損しています。";
			return false;
		}
		if (!std::filesystem::is_regular_file(std::filesystem::path(chunk.assetPath))) {
			error = "Chunk Meshが不足しています: " + chunk.assetPath;
			return false;
		}
		chunks.push_back(std::move(chunk));
	}
	if (chunks.size() < 2U || bondPairs.empty()) {
		error = "Cacheに有効なChunkまたはBondがありません。";
		return false;
	}
	for (const auto& [first, second] : bondPairs) {
		if (first >= chunks.size() || second >= chunks.size() || first == second) {
			error = "CacheのBondが無効なChunkを参照しています。";
			return false;
		}
	}
	return true;
}

bool EditorBlastDestructionManager::BakeCache(
	const std::string& sourceAssetPath,
	const EditorComponent& component,
	const std::string& cacheKey,
	std::vector<CachedChunk>& chunks,
	std::vector<std::pair<uint32_t, uint32_t>>& bondPairs,
	std::string& error) {
	const ModelData* modelData = EditorAssetUtility::GetSharedModelAssetData(sourceAssetPath, false);
	if (modelData == nullptr || modelData->vertices.size() < 3U) {
		error = "Source MeshのCPU頂点を読み込めません。";
		return false;
	}
	std::vector<NvcVec3> positions;
	std::vector<NvcVec3> normals;
	std::vector<NvcVec2> uvs;
	positions.reserve(modelData->vertices.size());
	normals.reserve(modelData->vertices.size());
	uvs.reserve(modelData->vertices.size());
	for (const VertexData& vertex : modelData->vertices) {
		positions.push_back({vertex.position.x, vertex.position.y, vertex.position.z});
		normals.push_back({vertex.normal.x, vertex.normal.y, vertex.normal.z});
		uvs.push_back({vertex.texcoord.x, vertex.texcoord.y});
	}
	std::vector<uint32_t> indices = modelData->indices;
	if (indices.empty()) {
		indices.resize(positions.size());
		for (uint32_t index = 0U; index < indices.size(); ++index) indices[index] = index;
	}
	if (indices.size() < 3U || indices.size() % 3U != 0U) {
		error = "Source MeshのIndexが三角形リストではありません。";
		return false;
	}

	AuthoringModule authoring;
	if (!authoring.Load(error)) return false;
	Nv::Blast::Mesh* mesh = authoring.createMesh(
		positions.data(), normals.data(), uvs.data(), static_cast<uint32_t>(positions.size()),
		indices.data(), static_cast<uint32_t>(indices.size()));
	DeterministicBlastRandom random(component.destructibleBlastRandomSeed);
	Nv::Blast::VoronoiSitesGenerator* sites = mesh != nullptr ? authoring.createSites(mesh, &random) : nullptr;
	Nv::Blast::FractureTool* tool = authoring.createTool();
	if (mesh == nullptr || !mesh->isValid() || sites == nullptr || tool == nullptr) {
		if (sites != nullptr) sites->release();
		if (tool != nullptr) tool->release();
		if (mesh != nullptr) mesh->release();
		error = "Blast AuthoringのMeshまたはFracture Toolを作成できません。Meshが閉じた形状か確認してください。";
		return false;
	}

	tool->setSourceMesh(mesh);
	sites->uniformlyGenerateSitesInMesh(static_cast<uint32_t>(component.destructibleBlastChunkCount));
	const NvcVec3* sitePoints = nullptr;
	const uint32_t siteCount = sites->getVoronoiSites(sitePoints);
	const int32_t fractureResult = siteCount >= 2U
		? tool->voronoiFracturing(0U, siteCount, sitePoints, false)
		: -1;
	if (fractureResult != 0) {
		sites->release();
		tool->release();
		mesh->release();
		error = "Voronoi Fractureに失敗しました。閉じたMesh、法線、Scaleを確認してください。";
		return false;
	}
	tool->finalizeFracturing();

	const std::filesystem::path cacheDirectory = std::filesystem::path(kFractureCacheRoot) / cacheKey;
	std::error_code fileError;
	std::filesystem::create_directories(cacheDirectory, fileError);
	if (fileError) {
		sites->release(); tool->release(); mesh->release();
		error = "Fracture Cacheフォルダーを作成できません: " + fileError.message();
		return false;
	}

	chunks.clear();
	const uint32_t fractureChunkCount = tool->getChunkCount();
	for (uint32_t fractureIndex = 0U; fractureIndex < fractureChunkCount; ++fractureIndex) {
		const Nv::Blast::ChunkInfo& info = tool->getChunkInfo(static_cast<int32_t>(fractureIndex));
		if (!info.isLeaf || info.parent < 0) continue;
		Nv::Blast::Triangle* triangles = nullptr;
		const uint32_t triangleCount = tool->getBaseMesh(static_cast<int32_t>(fractureIndex), triangles);
		if (triangles == nullptr || triangleCount == 0U) {
			delete[] triangles;
			continue;
		}
		Vector3 center{};
		for (uint32_t triangleIndex = 0U; triangleIndex < triangleCount; ++triangleIndex) {
			const Nv::Blast::Triangle& triangle = triangles[triangleIndex];
			center.x += triangle.a.p.x + triangle.b.p.x + triangle.c.p.x;
			center.y += triangle.a.p.y + triangle.b.p.y + triangle.c.p.y;
			center.z += triangle.a.p.z + triangle.b.p.z + triangle.c.p.z;
		}
		const float divisor = 1.0f / static_cast<float>(triangleCount * 3U);
		center = {center.x * divisor, center.y * divisor, center.z * divisor};
		std::ostringstream fileName;
		fileName << "chunk_" << std::setw(3) << std::setfill('0') << chunks.size() << ".obj";
		const std::string assetPath = ToCachePath(cacheKey, fileName.str());
		std::ofstream output(std::filesystem::path(assetPath), std::ios::binary | std::ios::trunc);
		if (!output) {
			delete[] triangles;
			error = "Chunk Meshを書き込めません: " + assetPath;
			break;
		}
		output.write("\xEF\xBB\xBF", 3);
		for (uint32_t triangleIndex = 0U; triangleIndex < triangleCount; ++triangleIndex) {
			const Nv::Blast::Vertex vertices[3]{triangles[triangleIndex].a, triangles[triangleIndex].b, triangles[triangleIndex].c};
			for (const Nv::Blast::Vertex& vertex : vertices) {
				output << "v " << vertex.p.x - center.x << ' ' << vertex.p.y - center.y << ' ' << vertex.p.z - center.z << '\n';
				output << "vt " << vertex.uv[0].x << ' ' << vertex.uv[0].y << '\n';
				output << "vn " << vertex.n.x << ' ' << vertex.n.y << ' ' << vertex.n.z << '\n';
			}
		}
		for (uint32_t triangleIndex = 0U; triangleIndex < triangleCount; ++triangleIndex) {
			const uint32_t base = triangleIndex * 3U + 1U;
			output << "f " << base << '/' << base << '/' << base << ' '
				<< base + 1U << '/' << base + 1U << '/' << base + 1U << ' '
				<< base + 2U << '/' << base + 2U << '/' << base + 2U << '\n';
		}
		delete[] triangles;
		if (!output.good()) {
			error = "Chunk Meshの書き込み中に失敗しました: " + assetPath;
			break;
		}
		chunks.push_back(CachedChunk{assetPath, center});
	}
	sites->release();
	tool->release();
	mesh->release();
	if (!error.empty() || chunks.size() < 2U) {
		if (error.empty()) error = "Blast Authoringが有効なChunkを2個以上生成できませんでした。";
		return false;
	}
	// Bond定義もCacheへ保存し、同じPrefab配置ごとに近傍探索を繰り返さない。
	bondPairs.clear();
	const uint32_t neighborCount = static_cast<uint32_t>((std::clamp)(
		component.destructibleBlastBondNeighborCount, 1, static_cast<int32_t>(chunks.size() - 1U)));
	std::set<std::pair<uint32_t, uint32_t>> uniqueBonds;
	std::vector<bool> connected(chunks.size(), false);
	connected[0] = true;
	for (uint32_t connectedCount = 1U; connectedCount < chunks.size(); ++connectedCount) {
		float nearestDistance = (std::numeric_limits<float>::max)();
		uint32_t nearestConnected = 0U;
		uint32_t nearestUnconnected = 0U;
		for (uint32_t first = 0U; first < chunks.size(); ++first) {
			if (!connected[first]) continue;
			for (uint32_t second = 0U; second < chunks.size(); ++second) {
				if (connected[second]) continue;
				const float distance = DistanceSquared(chunks[first].localCenter, chunks[second].localCenter);
				if (distance < nearestDistance) {
					nearestDistance = distance;
					nearestConnected = first;
					nearestUnconnected = second;
				}
			}
		}
		connected[nearestUnconnected] = true;
		uniqueBonds.emplace((std::min)(nearestConnected, nearestUnconnected), (std::max)(nearestConnected, nearestUnconnected));
	}
	for (uint32_t first = 0U; first < chunks.size(); ++first) {
		std::vector<std::pair<float, uint32_t>> distances;
		for (uint32_t second = 0U; second < chunks.size(); ++second) {
			if (first != second) distances.emplace_back(DistanceSquared(chunks[first].localCenter, chunks[second].localCenter), second);
		}
		std::sort(distances.begin(), distances.end());
		for (uint32_t neighbor = 0U; neighbor < neighborCount; ++neighbor) {
			uniqueBonds.emplace((std::min)(first, distances[neighbor].second), (std::max)(first, distances[neighbor].second));
		}
	}
	bondPairs.assign(uniqueBonds.begin(), uniqueBonds.end());

	std::ofstream manifest(cacheDirectory / "manifest.txt", std::ios::binary | std::ios::trunc);
	if (!manifest) {
		error = "Cache manifestを書き込めません。";
		return false;
	}
	manifest.write("\xEF\xBB\xBF", 3);
	manifest << "CG2_FRACTURE_CACHE|2|" << cacheKey << '\n';
	manifest << std::setprecision(9);
	for (const CachedChunk& chunk : chunks) {
		manifest << "Chunk|" << chunk.assetPath << '|' << chunk.localCenter.x << '|'
			<< chunk.localCenter.y << '|' << chunk.localCenter.z << '\n';
	}
	for (const auto& [first, second] : bondPairs) manifest << "Bond|" << first << '|' << second << '\n';
	return manifest.good();
}

bool EditorBlastDestructionManager::LoadOrBakeCache(
	const EditorGameObject& owner,
	EditorComponent& component,
	std::vector<CachedChunk>& chunks,
	std::vector<std::pair<uint32_t, uint32_t>>& bondPairs) {
	const std::string sourceAssetPath = ResolveSourceMeshPath(owner);
	if (sourceAssetPath.empty()) {
		SetBakeFailure(component, "Missing Source", "MeshFilterまたはRendererにSource Meshがありません。");
		return false;
	}
	if (EditorAssetUtility::GetSharedModelAssetData(sourceAssetPath, false) == nullptr) {
		SetBakeFailure(component, "Missing Source", "Source Meshを読み込めません: " + sourceAssetPath);
		return false;
	}
	const std::string cacheKey = ComputeCacheKey(sourceAssetPath, component);
	const std::filesystem::path cacheDirectory = std::filesystem::path(kFractureCacheRoot) / cacheKey;
	if (component.destructibleBlastClearCacheRequested) {
		std::error_code removeError;
		std::filesystem::remove_all(cacheDirectory, removeError);
		component.destructibleBlastClearCacheRequested = false;
		if (removeError) {
			SetBakeFailure(component, "Failed", "Cacheを削除できません: " + removeError.message());
			return false;
		}
	}
	std::string cacheError;
	if (!component.destructibleBlastForceRebake && LoadCacheManifest(cacheKey, chunks, bondPairs, cacheError)) {
		component.destructibleBlastBakeStatus = "Ready";
		component.destructibleBlastBakeError.clear();
		return true;
	}
	if (!component.destructibleBlastAutoBake && !component.destructibleBlastForceRebake) {
		SetBakeFailure(component, "Needs Bake", cacheError);
		return false;
	}
	component.destructibleBlastBakeStatus = "Baking";
	component.destructibleBlastBakeError.clear();
	std::string bakeError;
	if (!BakeCache(sourceAssetPath, component, cacheKey, chunks, bondPairs, bakeError)) {
		SetBakeFailure(component, "Failed", bakeError);
		PushConsoleMessage("Blast Bake失敗: " + owner.name + ": " + bakeError);
		return false;
	}
	component.destructibleBlastForceRebake = false;
	component.destructibleBlastBakeStatus = "Ready";
	component.destructibleBlastBakeError.clear();
	PushConsoleMessage("Blast Bake完了: " + owner.name + " Chunks=" + std::to_string(chunks.size()));
	return true;
}

bool EditorBlastDestructionManager::CreateGeneratedChunkObjects(
	const EditorGameObject& owner,
	const EditorComponent& component,
	const std::vector<CachedChunk>& chunks,
	std::vector<int32_t>& chunkIds) {
	chunkIds.clear();
	const EditorComponent* sourceRenderer = EditorComponentUtility::FindComponent(
		owner, EditorComponentType::ModelRenderer);
	for (size_t chunkIndex = 0U; chunkIndex < chunks.size(); ++chunkIndex) {
		const int32_t chunkId = editorScene_->CreateGameObject(
			"__BlastChunk_" + std::to_string(owner.id) + "_" + std::to_string(chunkIndex));
		if (!editorScene_->SetParent(chunkId, owner.id, false) ||
			!editorScene_->AddComponent(chunkId, EditorComponentType::MeshFilter) ||
			!editorScene_->AddComponent(chunkId, EditorComponentType::ModelRenderer) ||
			!editorScene_->AddComponent(chunkId, EditorComponentType::AutoConvexCollision)) {
			editorScene_->DeleteGameObject(chunkId);
			for (const int32_t createdId : chunkIds) editorScene_->DeleteGameObject(createdId);
			chunkIds.clear();
			return false;
		}
		EditorGameObject* chunk = editorScene_->FindGameObject(chunkId);
		if (chunk == nullptr) {
			for (const int32_t createdId : chunkIds) editorScene_->DeleteGameObject(createdId);
			chunkIds.clear();
			return false;
		}
		chunk->translate = chunks[chunkIndex].localCenter;
		EditorComponent* meshFilter = EditorComponentUtility::FindComponent(*chunk, EditorComponentType::MeshFilter);
		EditorComponent* renderer = EditorComponentUtility::FindComponent(*chunk, EditorComponentType::ModelRenderer);
		EditorComponent* collider = EditorComponentUtility::FindComponent(*chunk, EditorComponentType::AutoConvexCollision);
		meshFilter->assetPath = chunks[chunkIndex].assetPath;
		if (sourceRenderer != nullptr) {
			const std::string generatedUuid = renderer->uuid;
			*renderer = *sourceRenderer;
			renderer->uuid = generatedUuid;
			renderer->type = EditorComponentType::ModelRenderer;
			renderer->isActive = true;
		}
		renderer->assetPath = chunks[chunkIndex].assetPath;
		renderer->assetId.clear();
		collider->assetPath = chunks[chunkIndex].assetPath;
		collider->autoConvexMaximumHulls = component.destructibleBlastCollisionQuality == 0
			? 1 : (component.destructibleBlastCollisionQuality == 1 ? 4 : 8);
		chunk->isActive = false;
		chunkIds.push_back(chunkId);
	}
	return chunkIds.size() >= 2U;
}

bool EditorBlastDestructionManager::PrepareAutomaticChunks(
	EditorGameObject& owner,
	EditorComponent& component,
	std::vector<int32_t>& chunkIds,
	std::vector<std::pair<uint32_t, uint32_t>>& bondPairs) {
	const EditorGameObject ownerSnapshot = owner;
	std::vector<CachedChunk> chunks;
	if (!LoadOrBakeCache(owner, component, chunks, bondPairs)) return false;
	const EditorComponent settingsSnapshot = component;
	const int32_t ownerId = ownerSnapshot.id;
	if (!CreateGeneratedChunkObjects(ownerSnapshot, settingsSnapshot, chunks, chunkIds)) {
		if (EditorGameObject* stableOwner = editorScene_->FindGameObject(ownerId)) {
			if (EditorComponent* stableComponent = EditorComponentUtility::FindComponent(*stableOwner, EditorComponentType::DestructiblePart)) {
				SetBakeFailure(*stableComponent, "Failed", "内部Chunk GameObjectを生成できませんでした。");
			}
		}
		PushConsoleMessage("Blast: 内部Chunk生成失敗 owner=" + std::to_string(ownerId));
		return false;
	}
	return true;
}

bool EditorBlastDestructionManager::BuildDestructible(
	EditorGameObject& owner,
	EditorComponent& component) {
	const int32_t ownerId = owner.id;
	const std::string ownerName = owner.name;
	const bool usePrefracturedChildren = component.destructibleBlastUsePrefracturedChildren;
	EditorComponent runtimeSettings = component;
	std::vector<int32_t> chunkIds;
	std::vector<std::pair<uint32_t, uint32_t>> cachedBondPairs;
	if (usePrefracturedChildren) {
		chunkIds = owner.children;
	} else if (!PrepareAutomaticChunks(owner, component, chunkIds, cachedBondPairs)) {
		return false;
	}
	if (chunkIds.size() < 2U) {
		if (EditorGameObject* stableOwner = editorScene_->FindGameObject(ownerId)) {
			if (EditorComponent* stableComponent = EditorComponentUtility::FindComponent(*stableOwner, EditorComponentType::DestructiblePart)) {
				SetBakeFailure(*stableComponent, "Failed", "有効なChunkを2個以上生成できませんでした。");
			}
		}
		PushConsoleMessage(
			"Blast: " + ownerName + " はChunkが2個未満のため作成できません。");
		if (!usePrefracturedChildren) {
			for (const int32_t chunkId : chunkIds) editorScene_->DeleteGameObject(chunkId);
		}
		return false;
	}
	const auto removeGeneratedChunksOnFailure = [&]() {
		if (!usePrefracturedChildren) {
			for (const int32_t chunkId : chunkIds) editorScene_->DeleteGameObject(chunkId);
		}
	};
	const auto markRuntimeFailure = [&](const std::string& reason) {
		if (EditorGameObject* stableOwner = editorScene_->FindGameObject(ownerId)) {
			if (EditorComponent* stableComponent = EditorComponentUtility::FindComponent(*stableOwner, EditorComponentType::DestructiblePart)) {
				SetBakeFailure(*stableComponent, "Failed", reason);
			}
		}
	};

	DestructibleRuntime runtime{};
	runtime.ownerGameObjectId = ownerId;
	runtime.chunkMass = (std::max)(runtimeSettings.destructibleBlastChunkMass, 0.001f);
	runtime.chunkGameObjectIds = chunkIds;
	if (!usePrefracturedChildren) {
		runtime.generatedChunkGameObjectIds = chunkIds;
	}
	const uint32_t chunkCount = static_cast<uint32_t>(runtime.chunkGameObjectIds.size());
	std::vector<NvBlastChunkDesc> chunkDescs(chunkCount);
	runtime.chunkWorldPositions.resize(chunkCount);

	for (uint32_t chunkIndex = 0U; chunkIndex < chunkCount; ++chunkIndex) {
		const int32_t chunkGameObjectId = runtime.chunkGameObjectIds[chunkIndex];
		const EditorGameObject* chunk = editorScene_->FindGameObject(chunkGameObjectId);
		if (chunk == nullptr) {
			PushConsoleMessage("Blast: Chunk参照切れ id=" + std::to_string(chunkGameObjectId));
			markRuntimeFailure("生成Chunkの参照が切れています。");
			removeGeneratedChunksOnFailure();
			return false;
		}

		Vector3 worldScale{1.0f, 1.0f, 1.0f};
		Vector3 worldRotation{};
		Vector3 worldPosition{};
		editorScene_->GetWorldTransform(chunkGameObjectId, worldScale, worldRotation, worldPosition);
		(void)worldRotation;
		runtime.chunkWorldPositions[chunkIndex] = worldPosition;

		NvBlastChunkDesc& chunkDesc = chunkDescs[chunkIndex];
		chunkDesc.centroid[0] = worldPosition.x;
		chunkDesc.centroid[1] = worldPosition.y;
		chunkDesc.centroid[2] = worldPosition.z;
		chunkDesc.volume = (std::max)(
			std::fabs(worldScale.x * worldScale.y * worldScale.z),
			0.000001f);
		chunkDesc.parentChunkIndex = UINT32_MAX;
		chunkDesc.flags = NvBlastChunkDesc::SupportFlag;
		chunkDesc.userData = chunkIndex;

		// Chunkは分離前にJoltへ登録しない。表示を維持する設定でも物理だけはBlast分裂まで停止する。
		EditorGameObject* mutableChunk = editorScene_->FindGameObject(chunkGameObjectId);
		for (EditorComponent& chunkComponent : mutableChunk->components) {
			if (chunkComponent.type == EditorComponentType::RigidBody || IsColliderType(chunkComponent.type)) {
				chunkComponent.isActive = false;
			}
		}
	}

	// 近傍K個へBondを張る。全結合を避けることで局所Damageが自然に塊を切り離せる。
	const uint32_t neighborCount = static_cast<uint32_t>((std::clamp)(
		runtimeSettings.destructibleBlastBondNeighborCount,
		1,
		static_cast<int32_t>(chunkCount - 1U)));
	std::set<std::pair<uint32_t, uint32_t>> uniquePairs(
		cachedBondPairs.begin(), cachedBondPairs.end());
	if (uniquePairs.empty()) {
	// K近傍だけでは離れたCluster同士が未接続になるため、最小全域木を先に加えて必ず1つの構造体にする。
	std::vector<bool> connectedChunks(chunkCount, false);
	connectedChunks[0] = true;
	for (uint32_t connectedCount = 1U; connectedCount < chunkCount; ++connectedCount) {
		float nearestDistance = (std::numeric_limits<float>::max)();
		uint32_t nearestConnected = 0U;
		uint32_t nearestUnconnected = 0U;
		for (uint32_t firstIndex = 0U; firstIndex < chunkCount; ++firstIndex) {
			if (!connectedChunks[firstIndex]) continue;
			for (uint32_t secondIndex = 0U; secondIndex < chunkCount; ++secondIndex) {
				if (connectedChunks[secondIndex]) continue;
				const float distance = DistanceSquared(
					runtime.chunkWorldPositions[firstIndex],
					runtime.chunkWorldPositions[secondIndex]);
				if (distance < nearestDistance) {
					nearestDistance = distance;
					nearestConnected = firstIndex;
					nearestUnconnected = secondIndex;
				}
			}
		}
		connectedChunks[nearestUnconnected] = true;
		uniquePairs.emplace(
			(std::min)(nearestConnected, nearestUnconnected),
			(std::max)(nearestConnected, nearestUnconnected));
	}
	for (uint32_t firstIndex = 0U; firstIndex < chunkCount; ++firstIndex) {
		std::vector<std::pair<float, uint32_t>> distances;
		for (uint32_t secondIndex = 0U; secondIndex < chunkCount; ++secondIndex) {
			if (firstIndex == secondIndex) continue;
			distances.emplace_back(
				DistanceSquared(runtime.chunkWorldPositions[firstIndex], runtime.chunkWorldPositions[secondIndex]),
				secondIndex);
		}
		std::sort(distances.begin(), distances.end());
		for (uint32_t neighborIndex = 0U; neighborIndex < neighborCount; ++neighborIndex) {
			const uint32_t secondIndex = distances[neighborIndex].second;
			uniquePairs.emplace((std::min)(firstIndex, secondIndex), (std::max)(firstIndex, secondIndex));
		}
	}
	}

	std::vector<NvBlastBondDesc> bondDescs;
	bondDescs.reserve(uniquePairs.size());
	for (const auto& [firstIndex, secondIndex] : uniquePairs) {
		NvBlastBondDesc bondDesc{};
		const Vector3 direction = DirectionFromTo(
			runtime.chunkWorldPositions[firstIndex],
			runtime.chunkWorldPositions[secondIndex]);
		bondDesc.bond.normal[0] = direction.x;
		bondDesc.bond.normal[1] = direction.y;
		bondDesc.bond.normal[2] = direction.z;
		bondDesc.bond.area = 1.0f;
		bondDesc.bond.centroid[0] = (runtime.chunkWorldPositions[firstIndex].x + runtime.chunkWorldPositions[secondIndex].x) * 0.5f;
		bondDesc.bond.centroid[1] = (runtime.chunkWorldPositions[firstIndex].y + runtime.chunkWorldPositions[secondIndex].y) * 0.5f;
		bondDesc.bond.centroid[2] = (runtime.chunkWorldPositions[firstIndex].z + runtime.chunkWorldPositions[secondIndex].z) * 0.5f;
		bondDesc.bond.userData = static_cast<uint32_t>(bondDescs.size());
		bondDesc.chunkIndices[0] = firstIndex;
		bondDesc.chunkIndices[1] = secondIndex;
		bondDescs.push_back(bondDesc);
	}

	NvBlastAssetDesc assetDesc{};
	assetDesc.chunkCount = chunkCount;
	assetDesc.chunkDescs = chunkDescs.data();
	assetDesc.bondCount = static_cast<uint32_t>(bondDescs.size());
	assetDesc.bondDescs = bondDescs.data();
	const size_t assetMemorySize = NvBlastGetAssetMemorySize(&assetDesc, nullptr);
	const size_t assetScratchSize = NvBlastGetRequiredScratchForCreateAsset(&assetDesc, nullptr);
	runtime.assetMemory = AllocateBlastMemory(assetMemorySize);
	void* assetScratch = AllocateBlastMemory(assetScratchSize);
	if (runtime.assetMemory == nullptr || (assetScratchSize > 0U && assetScratch == nullptr)) {
		ReleaseAlignedMemory(assetScratch);
		ReleaseAlignedMemory(runtime.assetMemory);
		PushConsoleMessage("Blast: Asset用メモリを確保できませんでした。");
		markRuntimeFailure("Blast Asset用メモリを確保できませんでした。");
		removeGeneratedChunksOnFailure();
		return false;
	}
	runtime.asset = NvBlastCreateAsset(runtime.assetMemory, &assetDesc, assetScratch, nullptr);
	ReleaseAlignedMemory(assetScratch);
	if (runtime.asset == nullptr) {
		ReleaseAlignedMemory(runtime.assetMemory);
		PushConsoleMessage("Blast: NvBlastCreateAssetに失敗しました: " + ownerName);
		markRuntimeFailure("NvBlastCreateAssetに失敗しました。");
		removeGeneratedChunksOnFailure();
		return false;
	}

	const uint32_t* chunkToNode = NvBlastAssetGetChunkToGraphNodeMap(runtime.asset, nullptr);
	for (const NvBlastBondDesc& bondDesc : bondDescs) {
		runtime.bonds.push_back(BondRuntime{
			bondDesc.chunkIndices[0],
			bondDesc.chunkIndices[1],
			chunkToNode[bondDesc.chunkIndices[0]],
			chunkToNode[bondDesc.chunkIndices[1]],
			Vector3{bondDesc.bond.centroid[0], bondDesc.bond.centroid[1], bondDesc.bond.centroid[2]}});
	}

	const size_t familyMemorySize = NvBlastAssetGetFamilyMemorySize(runtime.asset, nullptr);
	runtime.familyMemory = AllocateBlastMemory(familyMemorySize);
	if (runtime.familyMemory == nullptr) {
		ReleaseAlignedMemory(runtime.assetMemory);
		PushConsoleMessage("Blast: Family用メモリを確保できませんでした。");
		markRuntimeFailure("Blast Family用メモリを確保できませんでした。");
		removeGeneratedChunksOnFailure();
		return false;
	}
	runtime.family = NvBlastAssetCreateFamily(runtime.familyMemory, runtime.asset, nullptr);

	NvBlastActorDesc actorDesc{};
	actorDesc.uniformInitialBondHealth = (std::max)(runtimeSettings.destructibleBlastBondHealth, 0.001f);
	actorDesc.uniformInitialLowerSupportChunkHealth = actorDesc.uniformInitialBondHealth;
	const size_t actorScratchSize = NvBlastFamilyGetRequiredScratchForCreateFirstActor(runtime.family, nullptr);
	void* actorScratch = AllocateBlastMemory(actorScratchSize);
	NvBlastActor* firstActor = NvBlastFamilyCreateFirstActor(runtime.family, &actorDesc, actorScratch, nullptr);
	ReleaseAlignedMemory(actorScratch);
	if (firstActor == nullptr) {
		ReleaseAlignedMemory(runtime.familyMemory);
		ReleaseAlignedMemory(runtime.assetMemory);
		PushConsoleMessage("Blast: First Actorの作成に失敗しました: " + ownerName);
		markRuntimeFailure("Blast First Actorの作成に失敗しました。");
		removeGeneratedChunksOnFailure();
		return false;
	}
	runtime.actors.push_back(firstActor);

	if (runtimeSettings.destructibleBlastHideChunksUntilFracture) {
		for (const int32_t chunkGameObjectId : runtime.chunkGameObjectIds) {
			if (EditorGameObject* chunk = editorScene_->FindGameObject(chunkGameObjectId)) {
				chunk->isActive = false;
			}
		}
	}

	if (EditorGameObject* stableOwner = editorScene_->FindGameObject(ownerId)) {
		if (EditorComponent* stableComponent = EditorComponentUtility::FindComponent(*stableOwner, EditorComponentType::DestructiblePart)) {
			stableComponent->destructibleDestroyed = false;
			stableComponent->destructibleBlastBakeStatus = "Ready";
			stableComponent->destructibleBlastBakeError.clear();
		}
	}
	// 破片予算の判断材料（Chunk Mesh、体積順、Cluster割り当て）は初期化時に1回だけ作る。
	BuildChunkVisuals(runtime);
	destructibles_.emplace(ownerId, std::move(runtime));
	PushConsoleMessage(
		"Blast: " + ownerName + " を初期化 (Chunks=" + std::to_string(chunkCount) +
		", Bonds=" + std::to_string(bondDescs.size()) + ")");
	return true;
}

bool EditorBlastDestructionManager::ApplyDamage(
	int32_t gameObjectId,
	const Vector3& worldPosition,
	float radius,
	float damage,
	float impulse) {
	auto iterator = destructibles_.find(gameObjectId);
	if (iterator == destructibles_.end() || damage <= 0.0f) {
		return false;
	}
	const EditorGameObject* owner = editorScene_ != nullptr ? editorScene_->FindGameObject(gameObjectId) : nullptr;
	const EditorComponent* destructible = owner != nullptr
		? EditorComponentUtility::FindComponent(*owner, EditorComponentType::DestructiblePart)
		: nullptr;
	const float effectiveRadius = radius > 0.0f
		? radius
		: (destructible != nullptr ? destructible->destructibleBlastDamageRadius : 0.0f);
	const float effectiveImpulse = impulse >= 0.0f
		? impulse
		: (destructible != nullptr ? destructible->destructibleBlastImpulse : 0.0f);
	return effectiveRadius > 0.0f &&
		ApplyDamage(iterator->second, worldPosition, effectiveRadius, damage, effectiveImpulse);
}

bool EditorBlastDestructionManager::FractureAll(int32_t gameObjectId, float impulse) {
	auto iterator = destructibles_.find(gameObjectId);
	if (iterator == destructibles_.end()) {
		return false;
	}

	// 全破壊の飛散中心はWorld原点ではなくObject自身の位置にする。
	// 原点を中心にすると全破片が同じ向きへ揃って飛び、爆発に見えない。
	Vector3 ownerScale{1.0f, 1.0f, 1.0f};
	Vector3 ownerRotation{};
	Vector3 ownerPosition{};
	if (editorScene_ != nullptr) {
		editorScene_->GetWorldTransform(gameObjectId, ownerScale, ownerRotation, ownerPosition);
	}
	(void)ownerScale;
	(void)ownerRotation;

	return ApplyDamage(
		iterator->second,
		ownerPosition,
		(std::numeric_limits<float>::max)(),
		(std::numeric_limits<float>::max)() * 0.25f,
		impulse);
}

bool EditorBlastDestructionManager::ApplyDamage(
	DestructibleRuntime& runtime,
	const Vector3& worldPosition,
	float radius,
	float damage,
	float impulse) {
	const float radiusSquared = radius >= (std::numeric_limits<float>::max)() * 0.5f
		? (std::numeric_limits<float>::max)()
		: radius * radius;
	bool appliedAnyDamage = false;
	bool splitAnyActor = false;
	std::vector<NvBlastActor*> resultingActors;

	for (NvBlastActor* actor : runtime.actors) {
		const uint32_t actorNodeCount = NvBlastActorGetGraphNodeCount(actor, nullptr);
		std::vector<uint32_t> actorNodes(actorNodeCount);
		NvBlastActorGetGraphNodeIndices(actorNodes.data(), actorNodeCount, actor, nullptr);
		std::unordered_set<uint32_t> actorNodeSet(actorNodes.begin(), actorNodes.end());
		std::vector<NvBlastBondFractureData> commands;
		for (const BondRuntime& bond : runtime.bonds) {
			if (!actorNodeSet.contains(bond.nodeIndex0) || !actorNodeSet.contains(bond.nodeIndex1)) {
				continue;
			}
			if (radiusSquared < (std::numeric_limits<float>::max)() &&
				DistanceSquared(bond.centroid, worldPosition) > radiusSquared) {
				continue;
			}
			commands.push_back(NvBlastBondFractureData{
				0U,
				bond.nodeIndex0,
				bond.nodeIndex1,
				damage});
		}

		if (commands.empty()) {
			resultingActors.push_back(actor);
			continue;
		}

		NvBlastFractureBuffers fractureCommands{};
		fractureCommands.bondFractureCount = static_cast<uint32_t>(commands.size());
		fractureCommands.bondFractures = commands.data();
		NvBlastActorApplyFracture(nullptr, actor, &fractureCommands, nullptr, nullptr);
		appliedAnyDamage = true;

		if (!NvBlastActorIsSplitRequired(actor, nullptr)) {
			resultingActors.push_back(actor);
			continue;
		}

		const uint32_t maximumActorCount = NvBlastActorGetMaxActorCountForSplit(actor, nullptr);
		std::vector<NvBlastActor*> newActors(maximumActorCount);
		NvBlastActorSplitEvent splitEvent{};
		splitEvent.newActors = newActors.data();
		const size_t splitScratchSize = NvBlastActorGetRequiredScratchForSplit(actor, nullptr);
		void* splitScratch = AllocateBlastMemory(splitScratchSize);
		const uint32_t newActorCount = NvBlastActorSplit(
			&splitEvent,
			actor,
			maximumActorCount,
			splitScratch,
			nullptr,
			nullptr);
		ReleaseAlignedMemory(splitScratch);
		if (newActorCount == 0U) {
			resultingActors.push_back(actor);
			continue;
		}
		newActors.resize(newActorCount);
		resultingActors.insert(resultingActors.end(), newActors.begin(), newActors.end());
		splitAnyActor = true;
	}

	runtime.actors = std::move(resultingActors);
	if (splitAnyActor) {
		ApplySplitToScene(runtime, worldPosition, impulse);
	}
	return appliedAnyDamage;
}

void EditorBlastDestructionManager::ApplySplitToScene(
	DestructibleRuntime& runtime,
	const Vector3& worldPosition,
	float impulse) {
	if (editorScene_ == nullptr || physicsManager_ == nullptr || runtime.actors.size() < 2U) {
		return;
	}

	NvBlastActor* primaryActor = nullptr;
	size_t primaryVisibleCount = 0U;
	for (NvBlastActor* actor : runtime.actors) {
		const uint32_t visibleCount = NvBlastActorGetVisibleChunkCount(actor, nullptr);
		std::vector<uint32_t> visibleChunks(visibleCount);
		NvBlastActorGetVisibleChunkIndices(visibleChunks.data(), visibleCount, actor, nullptr);
		const bool containsAnchor = std::find(visibleChunks.begin(), visibleChunks.end(), 0U) != visibleChunks.end();
		if (containsAnchor || (primaryActor == nullptr && visibleChunks.size() > primaryVisibleCount)) {
			primaryActor = actor;
			primaryVisibleCount = visibleChunks.size();
			if (containsAnchor) break;
		}
	}

	// Primary Actorは「残った本体」として静止させるが、可視Chunkが1個だけなら支える塊が無い。
	// FractureAllのような全破壊でChunk 0だけが空中へ取り残されないよう、これも破片として飛ばす。
	if (primaryVisibleCount <= 1U) {
		primaryActor = nullptr;
	}

	if (!runtime.hasSplit) {
		DisableIntactRoot(runtime.ownerGameObjectId);
		for (const int32_t chunkGameObjectId : runtime.chunkGameObjectIds) {
			if (EditorGameObject* chunk = editorScene_->FindGameObject(chunkGameObjectId)) {
				chunk->isActive = true;
			}
		}
		runtime.hasSplit = true;
		if (EditorGameObject* owner = editorScene_->FindGameObject(runtime.ownerGameObjectId)) {
			if (EditorComponent* destructible = EditorComponentUtility::FindComponent(
				*owner,
				EditorComponentType::DestructiblePart)) {
				destructible->destructibleDestroyed = true;
				if (scriptManager_ != nullptr && !destructible->destructibleDestroyedActionName.empty()) {
					EditorScriptActionPayload payload{};
					payload.type = EditorScriptActionPayloadTypeGameObject;
					payload.gameObjectId = runtime.ownerGameObjectId;
					const int32_t actionTargetGameObjectId = destructible->destructibleActionTargetGameObjectId >= 0
						? destructible->destructibleActionTargetGameObjectId
						: runtime.ownerGameObjectId;
					scriptManager_->QueueActionPayload(
						actionTargetGameObjectId,
						destructible->destructibleDestroyedActionName,
						payload);
				}
			}
		}
	}

	// 破片予算設定はScene配列の再確保でPointerが切れるため、値だけを取り出して使う。
	const EditorGameObject* settingsOwner = editorScene_->FindGameObject(runtime.ownerGameObjectId);
	const EditorComponent* settingsComponent = settingsOwner != nullptr
		? EditorComponentUtility::FindComponent(*settingsOwner, EditorComponentType::DestructiblePart)
		: nullptr;
	DebrisSettings settings = settingsComponent != nullptr
		? MakeDebrisSettings(*settingsComponent)
		: DebrisSettings{};
	settings.chunkMass = runtime.chunkMass;

	// この分裂で新しく切り離されたChunkを先に集める。
	std::vector<uint32_t> detachedChunkIndices;
	for (NvBlastActor* actor : runtime.actors) {
		if (actor == primaryActor) {
			continue;
		}
		const uint32_t visibleCount = NvBlastActorGetVisibleChunkCount(actor, nullptr);
		std::vector<uint32_t> visibleChunks(visibleCount);
		NvBlastActorGetVisibleChunkIndices(visibleChunks.data(), visibleCount, actor, nullptr);
		for (const uint32_t chunkIndex : visibleChunks) {
			if (chunkIndex >= runtime.chunkGameObjectIds.size()) continue;
			const int32_t chunkGameObjectId = runtime.chunkGameObjectIds[chunkIndex];
			if (runtime.dynamicChunkGameObjectIds.contains(chunkGameObjectId)) continue;
			if (editorScene_->FindGameObject(chunkGameObjectId) == nullptr) continue;
			detachedChunkIndices.push_back(chunkIndex);
		}
	}
	if (detachedChunkIndices.empty()) {
		return;
	}

	// 距離LODは最初の分裂時のCamera距離だけで決め、同じ破壊の途中で方式が変わらないようにする。
	if (!runtime.hasResolvedDebrisPlan) {
		Vector3 ownerScale{1.0f, 1.0f, 1.0f};
		Vector3 ownerRotation{};
		Vector3 ownerPosition = worldPosition;
		editorScene_->GetWorldTransform(runtime.ownerGameObjectId, ownerScale, ownerRotation, ownerPosition);
		(void)ownerScale;
		(void)ownerRotation;
		ResolveDebrisPlan(settings, ownerPosition, runtime);
	}

	// 物理破片の予算内かどうかで二分する。最適化OFFのときは全てが予算内になる。
	std::vector<uint32_t> physicsChunkIndices;
	std::vector<uint32_t> overflowChunkIndices;
	for (const uint32_t chunkIndex : detachedChunkIndices) {
		// Scene全体の上限は最適化OnOffに関係なく効かせる。同時破壊での物理破片の総数を抑えるため。
		const bool withinPhysicsBudget = HasScenePhysicsDebrisBudget() &&
			(!runtime.resolvedOptimizeEnabled ||
				(chunkIndex < runtime.chunkVisuals.size() &&
					runtime.chunkVisuals[chunkIndex].volumeRank < runtime.resolvedMaxPhysicsChunks));
		if (withinPhysicsBudget) {
			physicsChunkIndices.push_back(chunkIndex);
		}
		else {
			overflowChunkIndices.push_back(chunkIndex);
		}
	}

	int32_t physicsChunkCount = 0;
	int32_t clusterFollowerCount = 0;
	int32_t gpuDebrisCount = 0;
	int32_t vanishedChunkCount = 0;

	// 先に物理破片を作る。Cluster併用時はこれがCarrierになるため、追従破片より前に確定させる。
	for (const uint32_t chunkIndex : physicsChunkIndices) {
		DetachChunkAsPhysics(runtime, settings, chunkIndex, worldPosition, impulse);
		physicsChunkCount++;
	}

	// 予算を超えた破片は体積の大きい順に処理し、毎回同じ破片がClusterへ入るようにする。
	std::sort(
		overflowChunkIndices.begin(),
		overflowChunkIndices.end(),
		[&runtime](uint32_t first, uint32_t second) {
			if (first >= runtime.chunkVisuals.size() || second >= runtime.chunkVisuals.size()) {
				return first < second;
			}
			if (runtime.chunkVisuals[first].volume != runtime.chunkVisuals[second].volume) {
				return runtime.chunkVisuals[first].volume > runtime.chunkVisuals[second].volume;
			}
			return first < second;
		});

	for (const uint32_t chunkIndex : overflowChunkIndices) {
		// Cluster: 近いCarrierへ寄せる。空きが無い場合はGPU破片へ回し、GPUを使わない設定なら相乗りさせる。
		if (runtime.resolvedUseCluster) {
			bool carrierHasRoom = false;
			const int32_t carrierGameObjectId = FindClusterCarrier(runtime, chunkIndex, settings, carrierHasRoom);
			if (carrierGameObjectId >= 0 && (carrierHasRoom || !runtime.resolvedUseGpuDebris)) {
				AttachClusterFollower(runtime, settings, chunkIndex, carrierGameObjectId);
				clusterFollowerCount++;
				continue;
			}
		}
		if (runtime.resolvedUseGpuDebris) {
			if (SpawnGpuDebris(runtime, settings, chunkIndex, worldPosition, impulse)) {
				gpuDebrisCount++;
				continue;
			}
			if (runtime.remainingGpuDebrisBudget == 0) {
				// 遠距離LODの上限を超えた破片は、物理もGPUも使わずその場で消す。
				HideChunkObject(runtime.chunkGameObjectIds[chunkIndex]);
				runtime.dynamicChunkGameObjectIds.insert(runtime.chunkGameObjectIds[chunkIndex]);
				vanishedChunkCount++;
				continue;
			}
		}
		// Cluster もGPU破片も使えない破片は、消さずに従来の物理破片へ戻す。
		// Scene全体の上限を超えている場合だけは物理へ戻さず、描画だけ止めて総数を守る。
		if (!HasScenePhysicsDebrisBudget()) {
			HideChunkObject(runtime.chunkGameObjectIds[chunkIndex]);
			runtime.dynamicChunkGameObjectIds.insert(runtime.chunkGameObjectIds[chunkIndex]);
			vanishedChunkCount++;
			continue;
		}
		DetachChunkAsPhysics(runtime, settings, chunkIndex, worldPosition, impulse);
		physicsChunkCount++;
	}

	PushConsoleMessage(
		"Blast: Actor分裂 owner=" + std::to_string(runtime.ownerGameObjectId) +
		" actors=" + std::to_string(runtime.actors.size()) +
		(runtime.resolvedOptimizeEnabled ? " optimize=on" : " optimize=off") +
		" physics=" + std::to_string(physicsChunkCount) +
		" cluster=" + std::to_string(clusterFollowerCount) +
		" gpuDebris=" + std::to_string(gpuDebrisCount) +
		" vanished=" + std::to_string(vanishedChunkCount));
}

EditorBlastDestructionManager::DebrisSettings EditorBlastDestructionManager::MakeDebrisSettings(
	const EditorComponent& component) {
	DebrisSettings settings{};
	settings.optimizeEnabled = component.destructibleBlastOptimizeEnabled;
	settings.maxPhysicsChunks = (std::clamp)(component.destructibleBlastMaxPhysicsChunks, 0, 256);
	settings.useCluster = component.destructibleBlastUseClusterPhysics;
	settings.clusterSize = (std::clamp)(component.destructibleBlastClusterSize, 1, 256);
	settings.clusterScatterDelay = (std::max)(component.destructibleBlastClusterScatterDelay, 0.0f);
	settings.clusterScatterDistance = (std::clamp)(component.destructibleBlastClusterScatterDistance, 0.0f, 1000.0f);
	settings.physicsLifetime = (std::max)(component.destructibleBlastPhysicsLifetime, 0.0f);
	settings.debrisSinkDelay = (std::max)(component.destructibleBlastDebrisSinkDelay, 0.0f);
	settings.debrisSinkDuration = (std::clamp)(component.destructibleBlastDebrisSinkDuration, 0.01f, 600.0f);
	settings.debrisSinkDistance = (std::clamp)(component.destructibleBlastDebrisSinkDistance, 0.0f, 1000.0f);
	settings.useGpuDebris = component.destructibleBlastUseGpuDebris;
	settings.gpuDebrisLifetime = (std::clamp)(component.destructibleBlastGpuDebrisLifetime, 0.01f, 600.0f);
	settings.gpuDebrisGravity = (std::clamp)(component.destructibleBlastGpuDebrisGravity, -1000.0f, 1000.0f);
	settings.gpuDebrisDrag = (std::max)(component.destructibleBlastGpuDebrisDrag, 0.0f);
	settings.gpuDebrisWind = (std::max)(component.destructibleBlastGpuDebrisWind, 0.0f);
	settings.gpuDebrisMotionType = (std::clamp)(component.destructibleBlastGpuDebrisMotionType, 0, 2);
	settings.gpuDebrisRadialAcceleration = (std::clamp)(component.destructibleBlastGpuDebrisRadialAcceleration, 0.0f, 1000.0f);
	settings.gpuDebrisUpdraft = (std::clamp)(component.destructibleBlastGpuDebrisUpdraft, 0.0f, 1000.0f);
	settings.gpuDebrisAngularSpeed = (std::clamp)(component.destructibleBlastGpuDebrisAngularSpeed, -36000.0f, 36000.0f);
	settings.gpuDebrisSpin = (std::clamp)(component.destructibleBlastGpuDebrisSpin, -36000.0f, 36000.0f);
	settings.gpuDebrisMeshLimit = (std::clamp)(component.destructibleBlastGpuDebrisMeshLimit, 0, 256);
	settings.useDistanceLod = component.destructibleBlastUseDistanceLod;
	settings.lodNearDistance = (std::max)(component.destructibleBlastLodNearDistance, 0.0f);
	settings.lodFarDistance = (std::max)(component.destructibleBlastLodFarDistance, 0.0f);
	settings.lodFarDebrisCount = (std::max)(component.destructibleBlastLodFarDebrisCount, 0);
	settings.chunkMass = (std::max)(component.destructibleBlastChunkMass, 0.001f);
	settings.defaultImpulse = (std::max)(component.destructibleBlastImpulse, 0.0f);

	// OnOffがOFFのときは軽量化を一切効かせない。ここで打ち消しておけば後段は最適化の有無を気にしなくてよい。
	if (!settings.optimizeEnabled) {
		settings.maxPhysicsChunks = 256;
		settings.useCluster = false;
		settings.useGpuDebris = false;
		settings.physicsLifetime = 0.0f;
		settings.useDistanceLod = false;
		settings.lodFarDebrisCount = 0;
	}
	return settings;
}

void EditorBlastDestructionManager::BuildChunkVisuals(DestructibleRuntime& runtime) {
	const size_t chunkCount = runtime.chunkGameObjectIds.size();
	runtime.chunkVisuals.assign(chunkCount, ChunkVisual{});
	if (editorScene_ == nullptr || chunkCount == 0U) {
		return;
	}

	// Chunk MeshのBoundsから、GPU破片の実寸Scaleと物理化するChunkを選ぶ体積目安を作る。
	for (size_t chunkIndex = 0U; chunkIndex < chunkCount; ++chunkIndex) {
		ChunkVisual& visual = runtime.chunkVisuals[chunkIndex];
		const EditorGameObject* chunk = editorScene_->FindGameObject(runtime.chunkGameObjectIds[chunkIndex]);
		if (chunk == nullptr) continue;
		const EditorComponent* meshFilter = EditorComponentUtility::FindComponent(*chunk, EditorComponentType::MeshFilter);
		const EditorComponent* renderer = EditorComponentUtility::FindComponent(*chunk, EditorComponentType::ModelRenderer);
		if (meshFilter != nullptr && !meshFilter->assetPath.empty()) {
			visual.meshAssetPath = meshFilter->assetPath;
		}
		else if (renderer != nullptr) {
			visual.meshAssetPath = renderer->assetPath;
		}
		if (renderer != nullptr) {
			// GPU破片はTextureもLightingも持たないため、少なくとも元Rendererの色と発光は引き継ぐ。
			visual.color = renderer->color;
			visual.alpha = (std::clamp)(renderer->alpha, 0.0f, 1.0f);
			visual.emissionStrength = (std::max)(renderer->emissionStrength, 0.0f);
		}
		const ModelData* modelData = !visual.meshAssetPath.empty()
			? EditorAssetUtility::GetSharedModelAssetData(visual.meshAssetPath, false)
			: nullptr;
		if (modelData == nullptr) continue;
		const float sizeX = std::fabs(modelData->localBoundsSize.x);
		const float sizeY = std::fabs(modelData->localBoundsSize.y);
		const float sizeZ = std::fabs(modelData->localBoundsSize.z);
		visual.renderScale = (std::max)((std::max)((std::max)(sizeX, sizeY), sizeZ), 0.0001f);
		visual.volume = (std::max)(sizeX * sizeY * sizeZ, 0.0f);
	}

	// 体積降順の順位。同じ体積ならChunk順にして、毎回同じChunkが物理側へ残るようにする。
	std::vector<size_t> volumeOrder(chunkCount);
	for (size_t chunkIndex = 0U; chunkIndex < chunkCount; ++chunkIndex) volumeOrder[chunkIndex] = chunkIndex;
	std::sort(
		volumeOrder.begin(),
		volumeOrder.end(),
		[&runtime](size_t first, size_t second) {
			if (runtime.chunkVisuals[first].volume != runtime.chunkVisuals[second].volume) {
				return runtime.chunkVisuals[first].volume > runtime.chunkVisuals[second].volume;
			}
			return first < second;
		});
	for (size_t rank = 0U; rank < volumeOrder.size(); ++rank) {
		runtime.chunkVisuals[volumeOrder[rank]].volumeRank = static_cast<int32_t>(rank);
	}
}

void EditorBlastDestructionManager::ResolveDebrisPlan(
	const DebrisSettings& settings,
	const Vector3& ownerWorldPosition,
	DestructibleRuntime& runtime) const {
	runtime.resolvedOptimizeEnabled = settings.optimizeEnabled;
	runtime.resolvedMaxPhysicsChunks = settings.maxPhysicsChunks;
	runtime.resolvedUseCluster = settings.useCluster;
	runtime.resolvedUseGpuDebris = settings.useGpuDebris;
	runtime.remainingGpuDebrisBudget = -1;
	runtime.hasResolvedDebrisPlan = true;

	if (settings.useDistanceLod) {
		// 近距離は設定どおり、中距離は物理数を半分へ、遠距離はGPU破片だけへ落とす。
		const float nearDistance = (std::min)(settings.lodNearDistance, settings.lodFarDistance);
		const float farDistance = (std::max)(settings.lodNearDistance, settings.lodFarDistance);
		const float distance = std::sqrt(
			DistanceSquared(ownerWorldPosition, EditorSharedState::g_gameCameraPosition));
		if (distance >= farDistance) {
			runtime.resolvedMaxPhysicsChunks = 0;
			runtime.resolvedUseCluster = false;
			runtime.resolvedUseGpuDebris = true;
			runtime.remainingGpuDebrisBudget = settings.lodFarDebrisCount > 0 ? settings.lodFarDebrisCount : -1;
		}
		else if (distance >= nearDistance) {
			runtime.resolvedMaxPhysicsChunks = (std::max)(settings.maxPhysicsChunks / 2, 1);
			runtime.resolvedUseGpuDebris = true;
		}
	}

	// GPU破片はEffect ManagerのGPU Particleへ積むため、未接続なら使わない。
	if (effectManager_ == nullptr) {
		runtime.resolvedUseGpuDebris = false;
	}
}

int32_t EditorBlastDestructionManager::FindClusterCarrier(
	DestructibleRuntime& runtime,
	uint32_t chunkIndex,
	const DebrisSettings& settings,
	bool& outHasRoom) const {
	outHasRoom = false;
	if (runtime.clusterCarriers.empty() || chunkIndex >= runtime.chunkWorldPositions.size()) {
		return -1;
	}

	// 空きのあるCarrierを優先し、無ければ最も近いCarrierを返す。判断は呼び出し側に任せる。
	const int32_t followerLimit = (std::max)(settings.clusterSize - 1, 0);
	int32_t nearestCarrierGameObjectId = -1;
	int32_t nearestWithRoomGameObjectId = -1;
	float nearestDistance = (std::numeric_limits<float>::max)();
	float nearestWithRoomDistance = (std::numeric_limits<float>::max)();
	for (const ClusterCarrierRuntime& carrier : runtime.clusterCarriers) {
		if (carrier.chunkIndex >= runtime.chunkWorldPositions.size()) continue;
		if (editorScene_ != nullptr && editorScene_->FindGameObject(carrier.chunkGameObjectId) == nullptr) continue;
		const float distance = DistanceSquared(
			runtime.chunkWorldPositions[chunkIndex],
			runtime.chunkWorldPositions[carrier.chunkIndex]);
		if (distance < nearestDistance) {
			nearestDistance = distance;
			nearestCarrierGameObjectId = carrier.chunkGameObjectId;
		}
		if (carrier.followerCount < followerLimit && distance < nearestWithRoomDistance) {
			nearestWithRoomDistance = distance;
			nearestWithRoomGameObjectId = carrier.chunkGameObjectId;
		}
	}
	if (nearestWithRoomGameObjectId >= 0) {
		outHasRoom = true;
		return nearestWithRoomGameObjectId;
	}
	return nearestCarrierGameObjectId;
}

void EditorBlastDestructionManager::DetachChunkAsPhysics(
	DestructibleRuntime& runtime,
	const DebrisSettings& settings,
	uint32_t chunkIndex,
	const Vector3& blastCenter,
	float impulse) {
	if (editorScene_ == nullptr || physicsManager_ == nullptr ||
		chunkIndex >= runtime.chunkGameObjectIds.size()) {
		return;
	}
	const int32_t chunkGameObjectId = runtime.chunkGameObjectIds[chunkIndex];
	EditorGameObject* chunk = editorScene_->FindGameObject(chunkGameObjectId);
	if (chunk == nullptr) {
		return;
	}

	chunk->isActive = true;
	editorScene_->SetParent(chunkGameObjectId, -1, true);
	PrepareChunkPhysics(chunkGameObjectId, runtime.chunkMass);
	physicsManager_->RegisterRuntimeHierarchy(chunkGameObjectId);
	const Vector3 direction = chunkIndex < runtime.chunkWorldPositions.size()
		? DirectionFromTo(blastCenter, runtime.chunkWorldPositions[chunkIndex])
		: Vector3{0.0f, 1.0f, 0.0f};
	physicsManager_->AddImpulse(
		chunkGameObjectId,
		Vector3{direction.x * impulse, direction.y * impulse, direction.z * impulse});
	runtime.dynamicChunkGameObjectIds.insert(chunkGameObjectId);
	physicsDebrisGameObjectIds_.insert(chunkGameObjectId);

	// Cluster併用時は、この物理ChunkがあとからCluster追従破片を運ぶCarrierになる。
	if (runtime.resolvedUseCluster) {
		ClusterCarrierRuntime carrier{};
		carrier.chunkGameObjectId = chunkGameObjectId;
		carrier.chunkIndex = chunkIndex;
		runtime.clusterCarriers.push_back(carrier);
	}

	// 後片付け設定があれば、一定時間後に沈めてGameObjectごと破棄する。
	if (settings.debrisSinkDelay > 0.0f) {
		SinkingChunkRuntime sinkingChunk{};
		sinkingChunk.chunkGameObjectId = chunkGameObjectId;
		sinkingChunk.delaySeconds = settings.debrisSinkDelay;
		sinkingChunk.durationSeconds = settings.debrisSinkDuration;
		sinkingChunk.distance = settings.debrisSinkDistance;
		runtime.sinkingChunks.push_back(sinkingChunk);
	}

	// 短時間だけ物理化する設定では、飛び散る瞬間だけDynamicのままにする。
	if (settings.physicsLifetime > 0.0f) {
		TimedPhysicsChunk timedChunk{};
		timedChunk.chunkGameObjectId = chunkGameObjectId;
		timedChunk.remainingSeconds = settings.physicsLifetime;
		runtime.timedPhysicsChunks.push_back(timedChunk);
	}
}

bool EditorBlastDestructionManager::SpawnGpuDebris(
	DestructibleRuntime& runtime,
	const DebrisSettings& settings,
	uint32_t chunkIndex,
	const Vector3& blastCenter,
	float impulse) {
	if (effectManager_ == nullptr || editorScene_ == nullptr ||
		chunkIndex >= runtime.chunkVisuals.size() ||
		runtime.remainingGpuDebrisBudget == 0) {
		return false;
	}

	// Mesh種類ごとにGPU Draw Callが増えるため、上限があるときは先頭のChunk Meshを使い回す。
	size_t meshSourceIndex = chunkIndex;
	if (settings.gpuDebrisMeshLimit > 0) {
		const size_t meshLimit = (std::min)(
			static_cast<size_t>(settings.gpuDebrisMeshLimit),
			runtime.chunkVisuals.size());
		meshSourceIndex = meshLimit > 0U ? static_cast<size_t>(chunkIndex) % meshLimit : 0U;
	}
	const std::string meshAssetPath = runtime.chunkVisuals[meshSourceIndex].meshAssetPath;
	if (meshAssetPath.empty()) {
		return false;
	}

	const int32_t chunkGameObjectId = runtime.chunkGameObjectIds[chunkIndex];
	Vector3 worldScale{1.0f, 1.0f, 1.0f};
	Vector3 worldRotation{};
	Vector3 worldPosition = runtime.chunkWorldPositions[chunkIndex];
	editorScene_->GetWorldTransform(chunkGameObjectId, worldScale, worldRotation, worldPosition);
	(void)worldRotation;
	const float averageScale = (std::max)(
		(std::fabs(worldScale.x) + std::fabs(worldScale.y) + std::fabs(worldScale.z)) / 3.0f,
		0.0001f);
	const Vector3 direction = DirectionFromTo(blastCenter, worldPosition);
	// GPU破片はRigidbodyを持たないのでScriptからあとで押せない。
	// FractureAll(id, 0.0f)のようにImpulseを渡さない呼び出しでも飛ぶよう、0なら既定Impulseで代替する。
	const float effectiveImpulse = impulse > 0.0001f ? impulse : settings.defaultImpulse;
	const float baseSpeed = (std::min)(effectiveImpulse / (std::max)(settings.chunkMass, 0.001f), kMaxGpuDebrisSpeed);
	const float speed = baseSpeed * (0.6f + NextRandomUnit() * 0.8f);
	const float spread = baseSpeed * 0.3f;

	// 重力、風、回転はGPU Compute側が積分する。CPUはこの1個の発生要求だけを積む。
	EditorEffectManager::GpuParticleSpawn spawn{};
	spawn.position = worldPosition;
	spawn.lifetime = settings.gpuDebrisLifetime;
	spawn.velocity = Vector3{
		direction.x * speed + (NextRandomUnit() - 0.5f) * spread,
		direction.y * speed + NextRandomUnit() * spread,
		direction.z * speed + (NextRandomUnit() - 0.5f) * spread};
	spawn.startSize = (std::max)(runtime.chunkVisuals[chunkIndex].renderScale * averageScale, 0.001f);
	spawn.endSize = spawn.startSize;
	spawn.startColor = runtime.chunkVisuals[chunkIndex].color;
	spawn.endColor = runtime.chunkVisuals[chunkIndex].color;
	spawn.startAlpha = (std::max)(runtime.chunkVisuals[chunkIndex].alpha, 0.01f);
	spawn.endAlpha = spawn.startAlpha;
	spawn.gravity = settings.gpuDebrisGravity;
	spawn.drag = settings.gpuDebrisDrag;
	spawn.noiseStrength = settings.gpuDebrisWind;
	spawn.noiseFrequency = 1.0f;
	// GPU側は毎フレーム運動を積分する。初速だけでなく外向き加速・旋回・上昇気流も渡す。
	// Component値 0=直線 / 1=爆発 / 2=渦 を、Particle側の 0 / 6 / 2 へ対応させる。
	spawn.motionType = settings.gpuDebrisMotionType == 1
		? 6
		: (settings.gpuDebrisMotionType == 2 ? 2 : 0);
	spawn.motionCenter = blastCenter;
	spawn.radialAcceleration = settings.gpuDebrisRadialAcceleration;
	spawn.angularSpeed = settings.gpuDebrisAngularSpeed * (3.14159265f / 180.0f);
	spawn.updraft = settings.gpuDebrisUpdraft;
	spawn.rotation = NextRandomUnit() * 6.2831853f;
	spawn.rotationSpeed = settings.gpuDebrisSpin * (3.14159265f / 180.0f) *
		(NextRandomUnit() < 0.5f ? -1.0f : 1.0f);
	spawn.emissionStrength = runtime.chunkVisuals[chunkIndex].emissionStrength;
	spawn.useCollision = false;
	spawn.billboardMode = 0;
	spawn.billboardStretch = 1.0f;
	spawn.meshLighting = 1.0f;  // 破片は立体として見せるため簡易陰影を掛ける
	spawn.renderAssetPath = meshAssetPath;
	effectManager_->QueueGpuParticleSpawn(spawn);
	if (runtime.remainingGpuDebrisBudget > 0) {
		runtime.remainingGpuDebrisBudget--;
	}

	// GameObject、Rigidbody、Colliderは使わず、以降はGPU側だけが破片を動かす。
	HideChunkObject(chunkGameObjectId);
	runtime.dynamicChunkGameObjectIds.insert(chunkGameObjectId);
	return true;
}

void EditorBlastDestructionManager::AttachClusterFollower(
	DestructibleRuntime& runtime,
	const DebrisSettings& settings,
	uint32_t chunkIndex,
	int32_t carrierGameObjectId) {
	if (editorScene_ == nullptr || chunkIndex >= runtime.chunkGameObjectIds.size()) {
		return;
	}
	const int32_t chunkGameObjectId = runtime.chunkGameObjectIds[chunkIndex];
	EditorGameObject* chunk = editorScene_->FindGameObject(chunkGameObjectId);
	if (chunk == nullptr) {
		return;
	}

	// 見た目は細かく割れたまま、物理はCarrier 1個へまとめる。
	chunk->isActive = true;
	for (EditorComponent& component : chunk->components) {
		if (IsColliderType(component.type) || component.type == EditorComponentType::RigidBody) {
			component.isActive = false;
		}
	}
	if (physicsManager_ != nullptr) {
		physicsManager_->SetGameObjectSimulationActive(chunkGameObjectId, false);
	}
	if (!editorScene_->SetParent(chunkGameObjectId, carrierGameObjectId, true)) {
		return;
	}
	const EditorGameObject* attachedChunk = editorScene_->FindGameObject(chunkGameObjectId);
	if (attachedChunk == nullptr) {
		return;
	}

	const Vector3 scatterDirection = DirectionFromTo(
		Vector3{},
		Vector3{
			NextRandomUnit() * 2.0f - 1.0f,
			NextRandomUnit() * 2.0f - 1.0f,
			NextRandomUnit() * 2.0f - 1.0f});
	ClusterFollowerRuntime follower{};
	follower.chunkGameObjectId = chunkGameObjectId;
	follower.basePosition = attachedChunk->translate;
	follower.scatterOffset = Vector3{
		scatterDirection.x * settings.clusterScatterDistance,
		scatterDirection.y * settings.clusterScatterDistance,
		scatterDirection.z * settings.clusterScatterDistance};
	follower.delaySeconds = settings.clusterScatterDelay;
	runtime.clusterFollowers.push_back(follower);
	for (ClusterCarrierRuntime& carrier : runtime.clusterCarriers) {
		if (carrier.chunkGameObjectId == carrierGameObjectId) {
			carrier.followerCount++;
			break;
		}
	}
	runtime.dynamicChunkGameObjectIds.insert(chunkGameObjectId);
}

void EditorBlastDestructionManager::UpdateTimedPhysicsChunks(
	DestructibleRuntime& runtime,
	float deltaTime) {
	if (runtime.timedPhysicsChunks.empty()) {
		return;
	}
	for (TimedPhysicsChunk& timedChunk : runtime.timedPhysicsChunks) {
		timedChunk.remainingSeconds -= deltaTime;
		if (timedChunk.remainingSeconds > 0.0f) {
			continue;
		}
		FreezeChunkPhysics(timedChunk.chunkGameObjectId);
		timedChunk.chunkGameObjectId = -1;
	}
	runtime.timedPhysicsChunks.erase(
		std::remove_if(
			runtime.timedPhysicsChunks.begin(),
			runtime.timedPhysicsChunks.end(),
			[](const TimedPhysicsChunk& timedChunk) { return timedChunk.chunkGameObjectId < 0; }),
		runtime.timedPhysicsChunks.end());
}

void EditorBlastDestructionManager::UpdateSinkingChunks(
	DestructibleRuntime& runtime,
	float deltaTime) {
	if (runtime.sinkingChunks.empty() || editorScene_ == nullptr) {
		return;
	}

	std::vector<int32_t> removingChunkGameObjectIds;
	for (SinkingChunkRuntime& sinkingChunk : runtime.sinkingChunks) {
		sinkingChunk.elapsedSeconds += deltaTime;
		const float sinkTime = sinkingChunk.elapsedSeconds - sinkingChunk.delaySeconds;
		if (sinkTime <= 0.0f) {
			continue;
		}

		EditorGameObject* chunk = editorScene_->FindGameObject(sinkingChunk.chunkGameObjectId);
		if (chunk == nullptr) {
			sinkingChunk.chunkGameObjectId = -1;
			continue;
		}

		if (!sinkingChunk.hasFrozen) {
			// 沈めている間はこちらが姿勢を持つため、先にJolt Worldから外して現在位置を基準にする。
			FreezeChunkPhysics(sinkingChunk.chunkGameObjectId);
			sinkingChunk.basePosition = chunk->translate;
			sinkingChunk.hasFrozen = true;
		}

		const float ratio = (std::clamp)(sinkTime / sinkingChunk.durationSeconds, 0.0f, 1.0f);
		chunk->translate = Vector3{
			sinkingChunk.basePosition.x,
			sinkingChunk.basePosition.y - sinkingChunk.distance * ratio,
			sinkingChunk.basePosition.z};

		if (ratio >= 1.0f) {
			removingChunkGameObjectIds.push_back(sinkingChunk.chunkGameObjectId);
			sinkingChunk.chunkGameObjectId = -1;
		}
	}

	runtime.sinkingChunks.erase(
		std::remove_if(
			runtime.sinkingChunks.begin(),
			runtime.sinkingChunks.end(),
			[](const SinkingChunkRuntime& sinkingChunk) { return sinkingChunk.chunkGameObjectId < 0; }),
		runtime.sinkingChunks.end());

	// 沈み切った破片は破棄する。Draw Call、Constant Buffer、共有Textureの参照がここで解放される。
	// Cluster追従の破片は子として一緒に消える。
	for (const int32_t chunkGameObjectId : removingChunkGameObjectIds) {
		if (physicsManager_ != nullptr) {
			physicsManager_->SetGameObjectSimulationActive(chunkGameObjectId, false);
		}
		editorScene_->DeleteGameObject(chunkGameObjectId);
		runtime.dynamicChunkGameObjectIds.erase(chunkGameObjectId);
	}
}

void EditorBlastDestructionManager::UpdateClusterFollowers(
	DestructibleRuntime& runtime,
	float deltaTime) {
	if (runtime.clusterFollowers.empty() || editorScene_ == nullptr) {
		return;
	}
	for (ClusterFollowerRuntime& follower : runtime.clusterFollowers) {
		follower.elapsedSeconds += deltaTime;
		const float scatterTime = follower.elapsedSeconds - follower.delaySeconds;
		if (scatterTime <= 0.0f) {
			continue;
		}
		EditorGameObject* chunk = editorScene_->FindGameObject(follower.chunkGameObjectId);
		if (chunk == nullptr) {
			follower.chunkGameObjectId = -1;
			continue;
		}
		const float ratio = (std::clamp)(scatterTime / kClusterScatterDuration, 0.0f, 1.0f);
		const float smoothRatio = ratio * ratio * (3.0f - 2.0f * ratio);
		chunk->translate = Vector3{
			follower.basePosition.x + follower.scatterOffset.x * smoothRatio,
			follower.basePosition.y + follower.scatterOffset.y * smoothRatio,
			follower.basePosition.z + follower.scatterOffset.z * smoothRatio};
		if (ratio >= 1.0f) {
			follower.chunkGameObjectId = -1;
		}
	}
	runtime.clusterFollowers.erase(
		std::remove_if(
			runtime.clusterFollowers.begin(),
			runtime.clusterFollowers.end(),
			[](const ClusterFollowerRuntime& follower) { return follower.chunkGameObjectId < 0; }),
		runtime.clusterFollowers.end());
}

void EditorBlastDestructionManager::FreezeChunkPhysics(int32_t chunkGameObjectId) {
	EditorGameObject* chunk = editorScene_ != nullptr ? editorScene_->FindGameObject(chunkGameObjectId) : nullptr;
	if (chunk == nullptr) {
		return;
	}

	// 飛び散り終わった瓦礫は描画だけ残し、Jolt Worldから外して物理負荷を残さない。
	if (EditorComponent* rigidBody = EditorComponentUtility::FindComponent(*chunk, EditorComponentType::RigidBody)) {
		rigidBody->isKinematic = true;
		rigidBody->useGravity = false;
		rigidBody->isActive = false;
	}
	for (EditorComponent& component : chunk->components) {
		if (IsColliderType(component.type)) {
			component.isActive = false;
		}
	}
	if (physicsManager_ != nullptr) {
		physicsManager_->SetGameObjectSimulationActive(chunkGameObjectId, false);
	}
	// Scene全体の物理破片枠を返す。以降この破片は描画だけになる。
	// 物理寿命と沈下の両方から呼ばれるため、集合から消すことで二重に枠を返さないようにする。
	physicsDebrisGameObjectIds_.erase(chunkGameObjectId);
}

void EditorBlastDestructionManager::HideChunkObject(int32_t chunkGameObjectId) {
	EditorGameObject* chunk = editorScene_ != nullptr ? editorScene_->FindGameObject(chunkGameObjectId) : nullptr;
	if (chunk == nullptr) {
		return;
	}

	chunk->isActive = false;
	physicsDebrisGameObjectIds_.erase(chunkGameObjectId);
	for (EditorComponent& component : chunk->components) {
		if (IsRendererType(component.type) || IsColliderType(component.type) ||
			component.type == EditorComponentType::RigidBody) {
			component.isActive = false;
		}
	}
	if (physicsManager_ != nullptr) {
		physicsManager_->SetGameObjectSimulationActive(chunkGameObjectId, false);
	}
}

float EditorBlastDestructionManager::NextRandomUnit() {
	std::uniform_real_distribution<float> distribution(0.0f, 1.0f);
	return distribution(debrisRandomEngine_);
}

void EditorBlastDestructionManager::PrepareChunkPhysics(int32_t chunkGameObjectId, float mass) {
	EditorGameObject* chunk = editorScene_ != nullptr ? editorScene_->FindGameObject(chunkGameObjectId) : nullptr;
	if (chunk == nullptr) {
		return;
	}

	EditorComponent* rigidBody = EditorComponentUtility::FindComponent(*chunk, EditorComponentType::RigidBody);
	if (rigidBody == nullptr) {
		editorScene_->AddComponent(chunkGameObjectId, EditorComponentType::RigidBody);
		rigidBody = EditorComponentUtility::FindComponent(*chunk, EditorComponentType::RigidBody);
	}
	if (rigidBody != nullptr) {
		rigidBody->isActive = true;
		rigidBody->isKinematic = false;
		rigidBody->mass = (std::max)(mass, 0.001f);
		rigidBody->useGravity = true;
	}

	bool hasCollider = false;
	for (EditorComponent& component : chunk->components) {
		if (IsColliderType(component.type)) {
			component.isActive = true;
			hasCollider = true;
			break;
		}
	}
	if (!hasCollider) {
		const bool hasMesh = EditorComponentUtility::FindComponent(*chunk, EditorComponentType::ModelRenderer) != nullptr ||
			EditorComponentUtility::FindComponent(*chunk, EditorComponentType::SkinnedMeshRenderer) != nullptr ||
			EditorComponentUtility::FindComponent(*chunk, EditorComponentType::MeshFilter) != nullptr;
		if (!hasMesh) {
			editorScene_->AddComponent(chunkGameObjectId, EditorComponentType::BoxCollider);
		}
	}
}

void EditorBlastDestructionManager::DisableIntactRoot(int32_t ownerGameObjectId) {
	EditorGameObject* owner = editorScene_ != nullptr ? editorScene_->FindGameObject(ownerGameObjectId) : nullptr;
	if (owner == nullptr) {
		return;
	}
	for (EditorComponent& component : owner->components) {
		if (IsRendererType(component.type) || IsColliderType(component.type) ||
			component.type == EditorComponentType::RigidBody) {
			component.isActive = false;
		}
	}
	physicsManager_->SetGameObjectSimulationActive(ownerGameObjectId, false);
}

bool EditorBlastDestructionManager::IsFractured(int32_t gameObjectId) const {
	const auto iterator = destructibles_.find(gameObjectId);
	return iterator != destructibles_.end() && iterator->second.hasSplit;
}

int32_t EditorBlastDestructionManager::GetChunkCount(int32_t gameObjectId) const {
	const auto iterator = destructibles_.find(gameObjectId);
	return iterator != destructibles_.end()
		? static_cast<int32_t>(iterator->second.chunkGameObjectIds.size())
		: 0;
}

int32_t EditorBlastDestructionManager::GetActorCount(int32_t gameObjectId) const {
	const auto iterator = destructibles_.find(gameObjectId);
	return iterator != destructibles_.end()
		? static_cast<int32_t>(iterator->second.actors.size())
		: 0;
}

int32_t EditorBlastDestructionManager::GetBondCount(int32_t gameObjectId) const {
	const auto iterator = destructibles_.find(gameObjectId);
	return iterator != destructibles_.end()
		? static_cast<int32_t>(iterator->second.bonds.size())
		: 0;
}

bool EditorBlastDestructionManager::GetChunkGameObjectId(
	int32_t gameObjectId, int32_t chunkIndex, int32_t& outChunkGameObjectId) const {
	const auto iterator = destructibles_.find(gameObjectId);
	if (iterator == destructibles_.end() || chunkIndex < 0 ||
		static_cast<size_t>(chunkIndex) >= iterator->second.chunkGameObjectIds.size()) {
		return false;
	}
	outChunkGameObjectId = iterator->second.chunkGameObjectIds[static_cast<size_t>(chunkIndex)];
	return true;
}

bool EditorBlastDestructionManager::HasScenePhysicsDebrisBudget() const {
	// Scene全体の上限。DestructiblePart単位の上限とは別に、同時破壊で物理破片が増えすぎるのを防ぐ。
	const int32_t maximumCount = ProjectSettings::Get().GetData().maxScenePhysicsDebris;
	return maximumCount <= 0 ||
		static_cast<int32_t>(physicsDebrisGameObjectIds_.size()) < maximumCount;
}

bool EditorBlastDestructionManager::IsChunkDetached(int32_t gameObjectId, int32_t chunkIndex) const {
	const auto iterator = destructibles_.find(gameObjectId);
	if (iterator == destructibles_.end() || chunkIndex < 0 ||
		static_cast<size_t>(chunkIndex) >= iterator->second.chunkGameObjectIds.size()) {
		return false;
	}
	const int32_t chunkGameObjectId = iterator->second.chunkGameObjectIds[static_cast<size_t>(chunkIndex)];
	return iterator->second.dynamicChunkGameObjectIds.find(chunkGameObjectId) !=
		iterator->second.dynamicChunkGameObjectIds.end();
}

void EditorBlastDestructionManager::PushConsoleMessage(const std::string& message) const {
	if (consoleMessages_ != nullptr) {
		consoleMessages_->push_back(message);
	}
}

void EditorBlastDestructionManager::ReleaseAlignedMemory(void*& memory) {
	if (memory != nullptr) {
		_aligned_free(memory);
		memory = nullptr;
	}
}
