#include "EditorEffekseerManager.h"

#pragma warning(push, 0)
#include <Windows.h>
#pragma warning(pop)

#include <algorithm>
#include <cctype>

namespace {
	Effekseer::Matrix44 ConvertMatrix(const Matrix4x4& sourceMatrix) {
		Effekseer::Matrix44 destinationMatrix{};

		for (int32_t rowIndex = 0; rowIndex < 4; rowIndex++) {
			for (int32_t columnIndex = 0; columnIndex < 4; columnIndex++) {
				destinationMatrix.Values[rowIndex][columnIndex] = sourceMatrix.matrix[rowIndex][columnIndex];
			}
		}

		return destinationMatrix;
	}

	std::u16string ConvertUtf8ToUtf16(const std::string& text) {
		if (text.empty()) {
			return {};
		}

		const int32_t wideLength = MultiByteToWideChar(
			CP_UTF8,
			MB_ERR_INVALID_CHARS,
			text.c_str(),
			static_cast<int32_t>(text.size()),
			nullptr,
			0);
		if (wideLength <= 0) {
			return {};
		}

		std::wstring wideText(static_cast<size_t>(wideLength), L'\0');
		MultiByteToWideChar(
			CP_UTF8,
			MB_ERR_INVALID_CHARS,
			text.c_str(),
			static_cast<int32_t>(text.size()),
			wideText.data(),
			wideLength);
		return std::u16string(
			reinterpret_cast<const char16_t*>(wideText.data()),
			reinterpret_cast<const char16_t*>(wideText.data()) + wideText.size());
	}
}

bool EditorEffekseerManager::InitializeGraphics(
	ID3D12Device* device,
	ID3D12CommandQueue* commandQueue,
	uint32_t backBufferCount,
	DXGI_FORMAT renderTargetFormat,
	DXGI_FORMAT depthStencilFormat) {
	FinalizeGraphics();

	if (device == nullptr || commandQueue == nullptr || backBufferCount == 0u) {
		return false;
	}

	graphicsDevice_ = EffekseerRendererDX12::CreateGraphicsDevice(
		device,
		commandQueue,
		static_cast<int32_t>(backBufferCount));
	if (graphicsDevice_ == nullptr) {
		return false;
	}

	renderer_ = EffekseerRendererDX12::Create(
		graphicsDevice_,
		&renderTargetFormat,
		1,
		depthStencilFormat,
		false,
		8000);
	if (renderer_ == nullptr) {
		return false;
	}

	memoryPool_ = EffekseerRenderer::CreateSingleFrameMemoryPool(renderer_->GetGraphicsDevice());
	effekseerCommandList_ = EffekseerRenderer::CreateCommandList(renderer_->GetGraphicsDevice(), memoryPool_);
	manager_ = Effekseer::Manager::Create(8000);
	if (memoryPool_ == nullptr || effekseerCommandList_ == nullptr || manager_ == nullptr) {
		return false;
	}

	manager_->SetSpriteRenderer(renderer_->CreateSpriteRenderer());
	manager_->SetRibbonRenderer(renderer_->CreateRibbonRenderer());
	manager_->SetRingRenderer(renderer_->CreateRingRenderer());
	manager_->SetTrackRenderer(renderer_->CreateTrackRenderer());
	manager_->SetModelRenderer(renderer_->CreateModelRenderer());
	manager_->SetTextureLoader(renderer_->CreateTextureLoader());
	manager_->SetModelLoader(renderer_->CreateModelLoader());
	manager_->SetMaterialLoader(renderer_->CreateMaterialLoader());
	manager_->SetCurveLoader(Effekseer::MakeRefPtr<Effekseer::CurveLoader>());
	return true;
}

void EditorEffekseerManager::InitializeScene(
	EditorScene* editorScene,
	std::vector<std::string>* consoleMessages) {
	editorScene_ = editorScene;
	consoleMessages_ = consoleMessages;
}

void EditorEffekseerManager::FinalizeGraphics() {
	Stop();
	effectCache_.clear();
	manager_.Reset();
	effekseerCommandList_.Reset();
	memoryPool_.Reset();
	renderer_.Reset();
	graphicsDevice_.Reset();
	hasPreparedDrawFrame_ = false;
}

