#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class EditorScene;

#pragma warning(push)
#pragma warning(disable : 4820)

enum class EditorTeamConnectionStatus : int32_t {
	Offline = 0,
	Connecting,
	Online,
	Synchronizing,
	Conflict,
	Disconnected,
};

struct EditorTeamChangeEvent {
	std::string changeId;
	std::string userId;
	std::string userName;
	std::string scenePath;
	std::string sceneUuid;
	std::string objectUuid;
	std::string componentUuid;
	std::string operation;
	std::string property;
	std::string oldValue;
	std::string newValue;
	std::string snapshotData;  // Scene全体を安全に反映するためのPayload。競合表示用の値とは分離する
	std::string assetHash;
	std::uint64_t fileSize = 0u;
	std::uint64_t timestampUnixMilliseconds = 0u;
	std::uint64_t baseRevision = 0u;
	std::uint64_t revision = 0u;
};

class EditorTeamCollaborationManager {
public:
	EditorTeamCollaborationManager();
	~EditorTeamCollaborationManager();
	EditorTeamCollaborationManager(const EditorTeamCollaborationManager&) = delete;
	EditorTeamCollaborationManager& operator=(const EditorTeamCollaborationManager&) = delete;

	void Initialize(EditorScene* editorScene, std::vector<std::string>* consoleMessages);
	void Finalize();
	void Update(float deltaTime, bool isPlaying);
	void Draw(bool* isWindowVisible);

	bool StartServer();
	void StopServer();
	bool ConnectToHost();
	void Disconnect();
	EditorTeamConnectionStatus GetStatus() const;
	bool IsGameObjectLockedByAnotherUser(int32_t gameObjectId) const;

private:
	struct NetworkState;
	struct AssetRecord {
		std::string uuid;
		std::string hash;
		std::string textContent;
		std::uint64_t fileSize = 0u;
		std::int64_t lastWriteTimestamp = 0;
	};
	struct MemberRecord {
		std::string userName;
		float idleSeconds = 0.0f;
		bool isOnline = false;
	};

	EditorScene* editorScene_ = nullptr;
	std::vector<std::string>* consoleMessages_ = nullptr;
	std::unique_ptr<NetworkState> networkState_;
	std::vector<EditorTeamChangeEvent> unsyncedChanges_;
	std::vector<EditorTeamChangeEvent> conflicts_;
	std::unordered_map<std::string, std::uint64_t> lastPropertyRevision_;
	std::unordered_map<std::string, std::string> lockedByUserId_;
	std::unordered_map<std::string, std::string> lockedByUserName_;
	std::unordered_map<std::string, float> remoteLockIdleSeconds_;
	std::unordered_map<std::string, AssetRecord> assetRecords_;
	std::unordered_map<std::string, MemberRecord> memberRecords_;
	std::array<char, 64> userNameBuffer_{};
	std::array<char, 64> hostAddressBuffer_{};
	std::string userId_;
	std::string lastSnapshotText_;
	std::string lastError_;
	std::string selectedObjectUuid_;
	std::uint64_t currentRevision_ = 0u;
	std::uint64_t lastSnapshotHash_ = 0u;
	float snapshotElapsedSeconds_ = 0.0f;
	float assetScanElapsedSeconds_ = 0.0f;
	float lockHeartbeatElapsedSeconds_ = 0.0f;
	float presenceElapsedSeconds_ = 0.0f;
	uint16_t port_ = 45678u;
	int32_t previousMemberCount_ = 0;
	bool isInitialized_ = false;
	bool isHost_ = false;
	bool startsServerAutomatically_ = false;
	bool wasConnected_ = false;
	bool isApplyingRemoteChange_ = false;

	void LoadSettings();
	void SaveSettings() const;
	void LoadChangeLog();
	void AppendChangeLog(const EditorTeamChangeEvent& changeEvent) const;
	void CaptureSceneChanges(bool forcesBroadcast);
	void ScanAssetChanges(bool recordsBaselineOnly);
	void QueueAssetChange(
		const std::string& assetPath,
		const AssetRecord* previousRecord,
		const AssetRecord* currentRecord);
	void QueueLocalChange(EditorTeamChangeEvent changeEvent);
	void ProcessIncomingMessages();
	void ProcessRemoteChange(EditorTeamChangeEvent changeEvent, bool isCommitted);
	void PrepareConflictCopies(const EditorTeamChangeEvent& changeEvent);
	void UpdateMemberPresence(float deltaTime);
	void ProcessPresenceMessage(const std::string& message);
	void UpdateSelectionLock(float deltaTime);
	void UpdateLockTimeouts(float deltaTime);
	void ProcessLockMessage(const std::string& message, const std::string& messageType);
	void BroadcastLockState(const std::string& objectUuid);
	bool ApplySceneSnapshot(const EditorTeamChangeEvent& changeEvent);
	bool ApplyAssetChange(const EditorTeamChangeEvent& changeEvent);
	bool MergeScriptConflict(const EditorTeamChangeEvent& changeEvent);
	void QueueOutgoingMessage(const std::string& message);
	void StartNetworkThread(bool startsAsServer);
	void StopNetworkThread();
	void AddConsoleMessage(const std::string& message) const;
};

bool IsEditorTeamGameObjectLockedByAnotherUser(int32_t gameObjectId);

#pragma warning(pop)
