#include "WhisperSpeechBackend.h"

#pragma warning(push, 0)
#include <Windows.h>
#include <mmsystem.h>
#pragma warning(pop)

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#pragma comment(lib, "winmm.lib")

namespace {
	constexpr uint32_t kSampleRate = 16000u;
	constexpr uint16_t kChannelCount = 1u;
	constexpr uint16_t kBitsPerSample = 16u;
	// 1024 Sample = 64 ms。無音終了を細かく検出し、従来の256 ms粒度を避ける。
	constexpr size_t kCaptureBufferSampleCount = 1024u;
	constexpr size_t kCaptureBufferCount = 4u;
	constexpr float kMinimumCaptureSeconds = 0.5f;
	constexpr float kMaximumCaptureSeconds = 30.0f;
	constexpr float kVoicePreRollSeconds = 0.25f;
	// Inspector の音量は視認性のため RMS を 4 倍して表示している。
	// 以前の 0.008 では表示上の音量が動いていても、小さめの発話を 4 秒単位で
	// 無言のまま破棄していたため、デジタル無音に近い入力だけを除外する。
	constexpr float kSilenceRootMeanSquare = 0.001f;

	std::wstring ToWideString(const std::string& text) {
		if (text.empty()) {
			return std::wstring();
		}

		const int requiredLength = MultiByteToWideChar(
			CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);

		if (requiredLength <= 0) {
			return std::wstring();
		}

		std::wstring wideText(static_cast<size_t>(requiredLength), L'\0');
		MultiByteToWideChar(
			CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), wideText.data(), requiredLength);
		return wideText;
	}

	std::string ToUtf8String(const wchar_t* wideText) {
		if (wideText == nullptr || wideText[0] == L'\0') {
			return std::string();
		}

		const int requiredLength = WideCharToMultiByte(
			CP_UTF8, 0, wideText, -1, nullptr, 0, nullptr, nullptr);

		if (requiredLength <= 1) {
			return std::string();
		}

		std::string utf8Text(static_cast<size_t>(requiredLength - 1), '\0');
		WideCharToMultiByte(
			CP_UTF8, 0, wideText, -1, utf8Text.data(), requiredLength, nullptr, nullptr);
		return utf8Text;
	}

	std::filesystem::path GetExecutableDirectory() {
		std::array<wchar_t, MAX_PATH> executablePath{};
		const DWORD length = GetModuleFileNameW(
			nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));

		if (length == 0u || length >= executablePath.size()) {
			return std::filesystem::current_path();
		}

		return std::filesystem::path(executablePath.data()).parent_path();
	}

	std::filesystem::path ResolveExistingPath(const std::string& configuredPath) {
		if (configuredPath.empty()) {
			return std::filesystem::path();
		}

		const std::filesystem::path sourcePath = ToWideString(configuredPath);
		std::error_code error;

		if (sourcePath.is_absolute() && std::filesystem::is_regular_file(sourcePath, error)) {
			return sourcePath;
		}

		const std::array<std::filesystem::path, 2> candidates = {
			std::filesystem::current_path() / sourcePath,
			GetExecutableDirectory() / sourcePath,
		};

		for (const std::filesystem::path& candidate : candidates) {
			error.clear();

			if (std::filesystem::is_regular_file(candidate, error)) {
				return std::filesystem::absolute(candidate, error);
			}
		}

		return std::filesystem::path();
	}

	std::filesystem::path FindWhisperExecutable() {
		const std::filesystem::path executableDirectory = GetExecutableDirectory();
		const std::filesystem::path workingDirectory = std::filesystem::current_path();
		const std::array<std::filesystem::path, 6> candidates = {
			executableDirectory / L"whisper-cli.exe",
			executableDirectory / L"Tools" / L"Whisper" / L"whisper-cli.exe",
			workingDirectory / L"Tools" / L"Whisper" / L"whisper-cli.exe",
			workingDirectory / L"ThirdParty" / L"whisper.cpp" / L"build" / L"bin" / L"Release" / L"whisper-cli.exe",
			workingDirectory / L"ThirdParty" / L"whisper.cpp" / L"build" / L"bin" / L"whisper-cli.exe",
			workingDirectory / L"whisper-cli.exe",
		};

		for (const std::filesystem::path& candidate : candidates) {
			std::error_code error;

			if (std::filesystem::is_regular_file(candidate, error)) {
				return std::filesystem::absolute(candidate, error);
			}
		}

		std::array<wchar_t, MAX_PATH> foundPath{};
		const DWORD length = SearchPathW(
			nullptr,
			L"whisper-cli.exe",
			nullptr,
			static_cast<DWORD>(foundPath.size()),
			foundPath.data(),
			nullptr);
		return length > 0u && length < foundPath.size()
			? std::filesystem::path(foundPath.data())
			: std::filesystem::path();
	}

	std::wstring QuoteCommandArgument(const std::wstring& argument) {
		std::wstring quoted = L"\"";
		size_t backslashCount = 0u;

		for (const wchar_t character : argument) {
			if (character == L'\\') {
				++backslashCount;
				continue;
			}

			if (character == L'\"') {
				quoted.append(backslashCount * 2u + 1u, L'\\');
				quoted.push_back(character);
				backslashCount = 0u;
				continue;
			}

			quoted.append(backslashCount, L'\\');
			backslashCount = 0u;
			quoted.push_back(character);
		}

		quoted.append(backslashCount * 2u, L'\\');
		quoted.push_back(L'\"');
		return quoted;
	}

	std::string NormalizeLanguage(const std::string& language) {
		if (language.empty()) {
			return "auto";
		}

		const size_t separatorIndex = language.find_first_of("-_");
		std::string normalized = language.substr(0u, separatorIndex);

		for (char& character : normalized) {
			if (character >= 'A' && character <= 'Z') {
				character = static_cast<char>(character - 'A' + 'a');
			}
		}

		return normalized.empty() ? std::string("auto") : normalized;
	}

	void WriteUint16(std::ofstream& stream, uint16_t value) {
		stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
	}

	void WriteUint32(std::ofstream& stream, uint32_t value) {
		stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
	}

	bool WriteWaveFile(const std::filesystem::path& path, const std::vector<int16_t>& samples) {
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);

		if (!stream.is_open()) {
			return false;
		}

		const uint32_t dataSize = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
		stream.write("RIFF", 4);
		WriteUint32(stream, 36u + dataSize);
		stream.write("WAVE", 4);
		stream.write("fmt ", 4);
		WriteUint32(stream, 16u);
		WriteUint16(stream, WAVE_FORMAT_PCM);
		WriteUint16(stream, kChannelCount);
		WriteUint32(stream, kSampleRate);
		WriteUint32(stream, kSampleRate * kChannelCount * (kBitsPerSample / 8u));
		WriteUint16(stream, kChannelCount * (kBitsPerSample / 8u));
		WriteUint16(stream, kBitsPerSample);
		stream.write("data", 4);
		WriteUint32(stream, dataSize);

		if (!samples.empty()) {
			stream.write(
				reinterpret_cast<const char*>(samples.data()),
				static_cast<std::streamsize>(dataSize));
		}

		return stream.good();
	}

	std::string ReadTextFile(const std::filesystem::path& path) {
		std::ifstream stream(path, std::ios::binary);

		if (!stream.is_open()) {
			return std::string();
		}

		std::string text(
			(std::istreambuf_iterator<char>(stream)),
			std::istreambuf_iterator<char>());

		if (text.size() >= 3u &&
			static_cast<unsigned char>(text[0]) == 0xEFu &&
			static_cast<unsigned char>(text[1]) == 0xBBu &&
			static_cast<unsigned char>(text[2]) == 0xBFu) {
			text.erase(0u, 3u);
		}

		while (!text.empty() &&
			(text.back() == '\r' || text.back() == '\n' || text.back() == ' ' || text.back() == '\t')) {
			text.pop_back();
		}

		const size_t firstText = text.find_first_not_of("\r\n \t");
		return firstText == std::string::npos ? std::string() : text.substr(firstText);
	}

	float CalculateRootMeanSquare(const std::vector<int16_t>& samples) {
		if (samples.empty()) {
			return 0.0f;
		}

		double squareSum = 0.0;

		for (const int16_t sample : samples) {
			const double normalized = static_cast<double>(sample) / 32768.0;
			squareSum += normalized * normalized;
		}

		return static_cast<float>(std::sqrt(squareSum / static_cast<double>(samples.size())));
	}

	size_t SecondsToSampleCount(float seconds) {
		return static_cast<size_t>(static_cast<float>(kSampleRate) * seconds);
	}
}

