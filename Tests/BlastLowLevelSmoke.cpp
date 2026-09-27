#include <NvBlast.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
	void* Allocate(size_t size) {
		return size > 0U ? _aligned_malloc(size, 16U) : nullptr;
	}
}

int main() {
	NvBlastChunkDesc chunks[3]{};
	for (uint32_t index = 0U; index < 3U; ++index) {
		chunks[index].centroid[0] = static_cast<float>(index);
		chunks[index].volume = 1.0f;
		chunks[index].parentChunkIndex = UINT32_MAX;
		chunks[index].flags = NvBlastChunkDesc::SupportFlag;
		chunks[index].userData = index;
	}

	NvBlastBondDesc bonds[2]{};
	for (uint32_t index = 0U; index < 2U; ++index) {
		bonds[index].bond.normal[0] = 1.0f;
		bonds[index].bond.area = 1.0f;
		bonds[index].bond.centroid[0] = static_cast<float>(index) + 0.5f;
		bonds[index].chunkIndices[0] = index;
		bonds[index].chunkIndices[1] = index + 1U;
	}

	NvBlastAssetDesc assetDesc{};
	assetDesc.chunkCount = 3U;
	assetDesc.chunkDescs = chunks;
	assetDesc.bondCount = 2U;
	assetDesc.bondDescs = bonds;
	void* assetMemory = Allocate(NvBlastGetAssetMemorySize(&assetDesc, nullptr));
	void* assetScratch = Allocate(NvBlastGetRequiredScratchForCreateAsset(&assetDesc, nullptr));
	NvBlastAsset* asset = NvBlastCreateAsset(assetMemory, &assetDesc, assetScratch, nullptr);
	_aligned_free(assetScratch);
	if (asset == nullptr) return 10;

	void* familyMemory = Allocate(NvBlastAssetGetFamilyMemorySize(asset, nullptr));
	NvBlastFamily* family = NvBlastAssetCreateFamily(familyMemory, asset, nullptr);
	NvBlastActorDesc actorDesc{};
	actorDesc.uniformInitialBondHealth = 10.0f;
	actorDesc.uniformInitialLowerSupportChunkHealth = 10.0f;
	void* actorScratch = Allocate(NvBlastFamilyGetRequiredScratchForCreateFirstActor(family, nullptr));
	NvBlastActor* actor = NvBlastFamilyCreateFirstActor(family, &actorDesc, actorScratch, nullptr);
	_aligned_free(actorScratch);
	if (actor == nullptr) return 20;

	const uint32_t* chunkToNode = NvBlastAssetGetChunkToGraphNodeMap(asset, nullptr);
	NvBlastBondFractureData command{};
	command.nodeIndex0 = chunkToNode[0];
	command.nodeIndex1 = chunkToNode[1];
	command.health = 11.0f;
	NvBlastFractureBuffers commands{};
	commands.bondFractureCount = 1U;
	commands.bondFractures = &command;
	NvBlastActorApplyFracture(nullptr, actor, &commands, nullptr, nullptr);
	if (!NvBlastActorIsSplitRequired(actor, nullptr)) return 30;

	const uint32_t maximumActorCount = NvBlastActorGetMaxActorCountForSplit(actor, nullptr);
	std::vector<NvBlastActor*> actors(maximumActorCount);
	NvBlastActorSplitEvent splitEvent{};
	splitEvent.newActors = actors.data();
	void* splitScratch = Allocate(NvBlastActorGetRequiredScratchForSplit(actor, nullptr));
	const uint32_t actorCount = NvBlastActorSplit(
		&splitEvent,
		actor,
		maximumActorCount,
		splitScratch,
		nullptr,
		nullptr);
	_aligned_free(splitScratch);
	for (uint32_t index = 0U; index < actorCount; ++index) {
		NvBlastActorDeactivate(actors[index], nullptr);
	}
	_aligned_free(familyMemory);
	_aligned_free(assetMemory);

	std::cout << "BlastLowLevelSmoke actors=" << actorCount << '\n';
	return actorCount == 2U ? 0 : 40;
}