void EditorEffekseerManager::Start() {
	Stop();
	isPlaying_ = true;
	elapsedTime_ = 0.0f;
	hasPreparedDrawFrame_ = false;

	if (editorScene_ == nullptr || manager_ == nullptr) {
		return;
	}

	for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		if (!gameObject.isActive) {
			continue;
		}

		for (const EditorComponent& component : gameObject.components) {
			if (component.isActive && component.animationPlayOnAwake) {
				PlayComponent(gameObject, component);
			}
		}
	}
}

void EditorEffekseerManager::Update(float deltaTime) {
	hasPreparedDrawFrame_ = false;

	if (!isPlaying_ || manager_ == nullptr || deltaTime <= 0.0f) {
		return;
	}

	elapsedTime_ += deltaTime;
	SynchronizeInstances();
	Effekseer::Manager::UpdateParameter updateParameter{};
	updateParameter.DeltaFrame = deltaTime * 60.0f;
	manager_->Update(updateParameter);

	for (auto& instancePair : instances_) {
		EffectInstance& instance = instancePair.second;

		if (manager_->Exists(instance.handle) || !instance.isLooping || editorScene_ == nullptr) {
			continue;
		}

		const EditorGameObject* gameObject = editorScene_->FindGameObject(instance.gameObjectId);
		if (gameObject == nullptr) {
			continue;
		}

		for (const EditorComponent& component : gameObject->components) {
			if (MakeComponentKey(gameObject->id, component.type) == instance.componentKey) {
				PlayComponent(*gameObject, component);
				break;
			}
		}
	}
}

void EditorEffekseerManager::Stop() {
	if (manager_ != nullptr) {
		manager_->StopAllEffects();
	}

	instances_.clear();
	elapsedTime_ = 0.0f;
	isPlaying_ = false;
}

void EditorEffekseerManager::Draw(
	ID3D12GraphicsCommandList* commandList,
	const Matrix4x4& viewMatrix,
	const Matrix4x4& projectionMatrix,
	const Vector3& cameraPosition) {
	if (!isPlaying_ || commandList == nullptr || manager_ == nullptr || renderer_ == nullptr || instances_.empty()) {
		return;
	}

	if (!hasPreparedDrawFrame_) {
		memoryPool_->NewFrame();
		hasPreparedDrawFrame_ = true;
	}
	EffekseerRendererDX12::BeginCommandList(effekseerCommandList_, commandList);
	renderer_->SetCommandList(effekseerCommandList_);
	renderer_->SetTime(elapsedTime_);
	renderer_->SetCameraMatrix(ConvertMatrix(viewMatrix));
	renderer_->SetProjectionMatrix(ConvertMatrix(projectionMatrix));
	Effekseer::Manager::LayerParameter layerParameter{};
	layerParameter.ViewerPosition = {cameraPosition.x, cameraPosition.y, cameraPosition.z};
	manager_->SetLayerParameter(0, layerParameter);
	renderer_->BeginRendering();
	Effekseer::Manager::DrawParameter drawParameter{};
	drawParameter.ZNear = 0.0f;
	drawParameter.ZFar = 1.0f;
	drawParameter.ViewProjectionMatrix = renderer_->GetCameraProjectionMatrix();
	manager_->Draw(drawParameter);
	renderer_->EndRendering();
	renderer_->SetCommandList(nullptr);
	EffekseerRendererDX12::EndCommandList(effekseerCommandList_);
}

bool EditorEffekseerManager::PlayEffect(int32_t gameObjectId) {
	if (!isPlaying_ || editorScene_ == nullptr || manager_ == nullptr) {
		return false;
	}

	const EditorGameObject* gameObject = editorScene_->FindGameObject(gameObjectId);
	if (gameObject == nullptr) {
		return false;
	}

	bool hasPlayed = false;
	for (const EditorComponent& component : gameObject->components) {
		hasPlayed = PlayComponent(*gameObject, component) || hasPlayed;
	}
	return hasPlayed;
}

void EditorEffekseerManager::StopEffect(int32_t gameObjectId) {
	if (manager_ == nullptr) {
		return;
	}

	for (auto& instancePair : instances_) {
		EffectInstance& instance = instancePair.second;

		if (instance.gameObjectId == gameObjectId && manager_->Exists(instance.handle)) {
			manager_->StopEffect(instance.handle);
		}
	}
}

bool EditorEffekseerManager::IsEffectPlaying(int32_t gameObjectId) const {
	return GetAliveEffectCount(gameObjectId) > 0;
}

