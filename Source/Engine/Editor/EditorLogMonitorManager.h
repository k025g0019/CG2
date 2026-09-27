#pragma once

// 汎用ログ・監視System(Runtime Inspector + Watch + Logger)。
// GameObject / Component / System のいずれの対象も、ユーザーがUI(EditorLogMonitorWindowManager)で
// 選択したWatch Entryに従って評価し、Logs/RuntimeLog.log(pipe区切り、機械可読)へ記録する。
//
// 対象は3種類:
//   GameObjectField: EditorGameObjectの固定Field(id/name/isActive/translate/rotate/scale)。
//                     登録不要、常に全GameObjectへ列挙できる。
//   ComponentField:   EditorLogFieldRegistry.generated.h 経由。Inspector描画コードから
//                     自動生成されたFieldのみ選べる。Config上は(componentType, fieldKey)という
//                     安定した文字列識別子で保存し、配列indexそのものは保存しない
//                     (Registry再生成で並びが変わっても過去のWatch設定が壊れないようにするため)。
//   SystemField:      EditorLogSystemProviders.h 経由。Manager/Global State由来。
//
// 性能・安全に関する設計方針:
//   - GameObject解決は EditorScene::FindGameObject の O(1) Hash Map引きを使う(Scene全体を
//     毎Frame走査する設計にはしない)。Component側はEntryごとにRegistry index / Component slot
//     indexをcacheし、型が一致する限り再探索しない。
//   - 対象が見つからない場合はdangling参照を作らず、Missing状態への遷移/復帰のときだけ
//     1回だけログに記録する(毎Frame大量に記録しない)。
//   - Buffer上限・1Frameあたり最大件数・File Size上限を超えた場合は黙って落とさず、
//     Drop件数をログへ記録する。

#include "EditorLogFieldRegistry.generated.h"
#include "EditorLogSystemProviders.h"
#include "EditorScene.h"

#include <cstdint>
#include <string>
#include <vector>

class EditorProfilerManager;
class EditorWeaponManager;
class EditorRuntimePropertyManager;

enum class LogWatchTargetKind : int32_t {
	GameObjectField,
	ComponentField,
	SystemField,
};

enum class LogCaptureMode : int32_t {
	EveryFrame,
	IntervalSeconds,
	OnChange,
	// 以下、ABI互換のため既存値を変えず末尾へ追加する(Config保存値と順序が対応するため)。
	IntervalFrames,  // 指定Frame数ごとに記録する(秒指定より軽量・決定的にしたい場合用)
	Manual,          // Update()内では自動発火しない。CaptureNow()呼び出し時だけ記録する
};

// 値を型付きで保持するSnapshot。OnChangeのThreshold判定とLog出力の両方で使う。
struct LogFieldSnapshot {
	bool hasValue = false;
	bool isMissing = false;  // true = 対象が解決できなかった(GameObject/Component/Fieldが見つからない)
	LogFieldValueKind kind = LogFieldValueKind::Float;
	float floatValue = 0.0f;
	int32_t intValue = 0;
	bool boolValue = false;
	Vector3 vector3Value{};
	std::string displayValue;  // Logへそのまま書き出す文字列表現
};

struct LogWatchEntry {
	LogWatchTargetKind targetKind = LogWatchTargetKind::ComponentField;
	bool enabled = true;
	std::string category;  // UI表示・カテゴリ一括ON/OFF用

	// GameObjectField / ComponentField 共通
	int32_t gameObjectId = -1;

	// ComponentField用。(componentType, componentFieldKey)がConfig保存対象の安定識別子。
	// componentFieldKeyはEditorLogFieldRegistry.generated.cppのLogComponentFieldDescriptor::fieldKeyと対応する
	// (実体はC++のMember名。Inspector描画コードのcomponent.フィールド参照から自動生成される)。
	EditorComponentType componentType = EditorComponentType::Transform;
	std::string componentFieldKey;

	// GameObjectField用。"id" / "name" / "isActive" / "translate" / "rotate" / "scale" のいずれか
	std::string gameObjectFieldName;

	// Vector3型のField(translate/rotate/scale、またはComponentFieldのVector3種別)を
	// 成分展開して見たい場合に使う。-1=Vector3全体、0/1/2=X/Y/Z成分単体。
	int32_t vectorComponent = -1;

	// SystemField用。EditorLogSystemProvidersの{category, name}と対応させる
	std::string systemFieldName;

	// 既定は毎Frame書き込みで負荷を作らないよう「変化時のみ」にする。必要な対象だけ
	// 個別に毎Frame/間隔指定へ切り替える。
	LogCaptureMode captureMode = LogCaptureMode::OnChange;
	float intervalSeconds = 1.0f;
	int32_t intervalFrames = 30;

	// OnChange用。Float/Vector3成分ではこのThreshold以上変化した時だけ記録する
	// (abs(current-previous) >= threshold、Vector3全体はVector距離で判定)。
	// Bool/Int/GameObjectReferenceは常に完全一致比較(Thresholdは無視される)。
	float changeThreshold = 0.0f;

	// ---- 実行時状態。Config保存対象外 ----
	float elapsedSinceLastCapture = 0.0f;
	uint64_t elapsedFramesSinceLastCapture = 0u;
	LogFieldSnapshot previousSnapshot;
	bool hasPreviousSnapshot = false;
	bool wasMissingLastFrame = false;  // TargetMissing/TargetRestoredを1回だけ記録するための状態
	// ComponentField解決の高速化用cache。型が一致する限り再探索しない。
	mutable int32_t cachedRegistryIndex = -1;    // GetLogComponentFieldRegistry()内のindex
	mutable int32_t cachedComponentSlot = -1;    // gameObject->components内のindex
};

