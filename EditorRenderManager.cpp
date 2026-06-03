#pragma warning(disable : 4189 4514)

#include "EditorRenderManager.h"

#include "EditorSharedState.h"

using namespace EditorSharedState;

void EditorRenderManager::Initialize() {
}

void EditorRenderManager::Update() {
}

void EditorRenderManager::Draw() {
	auto& hr = g_hr;  // hr は DirectX API の成否を受け取る共有 HRESULT。
	auto& device = g_device;  // device は今後の描画拡張で Resource を追加作成する時のために参照する。
	auto& commandQueue = g_commandQueue;  // commandQueue / Allocator / List は GPU へ描画命令を送るために使う。
	auto& commandAllocator = g_commandAllocator;
	auto& commandList = g_commandList;

	auto& swapChain = g_swapChain;  // swapChain は描画した back buffer を Window に Present するために使う。
	auto& srvDescriptorHeap = g_srvDescriptorHeap;  // srvDescriptorHeap は Texture SRV と ImGui SRV を Shader へ渡す DescriptorHeap。
	auto& swapChainResources = g_swapChainResources;  // swapChainResources は Barrier 対象の back buffer 実体。
	auto& rtvHandles = g_rtvHandles;  // rtvHandles / dsvHandle は RenderTarget と DepthStencil を OMSetRenderTargets に渡す。
	auto& dsvHandle = g_dsvHandle;

	auto& rootSignature = g_rootSignature;  // rootSignature / graphicsPipelineState は Shader と RenderState の固定設定。
	auto& graphicsPipelineState = g_graphicsPipelineState;

	auto& spriteMaterialResource = g_spriteMaterialResource;  // spriteMaterialResource / sphereMaterialResource は PixelShader の Material CBV。
	auto& spriteMaterialData = g_spriteMaterialData;
	auto& sphereMaterialResource = g_sphereMaterialResource;
	auto& sphereMaterialData = g_sphereMaterialData;

	auto& directionalLightResource = g_directionalLightResource;  // directionalLightResource は PixelShader の平行光源 CBV。
	auto& spriteTransformationMatrixData = g_spriteTransformationMatrixData;  // spriteTransformationMatrixData は旧 Sprite プレビュー用 WVP / World の書き込み先。
	auto& sphereTransformationMatrixResource = g_sphereTransformationMatrixResource;  // sphereTransformationMatrixResource / Data は旧 3D プレビュー用 WVP / World。
	auto& sphereTransformationMatrixData = g_sphereTransformationMatrixData;

	auto& modelData = g_modelData;  // modelData は plane.obj の頂点数を DrawInstanced に渡すために使う。
	auto& primitiveVertexBufferViews = g_editorPrimitiveVertexBufferViews;  // primitiveVertexBufferViews は基本形ごとの GPU 頂点 Buffer。
	auto& primitiveVertexCounts = g_editorPrimitiveVertexCounts;  // primitiveVertexCounts は基本形ごとの DrawInstanced 頂点数。
	auto& spriteIndices = g_spriteIndices;  // spriteIndices は Sprite の DrawIndexedInstanced の index 数に使う。
	auto& transform = g_transform;  // transform / spriteTransform / cameraTransform / uvTransform は今フレームの行列作成元。
	auto& spriteTransform = g_spriteTransform;
	auto& cameraTransform = g_cameraTransform;
	auto& uvTransform = g_uvTransform;

	auto& modelVertexBufferView = g_modelVertexBufferView;  // model / sprite BufferView は IASetVertexBuffers に渡す GPU 頂点情報。
	auto& spriteVertexBufferView = g_spriteVertexBufferView;

	auto& spriteIndexBufferView = g_spriteIndexBufferView;  // spriteIndexBufferView は Sprite 四角形の IndexBuffer。
	auto& viewport = g_viewport;  // viewport / scissorRect は SceneView 内だけへ描くための描画矩形。
	auto& scissorRect = g_scissorRect;

	auto& cameraMatrix = g_cameraMatrix;  // camera/view/projection 行列は 3D モデルを SceneView に投影するために使う。
	auto& viewMatrix = g_viewMatrix;
	auto& projectionMatrix = g_projectionMatrix;

	auto& spriteProjectionMatrix = g_spriteProjectionMatrix;  // spriteProjectionMatrix は Sprite を 2D 座標で描くための正射影行列。
	auto& sceneClearColor = g_sceneClearColor;  // sceneClearColor は RenderTarget を塗りつぶす背景色。
	auto& editorSceneObjects = g_editorSceneObjectManager.GetSceneObjects();  // editorSceneObjects は配置済み GameObject に対応する DirectX 描画対象。
	auto& fence = g_fence;  // fence / fenceValue / fenceEvent は Present 後に GPU 完了を待つための同期一式。
	auto& fenceValue = g_fenceValue;
	auto& fenceEvent = g_fenceEvent;

	auto& textureFilePaths = g_textureFilePaths;  // textureFilePaths は Texture 数の上限判定に使う。
	auto& textureSrvHandlesGPU = g_textureSrvHandlesGPU;  // textureSrvHandlesGPU は Shader に Texture SRV を渡す GPU ハンドル。
	auto& isLegacyPreviewVisible = g_isLegacyPreviewVisible;  // isLegacyPreviewVisible は旧モデルプレビューを描くかどうかのフラグ。

	// 初期化前、終了後、ImGui::Render 前のフレームでは GPU 描画を行わない。
	if (!g_isInitialized || g_isFinalized || !g_isDrawRequested) {
		return;
	}

	cameraMatrix = MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);  // cameraMatrix は SceneView カメラの Transform から作るワールド行列。
	viewMatrix = Inverse(cameraMatrix);  // viewMatrix は cameraMatrix の逆行列。3D モデルをカメラ空間へ移す。

	// spriteWorldMatrix は旧 Sprite プレビューの Transform を行列化したもの。
	Matrix4x4 spriteWorldMatrix = MakeAffineMatrix(
		spriteTransform.scale,
		spriteTransform.rotate,
		spriteTransform.translate);

	Matrix4x4 spriteWorldViewProjectionMatrix = Multiply(spriteWorldMatrix, spriteProjectionMatrix);  // spriteWorldViewProjectionMatrix は Sprite の World と 2D 正射影を合成した WVP。
	Matrix4x4 worldMatrix = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);  // worldMatrix は旧 3D モデルプレビューの Transform を行列化したもの。
	Matrix4x4 worldViewProjectionMatrix = Multiply(worldMatrix, Multiply(viewMatrix, projectionMatrix));  // worldViewProjectionMatrix は 3D モデルを SceneView へ投影する WVP。
	Matrix4x4 uvTransformMatrix = MakeAffineMatrix(uvTransform.scale, uvTransform.rotate, uvTransform.translate);  // uvTransformMatrix は Material へ渡す UV 変換行列。
	spriteTransformationMatrixData->WVP = spriteWorldViewProjectionMatrix;  // 旧プレビュー用の定数バッファへ今フレームの行列を書き込む。
	spriteTransformationMatrixData->World = spriteWorldMatrix;
	sphereTransformationMatrixData->WVP = worldViewProjectionMatrix;
	sphereTransformationMatrixData->World = worldMatrix;

	spriteMaterialData->uvTransform = uvTransformMatrix;  // Sprite と 3D モデルの Material に同じ UV 変換を反映する。
	sphereMaterialData->uvTransform = uvTransformMatrix;

	for (EditorSceneObject& sceneObject : editorSceneObjects) {
		// sceneObjectWorldMatrix は配置済み GameObject の Transform を行列化したもの。
		Matrix4x4 sceneObjectWorldMatrix = MakeAffineMatrix(
			sceneObject.transform.scale,
			sceneObject.transform.rotate,
			sceneObject.transform.translate);

		// Sprite は 2D 正射影、Model は SceneView の ViewProjection を使って描画空間へ送る。
		Matrix4x4 sceneObjectProjectionMatrix =
			sceneObject.type == EditorSceneObjectType::Sprite
				? spriteProjectionMatrix
				: Multiply(viewMatrix, projectionMatrix);

		if (sceneObject.transformationData != nullptr) {
			sceneObject.transformationData->WVP = Multiply(sceneObjectWorldMatrix, sceneObjectProjectionMatrix);  // SceneView 用定数バッファへ WVP と World を書き込む。
			sceneObject.transformationData->World = sceneObjectWorldMatrix;
		}

		// GameView は Camera Component の ViewProjection を使う。Sprite は画面座標表示なので同じ正射影を使う。
		Matrix4x4 gameObjectProjectionMatrix =
			sceneObject.type == EditorSceneObjectType::Sprite
				? spriteProjectionMatrix
				: Multiply(g_gameViewMatrix, g_gameProjectionMatrix);

		if (sceneObject.gameTransformationData != nullptr) {
			sceneObject.gameTransformationData->WVP = Multiply(sceneObjectWorldMatrix, gameObjectProjectionMatrix);
			sceneObject.gameTransformationData->World = sceneObjectWorldMatrix;
		}

		if (sceneObject.materialData != nullptr) {
			sceneObject.materialData->uvTransform = uvTransformMatrix;  // Renderer ごとの色は Synchronizer、UV だけは共通設定から毎フレーム反映する。
		}
	}

	hr = commandAllocator->Reset();  // CommandAllocator / CommandList を今フレームの描画記録用に Reset する。
	assert(SUCCEEDED(hr));
	hr = commandList->Reset(commandAllocator.Get(), graphicsPipelineState.Get());
	assert(SUCCEEDED(hr));

	UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();  // backBufferIndex は今回描画する SwapChain buffer の番号。

	// barrier は back buffer を Present 状態から RenderTarget 状態へ切り替える命令。
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = swapChainResources[backBufferIndex];
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	commandList->ResourceBarrier(1, &barrier);

	commandList->SetGraphicsRootSignature(rootSignature.Get());  // Shader Resource の入口、PSO、ライト、Texture Heap、三角形描画形式を設定する。
	commandList->SetPipelineState(graphicsPipelineState.Get());
	commandList->SetGraphicsRootConstantBufferView(2, directionalLightResource->GetGPUVirtualAddress());
	ID3D12DescriptorHeap* descriptorHeaps[] = {srvDescriptorHeap};
	commandList->SetDescriptorHeaps(1, descriptorHeaps);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], FALSE, &dsvHandle);  // back buffer と DepthStencil を今回の描画先として設定する。
	commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], sceneClearColor, 0, nullptr);  // Scene 背景色で RenderTarget をクリアし、Depth は一番奥の 1.0f に戻す。
	commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	auto drawSceneObjects = [&](bool isGameViewPass) {
		for (const EditorSceneObject& sceneObject : editorSceneObjects) {
			ID3D12Resource* transformationResource = isGameViewPass
				                                         ? sceneObject.gameTransformationResource
				                                         : sceneObject.transformationResource;
			if (transformationResource == nullptr) {
				continue;
			}

			ID3D12Resource* materialResource = sceneObject.materialResource;  // GameObject ごとの Renderer 色を PixelShader へ渡す。
			if (materialResource == nullptr) {
				materialResource = sceneObject.type == EditorSceneObjectType::Sprite
					                   ? spriteMaterialResource
					                   : sphereMaterialResource;
			}

			// Sprite は個別 Texture と IndexBuffer 付き四角形で描く。
			if (sceneObject.type == EditorSceneObjectType::Sprite) {
				// textureIndex は不正値が来ても SRV 配列外を読まないように範囲内へ丸める。
				int32_t textureIndex =
					(std::clamp)(sceneObject.textureIndex, 0, static_cast<int32_t>(_countof(textureFilePaths)) - 1);
				commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());
				commandList->SetGraphicsRootConstantBufferView(
					1,
					transformationResource->GetGPUVirtualAddress());
				commandList->SetGraphicsRootDescriptorTable(3, textureSrvHandlesGPU[textureIndex]);
				commandList->IASetVertexBuffers(0, 1, &spriteVertexBufferView);
				commandList->IASetIndexBuffer(&spriteIndexBufferView);
				commandList->DrawIndexedInstanced(_countof(spriteIndices), 1, 0, 0, 0);
			} else {
				size_t meshTypeIndex = static_cast<size_t>(sceneObject.meshType);  // meshTypeIndex は SceneObject が要求する基本形メッシュ番号。
				if (meshTypeIndex >= kEditorModelMeshTypeCount ||
					primitiveVertexCounts[meshTypeIndex] == 0u) {
					meshTypeIndex = static_cast<size_t>(EditorModelMeshType::Plane);
				}

				commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());  // Model は meshType ごとの VertexBuffer で描く。
				commandList->SetGraphicsRootConstantBufferView(
					1,
					transformationResource->GetGPUVirtualAddress());
				commandList->SetGraphicsRootDescriptorTable(3, textureSrvHandlesGPU[2]);
				if (sceneObject.usesCustomMesh &&
					sceneObject.customMeshVertexResource != nullptr &&
					sceneObject.customMeshVertexCount > 0u) {
					commandList->IASetVertexBuffers(0, 1, &sceneObject.customMeshVertexBufferView);
					commandList->DrawInstanced(sceneObject.customMeshVertexCount, 1, 0, 0);
				}
				else {
					commandList->IASetVertexBuffers(0, 1, &primitiveVertexBufferViews[meshTypeIndex]);
					commandList->DrawInstanced(primitiveVertexCounts[meshTypeIndex], 1, 0, 0);
				}
			}
		}
	};

	commandList->RSSetViewports(1, &viewport);  // Rasterizer に SceneView の描画範囲と切り取り範囲を渡す。
	commandList->RSSetScissorRects(1, &scissorRect);

	if (isLegacyPreviewVisible) {
		commandList->SetGraphicsRootConstantBufferView(0, sphereMaterialResource->GetGPUVirtualAddress());  // 旧 3D プレビューを表示する場合だけ、plane.obj を固定 Texture で描く。
		commandList->SetGraphicsRootConstantBufferView(
			1,
			sphereTransformationMatrixResource->GetGPUVirtualAddress());
		commandList->SetGraphicsRootDescriptorTable(3, textureSrvHandlesGPU[2]);
		commandList->IASetVertexBuffers(0, 1, &modelVertexBufferView);
		commandList->DrawInstanced(static_cast<UINT>(modelData.vertices.size()), 1, 0, 0);
	}

	drawSceneObjects(false);

	if (g_isGameViewVisible) {
		// GameView 用矩形へ同じ SceneObject を Camera Component の行列で再描画する。
		D3D12_VIEWPORT gameViewport{};
		gameViewport.TopLeftX = g_editorGameX;
		gameViewport.TopLeftY = g_editorGameY;
		gameViewport.Width = g_editorGameWidth;
		gameViewport.Height = g_editorGameHeight;
		gameViewport.MinDepth = 0.0f;
		gameViewport.MaxDepth = 1.0f;

		D3D12_RECT gameScissorRect{};
		gameScissorRect.left = static_cast<LONG>(g_editorGameX);
		gameScissorRect.top = static_cast<LONG>(g_editorGameY);
		gameScissorRect.right = static_cast<LONG>(g_editorGameX + g_editorGameWidth);
		gameScissorRect.bottom = static_cast<LONG>(g_editorGameY + g_editorGameHeight);

		commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
		commandList->RSSetViewports(1, &gameViewport);
		commandList->RSSetScissorRects(1, &gameScissorRect);
		drawSceneObjects(true);
	}
