

#include "Particle.hlsli"

// CPUで決めた粒子固有の初期値。現在の位置と板の頂点はGPUで求める。
struct ParticleData
{
    float4 positionAge;
    float4 velocityLife;
    float4 accelerationTrail;
    float4 scaleFade;
    float4 lineDirectionDrag;
    float4 motion;
    float4 color;
};

StructuredBuffer<ParticleData> gParticles : register(t0);

cbuffer ParticleCamera : register(b2)
{
    row_major float4x4 gViewProjection;
    float4 gRight;
    float4 gUp;
    float4 gForward;
};

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

VertexShaderOutput main(VertexShaderInput input, uint32_t instanceId : SV_InstanceID)
{
    const ParticleData particle = gParticles[instanceId];
    const float step = max(particle.motion.y, 0.000001f);
    const float steps = particle.motion.z;
    const float elapsed = steps * step;
    const float3 acceleration = particle.accelerationTrail.xyz;
    const float3 initialVelocity = particle.velocityLife.xyz;
    float3 velocity = initialVelocity + acceleration * elapsed;
    float3 position = particle.positionAge.xyz + initialVelocity * elapsed
        + acceleration * (0.5f * elapsed * (elapsed + step));

    // 冠菊は指定時刻から水平速度だけを毎フレーム減衰させる。
    if (particle.motion.x > 0.0f && steps > 0.0f)
    {
        const float beforeDrag = clamp(
            ceil((particle.lineDirectionDrag.w - particle.scaleFade.w) / step) - 1.0f,
            0.0f, steps);
        const float dampedSteps = steps - beforeDrag;
        const float damping = exp(-particle.motion.x * step);
        const float dampingPower = pow(damping, dampedSteps);
        const float dampedDistance = abs(1.0f - damping) > 0.000001f
            ? damping * (1.0f - dampingPower) / (1.0f - damping)
            : dampedSteps;
        position.xz = particle.positionAge.xz
            + initialVelocity.xz * (step * (beforeDrag + dampedDistance));
        velocity.xz = initialVelocity.xz * dampingPower;
    }

    const float fadeStart = clamp(particle.scaleFade.y, 0.0f, particle.velocityLife.w);
    const float fadeDuration = particle.velocityLife.w - fadeStart;
    const float fade = fadeDuration > 0.0f
        ? saturate((particle.velocityLife.w - particle.positionAge.w) / fadeDuration)
        : 1.0f;
    const bool fixedLine = particle.scaleFade.z > 0.0f;
    const float3 direction = fixedLine ? particle.lineDirectionDrag.xyz : velocity;
    const float vx = dot(direction, gRight.xyz);
    const float vy = dot(direction, gUp.xyz);
    const float speedOnScreen = length(float2(vx, vy));
    const float cosine = speedOnScreen > 0.001f ? vy / speedOnScreen : 1.0f;
    const float sine = speedOnScreen > 0.001f ? -vx / speedOnScreen : 0.0f;
    const float width = particle.scaleFade.x * fade;
    const float trailTime = particle.accelerationTrail.w * fade;
    const float directionLength = fixedLine ? length(direction) : 0.0f;
    const float projectedLength = fixedLine && directionLength > 0.0f
        ? particle.scaleFade.z * speedOnScreen / directionLength
        : speedOnScreen * trailTime;
    const float lineLength = max(projectedLength, width);
    const float3 center = fixedLine ? position : position - velocity * (trailTime * 0.5f);

    // plane.obj の座標は縦横 -1～1。行列を粒子ごとに作らず軸を直接合成する。
    const float3 axisX = (cosine * gRight.xyz + sine * gUp.xyz) * (width * 0.5f);
    const float3 axisY = (-sine * gRight.xyz + cosine * gUp.xyz) * (lineLength * 0.5f);
    const float3 axisZ = gForward.xyz;
    const float3 worldPosition = center + input.position.x * axisX
        + input.position.y * axisY + input.position.z * axisZ;

    VertexShaderOutput output;
    output.position = mul(float4(worldPosition, 1.0f), gViewProjection);
    output.texcoord = input.texcoord;
    output.normal = normalize(input.normal.x * axisX + input.normal.y * axisY
        + input.normal.z * axisZ);
    output.color = float4(particle.color.rgb, particle.color.a * fade);
    return output;
}
