# CG2 エディタ README

CG2 エディタは、DirectX12、ImGui Docking、ImGuizmo、Jolt Physics を使った Unity 風の簡易ゲームエンジン / エディタです。  
GameObject を Scene に置き、インスペクターから日本語名のコンポーネントを追加して、描画、入力、物理、保存、Prefab、Console 確認を行えます。

## 機能レベルの見方

| レベル | 意味 |
| --- | --- |
| 実装済み | エディタ上で実際に使える段階です。 |
| 一部実装 | 基本動作はありますが、Unity と同じ完成度ではありません。 |
| 追加・表示 | コンポーネント追加や Inspector 表示はできますが、Play 中の本格動作はまだ弱い段階です。 |
| 未対応 | 今後追加する必要がある機能です。 |

## 全体機能

| 機能 | レベル | 内容 |
| --- | --- | --- |
| Unity 風レイアウト | 実装済み | ヒエラルキー、シーン、ゲーム、インスペクター、Project、Console を表示します。 |
| Docking UI | 実装済み | ImGui Docking により、各パネルをドラッグ移動、ドッキング、分離できます。 |
| Scene View | 実装済み | 編集用カメラで GameObject、ライト、カメラ、ガイド線、当たり判定デバッグを見られます。 |
| Game View | 一部実装 | Camera コンポーネントの視点で実行確認できます。Unity の Game View ほど描画設定は多くありません。 |
| Project | 一部実装 | `resources` 内の PNG、JPG、OBJ、MTL、FBX、WAV を表示します。追加したファイルも自動で出ます。 |
| Console | 一部実装 | 操作ログ、物理ログ、生成ログを画面に表示します。スタックトレースやクリック移動は未対応です。 |
| Play / Stop | 一部実装 | Play で物理や入力を動かし、Stop で Play 前の Scene 状態へ戻します。 |
| Undo / Redo | 一部実装 | 生成、削除、Transform など一部操作を戻せます。すべての Inspector 変更までは未対応です。 |

## Scene と GameObject

| 機能 | レベル | 内容 |
| --- | --- | --- |
| GameObject 作成 | 実装済み | 空の GameObject、モデル、スプライト、ライト、カメラを Scene に追加できます。 |
| GameObject 選択 | 実装済み | ヒエラルキーまたは Scene 上のオブジェクトから選択できます。 |
| 名前編集 | 実装済み | インスペクターで GameObject 名を変更できます。 |
| 有効 / 無効 | 実装済み | GameObject と各コンポーネントの有効フラグを切り替えられます。 |
| タグ / レイヤー | 一部実装 | Inspector 表示、保存、物理レイヤー判定の入口があります。Unity の Tag Manager 相当はまだ弱いです。 |
| 親子付け | 一部実装 | ヒエラルキーで階層を持てます。Prefab 差分や複雑な親子 Transform 制御は未完成です。 |
| 複製 / 削除 | 一部実装 | GameObject の複製と削除ができます。削除時は描画側の見た目も同期します。 |
| 複数選択 | 未対応 | 複数 GameObject の一括編集はまだありません。 |

## Transform とギズモ

| 機能 | レベル | 内容 |
| --- | --- | --- |
| 位置 | 実装済み | X / Y / Z を Inspector で同じ行に並べて編集できます。 |
| 回転 | 実装済み | X / Y / Z 回転を編集できます。Scene ギズモは `E` で回転モードになります。 |
| 拡縮 | 実装済み | X / Y / Z スケールを編集できます。Scene ギズモは `R` で拡縮モードになります。 |
| 移動ギズモ | 実装済み | `W` で移動モードになり、Scene 上で引っ張って動かせます。 |
| Local / World | 一部実装 | Local / World の切り替えがあります。Unity と同等の全操作感ではありません。 |
| Snap | 一部実装 | Snap の状態表示があります。細かいグリッド吸着は今後強化が必要です。 |
| Scene カメラ操作 | 実装済み | 右ドラッグ回転、中ドラッグ平行移動、ホイール前後移動、WASD 移動に対応しています。 |
| ガイド線 | 実装済み | Scene 上に床グリッドの目安線を表示します。 |

## アセット

| 機能 | レベル | 内容 |
| --- | --- | --- |
| PNG | 一部実装 | Project にサムネイル表示し、スプライトとして Scene に配置できます。 |
| JPG | 追加・表示 | Project に表示できます。Scene 配置用テクスチャ登録はまだ限定的です。 |
| OBJ | 一部実装 | `plane.obj` などを 3D モデルとして配置できます。複雑な OBJ の完全な Material 読み込みは限定的です。 |
| FBX 基本形 | 一部実装 | FBX ファイル名から内部プリミティブへ対応付けて表示します。 |
| 球系 FBX | 一部実装 | `sphere.fbx`、`ball.fbx` などは球メッシュとして表示し、球の当たり判定を自動追加します。 |
| WAV | 追加・表示 | Project に表示できます。AudioSource の本格再生設定はまだ限定的です。 |
| Asset Database | 未対応 | GUID、meta、参照関係、参照切れ検出はまだありません。 |
| Importer | 未対応 | Unity の Import Settings のような詳細変換設定はまだありません。 |

