#pragma warning(disable : 4189 4514)

#include "EditorGameViewManager.h"

#include "EditorComponentUtility.h"
#include "EditorSharedState.h"
#include "Vector&Matrix.h"
#include "ThirdParty/imgui-docking/imgui-docking/imgui_internal.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>

using namespace EditorSharedState;

namespace {
	Vector3 AddVector3(const Vector3& firstValue, const Vector3& secondValue) {
		return Vector3{
			firstValue.x + secondValue.x,
			firstValue.y + secondValue.y,
			firstValue.z + secondValue.z};
	}

	bool IsNearlyZeroVector3(const Vector3& value) {
		return
			std::fabs(value.x) <= 0.001f &&
			std::fabs(value.y) <= 0.001f &&
			std::fabs(value.z) <= 0.001f;
	}

	Transforms ResolveWorldTransform(const EditorGameObject& gameObject) {
		Transforms worldTransform{gameObject.scale, gameObject.rotate, gameObject.translate};
		g_editorScene.GetWorldTransform(
			gameObject.id,
			worldTransform.scale,
			worldTransform.rotate,
			worldTransform.translate);
		return worldTransform;
	}

	Vector3 RotateVectorByEuler(const Vector3& value, const Vector3& rotation) {
		const Matrix4x4 rotationMatrix = MakeAffineMatrix(
			{1.0f, 1.0f, 1.0f},
			rotation,
			{0.0f, 0.0f, 0.0f});
		return Transform(value, rotationMatrix);
	}

	const EditorComponent* FindRuntimeCameraComponent(const EditorGameObject& gameObject) {
		const EditorComponent* cameraComponent =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::Camera);
		if (cameraComponent != nullptr && cameraComponent->isActive) {
			return cameraComponent;
		}

		const EditorComponent* cinemachineCameraComponent =
			EditorComponentUtility::FindComponent(gameObject, EditorComponentType::CinemachineCamera);
		if (cinemachineCameraComponent != nullptr && cinemachineCameraComponent->isActive) {
			return cinemachineCameraComponent;
		}

