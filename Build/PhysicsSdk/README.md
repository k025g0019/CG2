# PhysX / Blast SDK の導入

CG2.vcxproj は `PhysicsSdk.props` を読み込み、Debug / Release x64 で SDK のヘッダーとライブラリを使用します。実行用 DLL はビルド後に EXE の隣へコピーします。ゲーム書き出し時は EditorGameBuildManager の既存 DLL コピー処理が引き継ぎます。

## 導入した範囲

| SDK | 対象 |
| --- | --- |
| PhysX 5.10.0 (`5.10.0.75b25f28f9`) | CPU / GPU simulation、Foundation、Common、Cooking、Extensions、PVD SDK、Character controller、Vehicle |
| Blast 1.1.5 | Low-level API (`NvBlast.h` / `NvBlast.dll`)、asset / family / actor、結合破壊・分離 |

SDK 導入であり、既存の Collider / Rigidbody は引き続き Jolt で処理します。Blast の authoring / toolkit / ExtPhysX、Inspector の破壊コンポーネントや Script API は今回の対象外です。

Blast 1.1.5 の `target_platform_deps.xml` は PhysX **3.4.24990349** を要求します。このため旧 ExtPhysX を PhysX 5.10 へ直接リンクせず、物理エンジン非依存の low-level API を組み込みます。PhysX と破壊片を連動させるには、生成した Blast actor に対応する PhysX rigid body / 描画メッシュを管理する実装が必要です。

参考: [Blast の公式説明](https://github.com/NVIDIAGameWorks/Blast)、[PhysX 公式リポジトリ](https://github.com/NVIDIA-Omniverse/PhysX)。

## セットアップ

Visual Studio 2022 の C++ x64 ツール、Windows SDK、CMake、CUDA Toolkit 13.0 以降が必要です。GPU 実動作テストには対応 NVIDIA GPU とドライバーも必要です。CG2 のルートで実行します。

```powershell
powershell -File Build\PhysicsSdk\Setup.ps1
```

既定の入力パスは、今回確認した次の実在フォルダーです。

- `C:\Users\shota\Downloads\PhysX-main\PhysX-main`
- `C:\Users\shota\Downloads\Blast-1.1.5_release\Blast-1.1.5_release`

別の場所の場合は `-PhysXSource` / `-BlastSource` に、直下に `physx` / `sdk` があるディレクトリを指定します。ユーザー名は既定で USERPROFILE から取得します。

配置済みのソースから再ビルドする場合:

```powershell
powershell -File Build\PhysicsSdk\Setup.ps1 -SkipCopy
```

ソースは `ThirdParty\PhysX` と `ThirdParty\Blast-1.1.5`、ライブラリは `ThirdParty\PhysicsSdk\lib\debug|release`、DLL は `ThirdParty\PhysicsSdk\bin\debug|release` に配置します。元の Downloads のファイルは変更しません。ライセンス原文も各 SDK ディレクトリへコピーします。ThirdParty は既存の .gitignore で除外されるため、別 PC ではセットアップが必要です。

## 使用例

```cpp
#include <PxPhysicsAPI.h>
#include <NvBlast.h>
```

初期化・解放や API の具体例は `Smoke.cpp` を参照してください。これは独立したコンソール検証プログラムで、CG2 のゲーム処理としては実行されません。

## 検証

セットアップは両構成をビルドし、CTest で次の実動作を検証します。

- PhysX: 高さ 2.0 の球を 120 ステップ落下させ、床との衝突後に高さ 0.5 付近で停止する。
- Blast: 2 support chunks の bond を破壊し、2 actors に分離する。

CPU 版では 2026-09-03 に Debug / Release の両構成で成功しました。実測の球中心高さは両方とも 0.5、Blast 分離数は両方とも 2 でした。エディター Play での PhysX / Blast 連動は実装・検証していません。

CG2 本体の Release x64 ビルドも成功し、x64\Release に PhysX の DLL（GPU 版を含む）と NvBlast.dll の配置を確認しました。

## GPU 対応と CPU / GPU の選択

同じ SDK で PhysX Scene の生成時に CPU / GPU を選べます。既存 Scene の実行途中でフラグを変更する方式ではなく、変更時は Scene の再生成が必要です。CG2 の Inspector に選択 UI を追加したものではありません。

GPU を使う Scene では CUDA context manager を作成し、次を設定します。CPU Scene ではこの設定と CUDA 初期化を行いません。実装例は `Smoke.cpp` の `CheckPhysX(bool useGpu)` です。

```cpp
sceneDesc.cudaContextManager = cudaContext.get();
sceneDesc.flags |= physx::PxSceneFlag::eENABLE_GPU_DYNAMICS;
sceneDesc.flags |= physx::PxSceneFlag::eENABLE_PCM;
sceneDesc.broadPhaseType = physx::PxBroadPhaseType::eGPU;
```

`PhysXGpu_64.dll` も EXE の隣へ配置します。CG2 自体は GPU DLL を直接リンクせず、PhysX が GPU 初期化時に動的ロードします。配布先への CUDA Toolkit インストールは不要です。

CUDA 13 で削除された sm_70 を除くため、公式の reduced architectures 設定を使います。生成構成は CMake の sm_75 と公式の sm_80 / 86 / 89 / 90 / 100 / 120、将来 GPU 用 PTX を含みます。

GPU テストは `CG2PhysicsSdkSmoke.exe --gpu` で実行します。CUDA context の有効性、GPU シーンフラグ、GPU broad phase、実際の GPU heap 使用量、球の衝突結果を検証します。CPU へのフォールバックは失敗扱いです。

GPU のないビルド専用 PC では `Setup.ps1 -SkipGpuTest` を指定して GPU 実動作テストだけを省略できます。GPU ライブラリのビルドは行い、テスト省略を明示します。

CUDA 13 対応として、ApplyCompatibility.ps1 が CG2 内の PhysX コピーの cuCtxCreate 呼び出しをバージョン分岐で修正します。CUDA ヘッダーの C4819 対策はビルド側の UTF-8 指定で行い、Toolkit のファイルは変更しません。

SDK 内部の CudaKernelWrangler が CUDA カーネルの登録・起動を担当するため、重複する cudart リンクを外しています。GPU DLL の名前も PhysX の動的ロード名 PhysXGpu_64.dll に統一しています。

CUDA 13 の未使用 host launch wrapper が参照する追加シンボルは Cuda13Compatibility.cpp で解決します。実際の計算は従来の CUDA driver API を通り、この未使用経路が呼ばれた場合は明示的に停止します。

## GPU 対応版の検証結果（2026-09-03）

- Release / Debug ともに CPU・GPU の CTest が成功。
- RTX 4060 Laptop GPU の実使用を確認。GPU heap は 67,108,864 bytes、球の最終高さは 0.5。
- Blast の bond 破壊は両モードとも 2 actors に分離。
- GPU DLL を置かず CUDA の PATH も外した独立フォルダーで、CPU テストの成功を確認。
- CG2 本体の Release x64 ビルドと GPU DLL 自動配置を確認。

CPU / GPU の速度比較は実施していません。エディターの Play で PhysX と Blast を連動させる機能や、CPU / GPU 選択 UI は今回の SDK 導入には含みません。
