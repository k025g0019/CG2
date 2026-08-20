#pragma once

#include "EditorScene.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorScriptManager;
class EditorWeaponManager;

class EditorWeaponLoadoutManager {
public:
	void Initialize(
		EditorScene* editorScene,
		EditorWeaponManager* weaponManager,
		EditorScriptManager* scriptManager);  // Loadoutを既存Weapon実行系へ接続する。
	void Start();  // 子Slot一覧とVisual状態を構築する。
	void Update(float deltaTime);  // Reload Timerを進める。
	void Stop();  // Play実行状態を破棄する。
	bool SelectSlot(int32_t loadoutGameObjectId, int32_t slotIndex);  // 指定Slotへ装備を切り替える。
	bool SelectNext(int32_t loadoutGameObjectId);  // 次のSlotへ循環する。
	bool SelectPrevious(int32_t loadoutGameObjectId);  // 前のSlotへ循環する。
	bool FireSelected(int32_t loadoutGameObjectId);  // 弾薬を消費して選択Weaponを1回発射する。
	bool Reload(int32_t loadoutGameObjectId);  // 選択SlotのReloadを開始する。
	bool GetSelectedWeapon(int32_t loadoutGameObjectId, int32_t& weaponGameObjectId) const;  // 選択Weapon IDを返す。
	bool GetSelectedSlotName(int32_t loadoutGameObjectId, std::string& slotName) const;  // HUD等へ選択中の武器名を返す。
	bool GetAmmo(int32_t loadoutGameObjectId, int32_t& currentAmmo, int32_t& reserveAmmo) const;  // 選択Slotの弾薬を返す。
	bool GetAmmoAtSlot(int32_t loadoutGameObjectId, int32_t slotIndex, int32_t& currentAmmo, int32_t& reserveAmmo, int32_t& maximumAmmo) const;  // 任意Slotの弾薬を返す。-1は選択Slot。
	bool AddMagazineAmmo(int32_t loadoutGameObjectId, int32_t slotIndex, int32_t amount);  // Magazineへ加算して0～Maximumへ収める。
	bool AddReserveAmmo(int32_t loadoutGameObjectId, int32_t slotIndex, int32_t amount);  // Reserveへ加算して0以上へ収める。
	bool SetMagazineAmmo(int32_t loadoutGameObjectId, int32_t slotIndex, int32_t amount);  // Magazineを0～Maximumへ設定する。
	bool SetReserveAmmo(int32_t loadoutGameObjectId, int32_t slotIndex, int32_t amount);  // Reserveを0以上へ設定する。
	bool SetMaximumAmmo(int32_t loadoutGameObjectId, int32_t slotIndex, int32_t amount);  // Magazine上限を変更して現在値も収める。
	bool RefillMagazine(int32_t loadoutGameObjectId, int32_t slotIndex);  // Reserveを消費せずMagazineだけをMaximumへ戻す。

private:
	struct LoadoutRuntime {
		std::vector<int32_t> slotGameObjectIds;  // Hierarchy順の可変Slot。
		int32_t reloadingSlotGameObjectId = -1;  // Reload中のSlot。
		float reloadRemainingSeconds = 0.0f;  // Reload完了までの残り秒。
	};

	EditorScene* editorScene_ = nullptr;  // Loadoutと子Slotを検索するScene。
	EditorWeaponManager* weaponManager_ = nullptr;  // 選択Weaponの実発射を行うManager。
	EditorScriptManager* scriptManager_ = nullptr;  // 装備変更・Reload完了Action通知先。
	std::unordered_map<int32_t, LoadoutRuntime> loadoutRuntimes_;  // Loadout所有者ごとの実行状態。

	EditorComponent* FindLoadoutComponent(int32_t loadoutGameObjectId) const;  // 編集可能なLoadout設定を返す。
	EditorComponent* FindSlotComponent(int32_t slotGameObjectId) const;  // 編集可能なSlot設定を返す。
	const EditorComponent* FindSlotByIndex(int32_t loadoutGameObjectId, int32_t slotIndex) const;  // -1なら選択中、それ以外はHierarchy順Indexを返す。
	EditorComponent* FindSlotByIndex(int32_t loadoutGameObjectId, int32_t slotIndex);  // 任意Slotを編集可能で返す。
	const EditorComponent* FindSelectedSlot(int32_t loadoutGameObjectId) const;  // 現在選択中のSlotを返す。
	EditorComponent* FindSelectedSlot(int32_t loadoutGameObjectId);  // 現在選択中のSlotを編集可能で返す。
	void UpdateVisuals(int32_t loadoutGameObjectId);  // 選択SlotだけVisualを有効化する。
	void QueueLoadoutAction(int32_t loadoutGameObjectId, const std::string& actionName);  // 任意Script Actionを通知する。
};

#pragma warning(pop)
