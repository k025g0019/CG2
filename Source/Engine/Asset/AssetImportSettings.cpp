#include "AssetImportSettings.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace {
	constexpr const char* kImportSettingsPath = "ProjectSettings/AssetImportSettings.txt";
	constexpr size_t kColumnCount = 16u;

	// 保存フォーマットの区切り文字'|'とテキストが衝突しないよう、保存直前に取り除く。
	std::string SanitizeForLine(const std::string& text) {
		std::string sanitized = text;
		for (char& character : sanitized) {
			if (character == '|' || character == '\n' || character == '\r') {
				character = ' ';
			}
		}
		return sanitized;
	}

	std::vector<std::string> SplitLine(const std::string& line) {
		std::vector<std::string> elements;
		std::string current;
		for (const char character : line) {
			if (character == '|') {
				elements.push_back(current);
				current.clear();
			}
			else {
				current.push_back(character);
			}
		}
		elements.push_back(current);
		return elements;
	}

	int32_t ToInt(const std::string& text, int32_t fallbackValue = 0) {
		try {
			return text.empty() ? fallbackValue : std::stoi(text);
		}
		catch (...) {
			return fallbackValue;
		}
	}

	float ToFloat(const std::string& text, float fallbackValue = 0.0f) {
		try {
			return text.empty() ? fallbackValue : std::stof(text);
		}
		catch (...) {
			return fallbackValue;
		}
	}

	bool ToBool(const std::string& text, bool fallbackValue = false) {
		return text.empty() ? fallbackValue : text != "0";
	}
}

int32_t GetCurrentImporterVersion(AssetType assetType) {
	switch (assetType) {
	case AssetType::Model: return kCurrentModelImporterVersion;
	case AssetType::Texture: return kCurrentTextureImporterVersion;
	case AssetType::Audio: return kCurrentAudioImporterVersion;
	case AssetType::Animation: return kCurrentAnimationImporterVersion;
	default: return 0;
	}
}

AssetImportSettingsStore& AssetImportSettingsStore::Get() {
	static AssetImportSettingsStore instance;
	return instance;
}

void AssetImportSettingsStore::Load() {
	if (isLoaded_) {
		return;
	}

	isLoaded_ = true;
	std::ifstream file(kImportSettingsPath, std::ios::binary);

	if (!file.is_open()) {
		return;
	}

	std::string line;
	while (std::getline(file, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}

		if (line.empty()) {
			continue;
		}

		const std::vector<std::string> elements = SplitLine(line);
		if (elements.size() < kColumnCount || elements[0].empty()) {
			continue;
		}

		AssetImportMetadata metadata{};
		metadata.assetId = elements[0];
		metadata.assetType = static_cast<AssetType>(ToInt(elements[1]));
		metadata.importerVersion = ToInt(elements[2]);
		metadata.sourceHash = elements[3];
		metadata.state = static_cast<AssetImportState>(ToInt(elements[4]));
		metadata.lastErrorReason = elements[5];
		metadata.model.importScale = ToFloat(elements[6], 1.0f);
		metadata.model.generateNormals = ToBool(elements[7]);
		metadata.model.flipUVs = ToBool(elements[8]);
		metadata.model.importAnimation = ToBool(elements[9], true);
		metadata.texture.srgb = ToBool(elements[10], true);
		metadata.texture.isNormalMap = ToBool(elements[11]);
		metadata.texture.generateMipmaps = ToBool(elements[12], true);
		metadata.texture.maxSize = ToInt(elements[13]);
		metadata.audio.preloadOnPlayStart = ToBool(elements[14]);
		metadata.animation.reserved = ToInt(elements[15]);

		entries_[metadata.assetId] = std::move(metadata);
	}
}

