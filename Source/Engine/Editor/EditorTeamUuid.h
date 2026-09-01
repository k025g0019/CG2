#pragma once

#include <string>

//================================================================
// 共同制作対象の永続 UUID
//================================================================

std::string CreateEditorTeamUuid();
bool IsEditorTeamUuidValid(const std::string& uuid);