int32_t EditorEffekseerManager::GetAliveEffectCount(int32_t gameObjectId) const {
	if (manager_ == nullptr) {
		return 0;
	}

	int32_t aliveCount = 0;
	for (const auto& instancePair : instances_) {
		const EffectInstance& instance = instancePair.second;

		if (instance.gameObjectId == gameObjectId && manager_->Exists(instance.handle)) {
			aliveCount++;
		}
	}
	return aliveCount;
}

bool EditorEffekseerManager::IsEffekseerAssetPath(const std::string& assetPath) {
	const size_t extensionOffset = assetPath.find_last_of('.');
	if (extensionOffset == std::string::npos) {
		return false;
	}

	std::string extension = assetPath.substr(extensionOffset);
	std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
		return static_cast<char>(std::tolower(character));
	});
	return extension == ".efk" || extension == ".efkefc";
}

uint64_t EditorEffekseerManager::MakeComponentKey(
	int32_t gameObjectId,
	EditorComponentType componentType) {
	const uint64_t ownerValue = static_cast<uint64_t>(static_cast<uint32_t>(gameObjectId));
	const uint64_t componentValue = static_cast<uint64_t>(static_cast<uint32_t>(componentType));
	return (ownerValue << 32u) | componentValue;
}

Effekseer::EffectRef EditorEffekseerManager::LoadEffect(const std::string& assetPath) {
	const auto cachedEffectIterator = effectCache_.find(assetPath);
	if (cachedEffectIterator != effectCache_.end()) {
		return cachedEffectIterator->second;
	}

	const std::u16string utf16Path = ConvertUtf8ToUtf16(assetPath);
	if (utf16Path.empty()) {
		PushConsoleMessage("Effekseer: Asset パスを UTF-16 へ変換できません: " + assetPath);
		return nullptr;
	}

	Effekseer::EffectRef effect = Effekseer::Effect::Create(manager_, utf16Path.c_str());
	if (effect == nullptr) {
		PushConsoleMessage("Effekseer: .efk / .efkefc を読み込めません: " + assetPath);
		return nullptr;
	}

	effectCache_.emplace(assetPath, effect);
	return effect;
}

bool EditorEffekseerManager::PlayComponent(
	const EditorGameObject& gameObject,
	const EditorComponent& component) {
	const bool isEffectComponent =
		component.type == EditorComponentType::ParticleSystem ||
		component.type == EditorComponentType::VisualEffect;
	if (!component.isActive || !isEffectComponent || !IsEffekseerAssetPath(component.assetPath) || manager_ == nullptr) {
		return false;
	}

	Effekseer::EffectRef effect = LoadEffect(component.assetPath);
	if (effect == nullptr) {
		return false;
	}

	const uint64_t componentKey = MakeComponentKey(gameObject.id, component.type);
	auto existingInstanceIterator = instances_.find(componentKey);
	if (existingInstanceIterator != instances_.end() && manager_->Exists(existingInstanceIterator->second.handle)) {
		manager_->StopEffect(existingInstanceIterator->second.handle);
	}

	EffectInstance instance{};
	instance.componentKey = componentKey;
	instance.gameObjectId = gameObject.id;
	instance.handle = manager_->Play(effect, gameObject.translate.x, gameObject.translate.y, gameObject.translate.z);
	instance.assetPath = component.assetPath;
	instance.isLooping = component.particleLooping;
	instances_[componentKey] = instance;
	SynchronizeInstances();
	return manager_->Exists(instance.handle);
}

void EditorEffekseerManager::SynchronizeInstances() {
	if (editorScene_ == nullptr || manager_ == nullptr) {
		return;
	}

	for (auto& instancePair : instances_) {
		EffectInstance& instance = instancePair.second;
		if (!manager_->Exists(instance.handle)) {
			continue;
		}

		const EditorGameObject* gameObject = editorScene_->FindGameObject(instance.gameObjectId);
		if (gameObject == nullptr) {
			manager_->StopEffect(instance.handle);
			continue;
		}

		manager_->SetLocation(instance.handle, gameObject->translate.x, gameObject->translate.y, gameObject->translate.z);
		manager_->SetRotation(instance.handle, gameObject->rotate.x, gameObject->rotate.y, gameObject->rotate.z);
		manager_->SetScale(instance.handle, gameObject->scale.x, gameObject->scale.y, gameObject->scale.z);
	}
}

void EditorEffekseerManager::PushConsoleMessage(const std::string& message) {
	if (consoleMessages_ != nullptr) {
		consoleMessages_->push_back(message);
	}
}