#ifdef USE_IMGUI
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList.Get());  // ImGui の確定済み DrawData を同じ back buffer に重ねて描画する。

#endif

	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;  // back buffer を RenderTarget 状態から Present 状態へ戻す。
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	commandList->ResourceBarrier(1, &barrier);

	hr = commandList->Close();  // CommandList を閉じ、GPU に実行できる状態へ確定する。
	assert(SUCCEEDED(hr));

	// commandLists は ExecuteCommandLists に渡す描画 CommandList 配列。
	ID3D12CommandList* commandLists[] = {commandList.Get()};
	commandQueue->ExecuteCommandLists(1, commandLists);

	hr = swapChain->Present(1, 0);  // Present は描画済み back buffer を Window へ表示する。
	assert(SUCCEEDED(hr));

	fenceValue++;  // fenceValue を進め、今回の描画完了位置を GPU に記録する。
	hr = commandQueue->Signal(fence.Get(), fenceValue);
	assert(SUCCEEDED(hr));

	// GPU が今回の描画を終えるまで待ち、次フレームでリソースを書き換えても安全にする。
	if (fence->GetCompletedValue() < fenceValue) {
		hr = fence->SetEventOnCompletion(fenceValue, fenceEvent);
		assert(SUCCEEDED(hr));
		if (fenceEvent != nullptr) {
			WaitForSingleObject(fenceEvent, INFINITE);
		}
	}

	g_isDrawRequested = false;  // 今フレームの描画要求を消費したので、次の ImGui::Render まで Renderer を止める。
}
