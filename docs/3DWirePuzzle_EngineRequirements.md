# 3Dワイヤーパズル エンジン要件

この資料は「フック＋ワイヤー＋物体の性質」だけで成立させる3D物理パズルを正とする。

## 制作ルール

- WireはHook同士だけを接続する。
- Hookは接続点だけを担当し、DoorHookやGearHookには分けない。
- 結果はRigidbody、質量、軸固定、Joint、既存運動の組み合わせから生じさせる。
- Wire接続距離は制限しない。接続時の2点間距離を初期長として使用する。
- 通常、照準中、選択中、接続中をHook自身の色と発光で区別する。
- 質量は`Renderer::SetColorFromMass`を使い、最小質量色から最大質量色まで連続補間して示す。必要なタイミングで呼び出す。
- 続行不能時は最低限`SceneManager::Reload`でステージ全体を戻せるようにする。

## エンジン構成

```text
Hook GameObject
├─ ModelRenderer
├─ Collider
└─ HookPoint
   └─ 力を伝えるRigidbody → 親の物理物体

物理物体
├─ Rigidbody
├─ Collider
├─ 軸固定またはJoint
└─ Hook GameObject（1個以上）
```

ユーザーScriptは`Physics::FindBestHook`、`HookPoint::SetVisualState`、`Wire::Create`、`Wire::SetShrinkSpeed`、`Wire::Destroy`を組み合わせる。DLL ABI、Instance生成、Action配送はGenerated側が担当する。

## 追加済みの制作機能と参照先（2026-09-02）

| 作りたいこと | 現在の入口 | 注意 |
| --- | --- | --- |
| Updateだけを書くScript | Script基底クラスとGenerated ABI | 必要な関数だけ.hで宣言・.cppで実装。ABIをユーザー.cppへ書かない。 |
| .hを触らずInspectorへ調整値を追加 | `.cpp`の`SCRIPT_FIELD_*`と`FieldFloat`等 | インスタンス単位の公開値。 |
| 必要になった対象だけにComponentを追加 | `AddComponent<T>` / `GetOrAddComponent<T>` / `RemoveComponent<T>` | 全物体へのWire事前配置は不要。型付き追加後も成功確認が必要。 |
| 実行時にHookの構成を作る | `GameObject::Create` / `Instantiate` / `SetParent` | HookPoint単体では見た目や選択Colliderは作られない。それぞれ用意する。 |
| 子Hookから親物体へ力を伝える | HookPointの「力を伝えるRigidbody」 | 未指定の-1は自身。親を自動推測しない。 |
| Hookを狙って選ぶ | `Physics::FindBestHook` + `HookPoint::CanConnect` | 探索範囲・角度と接続可能条件を分けて確認する。 |
| 複数の縄を作って操作する | `Wire::Create` / `SetLength` / `SetShrinkSpeed` / `Repair` / `Destroy` | WireごとのHandle。RopeConstraintの事前配置は不要。 |
| Hookに付いた縄を取得する | `HookPoint::GetWires` | Active・未破断のみ。選択した縄の個別解除を作るための入口。 |
| Hookを4状態で色分けする | `HookPoint::SetVisualState` | 状態遷移をScriptから呼ぶ。Hook自身にRendererが必要。 |
| 重さを色にする | `Renderer::SetColorFromMass` / `SetColor` / `SetEmission` | 質量Objectと表示Objectを分けられる。 |
| 階層と接続位置を扱う | `GetParent` / `GetChildCount` / `GetChild` / World・Local変換 | Anchorは各Hookのローカル座標、距離計算はWorld座標。 |
| ステージをやり直す | `SceneManager::Reload` | Primary Sceneの読込要求。エリア単位のSnapshot/Restoreではない。 |

APIの引数・戻り値・失敗条件は[C++ Script仕様書](cpp-script-documentation-detail-seed.md)の「追加Wrapperの契約」、設定名と保存・描画の制約は[Component仕様書](component-documentation-detail-seed.md)のHookPoint / WireRenderer節を参照する。

## 現在のサンプル操作

対象は`Assets/InGameInputAction3.inputactions`と`resources/scripts/WirePlayerScript/WirePlayerScript.cpp`。エンジン全体の固定操作ではなく、このInput Actions AssetをPlayerInputへ設定した場合の割り当て。

| 操作 | Action Map / Action | サンプルの処理 |
| --- | --- | --- |
| WASD | Player / Move | XZ方向へ移動。 |
| 左クリック | Player / Connect | 1個目のHookを選択し、2個目で接続。同一Hookへの接続は拒否。 |
| Space保持 | Player / Shrink | サンプルが管理する全Wireを収縮。離すと速度0を設定。 |
| E | Player / Detach | 最後に作ったWireを削除。狙ったHookのWireを選んで削除する操作ではない。 |
| 右クリック | Player / Cancel | 接続待ちの選択を解除。既存Wireの削除ではない。 |

Reset用ActionはこのAssetに未登録。Reload APIがあるだけで、自動的にリセットキーが付くわけではない。

| Script公開値 | 既定値 | 用途 |
| --- | --- | --- |
| `moveSpeed` | 6.0 | 移動速度。 |
| `hookSelectionDistance` | 1000.0 | 照準探索距離。Hook間の最大接続長ではない。 |
| `hookSelectionAngle` | 4.0 | 照準探索角度degree。 |
| `minimumWireLength` | 0.5 | 収縮下限m。 |
| `shrinkSpeed` | 3.0 | 収縮速度m/s。 |
| `wireStiffness` / `wireDamping` | 1200.0 / 80.0 | 張力係数／減衰。 |
| `maximumTension` / `breakingTension` | 0.0 / 0.0 | 張力上限／Wire側破断閾値。Hook側強度も別途適用。 |

サンプルの初期長は`max(選択時の2点間距離 - 0.05m, minimumWireLength)`。軽く張った状態を作る補正があり、距離そのものと完全に同じではない。最大接続距離の拒否判定はない。

## ワイヤーの見た目と未実装の境界

現行WireRendererは、World上の両端・余長からたるんだ線を計算し、Game Viewへ投影して太いImGui線として描く。外周・ハイライト・疑似Glowで縄らしく見せる。専用縄モデルは不要だが、立体メッシュを生成しているわけではない。

- 半径・色・透明度・長手分割・たるみを設定できる。線幅pxは最小表示幅になる。
- 遮蔽は区間中点へのCollider Raycastによる近似。GPU深度判定ではなく、Colliderなしの物体では隠れない。
- 断面分割数は保存・Inspector表示のみで、描画には未使用。
- 縄のチューブメッシュ、Texture、影・反射、障害物への巻き付きは未実装。
- エリア単位の物理状態Snapshot/Restore、名前指定の`FindChild` APIは未実装。現在の代替はScene ReloadとGetChild列挙。
- 任意HookのWire取得・削除APIはあるが、サンプル操作は「最新Wire削除」まで。Wire選択UIは別途ゲームScriptで組む必要がある。

サンプルの既知の制約として、選択状態とWire配列は名前空間変数であり複数Scriptインスタンス間で共有される。また、1点目のWorld位置は選択時に保持されるため、2点目を選ぶまでに1点目が動く場合の追従は未対応。これらを完成済みの汎用挙動として扱わない。

## 検証範囲

本資料は現行ソースとの照合結果。Runtime API索引は271 Entryへ更新した。以前のビルド成功はPlayでの挙動・視認性・遮蔽精度の確認を意味しない。今回の作業はmd更新のみで、ゲーム実行検証や上記未実装機能の追加は行っていない。
