#include "GameScene.h"

#include "ApplicationWindow.h"
#include "EditorSharedState.h"
#include "ProjectSettings.h"
#include "Source/Engine/Asset/AssetRegistry.h"
#include "Source/Engine/Editor/EditorAssetManagerAdapters.h"

#include <algorithm>
#include <chrono>
#include <thread>

using namespace EditorSharedState;

namespace {
	// Diagnostics の Frame History へ、このフレームの実時間と GPU 時間を積む。
	// 集計値では潰れてしまう単発スパイクを、時系列として残すのが目的。
	void RecordFrameHistory(EditorProfilerManager& profilerManager) {
		using Clock = std::chrono::steady_clock;
		static Clock::time_point previousFrameTime = Clock::now();
		const Clock::time_point currentTime = Clock::now();
		const float cpuMilliseconds =
			std::chrono::duration<float, std::milli>(currentTime - previousFrameTime).count();
		previousFrameTime = currentTime;

		// GPU 時間は Renderer 側が Resolve 済みの "GPU Frame" サンプルから読む。
		float gpuMilliseconds = 0.0f;

		for (const EditorProfilerSample& sample : profilerManager.GetSortedSamples()) {
			if (sample.source == "GPU") {
				gpuMilliseconds = (std::max)(gpuMilliseconds, sample.totalMilliseconds);
			}
		}

		profilerManager.PushFrameHistory(cpuMilliseconds, gpuMilliseconds);
	}

	// Project Settings の FPS 上限をフレーム末尾で守る。0 なら何もしない。
	// VSync を切って上限だけ掛けたい場合(可変リフレッシュレート環境など)に効く。
	void ApplyFrameRateLimit() {
		const int32_t frameRateLimit = ProjectSettings::Get().GetData().frameRateLimit;

		if (frameRateLimit <= 0) {
			return;
		}

		using Clock = std::chrono::steady_clock;
		static Clock::time_point nextFrameTime = Clock::now();
		const auto targetFrameDuration =
			std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / frameRateLimit));
		const Clock::time_point currentTime = Clock::now();

		// 想定より大きく遅れている場合は取り戻そうとせず、基準時刻を現在へ寄せ直す。
		if (nextFrameTime < currentTime - targetFrameDuration) {
			nextFrameTime = currentTime;
		}

		nextFrameTime += targetFrameDuration;

		if (nextFrameTime > currentTime) {
			std::this_thread::sleep_until(nextFrameTime);
		}
	}
}

