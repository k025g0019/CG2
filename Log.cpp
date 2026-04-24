#include "Log.h"

#include <Windows.h>
#include <ostream>

void Log(const std::string& message) {
	OutputDebugStringA(message.c_str());
}

void Log(std::ostream& os, const std::string& message) {
	os << message << std::endl;
	Log(message + "\n");
}
