#include "RenderQueue.h"
#include "RenderBatcher.h"
#include "RenderResources.h"
#include "RenderStatistics.h"
#include "GL/GLDebug.h"
#include "../Application.h"
#include "GL/GLScissorTest.h"
#include "GL/GLDepthTest.h"
#include "GL/GLBlending.h"
#include "../Base/Algorithms.h"
#include "../tracy_opengl.h"

namespace nCine
{
	RenderQueue::RenderQueue()
	{
		opaqueQueue_.reserve(16);
		opaqueBatchedQueue_.reserve(16);
		transparentQueue_.reserve(16);
		transparentBatchedQueue_.reserve(16);
	}

	bool RenderQueue::empty() const
	{
		return (opaqueQueue_.empty() && transparentQueue_.empty());
	}

	void RenderQueue::addCommand(RenderCommand* command)
	{
		// Calculating the material sorting key before adding the command to the queue
		command->calculateMaterialSortKey();

		if (!command->material().IsBlendingEnabled()) {
			opaqueQueue_.push_back(command);
		} else {
			transparentQueue_.push_back(command);
		}
	}

	namespace
	{
		bool descendingOrder(const RenderCommand* a, const RenderCommand* b)
		{
			return (a->materialSortKey() != b->materialSortKey())
				? a->materialSortKey() > b->materialSortKey()
				: a->idSortKey() > b->idSortKey();
		}

		bool ascendingOrder(const RenderCommand* a, const RenderCommand* b)
		{
			return (a->materialSortKey() != b->materialSortKey())
				? a->materialSortKey() < b->materialSortKey()
				: a->idSortKey() < b->idSortKey();
		}

#if defined(DEATH_DEBUG) && defined(NCINE_PROFILING)
		const char* commandTypeString(const RenderCommand& command)
		{
			switch (command.type()) {
				case RenderCommand::Type::Unspecified: return "unspecified";
				case RenderCommand::Type::Sprite: return "sprite";
				case RenderCommand::Type::MeshSprite: return "mesh sprite";
				case RenderCommand::Type::TileMap: return "tile map";
				case RenderCommand::Type::Particle: return "particle";
				case RenderCommand::Type::Lighting: return "lighting";
				case RenderCommand::Type::Text: return "text";
#	if defined(WITH_IMGUI)
				case RenderCommand::Type::ImGui: return "imgui";
#	endif
				default: return "unknown";
			}
		}
#endif
	}

	void RenderQueue::sortAndCommit()
	{
#if defined(DEATH_DEBUG)
		static char debugString[128];
#endif
		const bool batchingEnabled = theApplication().GetRenderingSettings().batchingEnabled;

		// Sorting the queues with the relevant orders
		sort(opaqueQueue_.begin(), opaqueQueue_.end(), descendingOrder);
		sort(transparentQueue_.begin(), transparentQueue_.end(), ascendingOrder);

		SmallVectorImpl<RenderCommand*>* opaques = batchingEnabled ? &opaqueBatchedQueue_ : &opaqueQueue_;
		SmallVectorImpl<RenderCommand*>* transparents = batchingEnabled ? &transparentBatchedQueue_ : &transparentQueue_;

		if (batchingEnabled) {
			ZoneScopedNC("Batching", 0x81A861);
			// Always create batches after sorting
			RenderResources::GetRenderBatcher().createBatches(opaqueQueue_, opaqueBatchedQueue_);
			RenderResources::GetRenderBatcher().createBatches(transparentQueue_, transparentBatchedQueue_);
		}

		// Avoid GPU stalls by uploading to VBOs, IBOs and UBOs before drawing
		if (!opaques->empty()) {
			ZoneScopedNC("Commit opaques", 0x81A861);
#if defined(DEATH_DEBUG)
			std::size_t length = formatInto(debugString, "Commit {} opaque command(s) for viewport 0x{:x}", opaques->size(), std::uintptr_t(RenderResources::GetCurrentViewport()));
			GLDebug::ScopedGroup scoped({ debugString, length });
#endif
			for (RenderCommand* opaqueRenderCommand : *opaques) {
				opaqueRenderCommand->commitAll();
			}
		}

		if (!transparents->empty()) {
			ZoneScopedNC("Commit transparents", 0x81A861);
#if defined(DEATH_DEBUG)
			std::size_t length = formatInto(debugString, "Commit {} transparent command(s) for viewport 0x{:x}", transparents->size(), std::uintptr_t(RenderResources::GetCurrentViewport()));
			GLDebug::ScopedGroup scoped({ debugString, length });
#endif
			for (RenderCommand* transparentRenderCommand : *transparents) {
				transparentRenderCommand->commitAll();
			}
		}
	}