		return nullptr;
	}

	Transforms BuildFollowCameraTransform(
		const EditorGameObject& cameraGameObject,
		const EditorComponent& cameraComponent) {
		Transforms gameCameraTransform = ResolveWorldTransform(cameraGameObject);
		const EditorComponent* horizonStabilizer = EditorComponentUtility::FindComponent(
			cameraGameObject,
			EditorComponentType::CameraHorizonStabilizer);

		// Constraint更新済みのWorld Transformを再度Camera Followで上書きしない。
		// これにより船体Yawは追従しつつ、Pitch/Roll継承率をInspectorから調整できる。
		if (horizonStabilizer != nullptr &&
			horizonStabilizer->isActive &&
			horizonStabilizer->horizonSourceGameObjectId >= 0) {
			return gameCameraTransform;
		}

		if (cameraComponent.connectedGameObjectId < 0 ||
			cameraComponent.connectedGameObjectId == cameraGameObject.id) {
			return gameCameraTransform;
		}

		const EditorGameObject* targetGameObject =
			g_editorScene.FindGameObject(cameraComponent.connectedGameObjectId);
		if (targetGameObject == nullptr || !targetGameObject->isActive) {
			return gameCameraTransform;
		}

		constexpr Vector3 defaultFollowOffset = {0.0f, 2.0f, -6.0f};
		Vector3 followOffset = cameraGameObject.translate;
		if (IsNearlyZeroVector3(followOffset)) {
			followOffset = defaultFollowOffset;
		}

		const Transforms targetWorldTransform = ResolveWorldTransform(*targetGameObject);
		const int32_t positionSpace = (std::clamp)(cameraComponent.cameraFollowPositionSpace, 0, 1);

		if (positionSpace == 1) {
			followOffset = RotateVectorByEuler(followOffset, targetWorldTransform.rotate);
		}

		gameCameraTransform.translate = AddVector3(targetWorldTransform.translate, followOffset);
		const int32_t rotationMode = (std::clamp)(cameraComponent.cameraFollowRotationMode, 0, 2);

		if (rotationMode == 1) {
			gameCameraTransform.rotate = AddVector3(
				targetWorldTransform.rotate,
				cameraGameObject.rotate);
		}
		else if (rotationMode == 2) {
			const Vector3 lookDirection = Subtract(
				targetWorldTransform.translate,
				gameCameraTransform.translate);
			const float lookDistance = Length(lookDirection);

			if (lookDistance > 0.0001f) {
				const Vector3 normalizedDirection = Multiply(1.0f / lookDistance, lookDirection);
				gameCameraTransform.rotate = {
					-std::asin((std::clamp)(normalizedDirection.y, -1.0f, 1.0f)) + cameraGameObject.rotate.x,
					std::atan2(normalizedDirection.x, normalizedDirection.z) + cameraGameObject.rotate.y,
					cameraGameObject.rotate.z};
			}
		}

		return gameCameraTransform;
	}

	Transforms GetGameCameraTransform() {
		g_isGameViewUsingSceneCamera = true;  // Camera Component が見つからない場合は Scene カメラを使う。
		Transforms gameCameraTransform{};

		if (g_runtimeGameCameraOverrideActive) {
			gameCameraTransform = g_runtimeGameCameraOverrideTransform;
			g_isGameViewUsingSceneCamera = false;
		}
		else {
			const EditorGameObject* selectedCameraGameObject = nullptr;
			const EditorComponent* selectedCameraComponent = nullptr;
			int32_t selectedPriority = INT32_MIN;

			for (const EditorGameObject& gameObject : g_editorScene.GetGameObjects()) {
				if (!gameObject.isActive) {
					continue;
				}

				const EditorComponent* cameraComponent = FindRuntimeCameraComponent(gameObject);
				if (cameraComponent == nullptr || cameraComponent->cameraPriority <= selectedPriority) {
					continue;
				}

				selectedCameraGameObject = &gameObject;
				selectedCameraComponent = cameraComponent;
				selectedPriority = cameraComponent->cameraPriority;
			}

			if (selectedCameraGameObject != nullptr && selectedCameraComponent != nullptr) {
				g_isGameViewUsingSceneCamera = false;
				gameCameraTransform = BuildFollowCameraTransform(
					*selectedCameraGameObject,
					*selectedCameraComponent);
			}
			else {
				gameCameraTransform = g_cameraTransform;
			}
		}

		gameCameraTransform.translate = AddVector3(
			gameCameraTransform.translate,
			g_runtimeGameCameraPositionOffset);
		gameCameraTransform.rotate = AddVector3(
			gameCameraTransform.rotate,
			g_runtimeGameCameraRotationOffset);
		return gameCameraTransform;
	}

	void UpdateGameCameraMatrices() {
		Transforms gameCameraTransform = GetGameCameraTransform();  // GameView は Camera Component を優先し、なければ Scene カメラを使う。
		g_gameCameraPosition = gameCameraTransform.translate;  // PixelShader の視線方向計算で使う GameView カメラ位置。
		g_gameCameraMatrix = MakeAffineMatrix(
			gameCameraTransform.scale,
			gameCameraTransform.rotate,
			gameCameraTransform.translate);
		g_gameViewMatrix = Inverse(g_gameCameraMatrix);

		// 既定の投影パラメータ（Camera Component がなければこの値を使う）
		float fovY = 0.45f;
		float nearZ = 0.1f;
		float farZ = 1000.0f;
		int32_t projectionMode = 0;

		// Camera Component をPriority順で検索して投影パラメータを上書き
		const EditorComponent* selectedProjectionCamera = nullptr;
		int32_t selectedProjectionPriority = INT32_MIN;
		for (const EditorGameObject& gameObject : g_editorScene.GetGameObjects()) {
			if (!gameObject.isActive) {
				continue;
			}
			const EditorComponent* cameraComponent = FindRuntimeCameraComponent(gameObject);
			if (cameraComponent == nullptr || cameraComponent->cameraPriority <= selectedProjectionPriority) {
				continue;
			}

			selectedProjectionCamera = cameraComponent;
			selectedProjectionPriority = cameraComponent->cameraPriority;
		}

		if (selectedProjectionCamera != nullptr) {
			fovY = selectedProjectionCamera->cameraFieldOfView * (std::numbers::pi_v<float> / 180.0f);
			nearZ = selectedProjectionCamera->cameraNearClip;
			farZ = selectedProjectionCamera->cameraFarClip;
			projectionMode = selectedProjectionCamera->cameraProjectionMode;
		}

		if (projectionMode == 1) {
			const float orthoHeight = 10.0f;
			const float aspect = g_editorGameWidth / g_editorGameHeight;
			const float orthoWidth = orthoHeight * aspect;
			g_gameProjectionMatrix = MakeOrthographicMatrix(
				-orthoWidth * 0.5f, -orthoHeight * 0.5f,
				orthoWidth * 0.5f, orthoHeight * 0.5f,
				nearZ, farZ);
		} else {
			g_gameProjectionMatrix = MakePerspectiveFovMatrix(
				fovY,
				g_editorGameWidth / g_editorGameHeight,
				nearZ,
				farZ);
		}
	}

	ImVec4 ToImGuiColor(const Vector3& color, float alpha) {
		return ImVec4(
			(std::clamp)(color.x, 0.0f, 1.0f),
			(std::clamp)(color.y, 0.0f, 1.0f),
			(std::clamp)(color.z, 0.0f, 1.0f),
			(std::clamp)(alpha, 0.0f, 1.0f));
	}

	ImFont* ResolveUiFont(int32_t fontIndex) {
		if (fontIndex < 0 || fontIndex >= EditorSharedState::kUiFontVariantCount) {
			return ImGui::GetFont();
		}

		ImFont* resolvedFont = EditorSharedState::g_uiFontVariants[static_cast<size_t>(fontIndex)];
		return resolvedFont != nullptr ? resolvedFont : ImGui::GetFont();
	}

	// Rainbow/Wave演出用に1文字ずつ位置・色をずらして描画する。通常のAddTextと違い、
	// 影も含めて文字ごとに個別のPositionへ描く(波で上下する文字に影も追従させるため)。
	void DrawTextWithPerCharacterEffect(
		ImDrawList* drawList,
		ImFont* font,
		float fontSize,
		ImVec2 basePosition,
		const Vector3& baseColor,
		float alpha,
		const std::string& text,
		int32_t effectType,
		float paramA,
		float paramB,
		float paramC,
		float elapsedSeconds) {
		const char* cursor = text.c_str();
		const char* textEnd = text.c_str() + text.size();
		int32_t characterIndex = 0;
		float penX = basePosition.x;

		while (cursor < textEnd) {
			unsigned int codepoint = 0;
			const int32_t byteCount = ImTextCharFromUtf8(&codepoint, cursor, textEnd);
			const int32_t safeByteCount = (std::max)(byteCount, 1);
			const char* charBegin = cursor;
			const char* charEnd = cursor + safeByteCount;

			const float advance = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, charBegin, charEnd).x;

			Vector3 characterColor = baseColor;
			float characterYOffset = 0.0f;

			if (effectType == 5) {
				// レインボー: 文字Indexと時間で色相を回し続ける
				float hue = paramA * elapsedSeconds + paramB * static_cast<float>(characterIndex);
				hue -= std::floor(hue);
				float rainbowR = 0.0f;
				float rainbowG = 0.0f;
				float rainbowB = 0.0f;
				ImGui::ColorConvertHSVtoRGB(hue, 1.0f, 1.0f, rainbowR, rainbowG, rainbowB);
				characterColor = {rainbowR, rainbowG, rainbowB};
			}
			else if (effectType == 6) {
				// 波: 文字Indexごとに位相をずらしたSin波で上下させる
				characterYOffset = std::sin(
					static_cast<float>(characterIndex) * paramB + elapsedSeconds * paramC) * paramA;
			}

			const ImVec2 characterPosition{penX, basePosition.y + characterYOffset};
			const ImU32 shadowColor = IM_COL32(0, 0, 0, static_cast<int>(190 * alpha));
			const ImU32 characterDrawColor = ImGui::ColorConvertFloat4ToU32(
				ImVec4(characterColor.x, characterColor.y, characterColor.z, alpha));

			drawList->AddText(
				font, fontSize,
				ImVec2(characterPosition.x + 2.0f, characterPosition.y + 2.0f),
				shadowColor, charBegin, charEnd);
			drawList->AddText(font, fontSize, characterPosition, characterDrawColor, charBegin, charEnd);

			penX += advance;
			cursor = charEnd;
			characterIndex++;
		}
	}

	// 疑似乱数のように見える値を、時刻由来のSeedから決定的に作る(true RNGを使わずFrame間で再現可能にする)。
	float PseudoRandomFromSeed(float seed) {
		const float sineValue = std::sin(seed * 91.7f + 12.9898f) * 43758.5453f;
		return sineValue - std::floor(sineValue);
	}

	// 常時動き続けるText演出(発光/輪郭発光/色収差/微振動/不規則点滅/脈動/残像/簡易グリッチ)をまとめて描画する。
	// Rainbow/Waveと違い、これらは文字ごとではなく文字列全体へ複数回AddTextする(発光の縁取りやRGB分離など)。
	void DrawTextWithContinuousEffect(
		ImDrawList* drawList,
		ImFont* font,
		float fontSize,
		ImVec2 basePosition,
		const Vector3& baseColor,
		float alpha,
		const char* label,
		int32_t effectType,
		float paramA,
		float paramB,
		float paramC,
		float elapsedSeconds) {
		const auto drawPass = [&](ImVec2 position, const Vector3& color, float passAlpha, float passSize) {
			if (passAlpha <= 0.001f || passSize < 1.0f) {
				return;
			}
			const ImU32 passColor = ImGui::ColorConvertFloat4ToU32(
				ImVec4(color.x, color.y, color.z, (std::clamp)(passAlpha, 0.0f, 1.0f)));
			drawList->AddText(font, passSize, position, passColor, label);
		};

		if (effectType == 4) {
			// 点滅: ParamA秒間隔で完全にOn/Offする(表示され続ける間ずっと繰り返す)
			const float blinkInterval = (std::max)(paramA, 0.02f);
			const float phase = std::fmod(elapsedSeconds, blinkInterval * 2.0f);
			if (phase < blinkInterval) {
				drawPass(
					ImVec2(basePosition.x + 2.0f, basePosition.y + 2.0f),
					{0.0f, 0.0f, 0.0f}, alpha * 0.7f, fontSize);
				drawPass(basePosition, baseColor, alpha, fontSize);
			}
		}
		else if (effectType == 7) {
			// 発光: 8方向へ薄いGlow色を重ねてから本体を描く。脈動でGlowの半径と強さが上下する。
			const float breathing = paramB > 0.0f ? 1.0f + paramC * std::sin(elapsedSeconds * paramB) : 1.0f;
			const float radius = (std::max)(paramA, 0.0f) * (std::max)(breathing, 0.1f);
			const Vector3 glowColor = {
				(std::min)(baseColor.x * 0.5f + 0.5f, 1.0f),
				(std::min)(baseColor.y * 0.5f + 0.5f, 1.0f),
				(std::min)(baseColor.z * 0.5f + 0.5f, 1.0f)};
			constexpr int32_t kDirectionCount = 8;
			for (int32_t directionIndex = 0; directionIndex < kDirectionCount; directionIndex++) {
				const float angle = (static_cast<float>(directionIndex) / static_cast<float>(kDirectionCount)) *
					2.0f * std::numbers::pi_v<float>;
				const ImVec2 offsetPosition{
					basePosition.x + std::cos(angle) * radius,
					basePosition.y + std::sin(angle) * radius};
				drawPass(offsetPosition, glowColor, alpha * 0.16f, fontSize);
			}
			drawPass(
				ImVec2(basePosition.x + 2.0f, basePosition.y + 2.0f),
				{0.0f, 0.0f, 0.0f}, alpha * 0.7f, fontSize);
			drawPass(basePosition, baseColor, alpha, fontSize);
		}
		else if (effectType == 8) {
			// 輪郭の発光: 文字本体は普通の色。輪郭だけ8方向オフセットでGlow色を薄く重ねて縁取る。
			const float breathing = paramB > 0.0f ? 1.0f + paramC * std::sin(elapsedSeconds * paramB) : 1.0f;
			const float thickness = (std::max)(paramA, 0.1f) * (std::max)(breathing, 0.1f);
			const Vector3 glowColor = {
				(std::min)(baseColor.x * 0.4f + 0.6f, 1.0f),
				(std::min)(baseColor.y * 0.4f + 0.6f, 1.0f),
				(std::min)(baseColor.z * 0.4f + 0.6f, 1.0f)};
			constexpr int32_t kDirectionCount = 8;
			for (int32_t directionIndex = 0; directionIndex < kDirectionCount; directionIndex++) {
				const float angle = (static_cast<float>(directionIndex) / static_cast<float>(kDirectionCount)) *
					2.0f * std::numbers::pi_v<float>;
				const ImVec2 offsetPosition{
					basePosition.x + std::cos(angle) * thickness,
					basePosition.y + std::sin(angle) * thickness};
				drawPass(offsetPosition, glowColor, alpha * 0.55f * (std::clamp)(breathing, 0.0f, 2.0f), fontSize);
			}
			drawPass(
				ImVec2(basePosition.x + 2.0f, basePosition.y + 2.0f),
				{0.0f, 0.0f, 0.0f}, alpha * 0.7f, fontSize);
			drawPass(basePosition, baseColor, alpha, fontSize);
		}
		else if (effectType == 9) {
			// 色収差: 赤/青コピーを左右へずらして薄く重ね、最後に本来の色で本体を描く。
			const float breathing = paramB > 0.0f ? 1.0f + paramC * std::sin(elapsedSeconds * paramB) : 1.0f;
			const float offsetAmount = (std::max)(paramA, 0.0f) * (std::max)(breathing, 0.0f);
			drawPass(
				ImVec2(basePosition.x - offsetAmount, basePosition.y),
				{1.0f, 0.15f, 0.15f}, alpha * 0.65f, fontSize);
			drawPass(
				ImVec2(basePosition.x + offsetAmount, basePosition.y),
				{0.15f, 0.35f, 1.0f}, alpha * 0.65f, fontSize);
			drawPass(
				ImVec2(basePosition.x + 2.0f, basePosition.y + 2.0f),
				{0.0f, 0.0f, 0.0f}, alpha * 0.7f, fontSize);
			drawPass(basePosition, baseColor, alpha, fontSize);
		}
		else if (effectType == 10) {
			// 微振動: 複数の周波数を足したSinで、規則性の薄い揺れを作る(真の乱数は使わない)。
			const float amplitude = (std::max)(paramA, 0.0f);
			const float speed = (std::max)(paramB, 0.01f);
			const float jitterX = amplitude * 0.5f * (
				std::sin(elapsedSeconds * speed * 13.7f) + std::sin(elapsedSeconds * speed * 7.1f + 1.3f));
			const float jitterY = amplitude * 0.5f * (
				std::sin(elapsedSeconds * speed * 17.3f + 2.1f) + std::sin(elapsedSeconds * speed * 9.9f + 0.4f));
			const ImVec2 jitteredPosition{basePosition.x + jitterX, basePosition.y + jitterY};
			drawPass(
				ImVec2(jitteredPosition.x + 2.0f, jitteredPosition.y + 2.0f),
				{0.0f, 0.0f, 0.0f}, alpha * 0.7f, fontSize);
			drawPass(jitteredPosition, baseColor, alpha, fontSize);
		}
		else if (effectType == 11) {
			// 不規則点滅: 周波数の異なる複数のSinを合成し、蛍光灯のようなランダム風の明滅を作る。
			const float speed = (std::max)(paramB, 0.01f);
			const float rawFlicker =
				std::sin(elapsedSeconds * speed * 2.1f) +
				std::sin(elapsedSeconds * speed * 5.3f + 1.1f) +
				std::sin(elapsedSeconds * speed * 11.7f + 2.9f);
			const float normalizedFlicker = (std::clamp)(rawFlicker / 3.0f * 0.5f + 0.5f, 0.0f, 1.0f);
			const float flickerAlpha = alpha * ((std::clamp)(paramA, 0.0f, 1.0f) +
				(1.0f - (std::clamp)(paramA, 0.0f, 1.0f)) * normalizedFlicker);
			drawPass(
				ImVec2(basePosition.x + 2.0f, basePosition.y + 2.0f),
				{0.0f, 0.0f, 0.0f}, flickerAlpha * 0.7f, fontSize);
			drawPass(basePosition, baseColor, flickerAlpha, fontSize);
		}
		else if (effectType == 12) {
			// 脈動: FontSizeを1.0〜ParamA倍の間でゆっくり往復させ、心拍のような拡縮を作る。
			const float maximumScale = (std::max)(paramA, 1.0f);
			const float speed = (std::max)(paramB, 0.01f);
			const float scale = 1.0f + (maximumScale - 1.0f) * 0.5f * (1.0f + std::sin(elapsedSeconds * speed));
			const float scaledSize = fontSize * scale;
			const ImVec2 centeredPosition{
				basePosition.x - (scaledSize - fontSize) * 0.25f,
				basePosition.y - (scaledSize - fontSize) * 0.5f};
			drawPass(
				ImVec2(centeredPosition.x + 2.0f, centeredPosition.y + 2.0f),
				{0.0f, 0.0f, 0.0f}, alpha * 0.7f, scaledSize);
			drawPass(centeredPosition, baseColor, alpha, scaledSize);
		}
		else if (effectType == 13) {
			// 残像: 現在位置をSin/Cosの軌道で少し動かし、少し過去の軌道位置に薄いコピーを複数残す。
			const float amplitude = (std::max)(paramA, 0.0f);
			const float speed = (std::max)(paramB, 0.01f);
			const int32_t trailCount = (std::clamp)(static_cast<int32_t>(paramC + 0.5f), 2, 6);
			const auto driftAt = [&](float time) {
				return ImVec2{
					amplitude * std::sin(time * speed),
					amplitude * 0.5f * std::cos(time * speed * 1.3f)};
			};
			constexpr float kTrailLagSeconds = 0.05f;
			for (int32_t trailIndex = trailCount; trailIndex >= 1; trailIndex--) {
				const ImVec2 trailDrift = driftAt(elapsedSeconds - kTrailLagSeconds * static_cast<float>(trailIndex));
				const ImVec2 trailPosition{basePosition.x + trailDrift.x, basePosition.y + trailDrift.y};
				const float trailAlpha = alpha * 0.35f * std::pow(0.55f, static_cast<float>(trailIndex - 1));
				drawPass(trailPosition, baseColor, trailAlpha, fontSize);
			}
			const ImVec2 mainDrift = driftAt(elapsedSeconds);
			const ImVec2 mainPosition{basePosition.x + mainDrift.x, basePosition.y + mainDrift.y};
			drawPass(
				ImVec2(mainPosition.x + 2.0f, mainPosition.y + 2.0f),
				{0.0f, 0.0f, 0.0f}, alpha * 0.7f, fontSize);
			drawPass(mainPosition, baseColor, alpha, fontSize);
		}
		else if (effectType == 14) {
			// 簡易グリッチ: 発生頻度ごとのCycleを区切り、周期の先頭付近だけ「欠け」、
			// その後の短い間だけRGB分離+位置ズレを見せる。オフセット量はCycle番号から決定的な擬似乱数で決める。
			const float frequency = (std::max)(paramB, 0.05f);
			const float intensity = (std::clamp)(paramC, 0.0f, 1.0f);
			const float cycleLength = 1.0f / frequency;
			const float cyclePhase = std::fmod((std::max)(elapsedSeconds, 0.0f), cycleLength) / cycleLength;
			const float cycleIndex = std::floor(elapsedSeconds / cycleLength);
			const float randomOffset = (PseudoRandomFromSeed(cycleIndex) - 0.5f) * 2.0f * paramA * intensity;

			constexpr float kSkipWindow = 0.06f;
			constexpr float kGlitchWindow = 0.22f;

			if (intensity > 0.001f && cyclePhase < kSkipWindow) {
				// 一瞬だけ完全に欠ける
				return;
			}

			if (intensity > 0.001f && cyclePhase < kGlitchWindow) {
				const ImVec2 glitchedPosition{basePosition.x + randomOffset, basePosition.y};
				drawPass(
					ImVec2(glitchedPosition.x - paramA * 0.6f * intensity, glitchedPosition.y),
					{1.0f, 0.2f, 0.2f}, alpha * 0.7f, fontSize);
				drawPass(
					ImVec2(glitchedPosition.x + paramA * 0.6f * intensity, glitchedPosition.y),
					{0.2f, 0.4f, 1.0f}, alpha * 0.7f, fontSize);
				drawPass(glitchedPosition, baseColor, alpha, fontSize);
			}
			else {
				drawPass(
					ImVec2(basePosition.x + 2.0f, basePosition.y + 2.0f),
					{0.0f, 0.0f, 0.0f}, alpha * 0.7f, fontSize);
				drawPass(basePosition, baseColor, alpha, fontSize);
			}
		}
	}

	const EditorComponent* FindActiveComponent(
		const EditorGameObject& gameObject,
		EditorComponentType componentType) {
		const EditorComponent* component =
			EditorComponentUtility::FindComponent(gameObject, componentType);
		return component != nullptr && component->isActive ? component : nullptr;
	}

	const EditorComponent* FindActiveLayoutComponent(const EditorGameObject& gameObject) {
		const EditorComponentType layoutTypes[] = {
			EditorComponentType::HorizontalLayoutGroup,
			EditorComponentType::VerticalLayoutGroup,
			EditorComponentType::GridLayoutGroup};

		for (EditorComponentType layoutType : layoutTypes) {
			const EditorComponent* layoutComponent = FindActiveComponent(gameObject, layoutType);

			if (layoutComponent != nullptr) {
				return layoutComponent;
			}
		}

		return nullptr;
	}

	std::vector<std::string> SplitUiOptions(const std::string& optionText) {
		std::vector<std::string> options;
		std::stringstream optionStream(optionText);
		std::string option;

		while (std::getline(optionStream, option, '|')) {
			if (!option.empty()) {
				options.push_back(option);
			}
		}

		if (options.empty()) {
			options.push_back("Option");
		}

		return options;
	}

	bool CanReceiveUiInput(const EditorGameObject& gameObject) {
		bool hasEventSystem = false;
		bool hasActiveEventSystem = false;
		bool hasInputModule = false;
		bool hasActiveInputModule = false;

		for (const EditorGameObject& candidate : g_editorScene.GetGameObjects()) {
			const EditorComponent* eventSystem =
				EditorComponentUtility::FindComponent(candidate, EditorComponentType::EventSystem);
			if (eventSystem != nullptr) {
				hasEventSystem = true;
				hasActiveEventSystem |= candidate.isActive && eventSystem->isActive;
			}

			const EditorComponentType inputModuleTypes[] = {
				EditorComponentType::StandaloneInputModule,
				EditorComponentType::InputSystemUIInputModule,
				EditorComponentType::TouchInputModule};

			for (EditorComponentType inputModuleType : inputModuleTypes) {
				const EditorComponent* inputModule =
					EditorComponentUtility::FindComponent(candidate, inputModuleType);
				if (inputModule != nullptr) {
					hasInputModule = true;
					hasActiveInputModule |= candidate.isActive && inputModule->isActive;
				}
			}
		}

		if ((hasEventSystem && !hasActiveEventSystem) ||
			(hasInputModule && !hasActiveInputModule)) {
			return false;
		}

		const EditorGameObject* currentObject = &gameObject;
		while (currentObject != nullptr) {
			const EditorComponent* raycaster =
				EditorComponentUtility::FindComponent(*currentObject, EditorComponentType::GraphicRaycaster);

			if (raycaster != nullptr && (!raycaster->isActive || !raycaster->buttonInteractable)) {
				return false;
			}

			currentObject = g_editorScene.FindGameObject(currentObject->parentId);
		}

		return true;
	}

	void ResolveUiRect(
		const EditorGameObject& gameObject,
		const EditorComponent& visualComponent,
		EditorScriptVector2& position,
		EditorScriptVector2& size) {
		position = visualComponent.buttonPosition;
		size = visualComponent.buttonSize;

		const EditorComponent* rectTransform =
			FindActiveComponent(gameObject, EditorComponentType::RectTransform);

		if (rectTransform != nullptr) {
			position = rectTransform->buttonPosition;
			size = rectTransform->buttonSize;
		}

		const EditorGameObject* parentObject = g_editorScene.FindGameObject(gameObject.parentId);
		if (parentObject != nullptr) {
			const EditorComponent* layoutComponent = FindActiveLayoutComponent(*parentObject);
			if (layoutComponent != nullptr) {
				auto childIterator = std::find(
					parentObject->children.begin(),
					parentObject->children.end(),
					gameObject.id);
				const int32_t childIndex = childIterator != parentObject->children.end()
					? static_cast<int32_t>(std::distance(parentObject->children.begin(), childIterator))
					: 0;
				const float spacing = (std::max)(layoutComponent->sliderValue, 0.0f);
				position = layoutComponent->buttonPosition;
				size = layoutComponent->buttonSize;

				if (layoutComponent->type == EditorComponentType::HorizontalLayoutGroup) {
					position.x += static_cast<float>(childIndex) * (size.x + spacing);
				}
				else if (layoutComponent->type == EditorComponentType::VerticalLayoutGroup) {
					position.y += static_cast<float>(childIndex) * (size.y + spacing);
				}
				else {
					const int32_t columnCount = (std::max)(layoutComponent->inputBehavior, 1);
					const int32_t columnIndex = childIndex % columnCount;
					const int32_t rowIndex = childIndex / columnCount;
					position.x += static_cast<float>(columnIndex) * (size.x + spacing);
					position.y += static_cast<float>(rowIndex) * (size.y + spacing);
				}
			}

			const EditorComponent* scrollRect =
				FindActiveComponent(*parentObject, EditorComponentType::ScrollRect);
			if (scrollRect != nullptr) {
				position.x -= scrollRect->uvOffset.x * (std::max)(parentObject->children.size() * size.x, 0.0f);
				position.y -= scrollRect->uvOffset.y * (std::max)(parentObject->children.size() * size.y, 0.0f);
			}
		}

		const EditorComponent* layoutElement =
			FindActiveComponent(gameObject, EditorComponentType::LayoutElement);
		if (layoutElement != nullptr) {
			size = layoutElement->buttonSize;
		}

		const EditorComponent* aspectRatioFitter =
			FindActiveComponent(gameObject, EditorComponentType::AspectRatioFitter);
		if (aspectRatioFitter != nullptr && aspectRatioFitter->sliderValue > 0.0f) {
			size.x = size.y * aspectRatioFitter->sliderValue;
		}

		const EditorComponent* contentSizeFitter =
			FindActiveComponent(gameObject, EditorComponentType::ContentSizeFitter);
		if (contentSizeFitter != nullptr) {
			if (contentSizeFitter->freezePositionX) {
				const float textWidth =
					static_cast<float>(visualComponent.buttonLabel.size()) * (std::max)(size.y, 1.0f) * 0.55f;
				size.x = (std::max)(size.x, textWidth);
			}

			if (contentSizeFitter->freezePositionY) {
				size.y = (std::max)(size.y, visualComponent.buttonSize.y);
			}
		}
	}

	bool IsUiCanvasHierarchyActive(const EditorGameObject& gameObject) {
		const EditorGameObject* currentObject = &gameObject;

		while (currentObject != nullptr) {
			const EditorComponent* canvas =
				EditorComponentUtility::FindComponent(*currentObject, EditorComponentType::Canvas);

			if (canvas != nullptr) {
				return currentObject->isActive && canvas->isActive;
			}

			currentObject = g_editorScene.FindGameObject(currentObject->parentId);
		}

		return true;
	}

	int32_t GetUiCanvasOrder(const EditorGameObject& gameObject) {
		const EditorGameObject* currentObject = &gameObject;

		while (currentObject != nullptr) {
			const EditorComponent* canvas =
				FindActiveComponent(*currentObject, EditorComponentType::Canvas);

			if (canvas != nullptr) {
				return canvas->physicsLayer;
			}

			currentObject = g_editorScene.FindGameObject(currentObject->parentId);
		}

		return 0;
	}

	bool PushUiMaskClipRect(
		const EditorGameObject& gameObject,
		const ImVec2& gameContentPosition,
		float uiScaleX,
		float uiScaleY) {
		const EditorGameObject* currentObject = g_editorScene.FindGameObject(gameObject.parentId);

		while (currentObject != nullptr) {
			const EditorComponent* mask = FindActiveComponent(*currentObject, EditorComponentType::Mask);
			if (mask == nullptr) {
				mask = FindActiveComponent(*currentObject, EditorComponentType::RectMask2D);
			}

			if (mask != nullptr) {
				const ImVec2 clipMinimum{
					gameContentPosition.x + mask->buttonPosition.x * uiScaleX,
					gameContentPosition.y + mask->buttonPosition.y * uiScaleY};
				const ImVec2 clipMaximum{
					clipMinimum.x + (std::max)(mask->buttonSize.x * uiScaleX, 1.0f),
					clipMinimum.y + (std::max)(mask->buttonSize.y * uiScaleY, 1.0f)};
				ImGui::PushClipRect(clipMinimum, clipMaximum, true);
				return true;
			}

			currentObject = g_editorScene.FindGameObject(currentObject->parentId);
		}

		return false;
	}

	enum class TargetMarkerVisibility {
		NotConfigured,
		Visible,
		Hidden,
	};

	TargetMarkerVisibility ResolveTargetMarkerPosition(
		const EditorGameObject& markerGameObject,
		float referenceGameWidth,
		float referenceGameHeight,
		const EditorScriptVector2& markerSize,
		EditorScriptVector2& markerPosition,
		float& markerRotation) {
		const EditorComponent* marker = FindActiveComponent(markerGameObject, EditorComponentType::WorldTargetMarker);
		bool isOffScreenIndicator = false;

		if (marker == nullptr) {
			marker = FindActiveComponent(markerGameObject, EditorComponentType::OffScreenIndicator);
			isOffScreenIndicator = marker != nullptr;
		}

		if (marker == nullptr) {
			return TargetMarkerVisibility::NotConfigured;
		}

		int32_t targetGameObjectId = marker->targetMarkerTargetGameObjectId;
		bool isLocked = !marker->targetMarkerOnlyWhenLocked;

		if (targetGameObjectId < 0 && marker->targetMarkerLockGameObjectId >= 0) {
			const EditorGameObject* lockObject = g_editorScene.FindGameObject(marker->targetMarkerLockGameObjectId);
			const EditorComponent* lock = lockObject != nullptr
				? FindActiveComponent(*lockObject, EditorComponentType::TargetLock)
				: nullptr;

			if (lock != nullptr) {
				targetGameObjectId = lock->targetLockCurrentGameObjectId;
				isLocked = lock->targetLockLocked;
			}
			else if (lockObject != nullptr) {
				const EditorComponent* multiLock = FindActiveComponent(*lockObject, EditorComponentType::MultiTargetLock);

				if (multiLock != nullptr && !multiLock->multiTargetLockTargetGameObjectIds.empty()) {
					const size_t maximumIndex = multiLock->multiTargetLockTargetGameObjectIds.size() - 1u;
					const size_t targetIndex = (std::min)(
						static_cast<size_t>((std::max)(marker->targetMarkerMultiLockIndex, 0)),
						maximumIndex);
					const bool hasLockState = targetIndex < multiLock->multiTargetLockCompletedValues.size();

					if (hasLockState && (!marker->targetMarkerOnlyWhenLocked || multiLock->multiTargetLockCompletedValues[targetIndex])) {
						targetGameObjectId = multiLock->multiTargetLockTargetGameObjectIds[targetIndex];
						isLocked = multiLock->multiTargetLockCompletedValues[targetIndex];
					}
				}
			}
		}

		if (targetGameObjectId < 0 && marker->targetMarkerSelectorGameObjectId >= 0) {
			const EditorGameObject* selectorObject = g_editorScene.FindGameObject(marker->targetMarkerSelectorGameObjectId);
			const EditorComponent* selector = selectorObject != nullptr
				? FindActiveComponent(*selectorObject, EditorComponentType::TargetSelector)
				: nullptr;
			targetGameObjectId = selector != nullptr ? selector->targetSelectorCurrentTargetGameObjectId : -1;
		}

		if (targetGameObjectId < 0 || !isLocked) {
			return TargetMarkerVisibility::Hidden;
		}

		const EditorGameObject* targetGameObject = g_editorScene.FindGameObject(targetGameObjectId);
		if (targetGameObject == nullptr || !targetGameObject->isActive) {
			return TargetMarkerVisibility::Hidden;
		}

		Transforms targetTransform = ResolveWorldTransform(*targetGameObject);
		const Vector3 worldPosition{
			targetTransform.translate.x + marker->targetMarkerWorldOffset.x,
			targetTransform.translate.y + marker->targetMarkerWorldOffset.y,
			targetTransform.translate.z + marker->targetMarkerWorldOffset.z};
		const Matrix4x4 viewProjectionMatrix = Multiply(g_gameViewMatrix, g_gameProjectionMatrix);
		float clipX = worldPosition.x * viewProjectionMatrix.matrix[0][0] +
			worldPosition.y * viewProjectionMatrix.matrix[1][0] +
			worldPosition.z * viewProjectionMatrix.matrix[2][0] + viewProjectionMatrix.matrix[3][0];
		float clipY = worldPosition.x * viewProjectionMatrix.matrix[0][1] +
			worldPosition.y * viewProjectionMatrix.matrix[1][1] +
			worldPosition.z * viewProjectionMatrix.matrix[2][1] + viewProjectionMatrix.matrix[3][1];
		const float clipW = worldPosition.x * viewProjectionMatrix.matrix[0][3] +
			worldPosition.y * viewProjectionMatrix.matrix[1][3] +
			worldPosition.z * viewProjectionMatrix.matrix[2][3] + viewProjectionMatrix.matrix[3][3];
		const bool isBehindCamera = clipW <= 0.0001f;

		if (isBehindCamera && marker->targetMarkerHideBehindCamera) {
			return TargetMarkerVisibility::Hidden;
		}

		const float safeClipW = std::fabs(clipW) > 0.0001f ? std::fabs(clipW) : 0.0001f;
		float normalizedX = clipX / safeClipW;
		float normalizedY = clipY / safeClipW;

		if (isBehindCamera) {
			normalizedX = -normalizedX;
			normalizedY = -normalizedY;
		}

		const bool isOnScreen = !isBehindCamera && std::fabs(normalizedX) <= 1.0f && std::fabs(normalizedY) <= 1.0f;

		if (isOffScreenIndicator == isOnScreen) {
			return TargetMarkerVisibility::Hidden;
		}

		markerRotation = std::atan2(-normalizedY, normalizedX);

		if (isOffScreenIndicator) {
			const float horizontalPadding = (std::clamp)(marker->targetMarkerEdgePadding / (std::max)(referenceGameWidth * 0.5f, 1.0f), 0.0f, 0.95f);
			const float verticalPadding = (std::clamp)(marker->targetMarkerEdgePadding / (std::max)(referenceGameHeight * 0.5f, 1.0f), 0.0f, 0.95f);
			const float scaleToEdge = (std::max)(
				std::fabs(normalizedX) / (1.0f - horizontalPadding),
				std::fabs(normalizedY) / (1.0f - verticalPadding));

			if (scaleToEdge > 1.0f) {
				normalizedX /= scaleToEdge;
				normalizedY /= scaleToEdge;
			}
		}

		markerPosition = {
			(normalizedX + 1.0f) * 0.5f * referenceGameWidth - markerSize.x * 0.5f + marker->targetMarkerScreenOffset.x,
			(1.0f - normalizedY) * 0.5f * referenceGameHeight - markerSize.y * 0.5f + marker->targetMarkerScreenOffset.y};
		return TargetMarkerVisibility::Visible;
	}

	bool TryProjectGameWorldPosition(const Vector3& worldPosition, ImVec2& screenPosition) {
		const Matrix4x4 viewProjectionMatrix = Multiply(g_gameViewMatrix, g_gameProjectionMatrix);
		const float clipX = worldPosition.x * viewProjectionMatrix.matrix[0][0] +
			worldPosition.y * viewProjectionMatrix.matrix[1][0] +
			worldPosition.z * viewProjectionMatrix.matrix[2][0] + viewProjectionMatrix.matrix[3][0];
		const float clipY = worldPosition.x * viewProjectionMatrix.matrix[0][1] +
			worldPosition.y * viewProjectionMatrix.matrix[1][1] +
			worldPosition.z * viewProjectionMatrix.matrix[2][1] + viewProjectionMatrix.matrix[3][1];
		const float clipW = worldPosition.x * viewProjectionMatrix.matrix[0][3] +
			worldPosition.y * viewProjectionMatrix.matrix[1][3] +
			worldPosition.z * viewProjectionMatrix.matrix[2][3] + viewProjectionMatrix.matrix[3][3];

		if (clipW <= 0.0001f) {
			return false;
		}

		const float normalizedX = clipX / clipW;
		const float normalizedY = clipY / clipW;

		if (std::fabs(normalizedX) > 4.0f || std::fabs(normalizedY) > 4.0f) {
			return false;
		}

		screenPosition = {
			g_editorGameX + (normalizedX + 1.0f) * 0.5f * g_editorGameWidth,
			g_editorGameY + (1.0f - normalizedY) * 0.5f * g_editorGameHeight};
		return true;
	}

	void DrawGameTrajectoryPreviews(ImDrawList* drawList) {
		drawList->PushClipRect(
			ImVec2(g_editorGameX, g_editorGameY),
			ImVec2(g_editorGameX + g_editorGameWidth, g_editorGameY + g_editorGameHeight),
			true);

		for (const EditorGameObject& rendererObject : g_editorScene.GetGameObjects()) {
			const EditorComponent* trajectoryRenderer = EditorComponentUtility::FindComponent(
				rendererObject,
				EditorComponentType::TrajectoryRenderer);

			if (!rendererObject.isActive || trajectoryRenderer == nullptr ||
				!trajectoryRenderer->isActive || !trajectoryRenderer->trajectoryShowInGameView) {
				continue;
			}

			const int32_t predictionGameObjectId = trajectoryRenderer->trajectoryPredictionGameObjectId >= 0
				? trajectoryRenderer->trajectoryPredictionGameObjectId
				: rendererObject.id;
			const EditorGameObject* predictionObject = g_editorScene.FindGameObject(predictionGameObjectId);
			const EditorComponent* prediction = predictionObject != nullptr
				? EditorComponentUtility::FindComponent(*predictionObject, EditorComponentType::BallisticPrediction)
				: nullptr;

			if (prediction == nullptr || !prediction->isActive || !prediction->ballisticValid ||
				prediction->ballisticTrajectoryPoints.size() < 2u) {
				continue;
			}

			const ImU32 lineColor = ImGui::ColorConvertFloat4ToU32(ImVec4(
				(std::clamp)(trajectoryRenderer->trajectoryColor.x, 0.0f, 1.0f),
				(std::clamp)(trajectoryRenderer->trajectoryColor.y, 0.0f, 1.0f),
				(std::clamp)(trajectoryRenderer->trajectoryColor.z, 0.0f, 1.0f),
				(std::clamp)(trajectoryRenderer->trajectoryAlpha, 0.0f, 1.0f)));
			const int32_t pointCount = (std::min)(
				static_cast<int32_t>(prediction->ballisticTrajectoryPoints.size()),
				(std::clamp)(trajectoryRenderer->trajectoryMaximumPoints, 2, 2048));

			for (int32_t pointIndex = 1; pointIndex < pointCount; pointIndex++) {
				ImVec2 lineStart{};
				ImVec2 lineEnd{};

				if (TryProjectGameWorldPosition(
						prediction->ballisticTrajectoryPoints[static_cast<size_t>(pointIndex - 1)],
						lineStart) &&
					TryProjectGameWorldPosition(
						prediction->ballisticTrajectoryPoints[static_cast<size_t>(pointIndex)],
						lineEnd)) {
					drawList->AddLine(
						lineStart,
						lineEnd,
						lineColor,
						(std::max)(trajectoryRenderer->trajectoryThickness, 0.5f));
				}
			}

			if (trajectoryRenderer->trajectoryShowImpactPoint) {
				ImVec2 impactPosition{};

				if (TryProjectGameWorldPosition(prediction->ballisticImpactPosition, impactPosition)) {
					drawList->AddCircle(
						impactPosition,
						6.0f,
						lineColor,
						16,
						(std::max)(trajectoryRenderer->trajectoryThickness, 1.0f));
				}
			}
		}

		drawList->PopClipRect();
	}

	void DrawGameViewUiControls(const ImVec2& gameContentPosition, float gameWidth, float gameHeight) {
		float referenceGameWidth = 1280.0f;
		float referenceGameHeight = 720.0f;
		float widthHeightMatch = 0.5f;

		for (const EditorGameObject& gameObject : g_editorScene.GetGameObjects()) {
			const EditorComponent* canvasScaler =
				FindActiveComponent(gameObject, EditorComponentType::CanvasScaler);

			if (canvasScaler != nullptr) {
				referenceGameWidth = (std::max)(canvasScaler->buttonSize.x, 1.0f);
				referenceGameHeight = (std::max)(canvasScaler->buttonSize.y, 1.0f);
				widthHeightMatch = (std::clamp)(canvasScaler->sliderValue, 0.0f, 1.0f);
				break;
			}
		}
		const float widthScale = gameWidth / referenceGameWidth;
		const float heightScale = gameHeight / referenceGameHeight;
		const float uniformScale = std::exp(
			std::log((std::max)(widthScale, 0.0001f)) * (1.0f - widthHeightMatch) +
			std::log((std::max)(heightScale, 0.0001f)) * widthHeightMatch);
		const float uiScaleX = uniformScale;
		const float uiScaleY = uniformScale;
		const float fontScale = (std::min)(uiScaleX, uiScaleY);
		static std::vector<EditorGameObject*> orderedUiObjects;
		orderedUiObjects.clear();
		orderedUiObjects.reserve(g_editorScene.GetGameObjects().size());

		for (EditorGameObject& gameObject : g_editorScene.GetGameObjects()) {
			orderedUiObjects.push_back(&gameObject);
		}

		std::stable_sort(
			orderedUiObjects.begin(),
			orderedUiObjects.end(),
			[](const EditorGameObject* firstObject, const EditorGameObject* secondObject) {
				return GetUiCanvasOrder(*firstObject) < GetUiCanvasOrder(*secondObject);
			});

		ImGui::PushClipRect(
			gameContentPosition,
			ImVec2(gameContentPosition.x + gameWidth, gameContentPosition.y + gameHeight),
			true);

		for (EditorGameObject* gameObjectPointer : orderedUiObjects) {
			EditorGameObject& gameObject = *gameObjectPointer;

			if (!gameObject.isActive || !IsUiCanvasHierarchyActive(gameObject)) {
				continue;
			}

			for (EditorComponent& component : gameObject.components) {
				if (component.type != EditorComponentType::Button &&
					component.type != EditorComponentType::SceneButton &&
					component.type != EditorComponentType::Toggle &&
					component.type != EditorComponentType::Slider &&
					component.type != EditorComponentType::Scrollbar &&
					component.type != EditorComponentType::Dropdown &&
					component.type != EditorComponentType::TMPDropdown &&
					component.type != EditorComponentType::InputField &&
					component.type != EditorComponentType::TMPInputField &&
					component.type != EditorComponentType::Image &&
					component.type != EditorComponentType::RawImage &&
					component.type != EditorComponentType::Text &&
					component.type != EditorComponentType::TextMeshProUGUI) {
					continue;
				}

				EditorComponent* uiComponent = &component;
				if (!uiComponent->isActive) {
					continue;
				}

				Vector3 renderColor = uiComponent->color;
				Vector3 renderHoverColor = uiComponent->buttonHoverColor;
				Vector3 renderPressedColor = uiComponent->buttonPressedColor;
				float renderAlpha = uiComponent->alpha * uiComponent->intensity;
				const EditorComponent* canvasRenderer =
					FindActiveComponent(gameObject, EditorComponentType::CanvasRenderer);

				if (canvasRenderer != nullptr) {
					renderColor = {
						renderColor.x * canvasRenderer->color.x,
						renderColor.y * canvasRenderer->color.y,
						renderColor.z * canvasRenderer->color.z};
					renderHoverColor = {
						renderHoverColor.x * canvasRenderer->color.x,
						renderHoverColor.y * canvasRenderer->color.y,
						renderHoverColor.z * canvasRenderer->color.z};
					renderPressedColor = {
						renderPressedColor.x * canvasRenderer->color.x,
						renderPressedColor.y * canvasRenderer->color.y,
						renderPressedColor.z * canvasRenderer->color.z};
					renderAlpha *= canvasRenderer->alpha * canvasRenderer->intensity;
				}

				renderAlpha = (std::clamp)(renderAlpha, 0.0f, 1.0f);

				EditorScriptVector2 resolvedPosition{};
				EditorScriptVector2 resolvedSize{};
				ResolveUiRect(gameObject, *uiComponent, resolvedPosition, resolvedSize);
				float targetMarkerRotation = 0.0f;
				const TargetMarkerVisibility markerVisibility = ResolveTargetMarkerPosition(
					gameObject,
					referenceGameWidth,
					referenceGameHeight,
					resolvedSize,
					resolvedPosition,
					targetMarkerRotation);

				if (markerVisibility == TargetMarkerVisibility::Hidden) {
					continue;
				}
				const ImVec2 buttonPosition{
					gameContentPosition.x + resolvedPosition.x * uiScaleX,
					gameContentPosition.y + resolvedPosition.y * uiScaleY};
				const ImVec2 buttonSize{
					(std::max)(resolvedSize.x * uiScaleX, 1.0f),
					(std::max)(resolvedSize.y * uiScaleY, 1.0f)};
				const char* buttonLabel =
					uiComponent->buttonLabel.empty() ? "UI" : uiComponent->buttonLabel.c_str();
				const bool isTextComponent =
					uiComponent->type == EditorComponentType::Text ||
					uiComponent->type == EditorComponentType::TextMeshProUGUI;
				const bool isImageComponent =
					uiComponent->type == EditorComponentType::Image ||
					uiComponent->type == EditorComponentType::RawImage;
				const bool hasMaskClip = PushUiMaskClipRect(
					gameObject,
					gameContentPosition,
					uiScaleX,
					uiScaleY);

				if (isImageComponent) {
					ImDrawList* drawList = ImGui::GetWindowDrawList();
					const ImVec2 imageMaximum{
						buttonPosition.x + buttonSize.x,
						buttonPosition.y + buttonSize.y};
					const ImU32 imageColor = ImGui::ColorConvertFloat4ToU32(
						ToImGuiColor(renderColor, renderAlpha));
					const D3D12_GPU_DESCRIPTOR_HANDLE textureHandle =
						g_editorSceneObjectManager.GetOrLoadUiTexture(uiComponent->assetPath);
					const EditorComponent* offScreenIndicator = FindActiveComponent(
						gameObject,
						EditorComponentType::OffScreenIndicator);
					const bool rotatesToTarget = offScreenIndicator != nullptr &&
						offScreenIndicator->targetMarkerRotateToDirection;

					if (textureHandle.ptr != 0u) {
						if (rotatesToTarget) {
							const ImVec2 center{
								(buttonPosition.x + imageMaximum.x) * 0.5f,
								(buttonPosition.y + imageMaximum.y) * 0.5f};
							const float halfWidth = buttonSize.x * 0.5f;
							const float halfHeight = buttonSize.y * 0.5f;
							const float cosine = std::cos(targetMarkerRotation);
							const float sine = std::sin(targetMarkerRotation);
							auto rotatePoint = [&](float localX, float localY) {
								return ImVec2{
									center.x + localX * cosine - localY * sine,
									center.y + localX * sine + localY * cosine};
							};
							drawList->AddImageQuad(
								ImTextureRef(textureHandle.ptr),
								rotatePoint(-halfWidth, -halfHeight),
								rotatePoint(halfWidth, -halfHeight),
								rotatePoint(halfWidth, halfHeight),
								rotatePoint(-halfWidth, halfHeight),
								ImVec2(0.0f, 0.0f), ImVec2(1.0f, 0.0f),
								ImVec2(1.0f, 1.0f), ImVec2(0.0f, 1.0f),
								imageColor);
						}
						else {
							drawList->AddImage(
								ImTextureRef(textureHandle.ptr),
								buttonPosition,
								imageMaximum,
								ImVec2(0.0f, 0.0f),
								ImVec2(1.0f, 1.0f),
								imageColor);
						}
					}
					else {
						// 画像未設定や読込失敗時も、配置範囲を確認できる色付き矩形を表示する。
						drawList->AddRectFilled(buttonPosition, imageMaximum, imageColor);
					}

					if (hasMaskClip) {
						ImGui::PopClipRect();
					}
					continue;
				}

				if (isTextComponent) {
					// 出現演出(1回だけ): Alpha/Scale/表示文字数を変化させる。
					float appearAlphaMultiplier = 1.0f;
					float appearScaleMultiplier = 1.0f;
					size_t appearVisibleCharacterCount = std::string::npos;
					EditorComponent* textEffect =
						EditorComponentUtility::FindComponent(gameObject, EditorComponentType::TextEffect);
					const bool hasAnyTextEffect =
						textEffect != nullptr && textEffect->isActive &&
						(textEffect->textAppearEffectType != 0 || textEffect->textContinuousEffectType != 0);

					if (hasAnyTextEffect) {
						if (!textEffect->textEffectRuntimeWasActive) {
							textEffect->textEffectRuntimeElapsed = 0.0f;
							textEffect->textEffectRuntimeWasActive = true;
						}

						textEffect->textEffectRuntimeElapsed += ImGui::GetIO().DeltaTime;
					}
					else if (textEffect != nullptr) {
						textEffect->textEffectRuntimeWasActive = false;
						textEffect->textEffectRuntimeElapsed = 0.0f;
					}

					if (hasAnyTextEffect && textEffect->textAppearEffectType != 0) {
						const float appearTime = textEffect->textEffectRuntimeElapsed - textEffect->textAppearDelay;
						const float appearDuration = (std::max)(textEffect->textAppearDuration, 0.001f);

						if (appearTime < 0.0f) {
							// 開始遅延中は各効果の開始状態(非表示・文字数0・縮小0)を維持する
							switch (textEffect->textAppearEffectType) {
							case 1: appearAlphaMultiplier = 0.0f; break;
							case 2: appearVisibleCharacterCount = 0u; break;
							case 3: appearScaleMultiplier = 0.0f; break;
							default: break;
							}
						}
						else {
							const float progress = (std::clamp)(appearTime / appearDuration, 0.0f, 1.0f);

							if (textEffect->textAppearEffectType == 1) {
								// フェードイン: 0→1
								appearAlphaMultiplier = progress;
							}
							else if (textEffect->textAppearEffectType == 2) {
								// タイプライター: ParamA文字/秒で先頭から表示していく
								const float charCount =
									appearTime * (std::max)(textEffect->textAppearParamA, 0.1f);
								appearVisibleCharacterCount =
									static_cast<size_t>((std::max)(charCount, 0.0f));
							}
							else if (textEffect->textAppearEffectType == 3) {
								// スケールポップ: 0→ParamA倍→1倍とオーバーシュートして収束する
								const float overshoot = (std::max)(textEffect->textAppearParamA, 1.0f);
								appearScaleMultiplier = progress < 0.5f
									? (progress * 2.0f) * overshoot
									: overshoot - (progress - 0.5f) * 2.0f * (overshoot - 1.0f);
							}
						}
					}

					std::string effectLabelBuffer;
					const char* drawLabel = buttonLabel;

					if (appearVisibleCharacterCount != std::string::npos) {
						effectLabelBuffer = uiComponent->buttonLabel.substr(
							0, (std::min)(appearVisibleCharacterCount, uiComponent->buttonLabel.size()));
						drawLabel = effectLabelBuffer.c_str();
					}

					const float fontSize = (std::clamp)(
						resolvedSize.y * fontScale * appearScaleMultiplier,
						0.0f,
						512.0f);
					const float effectiveAlpha = (std::clamp)(renderAlpha * appearAlphaMultiplier, 0.0f, 1.0f);
					ImFont* resolvedFont = ResolveUiFont(uiComponent->textFontIndex);
					const ImVec2 textCenterOffset{
						resolvedSize.x * uiScaleX * 0.5f * (1.0f - appearScaleMultiplier),
						resolvedSize.y * uiScaleY * 0.5f * (1.0f - appearScaleMultiplier)};
					const ImVec2 scaledPosition{
						buttonPosition.x + textCenterOffset.x,
						buttonPosition.y + textCenterOffset.y};

					if (fontSize >= 1.0f && effectiveAlpha > 0.001f) {
						ImDrawList* drawList = ImGui::GetWindowDrawList();
						const int32_t continuousType =
							hasAnyTextEffect ? textEffect->textContinuousEffectType : 0;
						const float continuousElapsed = hasAnyTextEffect
							? (std::max)(textEffect->textEffectRuntimeElapsed - textEffect->textContinuousDelay, 0.0f)
							: 0.0f;

						if (continuousType == 2 || continuousType == 3) {
							// レインボー/波: 文字ごとに個別Positionへ描く
							DrawTextWithPerCharacterEffect(
								drawList,
								resolvedFont,
								fontSize,
								scaledPosition,
								renderColor,
								effectiveAlpha,
								drawLabel,
								continuousType == 2 ? 5 : 6,
								textEffect->textContinuousParamA,
								textEffect->textContinuousParamB,
								textEffect->textContinuousParamC,
								continuousElapsed);
						}
						else if (continuousType != 0) {
							// 点滅/発光/輪郭発光/色収差/微振動/不規則点滅/脈動/残像/簡易グリッチ。
							// DrawTextWithContinuousEffect内部の分岐番号(4,7-14)は、旧textEffectTypeの
							// 番号をそのまま流用しているため、textContinuousEffectType(1,4-11)に+3する。
							DrawTextWithContinuousEffect(
								drawList,
								resolvedFont,
								fontSize,
								scaledPosition,
								renderColor,
								effectiveAlpha,
								drawLabel,
								continuousType + 3,
								textEffect->textContinuousParamA,
								textEffect->textContinuousParamB,
								textEffect->textContinuousParamC,
								continuousElapsed);
						}
						else {
							const ImU32 textColor = ImGui::ColorConvertFloat4ToU32(
								ToImGuiColor(renderColor, effectiveAlpha));
							const ImU32 shadowColor = IM_COL32(0, 0, 0, static_cast<int>(190 * effectiveAlpha));
							drawList->AddText(
								resolvedFont,
								fontSize,
								ImVec2(scaledPosition.x + 2.0f, scaledPosition.y + 2.0f),
								shadowColor,
								drawLabel);
							drawList->AddText(
								resolvedFont,
								fontSize,
								scaledPosition,
								textColor,
								drawLabel);
						}
					}
					if (hasMaskClip) {
						ImGui::PopClipRect();
					}
					continue;
				}

				ImGui::SetCursorScreenPos(buttonPosition);
				ImGui::PushID(uiComponent);
				ImGui::PushStyleColor(ImGuiCol_Button, ToImGuiColor(renderColor, renderAlpha));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ToImGuiColor(renderHoverColor, renderAlpha));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ToImGuiColor(renderPressedColor, renderAlpha));
				ImGui::PushStyleColor(ImGuiCol_FrameBg, ToImGuiColor(renderColor, renderAlpha));
				ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ToImGuiColor(renderHoverColor, renderAlpha));
				ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ToImGuiColor(renderPressedColor, renderAlpha));

				const bool canInteract =
					uiComponent->buttonInteractable &&
					g_editorRuntimeManager.IsPlaying() &&
					CanReceiveUiInput(gameObject);
				if (!canInteract) {
					ImGui::BeginDisabled();
				}

				if (uiComponent->type == EditorComponentType::Button ||
					uiComponent->type == EditorComponentType::SceneButton) {
					const bool isClicked = ImGui::Button(buttonLabel, buttonSize);

					if (isClicked && canInteract && uiComponent->type == EditorComponentType::Button) {
						g_editorRuntimeManager.GetScriptManager().QueueUiEvent(
							gameObject.id,
							uiComponent->buttonOnClickFunction);
					}

					if (isClicked && canInteract && uiComponent->type == EditorComponentType::SceneButton) {
						const EditorComponent* sceneTransition = EditorComponentUtility::FindComponent(
							gameObject,
							EditorComponentType::SceneTransition);
						const bool startedTransition =
							sceneTransition != nullptr &&
							sceneTransition->isActive &&
							g_editorRuntimeManager.StartSceneTransition(gameObject, *sceneTransition);

						if (!startedTransition) {
							g_editorRuntimeManager.RequestSceneLoad(uiComponent->sceneButtonScenePath);
						}
					}
				}

				if (uiComponent->type == EditorComponentType::Toggle) {
					bool toggleValue = uiComponent->toggleValue;
					const bool wasChanged = ImGui::Checkbox(buttonLabel, &toggleValue);
					if (wasChanged && canInteract) {
						uiComponent->toggleValue = toggleValue;
						g_editorRuntimeManager.GetScriptManager().QueueUiEvent(
							gameObject.id,
							uiComponent->toggleOnValueChangedFunction,
							EditorScriptInputValueTypeButton,
							uiComponent->toggleValue ? 1.0f : 0.0f,
							EditorScriptVector2{});
					}
				}

				if (uiComponent->type == EditorComponentType::Slider ||
					uiComponent->type == EditorComponentType::Scrollbar) {
					float sliderMinValue = uiComponent->sliderMinValue;
					float sliderMaxValue = uiComponent->sliderMaxValue;
					if (sliderMaxValue < sliderMinValue) {
						std::swap(sliderMinValue, sliderMaxValue);
					}

					ImGui::TextUnformatted(buttonLabel);
					ImGui::SetNextItemWidth(buttonSize.x);
					float sliderValue = uiComponent->sliderValue;
					const bool wasChanged =
						ImGui::SliderFloat("##Slider", &sliderValue, sliderMinValue, sliderMaxValue);
					if (wasChanged && canInteract) {
						uiComponent->sliderValue = sliderValue;
						g_editorRuntimeManager.GetScriptManager().QueueUiEvent(
							gameObject.id,
							uiComponent->sliderOnValueChangedFunction,
							EditorScriptInputValueTypeVector2,
							0.0f,
							EditorScriptVector2{uiComponent->sliderValue, 0.0f});
					}
				}

				if (uiComponent->type == EditorComponentType::Dropdown ||
					uiComponent->type == EditorComponentType::TMPDropdown) {
					const std::vector<std::string> options = SplitUiOptions(uiComponent->assetPath);
					uiComponent->inputBehavior = (std::clamp)(
						uiComponent->inputBehavior,
						0,
						static_cast<int32_t>(options.size()) - 1);
					ImGui::SetNextItemWidth(buttonSize.x);
					if (ImGui::BeginCombo(buttonLabel, options[static_cast<size_t>(uiComponent->inputBehavior)].c_str())) {
						for (int32_t optionIndex = 0;
							 optionIndex < static_cast<int32_t>(options.size());
							 optionIndex++) {
							const bool isSelected = optionIndex == uiComponent->inputBehavior;
							if (ImGui::Selectable(options[static_cast<size_t>(optionIndex)].c_str(), isSelected) && canInteract) {
								uiComponent->inputBehavior = optionIndex;
								g_editorRuntimeManager.GetScriptManager().QueueUiEvent(
									gameObject.id,
									uiComponent->sliderOnValueChangedFunction,
									EditorScriptInputValueTypeButton,
									static_cast<float>(optionIndex),
									EditorScriptVector2{});
							}
						}
						ImGui::EndCombo();
					}
				}

				if (uiComponent->type == EditorComponentType::InputField ||
					uiComponent->type == EditorComponentType::TMPInputField) {
					char inputBuffer[1024]{};
					const size_t copyLength = (std::min)(uiComponent->buttonLabel.size(), sizeof(inputBuffer) - 1u);
					std::memcpy(inputBuffer, uiComponent->buttonLabel.data(), copyLength);
					ImGui::SetNextItemWidth(buttonSize.x);
					const std::string inputLabel = uiComponent->assetPath.empty()
						? "##InputField"
						: uiComponent->assetPath + "##InputField";
					const bool wasChanged = ImGui::InputText(inputLabel.c_str(), inputBuffer, sizeof(inputBuffer));

					if (wasChanged && canInteract) {
						uiComponent->buttonLabel = inputBuffer;
						g_editorRuntimeManager.GetScriptManager().QueueUiEvent(
							gameObject.id,
							uiComponent->sliderOnValueChangedFunction,
							EditorScriptInputValueTypeButton,
							static_cast<float>(uiComponent->buttonLabel.size()),
							EditorScriptVector2{});
					}
				}

				if (!canInteract) {
					ImGui::EndDisabled();
				}

				ImGui::PopStyleColor(6);
				ImGui::PopID();

				if (hasMaskClip) {
					ImGui::PopClipRect();
				}
			}
		}

		ImGui::PopClipRect();

		Vector3 transitionColor{1.0f, 1.0f, 1.0f};
		float transitionAlpha = 0.0f;
		g_editorRuntimeManager.GetSceneTransitionOverlay(transitionColor, transitionAlpha);

		if (transitionAlpha > 0.001f) {
			ImDrawList* overlayDrawList = ImGui::GetWindowDrawList();
			overlayDrawList->AddRectFilled(
				gameContentPosition,
				ImVec2(gameContentPosition.x + gameWidth, gameContentPosition.y + gameHeight),
				ImGui::ColorConvertFloat4ToU32(ToImGuiColor(transitionColor, transitionAlpha)));
		}
	}
}

