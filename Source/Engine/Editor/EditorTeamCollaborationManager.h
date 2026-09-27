#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Source/Engine/Collaboration/ICollaborationTransport.h"

class EditorScene;

#pragma warning(push)
#pragma warning(disable : 4820)

enum class EditorTeamConnectionStatus : int32_t {
	Offline = 0,
	Connecting,
	Online,
	Synchronizing,
	Conflict,
	Incompatible,
	Disconnected,
	// 一度確立した接続が切れ、再接続中。遠隔ではLANより頻繁に起こるため独立した状態にする。
	Reconnecting,
};

enum class EditorTeamLockMode : int32_t {
	Soft = 0,
	Hard,
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

// Editor各画面はTeamItem本体を所有せず、対象ごとの集計だけを参照する。
// これにより付箋UIをHierarchyやInspectorへ統合しても同期データの管理先は一つに保てる。
struct EditorTeamItemTargetSummary {
	int32_t noteCount = 0;
	int32_t pingCount = 0;
	int32_t chatCount = 0;
	int32_t reviewCount = 0;
	int32_t unresolvedCount = 0;

	int32_t GetTotalCount() const {
		return noteCount + pingCount + chatCount + reviewCount;
	}
};

struct EditorTeamSceneMarker {
	std::string targetType;
	std::string targetId;
	std::string creatorName;
	std::array<float, 3> worldPosition{};
	EditorTeamItemTargetSummary summary;
	std::uint32_t color = 0u;
	float pingAnimationSeconds = -1.0f;
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
	bool TestServerConnection();
	bool StartDedicatedServer();
	bool StopDedicatedServer();
	bool RestartDedicatedServer();
	bool IsDedicatedServerRunning() const;
	EditorTeamConnectionStatus GetStatus() const;
	bool IsGameObjectLockedByAnotherUser(int32_t gameObjectId) const;
	bool IsComponentLockedByAnotherUser(int32_t gameObjectId, const std::string& componentUuid) const;
	bool IsTargetHardLockedByAnotherUser(const std::string& targetType, const std::string& targetId) const;
	std::string GetGameObjectEditorLabel(int32_t gameObjectId) const;
	void RequestEditingLock(int32_t gameObjectId, const std::string& componentUuid);
	void RequestTargetLock(
		const std::string& targetType,
		const std::string& targetId,
		EditorTeamLockMode mode);
	void ReleaseTargetLock(const std::string& targetType, const std::string& targetId);
	void ReportActivity(
		const std::string& panel,
		const std::string& action,
		const std::string& componentUuid,
		const std::string& propertyName);
	void SetBuildActivity(bool isBuilding);
	EditorTeamItemTargetSummary GetTargetItemSummary(
		const std::string& targetType,
		const std::string& targetId) const;
	std::vector<EditorTeamSceneMarker> GetSceneMarkers() const;
	void OpenTargetItems(
		const std::string& targetType,
		const std::string& targetId,
		bool startsCreation,
		const std::string& initialKind = "Note");
	void OpenScenePositionItems(
		const std::array<float, 3>& worldPosition,
		bool startsCreation,
		const std::string& initialKind = "Note");

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
		std::string panel;
		std::string action;
		std::string scenePath;
		std::string assetPath;
		std::string objectUuid;
		std::string componentUuid;
		std::string propertyName;
		float cursorX = 0.0f;
		float cursorY = 0.0f;
		std::array<float, 3> cameraPosition{};
		std::array<float, 3> cameraRotation{};
		std::uint32_t color = 0u;
		float idleSeconds = 0.0f;
		bool isOnline = false;
		bool isPlaying = false;
		bool isBuilding = false;
	};
	struct TeamItem {
		std::string id;
		std::string kind;
		std::string targetType;
		std::string targetId;
		std::string scenePath;
		std::string componentUuid;
		std::string propertyName;
		std::string text;
		std::string parentId;
		std::string assigneeUserId;
		std::string mentionedUserId;
		std::string creatorUserId;
		std::string creatorUserName;
		std::uint64_t timestampUnixMilliseconds = 0u;
		std::uint64_t updatedTimestampUnixMilliseconds = 0u;
		std::string updatedByUserId;
		std::string updatedByUserName;
		std::array<float, 3> worldPosition{};
		std::string codeContext;
		std::string functionName;
		std::string targetChangeId;
		std::uint64_t targetRevision = 0u;
		std::uint64_t expiresAtUnixMilliseconds = 0u;
		int32_t scriptLine = 0;
		int32_t reviewStatus = 0;
		std::uint64_t revision = 0u;
		bool hasWorldPosition = false;
		bool keepsPingHistory = true;
		bool isResolved = false;
	};
	struct TeamItemConflict {
		TeamItem localItem;
		TeamItem remoteItem;
		std::string remoteUserName;
	};
	struct DeferredPlayModeChange {
		EditorTeamChangeEvent changeEvent;
	};
	// 分割送信中のAssetやSceneを受信側で組み立てるための一時状態。
	// assetBegin で確保し、assetChunk で追記し、assetEnd で完成した EditorTeamChangeEvent へ変換する。
	struct IncomingAssetTransfer {
		EditorTeamChangeEvent metadataEvent;
		std::string commitType;
		std::string accumulatedTransportPayload;
		std::uint64_t expectedTransportPayloadBytes = 0u;
		std::uint64_t expectedRawBytes = 0u;
		std::uint32_t expectedChunkCount = 0u;
		std::uint32_t receivedChunkCount = 0u;
	};