struct WhisperSpeechBackend::Impl {
	struct TranscriptionJob {
		std::vector<int16_t> samples;
		std::string language;
		std::filesystem::path modelPath;
	};

	SpeechConfig config{};
	std::filesystem::path whisperExecutablePath;
	std::filesystem::path modelPath;
	std::filesystem::path temporaryDirectory;
	std::string activeDeviceName;
	mutable std::mutex mutex;
	std::condition_variable workerCondition;
	std::thread workerThread;
	std::vector<int16_t> capturedSamples;
	std::vector<TranscriptionJob> jobs;
	std::vector<SpeechResult> results;
	ExternalFeatureError lastError{};
	std::atomic<float> audioLevel{0.0f};
	std::atomic<bool> isRecognizing{false};
	std::atomic<bool> isProcessing{false};
	size_t trailingSilenceSamples = 0u;
	bool hasDetectedVoice = false;
	bool utteranceReady = false;
	bool stopWorker = false;
	bool isInitialized = false;
	uint64_t transcriptionIndex = 0u;
	HWAVEIN waveInput = nullptr;
	std::array<std::array<int16_t, kCaptureBufferSampleCount>, kCaptureBufferCount> captureBuffers{};
	std::array<WAVEHDR, kCaptureBufferCount> waveHeaders{};

