#pragma once

#include <iosfwd>
#include <string>

void Log(const std::string& message);
void Log(std::ostream& os, const std::string& message);
