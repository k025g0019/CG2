#pragma once

#include <filesystem>
#include <string>
#include <vector>

enum class EnvironmentCheckMode {
	EditorUser,
	EngineDeveloper,
};

struct EnvironmentCheckItem {
	std::string name;
	bool succeeded = false;
	bool required = true;
	std::string detail;
};

struct EnvironmentCheckReport {
	std::vector<EnvironmentCheckItem> items;
	bool HasRequiredFailure() const;
	std::string ToText() const;
};

class EngineEnvironmentCheck {
public:
	static EnvironmentCheckReport Run(
		const std::filesystem::path& engineDirectory,
		EnvironmentCheckMode mode);
};
