#include "NvBlastExtAuthoringFractureToolImpl.h"
#include "NvBlastExtAuthoringMeshImpl.h"

// ManoEngine only exports the three authoring factories required by the automatic
// Voronoi bake.  Keeping this bridge small avoids pulling the optional exporter,
// serialization and legacy APEX tools into the editor distribution.
extern "C" {
	__declspec(dllexport) Nv::Blast::Mesh* ManoBlastAuthoringCreateMesh(
		const NvcVec3* positions,
		const NvcVec3* normals,
		const NvcVec2* uv,
		uint32_t vertexCount,
		const uint32_t* indices,
		uint32_t indexCount) {
		return new Nv::Blast::MeshImpl(
			positions,
			normals,
			uv,
			vertexCount,
			indices,
			indexCount);
	}

	__declspec(dllexport) Nv::Blast::VoronoiSitesGenerator* ManoBlastAuthoringCreateVoronoiSitesGenerator(
		Nv::Blast::Mesh* mesh,
		Nv::Blast::RandomGeneratorBase* randomGenerator) {
		return new Nv::Blast::VoronoiSitesGeneratorImpl(mesh, randomGenerator);
	}

	__declspec(dllexport) Nv::Blast::FractureTool* ManoBlastAuthoringCreateFractureTool() {
		return new Nv::Blast::FractureToolImpl();
	}
}