	void RenderQueue::draw()
	{
#if defined(DEATH_DEBUG)
		static char debugString[128];
#endif
		const bool batchingEnabled = theApplication().GetRenderingSettings().batchingEnabled;
		SmallVectorImpl<RenderCommand*>* opaques = batchingEnabled ? &opaqueBatchedQueue_ : &opaqueQueue_;
		SmallVectorImpl<RenderCommand*>* transparents = batchingEnabled ? &transparentBatchedQueue_ : &transparentQueue_;

#if defined(DEATH_DEBUG) && defined(NCINE_PROFILING)
		std::uint32_t commandIndex = 0;
#endif
		// Rendering opaque nodes front to back
		for (RenderCommand* opaqueRenderCommand : *opaques) {
			TracyGpuZone("Opaque");
#if defined(DEATH_DEBUG) && defined(NCINE_PROFILING)
			const std::int32_t numInstances = opaqueRenderCommand->numInstances();
			const std::int32_t batchSize = opaqueRenderCommand->batchSize();
			const std::uint16_t layer = opaqueRenderCommand->layer();
			const std::uint16_t visitOrder = opaqueRenderCommand->visitOrder();

			std::size_t length;
			if (numInstances > 0) {
				length = formatInto(debugString, "Opaque {} ({} {} on layer {}, visit order {}, sort key 0x{:x})",
								    commandIndex, numInstances, commandTypeString(*opaqueRenderCommand), layer, visitOrder, opaqueRenderCommand->materialSortKey());
			} else if (batchSize > 0) {
				length = formatInto(debugString, "Opaque {} ({} {} on layer {}, visit order {}, sort key 0x{:x})",
								    commandIndex, batchSize, commandTypeString(*opaqueRenderCommand), layer, visitOrder, opaqueRenderCommand->materialSortKey());
			} else {
				length = formatInto(debugString, "Opaque {} ({} {} layer {}, visit order {}, sort key 0x{:x})",
								    commandIndex, commandTypeString(*opaqueRenderCommand), layer, visitOrder, opaqueRenderCommand->materialSortKey());
			}
			GLDebug::ScopedGroup scoped({ debugString, length });
			commandIndex++;
#endif

#if defined(NCINE_PROFILING)
			RenderStatistics::GatherStatistics(*opaqueRenderCommand);
#endif
			opaqueRenderCommand->commitCameraTransformation();
			opaqueRenderCommand->issue();
		}

		GLBlending::Enable();
		GLDepthTest::DisableDepthMask();
		// Rendering transparent nodes back to front
		for (RenderCommand* transparentRenderCommand : *transparents) {
			TracyGpuZone("Transparent");
#if defined(DEATH_DEBUG) && defined(NCINE_PROFILING)
			const std::int32_t numInstances = transparentRenderCommand->numInstances();
			const std::int32_t batchSize = transparentRenderCommand->batchSize();
			const std::uint16_t layer = transparentRenderCommand->layer();
			const std::uint16_t visitOrder = transparentRenderCommand->visitOrder();

			std::size_t length;
			if (numInstances > 0) {
				length = formatInto(debugString, "Transparent {} ({} {} on layer {}, visit order {}, sort key 0x{:x})",
								    commandIndex, numInstances, commandTypeString(*transparentRenderCommand), layer, visitOrder, transparentRenderCommand->materialSortKey());
			} else if (batchSize > 0) {
				length = formatInto(debugString, "Transparent {} ({} {} on layer {}, visit order {}, sort key 0x{:x})",
								    commandIndex, batchSize, commandTypeString(*transparentRenderCommand), layer, visitOrder, transparentRenderCommand->materialSortKey());
			} else {
				length = formatInto(debugString, "Transparent {} ({} on layer {}, visit order {}, sort key 0x{:x})",
								    commandIndex, commandTypeString(*transparentRenderCommand), layer, visitOrder, transparentRenderCommand->materialSortKey());
			}
			GLDebug::ScopedGroup scoped({ debugString, length });
			commandIndex++;
#endif

#if defined(NCINE_PROFILING)
			RenderStatistics::GatherStatistics(*transparentRenderCommand);
#endif
			GLBlending::SetBlendFunc(transparentRenderCommand->material().GetSrcBlendingFactor(), transparentRenderCommand->material().GetDestBlendingFactor());
			transparentRenderCommand->commitCameraTransformation();
			transparentRenderCommand->issue();
		}
		// Depth mask has to be enabled again before exiting this method or glClear(GL_DEPTH_BUFFER_BIT) won't have any effect
		GLDepthTest::EnableDepthMask();
		GLBlending::Disable();

		GLScissorTest::Disable();
	}

	void RenderQueue::clear()
	{
		opaqueQueue_.clear();
		opaqueBatchedQueue_.clear();
		transparentQueue_.clear();
		transparentBatchedQueue_.clear();

		RenderResources::GetRenderBatcher().reset();
	}
}