void GameScene::Initialize(_In_ HINSTANCE instanceHandle) {
	std::string gameBuildMessage;
	isStandaloneGame_ = EditorGameBuildManager::TryLoadStandaloneManifest(
		gameBuildSettings_,
		gameBuildMessage);
	hasStandaloneInitializationFailed_ =
		!isStandaloneGame_ && !gameBuildMessage.empty();

	if (!isStandaloneGame_ && !hasStandaloneInitializationFailed_) {
		// Editor の Play 中も Player と同じ Build Index で Scene を切り替えられるようにする。
		EditorGameBuildManager::LoadProjectSettings(gameBuildSettings_);
	}

	g_isStandaloneGame = isStandaloneGame_;
	g_gameBuildScenePaths = gameBuildSettings_.scenePaths;

	if (isStandaloneGame_) {
		SetStandaloneWindowTitle(gameBuildSettings_.productName);
	}

	//================================================================
	// Win32 / DirectX / 入力デバイスの初期化
	//================================================================

	platformManager_.Initialize(instanceHandle);  // instanceHandle は CreateWindow と DirectInput 生成に使うアプリ実体ハンドル。

	// ウィンドウ生成や DirectX 初期化が失敗した場合は、後続 Manager が未生成リソースを触らないように止める。
	if (platformManager_.HasInitializationFailed()) {
		return;
	}

	if (hasStandaloneInitializationFailed_) {
		MessageBoxA(
			nullptr,
			gameBuildMessage.c_str(),
			"CG2Engine Game Build Error",
			MB_OK | MB_ICONERROR);
		PostQuitMessage(1);
		return;
	}

	//================================================================
	// エディター機能ごとの初期化
	//================================================================

	// 共同制作・Project・HotReload・Runtimeが同じAsset管理基盤を使えるよう、
	// 他のManagerより先にAdapterを登録しておく(登録前にNotifyFileChanged等が
	// 呼ばれてもNoHandlerとして安全に扱われるが、早めに揃えて隙間を作らない)。
	RegisterEditorAssetManagerAdapters();

	sceneLifecycleManager_.Initialize();  // Scene / Runtime / 選択同期を初期化して、空 Scene を編集できる状態にする。

	if (isStandaloneGame_) {
		if (!g_editorScene.LoadScene(gameBuildSettings_.startupScenePath)) {
			MessageBoxA(
				nullptr,
				"起動シーンを読み込めません",
				"CG2Engine Game Build Error",
				MB_OK | MB_ICONERROR);
			PostQuitMessage(1);
			return;
		}

		g_currentScenePath = gameBuildSettings_.startupScenePath;
		g_editorSceneSynchronizer.Update(
			g_editorTextureFilePaths,
			g_selectedPlacedSceneObjectIndex);
		g_editorRuntimeManager.TogglePlay();
	}
	frameInputManager_.Initialize();  // キーボード入力とウィンドウサイズ追従を使える状態にする。
	imguiFrameManager_.Initialize();  // ImGui のフレーム制御担当。現在は実体初期化済みリソースを使うだけなので空実装。
	mainMenuManager_.Initialize();  // ファイルメニューや Play ボタンの表示担当。状態は共有 Scene を直接参照する。
	dockingManager_.Initialize();  // Unity 風の DockSpace を作る担当。初回 Draw で DockBuilder を使う。
	sceneViewManager_.Initialize();  // Scene タブ、ギズモ、ガイド線、ドラッグ配置を使うための担当。
	gameViewManager_.Initialize();  // GameView は Camera Component の出力確認を使うための担当。
	hierarchyWindowManager_.Initialize();  // GameObject ツリーを表示する Hierarchy 担当。
	inspectorWindowManager_.Initialize();  // 選択中 GameObject / Component を編集する Inspector 担当。
	bottomPanelWindowManager_.Initialize();  // Project と Console を下部にまとめて表示する BottomPanel 担当。
	animationWindowManager_.Initialize();  // Property Animation Clip を Timeline で編集する独立 Window 担当。
	gameplayToolsWindowManager_.Initialize();  // 汎用Spline、Event Timeline、State Graphを初期化する。
	diagnosticsWindowManager_.Initialize();  // Runtime負荷とScene設定不足を検査できる状態にする。
	externalFeatureWindowManager_.Initialize();  // 外部認識・オンラインのDebug表示を使うための担当。
	logMonitorWindowManager_.Initialize();  // 汎用ログ・監視の選択UIを使うための担当。
	hookWireDebugWindowManager_.Initialize();  // Hook構成とWireの検査Windowを使うための担当。
	pvShootWindowManager_.Initialize();  // PV撮影モードのCamera/PostProcess/TimeScale調整を使うための担当。

	if (!isStandaloneGame_) {
		// Project全体を1度だけ走査してAssetId/Hash/依存関係の初期状態を作る。
		// 常時Pollingする常駐Watcherではないため、以後の更新はNotifyAssetChanged等の
		// 個別通知(AssetManager経由)に委ねる。Standalone Buildでは起動を遅らせたくないため行わない。
		AssetRegistry::Get().RefreshFromDisk();
		teamCollaborationManager_.Initialize(&g_editorScene, &g_editorConsoleMessages);
	}

	renderManager_.Initialize();  // DirectX12 の描画コマンドを積む Renderer 担当。
}

