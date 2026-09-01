# 3Dワイヤーパズル エンジン要件

この資料は「フック＋ワイヤー＋物体の性質」だけで成立させる3D物理パズルを正とする。

## 制作ルール

- WireはHook同士だけを接続する。
- Hookは接続点だけを担当し、DoorHookやGearHookには分けない。
- 結果はRigidbody、質量、軸固定、Joint、既存運動の組み合わせから生じさせる。
- Wire接続距離は制限しない。接続時の2点間距離を初期長として使用する。
- 通常、照準中、選択中、接続中をHook自身の色と発光で区別する。
- 質量は`Renderer::SetColor`を使い、最小質量色から最大質量色まで連続補間して示す。
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