	EditorScene* editorScene_ = nullptr;
	std::vector<std::string>* consoleMessages_ = nullptr;
	std::unique_ptr<NetworkState> networkState_;
	std::vector<EditorTeamChangeEvent> unsyncedChanges_;
	std::vector<EditorTeamChangeEvent> conflicts_;
	std::unordered_map<std::string, std::uint64_t> lastPropertyRevision_;
	std::unordered_map<std::string, std::string> lockedByUserId_;
	std::unordered_map<std::string, std::string> lockedByUserName_;
	std::unordered_map<std::string, EditorTeamLockMode> lockModes_;
	std::unordered_map<std::string, std::string> lockTargetTypes_;
	std::unordered_map<std::string, float> remoteLockIdleSeconds_;
	std::unordered_map<std::string, AssetRecord> assetRecords_;
	std::vector<std::string> missingReferencedAssetPaths_;
	std::unordered_map<std::string, MemberRecord> memberRecords_;
	// Editor Hostは複数Clientを同時に受けるため、認証済み状態をClientごとに保持する。
	// 1個のboolだけでは3台目の参加・拒否が既存参加者まで巻き込んでしまう。
	std::unordered_set<std::string> acceptedPeerUserIds_;
	std::vector<std::string> pendingJoinCatchUpUserIds_;
	std::unordered_map<std::string, TeamItem> teamItems_;
	std::vector<TeamItemConflict> teamItemConflicts_;
	std::vector<std::string> notificationItemIds_;
	std::unordered_map<std::string, std::string> lastChangedByObjectUuid_;
	std::vector<EditorTeamChangeEvent> recentChanges_;
	// Play中に届いた他ユーザーのScene/Asset変更。通信とRevision確定は継続し、
	// この端末のPlay終了後に編集Sceneへ順番どおり適用する。
	std::vector<DeferredPlayModeChange> deferredPlayModeChanges_;
	std::unordered_map<std::string, IncomingAssetTransfer> pendingIncomingTransfers_;
	std::unordered_map<std::string, EditorTeamChangeEvent> conflictLocalChanges_;
	std::string manualConflictId_;
	std::vector<char> manualConflictTextBuffer_;
	std::string incomingTransferLabel_;
	std::uint64_t incomingTransferReceivedBytes_ = 0u;
	std::uint64_t incomingTransferTotalBytes_ = 0u;
	std::array<char, 64> userNameBuffer_{};
	std::array<char, 128> hostAddressBuffer_{};  // IPだけでなくMagicDNS hostnameも入るため長めに取る
	std::array<char, 64> projectIdBuffer_{};
	std::array<char, 260> sharedSceneFolderBuffer_{};
	std::array<char, 2048> collaborationTextBuffer_{};
	std::array<char, 128> collaborationSearchBuffer_{};
	std::array<char, 128> historyUserFilterBuffer_{};
	std::array<char, 260> historyTargetFilterBuffer_{};
	std::array<char, 128> checkpointNameBuffer_{};
	std::array<char, 2048> teamItemEditTextBuffer_{};
	std::array<char, 260> scriptTargetPathBuffer_{};
	std::array<char, 256> codeContextBuffer_{};
	std::array<char, 128> functionNameBuffer_{};
	std::string userId_;
	std::string activityPanel_;
	std::string activityAction_;
	std::string activityComponentUuid_;
	std::string activityPropertyName_;
	std::string selectedTeamItemId_;
	std::string selectedHistoryTargetType_;
	std::string selectedHistoryTargetId_;
	std::string replyToTeamItemId_;
	std::string editingTeamItemId_;
	std::string manualTeamItemConflictId_;
	std::string selectedAssigneeUserId_;
	std::string followUserId_;
	std::string focusedTeamItemTargetType_;
	std::string focusedTeamItemTargetId_;
	std::array<float, 3> pendingTeamItemWorldPosition_{};
	std::string lastSnapshotText_;
	std::string lastError_;
	std::string selectedObjectUuid_;
	int32_t requestedEditingGameObjectId_ = -1;
	std::string requestedEditingComponentUuid_;
	std::uint64_t currentRevision_ = 0u;
	std::uint64_t lastSyncedRevision_ = 0u;
	std::uint64_t serverRevision_ = 0u;
	std::uint64_t lastSnapshotHash_ = 0u;
	float snapshotElapsedSeconds_ = 0.0f;
	float assetScanElapsedSeconds_ = 0.0f;
	float lockHeartbeatElapsedSeconds_ = 0.0f;
	float presenceElapsedSeconds_ = 0.0f;
	float activityIdleSeconds_ = 0.0f;
	uint16_t port_ = 45678u;
	int32_t previousMemberCount_ = 0;
	int32_t maximumClientCount_ = 4;  // 小規模チーム想定の既定値。Team設定から変更できる
	float heartbeatElapsedSeconds_ = 0.0f;
	float heartbeatSilenceSeconds_ = 0.0f;  // 相手からHeartbeatが来ていない時間
	float latencyMilliseconds_ = 0.0f;  // 診断用。必須機能はこの値へ依存させない
	std::uint64_t heartbeatSentUnixMilliseconds_ = 0u;
	std::uint64_t lastSyncUnixMilliseconds_ = 0u;
	bool handshakeAccepted_ = false;
	bool autoConnect_ = false;
	bool isInitialized_ = false;
	bool isHost_ = false;
	bool startsServerAutomatically_ = false;
	bool wasConnected_ = false;
	bool isApplyingRemoteChange_ = false;
	bool compatibilityHelloSent_ = false;
	bool compatibilityAccepted_ = false;
	bool historySyncInProgress_ = false;
	bool isPlaying_ = false;
	bool isBuilding_ = false;
	bool hasPendingTeamItemWorldPosition_ = false;
	int32_t collaborationKindIndex_ = 0;
	int32_t collaborationTargetIndex_ = 0;
	int32_t historyTypeFilterIndex_ = 0;
	int32_t historyTimeFilterIndex_ = 0;
	int32_t collaborationKindFilterIndex_ = 0;
	int32_t reviewStatusFilterIndex_ = 0;
	int32_t scriptTargetLine_ = 0;
	std::uint32_t dedicatedServerProcessId_ = 0u;
	bool dedicatedServerAutoStartAtLogon_ = false;

