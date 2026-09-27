#include "TestBlast.h"

#include <string>

namespace
{
	void TestLog(const std::string& message)
	{
		const EditorScriptRuntimeApi* runtimeApi =
			EditorNativeScriptRuntime::GetRuntimeApi();

		if (runtimeApi != nullptr &&
			runtimeApi->Log != nullptr)
		{
			runtimeApi->Log(message.c_str());
		}
	}
}

//================================================================
// ユーザーが編集する C++ Script 本体
//================================================================

TestBlast::TestBlast()
{
	BindStart([this]()
		{
			const int32_t rootId = GetGameObjectId();

			const int32_t chunkCount =
				BlastDestruction::GetChunkCount(rootId);

			const int32_t bondCount =
				BlastDestruction::GetBondCount(rootId);

			TestLog(
				"[BlastTest] Start"
				" ChunkCount=" + std::to_string(chunkCount) +
				" BondCount=" + std::to_string(bondCount));

			if (chunkCount <= 0)
			{
				TestLog(
					"[BlastTest] ERROR: ChunkCount <= 0");
			}
		});

	BindUpdate(
		[this,
		elapsedSeconds = 0.0f,
		hasExecuted = false](float deltaTime) mutable
		{
			if (hasExecuted)
			{
				return;
			}

			elapsedSeconds += deltaTime;

			// Play開始直後ではなく、1秒待ってから実行
			if (elapsedSeconds < 1.0f)
			{
				return;
			}

			hasExecuted = true;

			const int32_t rootId = GetGameObjectId();

			const int32_t chunkCount =
				BlastDestruction::GetChunkCount(rootId);

			const int32_t bondCount =
				BlastDestruction::GetBondCount(rootId);

			const int32_t actorCountBefore =
				BlastDestruction::GetActorCount(rootId);

			TestLog(
				"[BlastTest] Before"
				" ChunkCount=" + std::to_string(chunkCount) +
				" BondCount=" + std::to_string(bondCount) +
				" ActorCount=" + std::to_string(actorCountBefore));

			if (chunkCount <= 0)
			{
				TestLog(
					"[BlastTest] FAILED: "
					"Blast Chunkが生成されていません。");
				return;
			}

			TestLog(
				"[BlastTest] FractureAll 実行");

			const bool fractureResult =
				BlastDestruction::FractureAll(
					rootId,
					15.0f);

			if (!fractureResult)
			{
				TestLog(
					"[BlastTest] FAILED: "
					"FractureAll returned false");
				return;
			}

			// FractureAll後の状態確認
			const bool isFractured =
				BlastDestruction::IsFractured(rootId);

			const int32_t actorCountAfter =
				BlastDestruction::GetActorCount(rootId);

			TestLog(
				"[BlastTest] After"
				" IsFractured=" +
				std::string(isFractured ? "true" : "false") +
				" ActorCount=" +
				std::to_string(actorCountAfter));

			if (!isFractured)
			{
				TestLog(
					"[BlastTest] FAILED: "
					"IsFractured == false");
				return;
			}

			if (actorCountAfter <= actorCountBefore)
			{
				TestLog(
					"[BlastTest] WARNING: "
					"ActorCountが増えていません。");
				return;
			}

			TestLog(
				"[BlastTest] SUCCESS: "
				"Blast分裂を確認");
		});
}