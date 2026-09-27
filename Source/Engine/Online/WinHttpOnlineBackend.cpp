#include "WinHttpOnlineBackend.h"

#pragma warning(push, 0)
#include <Windows.h>
#include <winhttp.h>
#pragma warning(pop)

#include <algorithm>
#include <chrono>

namespace {
	constexpr DWORD kResolveTimeoutMs = 5000;
	constexpr DWORD kReadChunkSize = 8192;

	std::wstring ToWideString(const std::string& text) {
		if (text.empty()) {
			return std::wstring();
		}

		const int requiredLength = MultiByteToWideChar(
			CP_UTF8,
			0,
			text.c_str(),
			static_cast<int>(text.size()),
			nullptr,
			0);

		if (requiredLength <= 0) {
			return std::wstring();
		}

		std::wstring wideText(static_cast<size_t>(requiredLength), L'\0');
		MultiByteToWideChar(
			CP_UTF8,
			0,
			text.c_str(),
			static_cast<int>(text.size()),
			wideText.data(),
			requiredLength);
		return wideText;
	}

	std::string JoinUrl(const std::string& baseUrl, const std::string& endpoint) {
		if (baseUrl.empty()) {
			return endpoint;
		}

		std::string joinedUrl = baseUrl;

		while (!joinedUrl.empty() && joinedUrl.back() == '/') {
			joinedUrl.pop_back();
		}

		if (endpoint.empty()) {
			return joinedUrl;
		}

		if (endpoint.front() != '/') {
			joinedUrl.push_back('/');
		}

		joinedUrl += endpoint;
		return joinedUrl;
	}

	ExternalFeatureError MakeWin32Error(const char* functionName) {
		ExternalFeatureError error{};
		error.code = static_cast<int32_t>(GetLastError());
		error.message = std::string(functionName) + " が失敗しました (Win32 code=" +
			std::to_string(error.code) + ")";
		return error;
	}
}

WinHttpOnlineBackend::~WinHttpOnlineBackend() {
	Shutdown();
}

bool WinHttpOnlineBackend::Initialize(const OnlineConfig& config) {
	{
		const std::lock_guard<std::mutex> lock(mutex_);

		if (isInitialized_) {
			config_ = config;
			return true;
		}

		config_ = config;
		lastError_.Clear();
	}

	HINTERNET sessionHandle = WinHttpOpen(
		L"ManoEngine/1.0",
		WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
		WINHTTP_NO_PROXY_NAME,
		WINHTTP_NO_PROXY_BYPASS,
		0);

	if (sessionHandle == nullptr) {
		const std::lock_guard<std::mutex> lock(mutex_);
		lastError_ = MakeWin32Error("WinHttpOpen");
		return false;
	}

	const int32_t timeoutSeconds = (std::clamp)(config.timeoutSeconds, 1, 60);
	const DWORD timeoutMs = static_cast<DWORD>(timeoutSeconds) * 1000u;
	WinHttpSetTimeouts(
		sessionHandle,
		static_cast<int>(kResolveTimeoutMs),
		static_cast<int>(timeoutMs),
		static_cast<int>(timeoutMs),
		static_cast<int>(timeoutMs));

	{
		const std::lock_guard<std::mutex> lock(mutex_);
		sessionHandle_ = sessionHandle;
		isInitialized_ = true;
		isStopRequested_.store(false);
	}

	workerThread_ = std::thread(&WinHttpOnlineBackend::WorkerMain, this);
	return true;
}

void WinHttpOnlineBackend::Shutdown() {
	bool shouldJoin = false;

	{
		const std::lock_guard<std::mutex> lock(mutex_);

		if (!isInitialized_) {
			return;
		}

		isStopRequested_.store(true);
		shouldJoin = workerThread_.joinable();
	}

	condition_.notify_all();

	if (shouldJoin) {
		workerThread_.join();
	}

	const std::lock_guard<std::mutex> lock(mutex_);

	if (sessionHandle_ != nullptr) {
		WinHttpCloseHandle(static_cast<HINTERNET>(sessionHandle_));
		sessionHandle_ = nullptr;
	}

	requestQueue_.clear();
	responseQueue_.clear();
	inFlightCount_.store(0);
	isInitialized_ = false;
}

void WinHttpOnlineBackend::UpdateConfig(const OnlineConfig& config) {
	const std::lock_guard<std::mutex> lock(mutex_);
	config_ = config;
}

OnlineConfig WinHttpOnlineBackend::CopyConfig() const {
	const std::lock_guard<std::mutex> lock(mutex_);
	return config_;
}

