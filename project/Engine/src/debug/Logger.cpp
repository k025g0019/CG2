#include "debug/Logger.h"

#include "base/CrashHandler.h"

namespace Logger {
	void Log(const std::string& message) {
		OutputDebugStringA(message.c_str());
	}
}
