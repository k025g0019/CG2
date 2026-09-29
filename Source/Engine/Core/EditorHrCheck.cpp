#include "EditorHrCheck.h"

#include <Windows.h>

#include <cstdio>
#include <mutex>
#include <ostream>

//========================================
// 記録用の内部状態
//========================================

namespace {

	//------------------------------
	// 上限と共有状態
	//------------------------------

	// 同じ失敗が毎フレーム起きても記録が無限に増えないようにする上限。
	// 上限に達したあとは件数だけ数え、一覧へは追加しない。
	// Undo スタックを上限化したのと同じ理由で、長時間の Play でメモリが伸び続けるのを防ぐ。
	constexpr std::size_t kMaxRecordedFailures = 256u;

	std::mutex g_hrFailureMutex;                 // 描画スレッドと読み込みスレッドから同時に呼ばれても壊れないようにする
	std::vector<std::string> g_hrFailures;       // 表示用に保持する失敗行。上限で打ち切る
	std::size_t g_hrFailureCount = 0u;           // 打ち切ったあとも増え続ける総件数
	std::ostream* g_hrFailureLogStream = nullptr; // 実行ログ(logs/<日時>.Log)。未登録なら nullptr

	//------------------------------
	// 文字列整形
	//------------------------------

	// HRESULT を 0x80070005 形式へ整形する。
	// FormatMessage は環境の言語設定で読めない場合があるため、機械的に読める 16 進を必ず残す。
	std::string FormatHrValue(EditorHrValue hr) {
		char buffer[16] = {};
		const unsigned long unsignedHr = static_cast<unsigned long>(hr);
		std::snprintf(buffer, sizeof(buffer), "0x%08lX", unsignedHr);
		return std::string(buffer);
	}

	// __FILE__ は絶対パスでログが読みにくいため、ファイル名だけを取り出す。
	const char* ExtractFileName(const char* filePath) noexcept {
		if (filePath == nullptr) return "(unknown)";

		const char* fileName = filePath;
		for (const char* cursor = filePath; *cursor != '\0'; ++cursor) {
			if (*cursor == '\\' || *cursor == '/') fileName = cursor + 1;
		}
		return fileName;
	}

} // namespace

//========================================
// 失敗の検査と記録
//========================================

bool EditorCheckHr(
	EditorHrValue hr, const char* expression, const char* file, int line) noexcept {
	// SUCCEEDED(hr) と同じ判定。この Header を Windows.h へ依存させないため自前で書く。
	if (hr >= 0) return true;

	try {
		//------------------------------
		// 失敗行の組み立て
		//------------------------------

		std::string message = "[HRESULT] ";
		message += FormatHrValue(hr);
		message += "  ";
		message += (expression != nullptr ? expression : "(unknown expression)");
		message += "  at ";
		message += ExtractFileName(file);
		message += ":";
		message += std::to_string(line);

		//------------------------------
		// 一覧と実行ログへの記録
		//------------------------------

		{
			std::lock_guard<std::mutex> lock(g_hrFailureMutex);
			g_hrFailureCount++;

			if (g_hrFailures.size() < kMaxRecordedFailures) {
				g_hrFailures.push_back(message);
			}
			else if (g_hrFailures.size() == kMaxRecordedFailures) {
				// 打ち切ったことが表示側から分かるよう、目印を 1 行だけ残す。
				g_hrFailures.push_back("[HRESULT] 記録上限に達した。以降は件数のみ数える。");
			}

			if (g_hrFailureLogStream != nullptr) {
				*g_hrFailureLogStream << message << std::endl;
			}
		}

		//------------------------------
		// 出力ウィンドウへの送出
		//------------------------------

		message += "\n";
		OutputDebugStringA(message.c_str()); // Release 構成でも出力ウィンドウには必ず出す
	}
	catch (...) {
		// 記録処理そのものが例外を投げて呼び出し元を壊すことは避ける。
		OutputDebugStringA("[HRESULT] 失敗の記録中に例外が発生した。\n");
	}

	return false;
}

//========================================
// 記録の参照
//========================================

const std::vector<std::string>& EditorGetHrFailureLog() noexcept {
	// 参照を返すため、呼び出し側は描画スレッドと同じスレッドから読むこと。
	// Diagnostics Window は Editor のメインスレッドから呼ぶので問題ない。
	return g_hrFailures;
}

std::size_t EditorGetHrFailureCount() noexcept {
	std::lock_guard<std::mutex> lock(g_hrFailureMutex);
	return g_hrFailureCount;
}

//========================================
// 記録の設定と解除
//========================================

void EditorClearHrFailureLog() noexcept {
	std::lock_guard<std::mutex> lock(g_hrFailureMutex);
	g_hrFailures.clear();
	g_hrFailureCount = 0u;
}

void EditorSetHrFailureLogStream(std::ostream* logStream) noexcept {
	// Stream を閉じる前に必ず nullptr で解除する。寿命が逆転すると解放済みへ書き込む。
	std::lock_guard<std::mutex> lock(g_hrFailureMutex);
	g_hrFailureLogStream = logStream;
}