## 基本形 3D オブジェクト

| 基本形 | レベル | 内容 |
| --- | --- | --- |
| Cube | 実装済み | 1 x 1 x 1 の立方体を置けます。箱の当たり判定と Rigidbody を自動追加します。 |
| Box | 実装済み | 横長の箱を置けます。箱の当たり判定と Rigidbody を自動追加します。 |
| Cylinder | 実装済み | 円柱を置けます。初期当たり判定は箱近似です。 |
| Cone | 実装済み | 円錐を置けます。初期当たり判定は箱近似です。 |
| Torus | 実装済み | ドーナツ形状を置けます。初期当たり判定は箱近似です。 |
| Ico | 実装済み | 低ポリゴンのダイヤ形に近い球風メッシュを置けます。滑らかな球ではありません。 |
| Sphere | 実装済み | `sphere` / `ball` 系 FBX を滑らかな球として置けます。球の当たり判定を自動追加します。 |

## 描画

| 機能 | レベル | 内容 |
| --- | --- | --- |
| 3D モデル描画 | 一部実装 | DirectX12 で基本形メッシュ、OBJ、FBX 対応プリミティブを描画します。 |
| 2D スプライト描画 | 一部実装 | 登録済み PNG を Scene にスプライトとして表示します。 |
| Material 色 | 一部実装 | MeshRenderer / SpriteRenderer の色を Inspector から変更できます。初期値は白です。 |
| Texture 表示 | 一部実装 | 登録済みテクスチャをモデルやスプライトに使います。任意テクスチャ割り当て UI は限定的です。 |
| Directional Light | 一部実装 | 平行光源の色、向き、強さを持ちます。影や複数ライトは未対応です。 |
| Camera | 一部実装 | Game View 用の視点として使えます。FOV や Culling Mask などは限定的です。 |
| 背景色 | 実装済み | Inspector から Scene 背景色を変えられます。 |
| Gizmo / Icon | 一部実装 | ライト、カメラ、当たり判定のデバッグ表示があります。 |
| Shader 管理 | 未対応 | Unity の Shader Graph や Material Inspector 相当はありません。 |

## 物理

物理は Jolt Physics 5.5.0 を使っています。Play 中に固定時間ステップで 3D 物理を更新します。

| 機能 | レベル | 内容 |
| --- | --- | --- |
| 3D 重力 | 実装済み | Rigidbody と Collider を持つ GameObject が、3 次元空間で重力により落下します。 |
| Rigidbody | 一部実装 | 質量、重力使用、速度、角速度、線形減衰、角度減衰、反発、Freeze Position / Rotation を扱います。 |
| Box Collider | 実装済み | 箱の当たり判定です。床、壁、箱同士の押し返しに使えます。 |
| Sphere Collider | 実装済み | 球の当たり判定です。球 FBX や低ポリ球に自動で付きます。 |
| Capsule Collider | 一部実装 | Jolt の Capsule Shape として扱います。キャラクター用の基礎形状に使えます。 |
| Mesh Collider | 一部実装 | 静的形状の入口があります。Unity の複雑な Mesh Collider と同等ではありません。 |
| Terrain Collider | 追加・表示 | コンポーネント追加と Inspector 表示はあります。本格 Terrain 衝突は未完成です。 |
| Trigger | 一部実装 | 押し返さない当たり判定として区別します。イベントログの入口があります。 |
| Collision | 一部実装 | 押し返す衝突として扱います。Jolt の接触結果を Play ランタイムへ渡し、OnCollision 系の元データとして使える段階です。 |
| 物理イベント | 一部実装 | Enter / Stay / Exit を両オブジェクト向けイベント構造体へ変換し、FixedUpdate 前に Script 側へ渡します。ユーザー自作関数への最終ディスパッチは今後です。 |
| Raycast | 一部実装 | Scene 内 Collider に Ray を飛ばし、命中 GameObject、位置、法線、距離を返す API 入口があります。 |
| SphereCast | 一部実装 | 太さのある Raycast として Jolt に問い合わせる API 入口があります。 |
| CapsuleCast | 一部実装 | カプセル形状を移動させる問い合わせ API 入口があります。 |
| OverlapSphere | 一部実装 | 指定範囲に重なった GameObject 一覧を返す API 入口があります。 |
| OverlapBox | 一部実装 | 指定ボックス範囲に重なった GameObject 一覧を返す API 入口があります。 |
| AddForce / AddImpulse / SetVelocity | 一部実装 | Dynamic Rigidbody に力、瞬間力、速度を与えるランタイム API 入口があります。 |
| Layer Collision Matrix | 一部実装 | レイヤー同士の衝突可否を物理処理で見る入口があります。専用 Project Settings UI は未完成です。 |
| Physics Material | 一部実装 | 摩擦、静止摩擦、反発を Collider ごとに設定する入口があります。 |
| FixedUpdate | 実装済み | Play 中の物理は固定時間ステップで進みます。 |
| 連続衝突判定 | 一部実装 | 高速移動体向けの設定入口があります。Unity と同等の詳細設定は不足しています。 |
| Character Controller | 一部実装 | Jolt CharacterVirtual を使い、重力、移動、接地の基礎処理があります。坂、段差の細かい調整は今後です。 |
| Joint | 一部実装 | Fixed、Hinge、Spring、Character Joint を Jolt Constraint として作ります。細かい制限 UI はまだ少ないです。 |
| Physics Debug | 一部実装 | Scene 上に Collider の形、Trigger 色、接触系ログを表示できます。接触点や法線の詳細表示は今後です。 |
| Physics Settings | 一部実装 | 重力、固定更新時間、レイヤー判定の内部値があります。専用設定画面はまだありません。 |

