#include "NvBlastExtAuthoringFractureToolImpl.h"
#include "NvBlastExtAuthoringMeshImpl.h"

// CG2Engine only exports the three authoring factories required by the automatic
// Voronoi bake.  Keeping this bridge small avoids pulling the optional exporter,
// serialization and legacy APEX tools into the editor distribution.
extern "C" {
	__declspec(dllexport) Nv::Blast::Mesh* CG2BlastAuthoringCreateMesh(
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

	__declspec(dllexport) Nv::Blast::VoronoiSitesGenerator* CG2BlastAuthoringCreateVoronoiSitesGenerator(
		Nv::Blast::Mesh* mesh,
		Nv::Blast::RandomGeneratorBase* randomGenerator) {
		return new Nv::Blast::VoronoiSitesGeneratorImpl(mesh, randomGenerator);
	}

	__declspec(dllexport) Nv::Blast::FractureTool* CG2BlastAuthoringCreateFractureTool() {
		return new Nv::Blast::FractureToolImpl();
	}
}