void AssetImportSettingsStore::Save() const {
	const std::filesystem::path filePath(kImportSettingsPath);
	std::error_code directoryError;
	std::filesystem::create_directories(filePath.parent_path(), directoryError);

	std::ofstream file(filePath, std::ios::binary | std::ios::trunc);

	if (!file.is_open()) {
		return;
	}

	for (const auto& entryPair : entries_) {
		const AssetImportMetadata& metadata = entryPair.second;
		file << metadata.assetId
			<< "|" << static_cast<int32_t>(metadata.assetType)
			<< "|" << metadata.importerVersion
			<< "|" << SanitizeForLine(metadata.sourceHash)
			<< "|" << static_cast<int32_t>(metadata.state)
			<< "|" << SanitizeForLine(metadata.lastErrorReason)
			<< "|" << metadata.model.importScale
			<< "|" << (metadata.model.generateNormals ? 1 : 0)
			<< "|" << (metadata.model.flipUVs ? 1 : 0)
			<< "|" << (metadata.model.importAnimation ? 1 : 0)
			<< "|" << (metadata.texture.srgb ? 1 : 0)
			<< "|" << (metadata.texture.isNormalMap ? 1 : 0)
			<< "|" << (metadata.texture.generateMipmaps ? 1 : 0)
			<< "|" << metadata.texture.maxSize
			<< "|" << (metadata.audio.preloadOnPlayStart ? 1 : 0)
			<< "|" << metadata.animation.reserved
			<< "\r\n";
	}
}

AssetImportMetadata* AssetImportSettingsStore::GetOrCreate(const AssetId& assetId, AssetType assetType) {
	if (assetId.empty()) {
		return nullptr;
	}

	Load();

	const auto entryIterator = entries_.find(assetId);
	if (entryIterator != entries_.end()) {
		// Importer仕様が変わっていたら、既存設定は残しつつ次回使用時に気付けるようにする。
		if (entryIterator->second.importerVersion != GetCurrentImporterVersion(assetType) &&
			entryIterator->second.state != AssetImportState::Failed &&
			entryIterator->second.state != AssetImportState::MissingSource) {
			entryIterator->second.state = AssetImportState::NeedsReimport;
		}

		return &entryIterator->second;
	}

	AssetImportMetadata metadata{};
	metadata.assetId = assetId;
	metadata.assetType = assetType;
	metadata.importerVersion = GetCurrentImporterVersion(assetType);
	metadata.state = AssetImportState::Imported;

	return &entries_.emplace(assetId, std::move(metadata)).first->second;
}

const AssetImportMetadata* AssetImportSettingsStore::Find(const AssetId& assetId) const {
	const auto entryIterator = entries_.find(assetId);
	return entryIterator != entries_.end() ? &entryIterator->second : nullptr;
}

void AssetImportSettingsStore::MarkImported(const AssetId& assetId, const std::string& sourceHash) {
	const auto entryIterator = entries_.find(assetId);
	if (entryIterator == entries_.end()) {
		return;
	}

	entryIterator->second.state = AssetImportState::Imported;
	entryIterator->second.sourceHash = sourceHash;
	entryIterator->second.lastErrorReason.clear();
	entryIterator->second.importerVersion = GetCurrentImporterVersion(entryIterator->second.assetType);
	Save();
}

void AssetImportSettingsStore::MarkFailed(const AssetId& assetId, const std::string& reason) {
	const auto entryIterator = entries_.find(assetId);
	if (entryIterator == entries_.end()) {
		return;
	}

	entryIterator->second.state = AssetImportState::Failed;
	entryIterator->second.lastErrorReason = reason;
	Save();
}

void AssetImportSettingsStore::MarkMissingSource(const AssetId& assetId) {
	const auto entryIterator = entries_.find(assetId);
	if (entryIterator == entries_.end()) {
		return;
	}

	entryIterator->second.state = AssetImportState::MissingSource;
	entryIterator->second.lastErrorReason = "Source Fileが見つかりません";
	Save();
}

void AssetImportSettingsStore::MarkNeedsReimport(const AssetId& assetId) {
	const auto entryIterator = entries_.find(assetId);
	if (entryIterator == entries_.end()) {
		return;
	}

	entryIterator->second.state = AssetImportState::NeedsReimport;
	Save();
}

void AssetImportSettingsStore::ResetToDefault(const AssetId& assetId, AssetType assetType) {
	AssetImportMetadata metadata{};
	metadata.assetId = assetId;
	metadata.assetType = assetType;
	metadata.importerVersion = GetCurrentImporterVersion(assetType);
	metadata.state = AssetImportState::NeedsReimport;
	entries_[assetId] = std::move(metadata);
	Save();
}
