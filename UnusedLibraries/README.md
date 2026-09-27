# UnusedLibraries

CG2 のビルドから参照されていない外部ライブラリの置き場。
`CG2.vcxproj` / `CG2.sln` / `Build/PhysicsSdk/PhysicsSdk.props` の include / lib 設定と、
`Source` / `Tools` / `Assets/Shaders` の include を突き合わせて、参照ゼロだったものを移した。

| 移動元 | 内容 | 判定 |
|---|---|---|
| `ThirdParty/PhysX-3.4` | 旧 PhysX 3.4（約 2.5 GB）| ビルド設定・ソースともに参照なし。現行は `ThirdParty/PhysX` + `ThirdParty/PhysicsSdk` |
| `ThirdParty/imgui` | docking 版でない imgui の旧コピー | 参照なし。現行は `ThirdParty/imgui-docking` |
| `externals/` | AI / DirectXTex / FeelKitHaptics / JoltPhysics / eigen / glm などの控え | `ThirdParty/` と重複。ビルドからの参照なし |

`ThirdParty/imgui-node-editor-master` は移していない。
ノードエディタ本体は未使用だが `crude_json.h` / `crude_json.cpp` を
`AnimationGraph.cpp` `PropertyAnimationClip.cpp` `EffectAsset.cpp` `EffectDefinition.cpp` が使っている。

このフォルダーは `.gitignore` で除外している。