class EditorLogMonitorManager {
public:
	void Initialize(
		EditorScene* editorScene,
		EditorProfilerManager* profilerManager,
		EditorWeaponManager* weaponManager,
		EditorRuntimePropertyManager* runtimePropertyManager);
	void Start();  // Play開始時にLog Fileを初期化し、経過時間をリセットする
	void Update(float deltaTime);  // 有効なEntryを評価し、条件に合致すればLogへ追記する
	void Stop();  // バッファを最終Flushする
	void CaptureNow();  // 取得Modeを無視し、有効な全Entryを今すぐ1回記録する(ボタン等からの手動記録用)

	std::vector<LogWatchEntry>& GetEntries() { return entries_; }
	const std::vector<LogWatchEntry>& GetEntries() const { return entries_; }
	void AddEntry(const LogWatchEntry& entry) { entries_.push_back(entry); }
	void RemoveEntry(size_t index);

	// filePathを直接指定して保存/読込する。Presetは単に別Pathへ保存したものとして扱う
	// (例: Logs/Presets/PhysicsDebug.txt)ため、Preset機構のための特別な保存形式は無い。
	bool LoadConfig(const std::string& filePath);
	bool SaveConfig(const std::string& filePath) const;

	void SetPresetName(const std::string& name) { presetName_ = name; }
	const std::string& GetPresetName() const { return presetName_; }

	// UIパネルが「対象を選ぶTree/一覧」を描画するために使う
	const std::vector<LogComponentFieldDescriptor>& GetComponentFieldRegistry() const;
	std::vector<std::pair<std::string, std::string>> GetAvailableSystemFields() const;

	uint32_t GetDroppedEntryCount() const { return totalDroppedEntryCount_; }

private:
	EditorScene* editorScene_ = nullptr;
	EditorProfilerManager* profilerManager_ = nullptr;
	EditorWeaponManager* weaponManager_ = nullptr;
	EditorRuntimePropertyManager* runtimePropertyManager_ = nullptr;

	std::vector<LogWatchEntry> entries_;
	std::string presetName_ = "Default";
	std::string logBuffer_;
	float secondsSinceFlush_ = 0.0f;
	uint64_t frameCounter_ = 0u;
	float elapsedSeconds_ = 0.0f;

	// ---- ログ量に対する安全装置 ----
	static constexpr uint32_t kMaxEntriesPerFrame = 200u;       // 1Frameで記録できる最大件数
	static constexpr size_t kMaxBufferBytes = 1u * 1024u * 1024u;  // Flush前Bufferの上限(1MB)
	static constexpr uint64_t kMaxLogFileBytes = 50ull * 1024ull * 1024ull;  // 出力File上限(50MB)
	uint32_t entriesCapturedThisFrame_ = 0u;
	uint32_t framesDroppedSinceReport_ = 0u;
	uint32_t totalDroppedEntryCount_ = 0u;
	bool loggingSuspendedDueToFileSize_ = false;  // trueなら以後の書き込みを止める(File肥大化防止)

	LogSystemFieldContext BuildSystemFieldContext() const;

	// Component field の (componentType, componentFieldKey) をRegistry indexへ解決する。
	// 一度解決したら entry.cachedRegistryIndex にcacheし、以後は型一致チェックのみで再利用する。
	int32_t ResolveComponentRegistryIndex(LogWatchEntry& entry) const;
	// entry.gameObjectId が指すGameObjectの components 内で componentType に一致するslotを解決する。
	int32_t ResolveComponentSlot(EditorGameObject& gameObject, LogWatchEntry& entry) const;

	LogFieldSnapshot ResolveSnapshot(LogWatchEntry& entry) const;
	LogFieldSnapshot ResolveGameObjectFieldSnapshot(LogWatchEntry& entry) const;
	LogFieldSnapshot ResolveComponentFieldSnapshot(LogWatchEntry& entry) const;
	LogFieldSnapshot ResolveSystemFieldSnapshot(const LogWatchEntry& entry) const;
	LogFieldSnapshot MakeVectorComponentSnapshot(const Vector3& value, int32_t vectorComponent) const;

	bool ShouldCapture(LogWatchEntry& entry, float deltaTime, const LogFieldSnapshot& snapshot);
	bool ExceedsThreshold(const LogFieldSnapshot& previous, const LogFieldSnapshot& current, float threshold) const;

	void HandleMissingTransition(LogWatchEntry& entry, bool isMissing);
	bool TryRecordLine(const LogWatchEntry& entry, const LogFieldSnapshot& snapshot);
	void AppendLogLine(const LogWatchEntry& entry, const std::string& value);
	void AppendManualMarker();
	void ReportDroppedEntriesIfAny();
	void Flush();

	// Timestamp|Frame|Category|TargetKind|SourceId|SourceName|Component|Field|Value の1行を
	// logBuffer_へ追記する(機械可読形式の唯一の書き込み経路)。
	void WriteRow(
		const std::string& category,
		const std::string& targetKind,
		const std::string& sourceId,
		const std::string& sourceName,
		const std::string& componentName,
		const std::string& fieldName,
		const std::string& value);
};