void GameScene::Update() {
	EditorProfilerManager& profilerManager = g_editorRuntimeManager.GetProfilerManager();
	profilerManager.UpdateMeasurement();
	EditorProfilerManager::Scope editorFrameScope(
		profilerManager,
		"Editor Frame Update",
		"Editor");

	auto profileEditorUpdate = [&profilerManager](const char* eventName, auto&& updateFunction) {
		EditorProfilerManager::Scope eventScope(profilerManager, eventName, "Editor");
		updateFunction();
	};

	//================================================================
	// OS メッセージと終了要求の更新
	//================================================================

	profileEditorUpdate("Platform.Update", [this]() {
		platformManager_.Update();  // Windows メッセージを処理し、WM_QUIT が来たら終了フラグを立てる。
	});

	// 終了要求があるフレームでは、入力や UI が破棄済みリソースを触らないようにする。
	if (IsEndRequested()) {
		return;
	}

	//================================================================
	// フレーム中に変化する編集状態の更新
	//================================================================

	profileEditorUpdate("Frame Input", [this]() {
		frameInputManager_.Update();  // DIK キー状態、カメラ操作、ウィンドウリサイズ後の描画サイズを更新する。
	});
	profileEditorUpdate("Scene Lifecycle.Update", [this]() {
		sceneLifecycleManager_.Update();  // Play 中の物理・Input Component と、SceneObject / GameObject の同期を更新する。
	});
	profileEditorUpdate("ImGui Begin Frame", [this]() {
		imguiFrameManager_.Update();  // ImGui / ImGuizmo の新しいフレームを開始する。
	});

	if (isStandaloneGame_) {
		return;
	}

	// MainMenu / Docking / SceneView / GameView / Hierarchy / Inspector / BottomPanel /
	// LogMonitor / HookWireDebug / Renderer はフレーム前半に進める状態を持たない。
	// 選択・ドラッグ・Component 編集・行列更新はすべて Draw 中の ImGui 入力と
	// 矩形から直接処理するため、これらに Update は無い。以下は実際に状態が動くものだけ。
	profileEditorUpdate("Animation Window.Update", [this]() {
		animationWindowManager_.Update();  // Timeline Preview の時間進行と Record 中の Key 化を更新する。
	});
	profileEditorUpdate("Gameplay Tools.Update", [this]() {
		gameplayToolsWindowManager_.Update();  // Gameplay編集WindowはDraw時編集のため状態維持だけを行う。
	});
	profileEditorUpdate("Diagnostics.Update", [this]() {
		diagnosticsWindowManager_.Update();  // 表示中だけ一定間隔でScene構成を静的検査する。
	});
	externalFeatureWindowManager_.Update();  // Play外でもHaptics Previewとログ反映を進める。
	pvShootWindowManager_.Update();  // PV撮影モードのTimeScale倍率をRuntimeへ反映する。
	profileEditorUpdate("Team Collaboration.Update", [this]() {
		teamCollaborationManager_.Update(1.0f / 60.0f, g_editorRuntimeManager.IsPlaying());
	});
}