void EditorGameViewManager::Initialize() {
}

void EditorGameViewManager::Update() {
}

void EditorGameViewManager::Draw() {
#ifdef USE_IMGUI
	g_isGameViewVisible = false;  // Draw 中に有効な矩形を取れたフレームだけ true にする。

	if (g_isStandaloneGame) {
		g_isSceneViewVisible = false;
		const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
		constexpr ImGuiWindowFlags standaloneWindowFlags =
			ImGuiWindowFlags_NoDecoration |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoBackground |
			ImGuiWindowFlags_NoBringToFrontOnFocus |
			ImGuiWindowFlags_NoNav;
		ImGui::SetNextWindowPos(mainViewport->Pos);
		ImGui::SetNextWindowSize(mainViewport->Size);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin("###StandaloneGameView", nullptr, standaloneWindowFlags);

		const ImVec2 gameContentPosition = ImGui::GetCursorScreenPos();
		const ImVec2 gameContentSize = ImGui::GetContentRegionAvail();
		g_editorGameX = gameContentPosition.x;
		g_editorGameY = gameContentPosition.y;
		g_editorGameWidth = (std::max)(gameContentSize.x, 1.0f);
		g_editorGameHeight = (std::max)(gameContentSize.y, 1.0f);
		g_isGameViewVisible = true;

		UpdateGameCameraMatrices();
		ImGui::Dummy(ImVec2(g_editorGameWidth, g_editorGameHeight));
		DrawGameTrajectoryPreviews(ImGui::GetWindowDrawList());
		DrawGameViewUiControls(
			gameContentPosition,
			g_editorGameWidth,
			g_editorGameHeight);
		ImGui::End();
		ImGui::PopStyleVar();
		return;
	}

	constexpr ImGuiWindowFlags gameWindowFlags =
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoBackground |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse;

	ImGui::SetNextWindowSize(ImVec2(640.0f, 360.0f), ImGuiCond_FirstUseEver);

	if (!ImGui::Begin("ゲーム###GameView", nullptr, gameWindowFlags)) {
		ImGui::End();
		return;
	}

	ImGuiWindow* gameWindow = ImGui::GetCurrentWindowRead();  // gameWindow は Docking タブの表示状態込みで GameView が本当に見えているかを判定する。
	bool isGameDockTabVisible =
		gameWindow != nullptr &&
		!gameWindow->Hidden &&
		!gameWindow->SkipItems &&
		(!gameWindow->DockIsActive || gameWindow->DockTabIsVisible);  // Dock 中は選択中タブだけ描画対象にし、隠れタブの残像描画を防ぐ。
	if (!isGameDockTabVisible) {
		ImGui::End();
		return;
	}

	ImVec2 gameContentPosition = ImGui::GetCursorScreenPos();  // GameView 内で DirectX が描画する左上座標。
	ImVec2 gameContentSize = ImGui::GetContentRegionAvail();  // Docking 後の GameView 描画可能サイズ。

	g_editorGameX = gameContentPosition.x;
	g_editorGameY = gameContentPosition.y;
	g_editorGameWidth = (std::max)(gameContentSize.x, 240.0f);
	g_editorGameHeight = (std::max)(gameContentSize.y, 135.0f);
	g_isGameViewVisible = true;

	UpdateGameCameraMatrices();

	ImDrawList* gameDrawList = ImGui::GetWindowDrawList();  // GameView 上へ枠と状態表示だけ重ねる。
	ImVec2 gameMin{g_editorGameX, g_editorGameY};
	ImVec2 gameMax{g_editorGameX + g_editorGameWidth, g_editorGameY + g_editorGameHeight};
	gameDrawList->AddRect(gameMin, gameMax, IM_COL32(75, 95, 120, 255));

	const char* playStateText = g_editorRuntimeManager.IsPlaying() ? "Play中" : "停止中";
	const char* cameraText = g_isGameViewUsingSceneCamera ? "Camera: Scene カメラ代用" : "Camera: Camera Component";
	gameDrawList->AddRectFilled(
		ImVec2(g_editorGameX + 10.0f, g_editorGameY + 10.0f),
		ImVec2(g_editorGameX + 270.0f, g_editorGameY + 58.0f),
		IM_COL32(16, 22, 30, 185),
		6.0f);
	gameDrawList->AddText(
		ImVec2(g_editorGameX + 20.0f, g_editorGameY + 18.0f),
		IM_COL32(235, 240, 245, 255),
		playStateText);
	gameDrawList->AddText(
		ImVec2(g_editorGameX + 20.0f, g_editorGameY + 38.0f),
		g_isGameViewUsingSceneCamera ? IM_COL32(255, 210, 130, 255) : IM_COL32(170, 215, 255, 255),
		cameraText);

	char gameFpsText[192]{};
	const float gameFrameRate = ImGui::GetIO().Framerate;
	const float gameFrameTimeMilliseconds = gameFrameRate > 0.0f ? 1000.0f / gameFrameRate : 0.0f;
	constexpr double bytesPerMegabyte = 1024.0 * 1024.0;
	const double localVideoMemoryUsageMegabytes =
		static_cast<double>(g_renderProfile.localVideoMemoryUsage) / bytesPerMegabyte;
	const double localVideoMemoryBudgetMegabytes =
		static_cast<double>(g_renderProfile.localVideoMemoryBudget) / bytesPerMegabyte;
	std::snprintf(
		gameFpsText,
		_countof(gameFpsText),
		"%.1f FPS  CPU %.2f ms  GPU %.2f ms\nVRAM %.0f / %.0f MB  Obj %u  Inst %u",
		gameFrameRate,
		gameFrameTimeMilliseconds,
		g_renderProfile.gpuFrameMilliseconds,
		localVideoMemoryUsageMegabytes,
		localVideoMemoryBudgetMegabytes,
		g_renderProfile.sceneObjectCount,
		g_renderProfile.instanceCount);
	const ImVec2 gameFpsTextSize = ImGui::CalcTextSize(gameFpsText);
	const ImVec2 gameFpsTextPosition{
		g_editorGameX + 18.0f,
		g_editorGameY + g_editorGameHeight - gameFpsTextSize.y - 14.0f};
	const ImVec2 gameFpsBackgroundMin{
		gameFpsTextPosition.x - 8.0f,
		gameFpsTextPosition.y - 5.0f};
	const ImVec2 gameFpsBackgroundMax{
		gameFpsTextPosition.x + gameFpsTextSize.x + 8.0f,
		gameFpsTextPosition.y + gameFpsTextSize.y + 5.0f};
	gameDrawList->AddRectFilled(
		gameFpsBackgroundMin,
		gameFpsBackgroundMax,
		IM_COL32(12, 18, 24, 205),
		5.0f);
	gameDrawList->AddText(
		gameFpsTextPosition,
		IM_COL32(210, 245, 210, 255),
		gameFpsText);
	DrawGameTrajectoryPreviews(gameDrawList);

	ImGui::Dummy(ImVec2(g_editorGameWidth, g_editorGameHeight));  // ウィンドウの内容領域を GameView の描画領域として確保する。
	DrawGameViewUiControls(gameContentPosition, g_editorGameWidth, g_editorGameHeight);
	ImGui::End();
#endif
}