	void LoadSettings();
	void SaveSettings() const;
	bool SetDedicatedServerAutoStartAtLogon(bool enabled);
	bool IsDedicatedServerAutoStartAtLogonEnabled() const;
	void LoadChangeLog();
	void AppendChangeLog(const EditorTeamChangeEvent& changeEvent) const;
	void CaptureSceneChanges(bool forcesBroadcast);
	void ScanAssetChanges(bool recordsBaselineOnly);
	void QueueAssetChange(
		const std::string& assetPath,
		const AssetRecord* previousRecord,
		const AssetRecord* currentRecord);
	void QueueLocalChange(EditorTeamChangeEvent changeEvent);
	void QueueChangeEventMessage(const EditorTeamChangeEvent& changeEvent, const char* messageType);
	void QueueChangeEventMessage(
		const EditorTeamChangeEvent& changeEvent,
		const char* messageType,
		const std::string& targetUserId);
	void QueueCurrentSceneSnapshotForUser(const std::string& targetUserId);
	void ProcessIncomingMessages();
	void ProcessRemoteChange(EditorTeamChangeEvent changeEvent, bool isCommitted);
	void ApplyDeferredPlayModeChanges();
	bool IsTeamItemChange(const EditorTeamChangeEvent& changeEvent) const;
	void ApplyTeamItemChange(const EditorTeamChangeEvent& changeEvent);
	void QueueTeamItem(TeamItem item);
	void QueueTeamItemDelete(const TeamItem& item);
	std::string SerializeTeamItem(const TeamItem& item) const;
	TeamItem DeserializeTeamItem(const EditorTeamChangeEvent& changeEvent) const;
	void JumpToTarget(const std::string& targetType, const std::string& targetId);
	std::string BuildTargetLockKey(const std::string& targetType, const std::string& targetId) const;
	void PrepareConflictCopies(const EditorTeamChangeEvent& changeEvent);
	void UpdateMemberPresence(float deltaTime);
	void ProcessPresenceMessage(const std::string& message);
	void SendCompatibilityHello();
	void ProcessCompatibilityMessage(const std::string& message, bool isAcceptance);
	void UpdateSelectionLock(float deltaTime);
	void UpdateLockTimeouts(float deltaTime);
	void ProcessLockMessage(const std::string& message, const std::string& messageType);
	void BroadcastLockState(const std::string& objectUuid);
	bool ApplySceneSnapshot(const EditorTeamChangeEvent& changeEvent);
	bool ApplyAssetChange(const EditorTeamChangeEvent& changeEvent);
	bool MergeScriptConflict(const EditorTeamChangeEvent& changeEvent);
	void QueueOutgoingMessage(
		const std::string& message,
		bool isTransferChunk = false,
		std::uint64_t transferRawByteShare = 0u);
	void StartNetworkThread(bool startsAsServer);
	void StopNetworkThread();
	// TransportのStateを既存の接続状態へ写す。Socketのエラー番号は上位へ出さない。
	void SynchronizeTransportStatus();
	// Protocol / Engine / Project の互換性と Project ID をまとめて確認するHandshake。
	void SendHandshake();
	void ProcessHandshakeMessage(const std::string& message);
	void ProcessHandshakeResult(const std::string& message, bool isAccepted);
	// 遠隔Clientが無言で消えてもLockが残らないようにする。
	void UpdateHeartbeat(float deltaTime);
	void ProcessHeartbeatMessage(const std::string& message, bool isAcknowledgement);
	void RequestMissingHistory(std::uint64_t serverRevision);
	void CompleteHistorySynchronization(std::uint64_t serverRevision);
	void FlushUnsyncedChanges();
	void CaptureBaseState();
	void ReleaseLocksOwnedBy(const std::string& userId);
	void AddConsoleMessage(const std::string& message) const;
};