	void SetError(int32_t code, const std::string& message) {
		std::scoped_lock lock(mutex);
		lastError.code = code;
		lastError.message = message;
	}

	void OnWaveData(WAVEHDR* header) {
		if (header == nullptr) {
			return;
		}

		if (isRecognizing.load()) {
			const size_t sampleCount = header->dwBytesRecorded / sizeof(int16_t);
			const auto* samples = reinterpret_cast<const int16_t*>(header->lpData);
			double squareSum = 0.0;
			for (size_t sampleIndex = 0u; sampleIndex < sampleCount; ++sampleIndex) {
				const double normalized = static_cast<double>(samples[sampleIndex]) / 32768.0;
				squareSum += normalized * normalized;
			}

			const float level = sampleCount == 0u
				? 0.0f
				: static_cast<float>(std::sqrt(squareSum / static_cast<double>(sampleCount)));
			const float displayLevel = (std::min)(level * 4.0f, 1.0f);

			{
				std::scoped_lock lock(mutex);
				const float maximumSeconds = (std::clamp)(
					config.whisperMaximumCaptureSeconds,
					kMinimumCaptureSeconds,
					kMaximumCaptureSeconds);
				const size_t maximumSampleCount = SecondsToSampleCount(maximumSeconds);

				if (!config.whisperEndOnSilence) {
					capturedSamples.insert(capturedSamples.end(), samples, samples + sampleCount);
					// 推論が録音速度に追いつかない場合もMemoryを増やし続けない。
					const size_t maximumBufferedSamples = maximumSampleCount * 3u;
					if (capturedSamples.size() > maximumBufferedSamples) {
						const size_t discardCount = capturedSamples.size() - maximumBufferedSamples;
						capturedSamples.erase(
							capturedSamples.begin(),
							capturedSamples.begin() + static_cast<std::ptrdiff_t>(discardCount));
					}
				}
				else if (!utteranceReady) {
					const float voiceThreshold = (std::clamp)(config.whisperVoiceThreshold, 0.001f, 1.0f);
					const bool containsVoice = displayLevel >= voiceThreshold;
					capturedSamples.insert(capturedSamples.end(), samples, samples + sampleCount);

					if (!hasDetectedVoice) {
						if (containsVoice) {
							hasDetectedVoice = true;
							trailingSilenceSamples = 0u;
						}
						else {
							// 発話直前を失わない範囲だけ残し、待機中の無音を貯め続けない。
							const size_t preRollSampleCount = SecondsToSampleCount(kVoicePreRollSeconds);
							if (capturedSamples.size() > preRollSampleCount) {
								capturedSamples.erase(
									capturedSamples.begin(),
									capturedSamples.end() - static_cast<std::ptrdiff_t>(preRollSampleCount));
							}
						}
					}
					else if (containsVoice) {
						trailingSilenceSamples = 0u;
					}
					else {
						trailingSilenceSamples += sampleCount;
						const float silenceSeconds = (std::clamp)(config.whisperSilenceSeconds, 0.1f, 3.0f);
						if (trailingSilenceSamples >= SecondsToSampleCount(silenceSeconds)) {
							utteranceReady = true;
						}
					}

					if (hasDetectedVoice && capturedSamples.size() >= maximumSampleCount) {
						utteranceReady = true;
					}
				}
			}
			audioLevel.store(displayLevel);

			if (waveInput != nullptr) {
				header->dwBytesRecorded = 0u;
				waveInAddBuffer(waveInput, header, sizeof(WAVEHDR));
			}
		}
	}

