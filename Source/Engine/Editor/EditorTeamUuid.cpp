#include "EditorTeamUuid.h"

#pragma warning(push, 0)
#include <Windows.h>
#include <objbase.h>
#pragma warning(pop)

#include <array>
#include <cstdio>

#pragma comment(lib, "ole32.lib")

std::string CreateEditorTeamUuid() {
	GUID uuid{};

	if (FAILED(CoCreateGuid(&uuid))) {
		return {};
	}

	std::array<char, 37> uuidText{};
	const int32_t writtenLength = std::snprintf(
		uuidText.data(),
		uuidText.size(),
		"%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX",
		uuid.Data1,
		uuid.Data2,
		uuid.Data3,
		uuid.Data4[0],
		uuid.Data4[1],
		uuid.Data4[2],
		uuid.Data4[3],
		uuid.Data4[4],
		uuid.Data4[5],
		uuid.Data4[6],
		uuid.Data4[7]);

	return writtenLength == 36 ? std::string(uuidText.data()) : std::string{};
}

bool IsEditorTeamUuidValid(const std::string& uuid) {
	return uuid.size() == 36u &&
		uuid[8] == '-' &&
		uuid[13] == '-' &&
		uuid[18] == '-' &&
		uuid[23] == '-';
}