void GameScene::Draw() {
	EditorProfilerManager& profilerManager = g_editorRuntimeManager.GetProfilerManager();
	EditorProfilerManager::Scope editorFrameScope(
		profilerManager,
		"Editor Frame Draw",
		"Editor");

	auto profileEditorDraw = [&profilerManager](const char* eventName, auto&& drawFunction) {
		EditorProfilerManager::Scope eventScope(profilerManager, eventName, "Editor");
		drawFunction();
	};

	// 終了処理中のフレームでは、SwapChain や ImGui の描画を行わない。
	if (IsEndRequested()) {
		return;
	}

	//================================================================
	// UI と DirectX12 描画コマンドの発行
	//================================================================

	profileEditorDraw("Platform.Draw", [this]() {
		platformManager_.Draw();  // 描画フラグをフレーム先頭で下げ、ImGui Draw 後だけ Renderer が実行されるようにする。
	});
	profileEditorDraw("Scene Lifecycle.Draw", [this]() {
		sceneLifecycleManager_.Draw();  // Play 中だけ Runtime 用 Draw を呼び、物理状態の反映を描画前に完了させる。
	});

	if (isStandaloneGame_) {
		gameViewManager_.Draw();
		imguiFrameManager_.Draw();
		renderManager_.Draw();
		imguiFrameManager_.RenderPlatformWindows();
		ApplyFrameRateLimit();
		return;
	}

	profileEditorDraw("Main Menu.Draw", [this]() {
		mainMenuManager_.Draw();  // 上部メニューと Play / Stop の UI を構築する。
	});
	profileEditorDraw("PV Shoot.Draw", [this]() {
		pvShootWindowManager_.Draw();  // PV撮影モードの切り替えチェックボックスと調整パネルを描画する。
	});

	if (!pvShootWindowManager_.IsActive()) {
		profileEditorDraw("Docking.Draw", [this]() {
			dockingManager_.Draw();  // 各ウィンドウをドラッグ移動・ドッキングできる DockSpace を構築する。
		});
		profileEditorDraw("Scene View.Draw", [this]() {
			sceneViewManager_.Draw();  // Scene タブ、グリッド、ギズモ、ドラッグ配置、範囲選択を描画する。
		});
	}

	profileEditorDraw("Game View.Draw", [this]() {
		gameViewManager_.Draw();  // GameView の独立ウィンドウと Camera Component 出力範囲を描画する。
	});

	if (!pvShootWindowManager_.IsActive()) {
		profileEditorDraw("Hierarchy.Draw", [this]() { hierarchyWindowManager_.Draw(); });
		profileEditorDraw("Inspector.Draw", [this]() { inspectorWindowManager_.Draw(); });
		profileEditorDraw("Bottom Panel.Draw", [this]() { bottomPanelWindowManager_.Draw(); });
		profileEditorDraw("Animation Window.Draw", [this]() { animationWindowManager_.Draw(); });
		profileEditorDraw("Gameplay Tools.Draw", [this]() { gameplayToolsWindowManager_.Draw(); });
		profileEditorDraw("Diagnostics.Draw", [this]() { diagnosticsWindowManager_.Draw(); });
		profileEditorDraw("External Features.Draw", [this]() { externalFeatureWindowManager_.Draw(); });
		profileEditorDraw("Log Monitor.Draw", [this]() { logMonitorWindowManager_.Draw(); });
		profileEditorDraw("Team Collaboration.Draw", [this]() {
			teamCollaborationManager_.Draw(&g_isTeamCollaborationWindowVisible);
		});
		profileEditorDraw("Hook Wire Debug.Draw", [this]() { hookWireDebugWindowManager_.Draw(); });
	}

	profileEditorDraw("ImGui End Frame", [this]() {
		imguiFrameManager_.Draw();  // ImGui の DrawData を確定し、Renderer が GPU に送れる状態にする。
	});
	profileEditorDraw("Renderer.Draw CPU", [this]() {
		renderManager_.Draw();  // 3D/2D オブジェクト、ImGui、Present、Fence 待ちまでを実行する。
	});
	profileEditorDraw("ImGui Platform Windows", [this]() {
		imguiFrameManager_.RenderPlatformWindows();  // メイン枠の外へ切り離した Docking タブを個別 Window として描画する。
	});
	RecordFrameHistory(profilerManager);
	ApplyFrameRateLimit();
}

int GameScene::Finalize() {
	// DirectX / ImGui / Win32 / 音声リソースを解放し、WinMain に返す終了コードを受け取る。
	teamCollaborationManager_.Finalize();
	return platformManager_.Finalize();
}

bool GameScene::IsEndRequested() const {
	// WM_QUIT や初期化失敗で立つ終了フラグを WinMain のループ条件に使う。
	return platformManager_.IsEndRequested();
}
