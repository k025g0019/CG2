#include "EditorHrCheck.h"

#include <Windows.h>

#include <cstdio>
#include <mutex>
#include <ostream>

namespace {
	// 同じ失敗が毎フレーム起きても記録が無限に増えないよう上限を設ける。
	// 上限に達したあとは件数だけ数え、一覧へは追加しない。
	// (Undo スタックを上限化したのと同じ理由で、長時間の Play でメモリが
	//  伸び続けることを防ぐ)
	constexpr std::size_t kMaxRecordedFailures = 256u;

	std::mutex g_hrFailureMutex; // 描画スレッドと読み込みスレッドから同時に呼ばれても壊れないようにする。
	std::vector<std::string> g_hrFailures;
	std::size_t g_hrFailureCount = 0u; // 上限で打ち切ったあとも増える総件数。
	std::ostream* g_hrFailureLogStream = nullptr;

	// HRESULT を 0x80070005 形式の文字列にする。FormatMessage は環境の言語設定に
	// 依存して読めない場合があるため、機械的に読める 16 進を必ず残す。
	std::string FormatHrValue(EditorHrValue hr) {
		char buffer[16] = {};
		const unsigned long unsignedHr = static_cast<unsigned long>(hr);
		std::snprintf(buffer, sizeof(buffer), "0x%08lX", unsignedHr);
		return std::string(buffer);
	}

	// ファイル名だけを取り出す。__FILE__ は絶対パスなのでログが読みにくくなる。
	const char* ExtractFileName(const char* filePath) noexcept {
		if (filePath == nullptr) return "(unknown)";
		const char* fileName = filePath;
		for (const char* cursor = filePath; *cursor != '\0'; ++cursor) {
			if (*cursor == '\\' || *cursor == '/') fileName = cursor + 1;
		}
		return fileName;
	}
} // namespace

bool EditorCheckHr(
	EditorHrValue hr, const char* expression, const char* file, int line) noexcept {
	// SUCCEEDED(hr) と同じ判定。Windows.h のマクロに依存させないため自前で書く。
	if (hr >= 0) return true;

	try {
		std::string message = "[HRESULT] ";
		message += FormatHrValue(hr);
		message += "  ";
		message += (expression != nullptr ? expression : "(unknown expression)");
		message += "  at ";
		message += ExtractFileName(file);
		message += ":";
		message += std::to_string(line);

		{
			std::lock_guard<std::mutex> lock(g_hrFailureMutex);
			g_hrFailureCount++;
			if (g_hrFailures.size() < kMaxRecordedFailures) {
				g_hrFailures.push_back(message);
			}
			else if (g_hrFailures.size() == kMaxRecordedFailures) {
				g_hrFailures.push_back("[HRESULT] 記録上限に達した。以降は件数のみ数える。");
			}
			if (g_hrFailureLogStream != nullptr) {
				*g_hrFailureLogStream << message << std::endl;
			}
		}

		message += "\n";
		OutputDebugStringA(message.c_str()); // Release でも出力ウィンドウには必ず出す。
	}
	catch (...) {
		// 失敗の記録自体で例外を投げて呼び出し元を壊すことは避ける。
		OutputDebugStringA("[HRESULT] 失敗の記録中に例外が発生した。\n");
	}

	return false;
}

const std::vector<std::string>& EditorGetHrFailureLog() noexcept {
	// 参照を返すので、呼び出し側は描画スレッドと同じスレッドから読むこと。
	// Diagnostics Window は Editor のメインスレッドから呼ぶため問題ない。
	return g_hrFailures;
}

std::size_t EditorGetHrFailureCount() noexcept {
	std::lock_guard<std::mutex> lock(g_hrFailureMutex);
	return g_hrFailureCount;
}

void EditorClearHrFailureLog() noexcept {
	std::lock_guard<std::mutex> lock(g_hrFailureMutex);
	g_hrFailures.clear();
	g_hrFailureCount = 0u;
}

void EditorSetHrFailureLogStream(std::ostream* logStream) noexcept {
	std::lock_guard<std::mutex> lock(g_hrFailureMutex);
	g_hrFailureLogStream = logStream;
}