## 入力

| 機能 | レベル | 内容 |
| --- | --- | --- |
| キーボード入力 | 一部実装 | 旧 Input コンポーネントに加え、PlayerInput で Actions アセットを読んで Move / Jump / Fire を扱えます。 |
| Rigidbody 移動 | 一部実装 | Play 中は Transform 直書きではなく、Rigidbody / CharacterController の速度へ反映します。 |
| マウス入力 | 一部実装 | Scene 操作と選択で使います。ゲーム入力としての設定 UI はまだ弱いです。 |
| ゲームパッド | 未対応 | 統一 Input System とゲームパッド対応は今後です。 |
| PlayerInput | 一部実装 | Actions、Default Map、Behavior、Move / Jump / Fire の Event 名を Inspector から設定できます。 |
| Input Action | 一部実装 | `.inputactions` ファイルを作成し、Move / Jump / Fire の簡易 Action を読めます。 |

## コンポーネント

インスペクター下部の「コンポーネントを追加」から、日本語カテゴリで追加できます。  
名前は、分かりやすいものは日本語、Unity 用語として認識しやすいものはカタカナ英語を使っています。

| カテゴリ | レベル | 主な内容 |
| --- | --- | --- |
| 基本 | 実装済み | Transform、RectTransform、Canvas、GameObject、MonoBehaviour など。 |
| 描画 | 一部実装 | MeshFilter、MeshRenderer、SpriteRenderer、LineRenderer、TrailRenderer など。 |
| カメラ | 一部実装 | Camera、AudioListener、Cinemachine 風カメラなど。 |
| ライト・環境 | 一部実装 | Light、Reflection Probe、Volume など。 |
| 3D 物理 | 一部実装 | Rigidbody、箱 / 球 / カプセル / メッシュ当たり判定、CharacterController、Joint など。 |
| 2D 物理 | 追加・表示 | Rigidbody2D、BoxCollider2D、CircleCollider2D、各種 Joint2D など。 |
| アニメーション | 追加・表示 | Animator、Animation、各種 Constraint、PlayableDirector など。 |
| オーディオ | 追加・表示 | AudioSource、AudioListener、各種 Audio Filter など。 |
| UI | 追加・表示 | Button、Text、Image、Slider、Dropdown、Layout Group など。 |
| 入力・イベント | 一部実装 | Input、EventSystem、PlayerInput など。 |
| ナビゲーション・AI | 追加・表示 | NavMesh Agent、Obstacle、Surface など。 |
| エフェクト | 追加・表示 | ParticleSystem、VisualEffect、Trail、Line、Lens Flare など。 |
| 地形・タイルマップ | 追加・表示 | Terrain、Tilemap、Grid など。 |
| FeelKit | 追加・表示 | FeelKit 触覚ソースをコンポーネントとして置く入口があります。 |

## Script / MonoBehaviour 相当

| 機能 | レベル | 内容 |
| --- | --- | --- |
| Start | 一部実装 | Play 開始時に呼ぶ入口があります。 |
| Update | 一部実装 | 毎フレーム処理の入口があります。 |
| FixedUpdate | 一部実装 | 物理固定更新と合わせる入口があります。 |
| Stop | 一部実装 | Play 停止時に呼ぶ入口があります。 |
| OnCollision / OnTrigger | 一部実装 | Jolt の Enter / Stay / Exit をイベント構造体として ScriptManager へ渡します。ユーザー自作 C++ 関数へ直接呼び分ける最終段は今後です。 |
| ユーザー自作 C++ コンポーネント | 未対応 | エディタから新規 C++ クラスを作り、即追加する仕組みはまだありません。 |