OnlineRequestHandle WinHttpOnlineBackend::Send(const OnlineRequest& request) {
	OnlineRequestHandle handle = kInvalidOnlineRequestHandle;

	{
		const std::lock_guard<std::mutex> lock(mutex_);

		if (!isInitialized_) {
			return kInvalidOnlineRequestHandle;
		}

		handle = nextHandle_;
		nextHandle_ = nextHandle_ + 1u == kInvalidOnlineRequestHandle ? 1u : nextHandle_ + 1u;
		requestQueue_.push_back(QueuedRequest{handle, request});
	}

	inFlightCount_.fetch_add(1);
	condition_.notify_one();
	return handle;
}

bool WinHttpOnlineBackend::TryTakeResponse(
	OnlineRequestHandle& outHandle,
	OnlineResponse& outResponse) {
	const std::lock_guard<std::mutex> lock(mutex_);

	if (responseQueue_.empty()) {
		return false;
	}

	outHandle = responseQueue_.front().handle;
	outResponse = responseQueue_.front().response;
	responseQueue_.erase(responseQueue_.begin());
	return true;
}

int32_t WinHttpOnlineBackend::GetInFlightCount() const {
	return inFlightCount_.load();
}

const char* WinHttpOnlineBackend::GetName() const {
	return "Cloudflare (WinHTTP)";
}

ExternalFeatureError WinHttpOnlineBackend::GetLastError() const {
	const std::lock_guard<std::mutex> lock(mutex_);
	return lastError_;
}

void WinHttpOnlineBackend::WorkerMain() {
	while (true) {
		QueuedRequest queuedRequest{};

		{
			std::unique_lock<std::mutex> lock(mutex_);
			condition_.wait(lock, [this]() {
				return isStopRequested_.load() || !requestQueue_.empty();
			});

			if (isStopRequested_.load() && requestQueue_.empty()) {
				return;
			}

			if (requestQueue_.empty()) {
				continue;
			}

			queuedRequest = requestQueue_.front();
			requestQueue_.erase(requestQueue_.begin());
		}

		OnlineResponse response{};
		const auto startTime = std::chrono::steady_clock::now();
		ExecuteRequest(queuedRequest.request, response);
		const auto endTime = std::chrono::steady_clock::now();
		response.elapsedMilliseconds =
			std::chrono::duration<float, std::milli>(endTime - startTime).count();

		{
			const std::lock_guard<std::mutex> lock(mutex_);

			if (response.error.HasError()) {
				lastError_ = response.error;
			}

			responseQueue_.push_back(QueuedResponse{queuedRequest.handle, response});
		}

		inFlightCount_.fetch_sub(1);
	}
}

