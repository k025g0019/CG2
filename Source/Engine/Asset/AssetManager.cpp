#include "AssetManager.h"

// AssetRegistry.hはAssetManager.hに依存するが、逆方向(AssetManager.hがAssetRegistry.hを
// 必要とする)は無いため、ここは.cpp側だけの依存でヘッダーの循環を避けている。
#include "AssetRegistry.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>
#include <unordered_set>

//========================================
// Asset操作・Hot Reload処理の構成
//========================================

// AssetManagerはAsset種別ごとの実処理を直接実装せず、登録されたHandlerへ委譲する。
// Registryが「どのAssetが存在し、何へ依存するか」を管理するのに対し、
// Managerは「変更通知を受けて、再読込・無効化・解放を実行する」役割を持つ。
namespace {
	//------------------------------
	// Path・Hash補助処理
	//------------------------------

	std::string ToLowerExtension(const std::string& path) {
		// Windowsでは拡張子の大文字小文字が混在するため、比較前に小文字へ統一する。
		std::string extension = std::filesystem::path(path).extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
			return static_cast<char>(std::tolower(character));
		});
		return extension;
	}

	// Assets/ resources/ 配下だけを依存関係として認め、それ以外("..","絶対Path"を含む)は
	// 無視する。EditorTeamCollaborationManager::ResolveSafeAssetPathと同じ判定基準。
	bool IsValidProjectAssetPath(const std::string& candidatePath, std::filesystem::path& outResolvedPath) {
		const std::filesystem::path normalizedPath =
			std::filesystem::path(candidatePath).lexically_normal();

		if (normalizedPath.empty() || normalizedPath.is_absolute()) {
			return false;
		}

		const std::string normalizedText = normalizedPath.generic_string();
		const bool isInsideAssets = normalizedText == "Assets" ||
			normalizedText.starts_with("Assets/");
		const bool isInsideResources = normalizedText == "resources" ||
			normalizedText.starts_with("resources/");

		if ((!isInsideAssets && !isInsideResources) || normalizedText.find("..") != std::string::npos) {
			return false;
		}

		outResolvedPath = normalizedPath;
		return true;
	}

	// FNV-1a。比較用途(同一判定)なので暗号強度は不要で、既存の共同制作側Hash
	// (EditorTeamCollaborationManager::CalculateFileHash)と同じアルゴリズム・出力形式に
	// そろえている。片方だけ形式が変わると、通信で届いたHash文字列とここで計算した
	// Hash文字列が一致しなくなり、Asset同期が全滅するため厳密にそろえる必要がある。
	std::string CalculateFileHashChunked(const std::filesystem::path& filePath) {
		std::ifstream file(filePath, std::ios::binary);

		if (!file.is_open()) {
			return {};
		}

		std::uint64_t hash = 1469598103934665603ull;
		// File全体をMemoryへ載せず64KiB単位で読み、大きなModel/Textureでも一時Memoryを固定する。
		std::array<char, 65536> buffer{};

		while (file.good()) {
			file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
			const std::streamsize readByteCount = file.gcount();

			for (std::streamsize byteIndex = 0; byteIndex < readByteCount; byteIndex++) {
				hash ^= static_cast<std::uint64_t>(
					static_cast<unsigned char>(buffer[static_cast<std::size_t>(byteIndex)]));
				hash *= 1099511628211ull;
			}
		}

		std::ostringstream hashStream;
		hashStream << std::hex << std::uppercase << hash;
		return hashStream.str();
	}
}

//========================================
// Asset種別判定処理
//========================================

AssetType DetermineAssetTypeFromPath(const std::string& path) {
	const std::string extension = ToLowerExtension(path);

	if (extension == ".fbx" || extension == ".obj") {
		return AssetType::Model;
	}

	if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
		extension == ".tga" || extension == ".dds") {
		return AssetType::Texture;
	}

	if (extension == ".wav" || extension == ".mp3" || extension == ".ogg") {
		return AssetType::Audio;
	}

	if (extension == ".effect" || extension == ".effectdef") {
		return AssetType::Vfx;
	}

	if (extension == ".animclip" || extension == ".animgraph") {
		return AssetType::Animation;
	}

	if (extension == ".material" || extension == ".mtl") {
		return AssetType::Material;
	}

	if (extension == ".prefab") {
		return AssetType::Prefab;
	}

	if (extension == ".inputactions") {
		return AssetType::InputAction;
	}

	if (extension == ".cpp" || extension == ".h" || extension == ".hpp") {
		return AssetType::Script;
	}

	return AssetType::Unknown;
}