bool IsEditorTeamGameObjectLockedByAnotherUser(int32_t gameObjectId);
bool IsEditorTeamComponentLockedByAnotherUser(int32_t gameObjectId, const std::string& componentUuid);
bool IsEditorTeamTargetHardLockedByAnotherUser(const std::string& targetType, const std::string& targetId);
std::string GetEditorTeamGameObjectEditorLabel(int32_t gameObjectId);
void RequestEditorTeamEditingLock(int32_t gameObjectId, const std::string& componentUuid = {});
void RequestEditorTeamTargetLock(
	const std::string& targetType,
	const std::string& targetId,
	EditorTeamLockMode mode = EditorTeamLockMode::Hard);
void ReleaseEditorTeamTargetLock(const std::string& targetType, const std::string& targetId);
void ReportEditorTeamActivity(
	const std::string& panel,
	const std::string& action,
	const std::string& componentUuid = {},
	const std::string& propertyName = {});
void SetEditorTeamBuildActivity(bool isBuilding);
EditorTeamItemTargetSummary GetEditorTeamTargetItemSummary(
	const std::string& targetType,
	const std::string& targetId);
std::vector<EditorTeamSceneMarker> GetEditorTeamSceneMarkers();
void OpenEditorTeamTargetItems(
	const std::string& targetType,
	const std::string& targetId,
	bool startsCreation = false,
	const std::string& initialKind = "Note");
void OpenEditorTeamScenePositionItems(
	const std::array<float, 3>& worldPosition,
	bool startsCreation = true,
	const std::string& initialKind = "Note");

#pragma warning(pop)
