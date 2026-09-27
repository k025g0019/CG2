#pragma once

#include <filesystem>
#include <string>

bool DownloadHttpFile(const std::string& url, const std::filesystem::path& destination, std::string& error);