const char* ToString(AssetType assetType) {
	switch (assetType) {
	case AssetType::Model: return "Model";
	case AssetType::Texture: return "Texture";
	case AssetType::Audio: return "Audio";
	case AssetType::Vfx: return "Vfx";
	case AssetType::Animation: return "Animation";
	case AssetType::Material: return "Material";
	case AssetType::Prefab: return "Prefab";
	case AssetType::InputAction: return "InputAction";
	case AssetType::Script: return "Script";
	default: return "Unknown";
	}
}

std::vector<std::string> ExtractProjectPathReferences(const std::string& text) {
	// Text Asset内の任意文字列からProject Asset Pathだけを抽出する。
	// 完全な各Format Parserではないため、引用符・改行・区切り記号までを1 Pathとして扱う。
	constexpr const char* projectPrefixes[] = {"Assets/", "resources/"};
	std::unordered_set<std::string> paths;

	for (const char* prefix : projectPrefixes) {
		std::size_t searchPosition = 0u;

		while ((searchPosition = text.find(prefix, searchPosition)) != std::string::npos) {
			std::size_t pathEnd = searchPosition;

			while (pathEnd < text.size()) {
				const char character = text[pathEnd];

				if (character == '|' || character == '\r' || character == '\n' ||
					character == '"' || character == '\\') {
					break;
				}

				pathEnd++;
			}

			const std::string candidatePath =
				std::filesystem::path(text.substr(searchPosition, pathEnd - searchPosition))
					.lexically_normal().generic_string();
			std::filesystem::path safePath;

			if (IsValidProjectAssetPath(candidatePath, safePath)) {
				paths.insert(safePath.generic_string());
			}

			searchPosition = pathEnd > searchPosition ? pathEnd : searchPosition + 1u;
		}
	}

	return std::vector<std::string>(paths.begin(), paths.end());
}

AssetManager& AssetManager::Get() {
	// Process内でHandler/Hash Cacheを1つに保つためのMeyers Singleton。
	static AssetManager instance;
	return instance;
}

void AssetManager::RegisterHandler(AssetType assetType, AssetTypeHandler handler) {
	// 同じ種別を再登録した場合は、新しいAdapterで置き換える。
	handlers_[assetType] = std::move(handler);
}

bool AssetManager::HasHandler(AssetType assetType) const {
	return handlers_.find(assetType) != handlers_.end();
}

AssetType AssetManager::DetermineAssetType(const std::string& path) const {
	return DetermineAssetTypeFromPath(path);
}

namespace {
	//------------------------------
	// Hot Reload可否判定
	//------------------------------

	AssetNotifyResult ComputeNotifyResult(
		const std::unordered_map<AssetType, AssetTypeHandler>& handlers,
		const std::string& path) {
		const AssetType assetType = DetermineAssetTypeFromPath(path);

		if (assetType == AssetType::Unknown) {
			return AssetNotifyResult{
				AssetReloadResult::Unknown,
				"拡張子からAsset種別を判定できません: " + path};
		}

		const auto handlerIterator = handlers.find(assetType);

		if (handlerIterator == handlers.end()) {
			return AssetNotifyResult{
				AssetReloadResult::NoHandler,
				std::string(ToString(assetType)) + " 用のAdapterがまだ登録されていません"};
		}

		const AssetTypeHandler& handler = handlerIterator->second;

		// 自動反映できないAssetでも変更自体は認識し、手動操作が必要な理由を返す。
		if (!handler.supportsHotReload) {
			return AssetNotifyResult{
				AssetReloadResult::RequiresManualAction,
				handler.unsupportedReason.empty()
					? std::string(ToString(assetType)) + " は安全な自動反映に対応していません"
					: handler.unsupportedReason};
		}

		// Reload Callbackが無いHandlerは、外部状態を持たず通知だけで反映済みとみなす。
		if (!handler.reload) {
			return AssetNotifyResult{AssetReloadResult::Applied, {}};
		}

		if (!handler.reload(path)) {
			return AssetNotifyResult{
				AssetReloadResult::RequiresManualAction,
				handler.unsupportedReason.empty()
					? std::string(ToString(assetType)) + " の再読込に失敗しました: " + path
					: handler.unsupportedReason};
		}

		return AssetNotifyResult{AssetReloadResult::Applied, {}};
	}
}

//========================================
// Asset変更通知処理
//========================================

