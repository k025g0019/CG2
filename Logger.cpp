#include "Logger.h"

#include "CrashHandler.h"

namespace Logger {
	void Log(const std::string& message) {
		OutputDebugStringA(message.c_str());
	}
}
