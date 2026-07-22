#ifndef PARTICLECOMMON_HLSLI
#define PARTICLECOMMON_HLSLI

struct ParticleData
{
    float4 positionLifetime; // xyz: World位置 / w: 残り寿命秒。
    float4 velocitySize; // xyz: 速度 / w: 現在サイズ。
    float4 startColor; // 発生時色。
    float4 endColor; // 消滅時色。
    float4 lifeSize; // x: 経過秒 / y: 総寿命秒 / z: 開始サイズ / w: 終了サイズ。
    float4 physics; // x: 重力 / y: Drag / z: 乱流強さ / w: 乱流周波数。
    float4 motion0; // x: 運動方式 / y: 角速度 / z: 半径方向加速度 / w: 波振幅。
    float4 motion1; // xyz: 運動中心 / w: 波周波数。
    float4 motion2; // x: 吸引力 / y: 現在回転 / z: 回転速度 / w: 予約。
    float4 rendering; // x: 描画モデルグループ / y: 放射強度。0 は板ポリゴン。
};

#endif