AssetNotifyResult AssetManager::NotifyFileChanged(const std::string& path) {
	const AssetNotifyResult result = ComputeNotifyResult(handlers_, path);

	// Registryは「要求の結果どうなったか」だけを受け取る側に徹する。Reload可否の判断ロジック
	// 自体は上のComputeNotifyResultに1箇所だけ残し、Registry側で判定を重複させない。
	AssetRegistry::Get().NotifyAssetChanged(path);
	AssetRegistry::Get().UpdateReloadState(path, result);

	return result;
}

void AssetManager::Invalidate(const std::string& path) {
	// InvalidateはCacheを無効化するが、AssetがProjectから消えたとは扱わない。
	const AssetType assetType = DetermineAssetTypeFromPath(path);
	const auto handlerIterator = handlers_.find(assetType);

	if (handlerIterator != handlers_.end() && handlerIterator->second.invalidate) {
		handlerIterator->second.invalidate(path);
	}

	hashCache_.erase(path);
}

void AssetManager::Unload(const std::string& path) {
	// Unloadは利用Resourceを解放し、Registryからも削除された状態へ進める。
	const AssetType assetType = DetermineAssetTypeFromPath(path);
	const auto handlerIterator = handlers_.find(assetType);

	if (handlerIterator != handlers_.end() && handlerIterator->second.unload) {
		handlerIterator->second.unload(path);
	}

	hashCache_.erase(path);
	AssetRegistry::Get().NotifyAssetRemoved(path);
}

std::vector<std::string> AssetManager::GetDependencies(const std::string& path) const {
	// Scene / Prefab / .animgraph / .effect などのテキスト系Assetは、中身のPath文字列を
	// 直接読めば依存が分かるため、Editor側の状態を必要とせずここで完結させる。
	// Model のようにParse済みデータが要るものだけ、Handler登録(getDependencies)へ委ねる。
	const std::string extension = ToLowerExtension(path);
	constexpr const char* textAssetExtensions[] = {
		".scene",
		".prefab",
		".animgraph",
		".animclip",
		".effect",
		".effectdef",
		".material",
		".mtl",
		".inputactions",
		".gamedata",
		".rendertexture",
		".json",
		".xml"};

	//------------------------------
	// Text Asset依存抽出
	//------------------------------

	for (const char* textAssetExtension : textAssetExtensions) {
		if (extension != textAssetExtension) {
			continue;
		}

		std::ifstream file(path, std::ios::binary);

		if (!file.is_open()) {
			return {};
		}

		const std::string content(
			(std::istreambuf_iterator<char>(file)),
			std::istreambuf_iterator<char>());
		std::vector<std::string> dependencies = ExtractProjectPathReferences(content);

		// 自分自身への参照は依存に含めない。
		dependencies.erase(
			std::remove(dependencies.begin(), dependencies.end(),
				std::filesystem::path(path).lexically_normal().generic_string()),
			dependencies.end());
		return dependencies;
	}

	//------------------------------
	// Binary・専用Format依存抽出
	//------------------------------

	// Model等はText検索では内部参照を判定できないため、種別HandlerのParserへ任せる。
	const AssetType assetType = DetermineAssetTypeFromPath(path);
	const auto handlerIterator = handlers_.find(assetType);

	if (handlerIterator != handlers_.end() && handlerIterator->second.getDependencies) {
		return handlerIterator->second.getDependencies(path);
	}

	return {};
}

std::string AssetManager::GetHash(const std::string& path) {
	// File Sizeと更新時刻が変わっていなければHash再計算を省き、Project走査時のI/Oを減らす。
	std::error_code fileError;
	const std::uint64_t fileSize = static_cast<std::uint64_t>(std::filesystem::file_size(path, fileError));

	if (fileError) {
		hashCache_.erase(path);
		return {};
	}

	const std::int64_t lastWriteTime = static_cast<std::int64_t>(
		std::filesystem::last_write_time(path, fileError).time_since_epoch().count());

	if (fileError) {
		hashCache_.erase(path);
		return {};
	}

	const auto cacheIterator = hashCache_.find(path);

	if (cacheIterator != hashCache_.end() &&
		cacheIterator->second.fileSize == fileSize &&
		cacheIterator->second.lastWriteTime == lastWriteTime) {
		return cacheIterator->second.hash;
	}

	// Metadataが変わったFileだけ内容Hashを読み直す。
	const std::string hash = CalculateFileHashChunked(path);

	if (hash.empty()) {
		hashCache_.erase(path);
		return {};
	}

	hashCache_[path] = HashCacheEntry{hash, fileSize, lastWriteTime};
	return hash;
}
