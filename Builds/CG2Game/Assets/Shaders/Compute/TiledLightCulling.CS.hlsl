RWStructuredBuffer<uint> gTileLightCounts : register(u0);

[numthreads(16, 16, 1)]
void main(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID)
{
    if (groupThreadId.x == 0u && groupThreadId.y == 0u) {
        const uint tileIndex = groupId.y * 1024u + groupId.x;
        gTileLightCounts[tileIndex] = 0u;
    }
}