	static void CALLBACK WaveInputCallback(
		HWAVEIN waveInputHandle,
		UINT message,
		DWORD_PTR instance,
		DWORD_PTR parameter1,
		DWORD_PTR parameter2) {
		static_cast<void>(waveInputHandle);
		static_cast<void>(parameter2);

		if (message == WIM_DATA && instance != 0u) {
			auto* impl = reinterpret_cast<Impl*>(instance);
			impl->OnWaveData(reinterpret_cast<WAVEHDR*>(parameter1));
		}
	}

	bool OpenCaptureDevice() {
		activeDeviceName.clear();
		WAVEFORMATEX format{};
		format.wFormatTag = WAVE_FORMAT_PCM;
		format.nChannels = kChannelCount;
		format.nSamplesPerSec = kSampleRate;
		format.wBitsPerSample = kBitsPerSample;
		format.nBlockAlign = kChannelCount * (kBitsPerSample / 8u);
		format.nAvgBytesPerSec = kSampleRate * format.nBlockAlign;

		UINT deviceId = WAVE_MAPPER;

		if (!config.microphoneDeviceName.empty()) {
			const UINT deviceCount = waveInGetNumDevs();

			for (UINT candidateId = 0u; candidateId < deviceCount; ++candidateId) {
				WAVEINCAPSW capabilities{};

				if (waveInGetDevCapsW(candidateId, &capabilities, sizeof(capabilities)) == MMSYSERR_NOERROR) {
					const std::string candidateName = ToUtf8String(capabilities.szPname);

					if (candidateName == config.microphoneDeviceName ||
						candidateName.find(config.microphoneDeviceName) != std::string::npos) {
						deviceId = candidateId;
						activeDeviceName = candidateName;
						break;
					}
				}
			}
		}

		const MMRESULT openResult = waveInOpen(
			&waveInput,
			deviceId,
			&format,
			reinterpret_cast<DWORD_PTR>(&WaveInputCallback),
			reinterpret_cast<DWORD_PTR>(this),
			CALLBACK_FUNCTION);

		if (openResult != MMSYSERR_NOERROR) {
			waveInput = nullptr;
			SetError(static_cast<int32_t>(openResult), "Whisper: マイクを 16 kHz PCM で開けませんでした。");
			return false;
		}

		if (activeDeviceName.empty()) {
			WAVEINCAPSW capabilities{};

			if (waveInGetDevCapsW(deviceId, &capabilities, sizeof(capabilities)) == MMSYSERR_NOERROR) {
				activeDeviceName = ToUtf8String(capabilities.szPname);
			}
			else {
				activeDeviceName = "既定のマイク";
			}
		}

		for (size_t bufferIndex = 0u; bufferIndex < captureBuffers.size(); ++bufferIndex) {
			WAVEHDR& header = waveHeaders[bufferIndex];
			header = WAVEHDR{};
			header.lpData = reinterpret_cast<LPSTR>(captureBuffers[bufferIndex].data());
			header.dwBufferLength = static_cast<DWORD>(captureBuffers[bufferIndex].size() * sizeof(int16_t));

			if (waveInPrepareHeader(waveInput, &header, sizeof(header)) != MMSYSERR_NOERROR ||
				waveInAddBuffer(waveInput, &header, sizeof(header)) != MMSYSERR_NOERROR) {
				SetError(-204, "Whisper: マイク収録 Buffer の準備に失敗しました。");
				CloseCaptureDevice();
				return false;
			}
		}

		return true;
	}