## PlayerInput の使い方

1. Player 用 GameObject を選びます。
2. `プレイヤー入力` コンポーネントを追加します。
3. Inspector の `Create Actions...` を押します。
4. `Actions` に作成された `.inputactions` が入っていることを確認します。
5. `Default Map` は `Player` のまま使います。
6. `Behavior` は `Invoke C++ Events` を使います。
7. `Move / Jump / Fire` の Event 名を設定します。
8. `Rigidbody + 当たり判定` または `CharacterController` を付けて `Play` を押します。

作成される `.inputactions` の初期内容は次です。

```text
Action|Player|Move|Vector2|2DVector|W|S|A|D
Action|Player|Jump|Button|Key|Space
Action|Player|Fire|Button|Mouse|LeftButton
```

現状のランタイム対応は次です。

- `Move` はカメラ相対移動として動きます。
- `Jump` は床付近で上向き速度を与えます。
- `Fire` は Console にイベントログを出します。
- `Move / Jump / Fire` 以外の Action 名はまだ未対応です。
- `Invoke C++ Events` は Inspector で関数名を持てますが、ユーザー自作 C++ 関数への完全ディスパッチは今後です。

## 保存と Prefab

| 機能 | レベル | 内容 |
| --- | --- | --- |
| Scene 保存 | 一部実装 | GameObject、Transform、コンポーネント値を保存します。 |
| Scene 読み込み | 一部実装 | 保存した Scene を読み込めます。壊れたファイルの耐性はまだ弱いです。 |
| Prefab 保存 | 一部実装 | 選択中 GameObject を Prefab として保存できます。 |
| Prefab 生成 | 一部実装 | 保存した Prefab から GameObject を作れます。 |
| Prefab 差分管理 | 未対応 | Apply / Revert、差分表示、ネスト Prefab は未対応です。 |
| Play 状態復元 | 一部実装 | Play 前の Scene をバックアップし、Stop 時に戻します。完全な Unity Play Mode 復元ではありません。 |

## 外部ライブラリ利用

| ライブラリ | レベル | 用途 |
| --- | --- | --- |
| Jolt Physics 5.5.0 | 実装済み | 3D Rigidbody、Collider、Trigger、Raycast、Cast、Joint、CharacterController の物理処理に使います。 |
| ImGui Docking | 実装済み | Unity 風のドッキング可能なエディタ UI に使います。 |
| ImGuizmo | 実装済み | Scene 上の移動、回転、拡縮ギズモに使います。 |
| DirectX12 | 実装済み | モデル、スプライト、Scene View、Game View の描画に使います。 |
| XAudio2 | 一部実装 | WAV 読み込みと音声再生の土台に使います。 |
| FeelKitHaptics | 追加・表示 | 触覚コンポーネントの入口として扱います。 |
| GLM / Eigen | 未統合 | externals にはありますが、現状の主要実装では直接の中核依存にはしていません。 |

## 今できる具体例

- Cube を Scene に置き、Play を押すと 3D 重力で落下し、床や他 Collider に当たる挙動を確認できます。
- `sphere.fbx` や `ball.fbx` を `resources` に置くと、Project に表示され、Scene に置くと球メッシュと球の当たり判定が付きます。
- GameObject に Rigidbody、球の当たり判定、Physics Material を付けると、質量、摩擦、反発の基礎値を調整できます。
- CharacterController と Input を付けると、Play 中に入力で移動する基礎キャラクターを作れます。
- Camera を置くと、Game View でそのカメラからの見え方を確認できます。
- Inspector からコンポーネントを追加し、下部 Console で操作ログや物理ログを確認できます。

## まだ Unity と同じではないところ

- FBX はファイル名から基本形へ割り当てる段階で、スキンメッシュ、ボーン、アニメーションクリップの完全読み込みは未対応です。
- UI コンポーネントは追加と Inspector 表示が中心で、Button、Slider、EventSystem の本格実行は未完成です。
- Audio Mixer、Particle System、NavMesh、Terrain、Tilemap は追加・表示段階です。
- Prefab の Apply / Revert、差分管理、ネスト Prefab は未対応です。
- Asset Database、GUID、meta、参照切れ検出は未対応です。
- Console は画面表示できますが、Unity のようなスタックトレース、ログ種別フィルター、クリックジャンプは未完成です。
- Build 機能、Package Manager、Profiler、Editor 拡張 API は未対応です。