bool WinHttpOnlineBackend::ExecuteRequest(const OnlineRequest& request, OnlineResponse& outResponse) {
	const OnlineConfig config = CopyConfig();
	const std::string fullUrl = JoinUrl(config.GetActiveBaseUrl(), request.endpoint);

	if (fullUrl.empty()) {
		outResponse.error.code = -1;
		outResponse.error.message = "API Base URL が未設定です。";
		return false;
	}

	HINTERNET sessionHandle = nullptr;

	{
		const std::lock_guard<std::mutex> lock(mutex_);
		sessionHandle = static_cast<HINTERNET>(sessionHandle_);
	}

	if (sessionHandle == nullptr) {
		outResponse.error.code = -2;
		outResponse.error.message = "WinHTTP Session が未初期化です。";
		return false;
	}

	const std::wstring wideUrl = ToWideString(fullUrl);
	std::wstring hostName(256, L'\0');
	std::wstring urlPath(1024, L'\0');

	URL_COMPONENTS urlComponents{};
	urlComponents.dwStructSize = sizeof(urlComponents);
	urlComponents.lpszHostName = hostName.data();
	urlComponents.dwHostNameLength = static_cast<DWORD>(hostName.size());
	urlComponents.lpszUrlPath = urlPath.data();
	urlComponents.dwUrlPathLength = static_cast<DWORD>(urlPath.size());

	if (!WinHttpCrackUrl(wideUrl.c_str(), static_cast<DWORD>(wideUrl.size()), 0, &urlComponents)) {
		outResponse.error = MakeWin32Error("WinHttpCrackUrl");
		return false;
	}

	hostName.resize(urlComponents.dwHostNameLength);
	urlPath.resize(urlComponents.dwUrlPathLength);

	if (urlPath.empty()) {
		urlPath = L"/";
	}

	const bool isSecure = urlComponents.nScheme == INTERNET_SCHEME_HTTPS;
	const INTERNET_PORT port = urlComponents.nPort;

	HINTERNET connectHandle = WinHttpConnect(sessionHandle, hostName.c_str(), port, 0);

	if (connectHandle == nullptr) {
		outResponse.error = MakeWin32Error("WinHttpConnect");
		return false;
	}

	const std::string methodText = request.method.empty() ? std::string("GET") : request.method;
	const std::wstring wideMethod = ToWideString(methodText);
	const DWORD requestFlags = isSecure ? WINHTTP_FLAG_SECURE : 0u;

	HINTERNET requestHandle = WinHttpOpenRequest(
		connectHandle,
		wideMethod.c_str(),
		urlPath.c_str(),
		nullptr,
		WINHTTP_NO_REFERER,
		WINHTTP_DEFAULT_ACCEPT_TYPES,
		requestFlags);

	if (requestHandle == nullptr) {
		outResponse.error = MakeWin32Error("WinHttpOpenRequest");
		WinHttpCloseHandle(connectHandle);
		return false;
	}

	// 認証に関わる値はクライアント公開鍵だけを送り、秘密情報は Worker 側が持つ(仕様書 49 項)。
	std::string headerText = "Content-Type: application/json\r\n";
	headerText += "X-ManoEngine-Game-Id: " + config.gameId + "\r\n";
	headerText += "X-ManoEngine-Environment: ";
	headerText += config.environment == OnlineEnvironment::Production ? "production" : "development";
	headerText += "\r\n";

	if (!config.clientKey.empty()) {
		headerText += "X-ManoEngine-Client-Key: " + config.clientKey + "\r\n";
	}

	if (!config.playerId.empty()) {
		headerText += "X-ManoEngine-Player-Id: " + config.playerId + "\r\n";
	}

	const std::wstring wideHeaders = ToWideString(headerText);
	const BOOL wasSent = WinHttpSendRequest(
		requestHandle,
		wideHeaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : wideHeaders.c_str(),
		wideHeaders.empty() ? 0u : static_cast<DWORD>(wideHeaders.size()),
		request.body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<void*>(static_cast<const void*>(request.body.data())),
		static_cast<DWORD>(request.body.size()),
		static_cast<DWORD>(request.body.size()),
		0);

	if (wasSent == FALSE) {
		outResponse.error = MakeWin32Error("WinHttpSendRequest");
		WinHttpCloseHandle(requestHandle);
		WinHttpCloseHandle(connectHandle);
		return false;
	}

	if (WinHttpReceiveResponse(requestHandle, nullptr) == FALSE) {
		outResponse.error = MakeWin32Error("WinHttpReceiveResponse");
		WinHttpCloseHandle(requestHandle);
		WinHttpCloseHandle(connectHandle);
		return false;
	}

	DWORD statusCode = 0;
	DWORD statusCodeSize = sizeof(statusCode);
	WinHttpQueryHeaders(
		requestHandle,
		WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX,
		&statusCode,
		&statusCodeSize,
		WINHTTP_NO_HEADER_INDEX);

	std::string responseBody;

	while (true) {
		DWORD availableBytes = 0;

		if (WinHttpQueryDataAvailable(requestHandle, &availableBytes) == FALSE) {
			outResponse.error = MakeWin32Error("WinHttpQueryDataAvailable");
			break;
		}

		if (availableBytes == 0) {
			break;
		}

		const DWORD readSize = (std::min)(availableBytes, kReadChunkSize);
		const size_t previousSize = responseBody.size();
		responseBody.resize(previousSize + readSize);

		DWORD readBytes = 0;

		if (WinHttpReadData(requestHandle, responseBody.data() + previousSize, readSize, &readBytes) == FALSE) {
			outResponse.error = MakeWin32Error("WinHttpReadData");
			responseBody.resize(previousSize);
			break;
		}

		responseBody.resize(previousSize + readBytes);

		if (readBytes == 0) {
			break;
		}
	}

	WinHttpCloseHandle(requestHandle);
	WinHttpCloseHandle(connectHandle);

	outResponse.statusCode = static_cast<int32_t>(statusCode);
	outResponse.body = responseBody;
	outResponse.success = statusCode >= 200u && statusCode < 300u && !outResponse.error.HasError();

	if (!outResponse.success && !outResponse.error.HasError()) {
		outResponse.error.code = outResponse.statusCode;
		outResponse.error.message = "HTTP " + std::to_string(outResponse.statusCode) + " が返りました。";
	}

	return outResponse.success;
}
