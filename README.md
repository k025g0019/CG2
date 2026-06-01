# CG2



### 1. 三角形を1つ以上表示し、ImGuiで色を変える

- 該当行: `main.cpp:791-792`
- 内容: ImGui の設定ウィンドウを作り、`ColorEdit4` で三角錐の色を変更できるようにしています。

```cpp
ImGui::Begin("Triangle Settings");
ImGui::ColorEdit4("三角形の色", triangleColor);
```

### 2. 三角形のSRTをImGuiで変更する

- 該当行: `main.cpp:631-636`
- 内容: ImGuiで追加操作するためのSRT変数です。初期値は演出を変えない値にしています。

```cpp
Transforms triangleSrt{
    .scale = {1.0f, 1.0f, 1.0f},
    .rotate = {0.0f, 0.0f, 0.0f},
    .translate = {0.0f, 0.0f, 0.0f}
};
```

- 該当行: `main.cpp:793-795`
- 内容: ImGuiから移動・回転・拡大縮小を変更できます。

```cpp
ImGui::DragFloat3("Translate", &triangleSrt.translate.x, 0.01f);
ImGui::DragFloat3("Rotate", &triangleSrt.rotate.x, 0.01f);
ImGui::DragFloat3("Scale", &triangleSrt.scale.x, 0.01f, 0.01f, 10.0f);
```

- 該当行: `main.cpp:823-842`
- 内容: 元の演出用 `transform` に、ImGui用 `triangleSrt` を足して描画用の `renderTransform` を作っています。

```cpp
Transforms renderTransform{
    .scale = {
        transform.scale.x * triangleSrt.scale.x,
        transform.scale.y * triangleSrt.scale.y,
        transform.scale.z * triangleSrt.scale.z
    },
    .rotate = {
        transform.rotate.x + triangleSrt.rotate.x,
        transform.rotate.y + triangleSrt.rotate.y,
        transform.rotate.z + triangleSrt.rotate.z
    },
    .translate = {
        transform.translate.x + triangleSrt.translate.x,
        transform.translate.y + triangleSrt.translate.y,
        transform.translate.z + triangleSrt.translate.z
    }
};
```

### 3. 元の接近と回転演出を変えていない部分

- 該当行: `main.cpp:808-815`
- 内容: 三角錐が手前へ来て、その後 `0,0,0` で回転する処理です。

```cpp
if (transform.translate.z > -5.0f) {
    speed *= 1.03f;
    transform.translate.z += -speed;
} else {
    transform.translate.z = 0;
    transform.rotate.z += 0.01f;
    transform.rotate.x += 0.005f;
}
```

### 4. Textureを貼る

- 該当行: `main.cpp:602-627`
- 内容: 三角錐の各頂点にUVを入れています。

```cpp
{{-0.5f, -0.5f, 0.0f, 1.0f}, {0.0f, 1.0f}},
{{0.0f, 0.0f, -1.0f, 1.0f}, {0.5f, 0.0f}},
{{0.5f, -0.5f, 0.0f, 1.0f}, {1.0f, 1.0f}},
```

- 該当行: `Object3d.PS.hlsl:7-8`
- 内容: PixelShaderでTextureとSamplerを受け取っています。

```hlsl
Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);
```

- 該当行: `Object3d.PS.hlsl:24`
- 内容: TextureにImGuiの色を掛けて出力しています。

```hlsl
output.color = gTexture.Sample(gSampler, input.texcoord) * gMaterial.color;
```

### 5. Textureを手動で切り替える

- 該当行: `main.cpp:665-684`
- 内容: `uvChecker` と赤白Textureを用意しています。

```cpp
DirectX::ScratchImage uvCheckerMipImages = LoadTexture(L"resources/uvChecker.png");
DirectX::ScratchImage redWhiteMipImages{};
```

- 該当行: `main.cpp:801`
- 内容: ImGuiのComboでTextureを手動切り替えできます。自動切り替えは入れていません。

```cpp
ImGui::Combo("Texture", &selectedTextureIndex, textureNames, _countof(textureNames));
```

- 該当行: `main.cpp:872`
- 内容: 選択したTextureのSRVを描画に使っています。

```cpp
commandList->SetGraphicsRootDescriptorTable(
    2, textureSrvHandlesGPU[static_cast<size_t>(selectedTextureIndex)]);
```

### 6. DepthBufferを利用する

- 該当行: `main.cpp:589-591`
- 内容: DepthBufferを有効にして、前後関係を見られるようにしています。

```cpp
graphicsPipelineStateDesc.DepthStencilState.DepthEnable = true;
graphicsPipelineStateDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
graphicsPipelineStateDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
```

### 演出

- 内容:三角錐にすることでコストをかけずに演出としてよいものにした