	void CloseCaptureDevice() {
		isRecognizing.store(false);
		{
			std::scoped_lock lock(mutex);
			capturedSamples.clear();
			trailingSilenceSamples = 0u;
			hasDetectedVoice = false;
			utteranceReady = false;
		}

		if (waveInput == nullptr) {
			return;
		}

		waveInStop(waveInput);
		waveInReset(waveInput);

		for (WAVEHDR& header : waveHeaders) {
			if ((header.dwFlags & WHDR_PREPARED) != 0u) {
				waveInUnprepareHeader(waveInput, &header, sizeof(header));
			}
		}

		waveInClose(waveInput);
		waveInput = nullptr;
		audioLevel.store(0.0f);
	}

	bool RunWhisper(const TranscriptionJob& job, SpeechResult& outResult) {
		const uint64_t jobIndex = transcriptionIndex++;
		const std::filesystem::path basePath =
			temporaryDirectory / (L"chunk-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(jobIndex));
		const std::filesystem::path wavePath = basePath.wstring() + L".wav";
		const std::filesystem::path textPath = basePath.wstring() + L".txt";

		if (!WriteWaveFile(wavePath, job.samples)) {
			SetError(-205, "Whisper: 一時 WAV を作成できませんでした。");
			return false;
		}

		std::wstring commandLine = QuoteCommandArgument(whisperExecutablePath.wstring());
		commandLine += L" -m " + QuoteCommandArgument(job.modelPath.wstring());
		commandLine += L" -f " + QuoteCommandArgument(wavePath.wstring());
		commandLine += L" -l " + QuoteCommandArgument(ToWideString(job.language));
		commandLine += L" -otxt -of " + QuoteCommandArgument(basePath.wstring());

		std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
		mutableCommandLine.push_back(L'\0');
		STARTUPINFOW startupInfo{};
		startupInfo.cb = sizeof(startupInfo);
		PROCESS_INFORMATION processInfo{};
		const BOOL created = CreateProcessW(
			nullptr,
			mutableCommandLine.data(),
			nullptr,
			nullptr,
			FALSE,
			CREATE_NO_WINDOW,
			nullptr,
			nullptr,
			&startupInfo,
			&processInfo);

		if (created == FALSE) {
			std::error_code ignored;
			std::filesystem::remove(wavePath, ignored);
			SetError(static_cast<int32_t>(::GetLastError()), "Whisper: whisper-cli.exe を起動できませんでした。");
			return false;
		}

		WaitForSingleObject(processInfo.hProcess, INFINITE);
		DWORD exitCode = 0u;
		GetExitCodeProcess(processInfo.hProcess, &exitCode);
		CloseHandle(processInfo.hThread);
		CloseHandle(processInfo.hProcess);

		std::error_code ignored;
		const std::string text = ReadTextFile(textPath);
		const bool hasTextOutput = std::filesystem::is_regular_file(textPath, ignored);
		std::filesystem::remove(wavePath, ignored);
		std::filesystem::remove(textPath, ignored);

		if (exitCode != 0u) {
			if (exitCode == 0xC0000135u) {
				SetError(
					static_cast<int32_t>(exitCode),
					"Whisper: 実行に必要な DLL が見つかりません。whisper.dll と ggml*.dll を whisper-cli.exe と同じ Folder へ配置してください。");
			}
			else {
				SetError(static_cast<int32_t>(exitCode), "Whisper: whisper-cli.exe の推論に失敗しました。");
			}
			return false;
		}

		if (!hasTextOutput) {
			SetError(
				-206,
				"Whisper: 推論は終了しましたが文字起こし結果ファイルが作成されませんでした。");
			return false;
		}

		if (text.empty()) {
			SetError(
				-207,
				"Whisper: 音声を処理しましたが文字列を認識できませんでした。マイク音量またはモデルを確認してください。");
			return false;
		}

		outResult.text = text;
		// CLI は文全体の確率を返さないため、空でない確定結果は通過値 1.0 とする。
		outResult.confidence = 1.0f;
		outResult.isFinal = true;
		outResult.language = job.language;
		outResult.backendName = "Whisper (whisper.cpp)";

		{
			std::scoped_lock lock(mutex);
			lastError.Clear();
		}

		return true;
	}

	void WorkerMain() {
		for (;;) {
			TranscriptionJob job{};

			{
				std::unique_lock lock(mutex);
				workerCondition.wait(lock, [this]() { return stopWorker || !jobs.empty(); });

				if (stopWorker && jobs.empty()) {
					return;
				}

				job = std::move(jobs.front());
				jobs.erase(jobs.begin());
			}

			isProcessing.store(true);
			SpeechResult result{};

			if (RunWhisper(job, result)) {
				ExternalFeatureLog::Info(
					ExternalFeatureCategory::Speech,
					"Whisper: 文字起こしが完了しました。");
				std::scoped_lock lock(mutex);
				results.push_back(std::move(result));
			}

			isProcessing.store(false);
		}
	}
};

WhisperSpeechBackend::WhisperSpeechBackend()
	: impl_(std::make_unique<Impl>()) {
}

WhisperSpeechBackend::~WhisperSpeechBackend() {
	Shutdown();
}

bool WhisperSpeechBackend::Initialize() {
	if (impl_->isInitialized) {
		return true;
	}

	impl_->whisperExecutablePath = FindWhisperExecutable();

	if (impl_->whisperExecutablePath.empty()) {
		impl_->SetError(
			-200,
			"Whisper: whisper-cli.exe が見つかりません。Tools/Whisper または Engine 実行ファイルの隣へ配置してください。");
		return false;
	}

	impl_->modelPath = ResolveExistingPath(impl_->config.modelAssetPath);

	if (impl_->modelPath.empty()) {
		impl_->SetError(-201, "Whisper: Inspector のモデル欄に存在する whisper.cpp 用モデルを指定してください。");
		return false;
	}

	std::array<wchar_t, MAX_PATH> temporaryRoot{};
	const DWORD temporaryLength = GetTempPathW(
		static_cast<DWORD>(temporaryRoot.size()), temporaryRoot.data());

	if (temporaryLength == 0u || temporaryLength >= temporaryRoot.size()) {
		impl_->SetError(-202, "Whisper: 一時 Folder を取得できませんでした。");
		return false;
	}

	impl_->temporaryDirectory = std::filesystem::path(temporaryRoot.data()) / L"ManoEngineWhisper";
	std::error_code directoryError;
	std::filesystem::create_directories(impl_->temporaryDirectory, directoryError);

	if (directoryError) {
		impl_->SetError(-203, "Whisper: 一時 Folder を作成できませんでした。");
		return false;
	}

	{
		std::scoped_lock lock(impl_->mutex);
		impl_->lastError.Clear();
		impl_->stopWorker = false;
	}

	impl_->workerThread = std::thread([this]() { impl_->WorkerMain(); });
	impl_->isInitialized = true;
	return true;
}

void WhisperSpeechBackend::Shutdown() {
	StopRecognition();

	{
		std::scoped_lock lock(impl_->mutex);
		impl_->stopWorker = true;
		impl_->jobs.clear();
	}

	impl_->workerCondition.notify_all();

	if (impl_->workerThread.joinable()) {
		impl_->workerThread.join();
	}

	{
		std::scoped_lock lock(impl_->mutex);
		impl_->capturedSamples.clear();
		impl_->results.clear();
	}

	impl_->isInitialized = false;
}

void WhisperSpeechBackend::StartRecognition() {
	if (!impl_->isInitialized || impl_->isRecognizing.load()) {
		return;
	}

	if (!impl_->OpenCaptureDevice()) {
		return;
	}

	impl_->isRecognizing.store(true);
	const MMRESULT startResult = waveInStart(impl_->waveInput);

	if (startResult != MMSYSERR_NOERROR) {
		impl_->SetError(static_cast<int32_t>(startResult), "Whisper: マイク収録を開始できませんでした。");
		impl_->CloseCaptureDevice();
	}
}

void WhisperSpeechBackend::StopRecognition() {
	impl_->CloseCaptureDevice();
}

void WhisperSpeechBackend::Update() {
	if (!impl_->isRecognizing.load()) {
		return;
	}

	Impl::TranscriptionJob job{};

	{
		std::scoped_lock lock(impl_->mutex);
		if (impl_->jobs.size() >= 2u) {
			return;
		}

		const float maximumSeconds = (std::clamp)(
			impl_->config.whisperMaximumCaptureSeconds,
			kMinimumCaptureSeconds,
			kMaximumCaptureSeconds);
		const size_t maximumSampleCount = SecondsToSampleCount(maximumSeconds);
		if (impl_->config.whisperEndOnSilence) {
			if (!impl_->utteranceReady || !impl_->hasDetectedVoice) return;
			const size_t jobSampleCount = (std::min)(impl_->capturedSamples.size(), maximumSampleCount);
			job.samples.assign(
				impl_->capturedSamples.begin(),
				impl_->capturedSamples.begin() + static_cast<std::ptrdiff_t>(jobSampleCount));
			impl_->capturedSamples.clear();
			impl_->trailingSilenceSamples = 0u;
			impl_->hasDetectedVoice = false;
			impl_->utteranceReady = false;
		}
		else {
			if (impl_->capturedSamples.size() < maximumSampleCount) return;
			job.samples.assign(
				impl_->capturedSamples.begin(),
				impl_->capturedSamples.begin() + static_cast<std::ptrdiff_t>(maximumSampleCount));
			impl_->capturedSamples.erase(
				impl_->capturedSamples.begin(),
				impl_->capturedSamples.begin() + static_cast<std::ptrdiff_t>(maximumSampleCount));
		}
	}

	// 無音区間を CLI へ渡すと幻聴テキストが出やすいため、音量だけで除外する。
	if (CalculateRootMeanSquare(job.samples) < kSilenceRootMeanSquare) {
		return;
	}

	const float jobSeconds = static_cast<float>(job.samples.size()) / static_cast<float>(kSampleRate);

	{
		std::scoped_lock lock(impl_->mutex);
		job.language = NormalizeLanguage(impl_->config.language);
		job.modelPath = impl_->modelPath;
		impl_->jobs.push_back(std::move(job));
	}

	ExternalFeatureLog::Info(
		ExternalFeatureCategory::Speech,
		"Whisper: " + std::to_string(jobSeconds) + " 秒分の音声を推論しています。");
	impl_->workerCondition.notify_one();
}

std::vector<SpeechResult> WhisperSpeechBackend::GetResults() {
	std::scoped_lock lock(impl_->mutex);
	std::vector<SpeechResult> results = std::move(impl_->results);
	impl_->results.clear();
	return results;
}

bool WhisperSpeechBackend::ApplyConfig(const SpeechConfig& config) {
	bool modelChanged = false;
	{
		std::scoped_lock lock(impl_->mutex);
		modelChanged = impl_->isInitialized && config.modelAssetPath != impl_->config.modelAssetPath;
	}

	std::filesystem::path resolvedModelPath;
	if (modelChanged) {
		resolvedModelPath = ResolveExistingPath(config.modelAssetPath);

		if (resolvedModelPath.empty()) {
			impl_->SetError(-201, "Whisper: 指定されたモデルが見つかりません。");
			return false;
		}
	}

	{
		std::scoped_lock lock(impl_->mutex);
		if (modelChanged) impl_->modelPath = resolvedModelPath;
		impl_->config = config;
		impl_->config.whisperMaximumCaptureSeconds = (std::clamp)(
			config.whisperMaximumCaptureSeconds,
			kMinimumCaptureSeconds,
			kMaximumCaptureSeconds);
		impl_->config.whisperSilenceSeconds = (std::clamp)(config.whisperSilenceSeconds, 0.1f, 3.0f);
		impl_->config.whisperVoiceThreshold = (std::clamp)(config.whisperVoiceThreshold, 0.001f, 1.0f);
	}

	return true;
}

bool WhisperSpeechBackend::IsRecognizing() const {
	return impl_->isRecognizing.load();
}

bool WhisperSpeechBackend::IsSpeaking() const {
	if (!impl_->isRecognizing.load()) {
		return false;
	}

	std::scoped_lock lock(impl_->mutex);
	if (impl_->config.whisperEndOnSilence) {
		return impl_->hasDetectedVoice && !impl_->utteranceReady;
	}

	return impl_->audioLevel.load() >= impl_->config.whisperVoiceThreshold;
}

bool WhisperSpeechBackend::IsProcessing() const {
	if (impl_->isProcessing.load()) {
		return true;
	}

	std::scoped_lock lock(impl_->mutex);
	return !impl_->jobs.empty();
}

float WhisperSpeechBackend::GetAudioLevel() const {
	return impl_->audioLevel.load();
}

void WhisperSpeechBackend::EnumerateDevices(std::vector<SpeechDeviceInfo>& outDevices) const {
	outDevices.clear();
	const UINT deviceCount = waveInGetNumDevs();

	for (UINT deviceId = 0u; deviceId < deviceCount; ++deviceId) {
		WAVEINCAPSW capabilities{};

		if (waveInGetDevCapsW(deviceId, &capabilities, sizeof(capabilities)) != MMSYSERR_NOERROR) {
			continue;
		}

		SpeechDeviceInfo device{};
		device.deviceName = ToUtf8String(capabilities.szPname);
		device.deviceId = std::to_string(deviceId);
		device.isDefault = deviceId == 0u;
		outDevices.push_back(std::move(device));
	}
}

std::string WhisperSpeechBackend::GetActiveDeviceName() const {
	return impl_->activeDeviceName;
}

const char* WhisperSpeechBackend::GetName() const {
	return "Whisper (whisper.cpp)";
}

ExternalFeatureError WhisperSpeechBackend::GetLastError() const {
	std::scoped_lock lock(impl_->mutex);
	return impl_->lastError;
}
