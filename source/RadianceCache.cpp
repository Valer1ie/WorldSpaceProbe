#include "RadianceCache.h"



Vector3 operator-(const Vector3& target, float value) {
	return target - Vector3(value, value, value);
}
Vector3 operator+(const Vector3& target, float value) {
	return target + Vector3(value, value, value);
}

bool RadianceCache::UpdateRadianceCacheState(shared_ptr<Camera> camera, RadianceCacheInputs& radianceCacheInputs, RadianceCacheState& CacheState)
{

	bool bResetState = CacheState.ClipmapWorldExtent != radianceCacheInputs.ClipmapWorldExtent || CacheState.ClipmapDistributionBase != radianceCacheInputs.ClipmapDistributionBase;

	CacheState.ClipmapWorldExtent = radianceCacheInputs.ClipmapWorldExtent;
	CacheState.ClipmapDistributionBase = radianceCacheInputs.ClipmapDistributionBase;

	const int32 ClipmapResolution = radianceCacheInputs.RadianceProbeClipmapResolution;
	const int32 NumClipmaps = radianceCacheInputs.NumRadianceProbeClipmaps;

	const Vector3 NewViewOrigin = camera->frame().translation;

	CacheState.clipmaps.resize(NumClipmaps);

	for (int32 ClipmapIndex = 0; ClipmapIndex < NumClipmaps; ++ClipmapIndex)
	{
		RadianceCacheClipmap& Clipmap = CacheState.clipmaps[ClipmapIndex];

		const float ClipmapExtent = radianceCacheInputs.ClipmapWorldExtent * pow(radianceCacheInputs.ClipmapDistributionBase, ClipmapIndex);
		const float CellSize = (2.0f * ClipmapExtent) / ClipmapResolution;

		Vector3int32 GridCenter;
		GridCenter.x = floor(NewViewOrigin.x / CellSize);
		GridCenter.y = floor(NewViewOrigin.y / CellSize);
		GridCenter.z = floor(NewViewOrigin.z / CellSize);

		const Vector3 SnappedCenter = Vector3(GridCenter) * CellSize;

		Clipmap.Center = SnappedCenter;
		Clipmap.Extent = ClipmapExtent;
		Clipmap.VolumeUVOffset = Vector3(0.0f, 0.0f, 0.0f);
		Clipmap.CellSize = CellSize;

		// Shift the clipmap grid down so that probes align with other clipmaps
		const Vector3 ClipmapMin = Clipmap.Center - Clipmap.Extent - 0.5f * Clipmap.CellSize;

		Clipmap.ProbeCoordToWorldCenterBias = ClipmapMin + 0.5f * Clipmap.CellSize;
		Clipmap.ProbeCoordToWorldCenterScale = Clipmap.CellSize;

		Clipmap.WorldPositionToProbeCoordScale = 1.0f / CellSize;
		Clipmap.WorldPositionToProbeCoordBias = -ClipmapMin / CellSize;

		Clipmap.ProbeTMin = radianceCacheInputs.CalculateIrradiance ? 0.0f : Vector3(CellSize, CellSize, CellSize).magnitude();

		Vector3 WorldSpacePosition(100, 1, 1);
		Vector3 ProbeCoordFloat = WorldSpacePosition * Clipmap.WorldPositionToProbeCoordScale + Clipmap.WorldPositionToProbeCoordBias;
		Vector3 BottomEdgeFades = Vector3(ProbeCoordFloat - .5f);
		BottomEdgeFades.x = clamp(BottomEdgeFades.x, 0.f, 1.f);
		BottomEdgeFades.y = clamp(BottomEdgeFades.y, 0.f, 1.f);
		BottomEdgeFades.z = clamp(BottomEdgeFades.z, 0.f, 1.f);

		Vector3 TopEdgeFades = Vector3(Vector3(ClipmapResolution, ClipmapResolution, ClipmapResolution) - .5f - ProbeCoordFloat) * 1.f;

		TopEdgeFades.x = clamp(TopEdgeFades.x, 0.f, 1.f);
		TopEdgeFades.y = clamp(TopEdgeFades.y, 0.f, 1.f);
		TopEdgeFades.z = clamp(TopEdgeFades.z, 0.f, 1.f);

		float EdgeFade = min(min(BottomEdgeFades.x, BottomEdgeFades.y, BottomEdgeFades.z), min(TopEdgeFades.x, TopEdgeFades.y, TopEdgeFades.z));

	}

	return bResetState;
}

void RadianceCache::onGraphics3D(RenderDevice* rd, const Array<shared_ptr<Surface>>& surfaceArray)
{
	UpdateRadianceCache(rd);	
}

void RadianceCache::debugDraw() {
	shared_ptr<Image> radianceProbeWSPositionImg = RadianceProbeWorldPosition->toImage(ImageFormat::RGB32F());
	Color4 c = NumRadianceProbe->readTexel(0,0);

	int count = c.r;
	const float radius = 0.015f;
	for (int i = 0; i < count; ++i)
	{
		Color4 position;
		radianceProbeWSPositionImg->get(Point2int32(i, 0), position);
		Color3 color(0.f, 1.f, 1.f);
		//color = Color3::fromASRGB(0xff007e);

		::debugDraw(std::make_shared<SphereShape>((Vector3)position.rgb(), radius), 0.0f, color * 0.8f, Color4::clear());
	}
}

void RadianceCache::setupInputs(shared_ptr<Camera> active_camera, 
	shared_ptr<Texture> screenProbeWSAdaptivePositionTexture, 
	shared_ptr<Texture> screenProbeSSAdaptivePositionTexture, 
	shared_ptr<Texture> numAdaptiveScreenProbesTexture,
	shared_ptr<GBuffer> gbuffer)
{
	activeCamera = active_camera;
	int numMipMaps = 1;
	const int MaxClipmaps = 6;
	radianceCacheInputs.ReprojectionRadiusScale = 1.5f;
	radianceCacheInputs.ClipmapWorldExtent = 20.f;
	radianceCacheInputs.ClipmapDistributionBase = 2.f;
	radianceCacheInputs.RadianceProbeClipmapResolution = iClamp(64, 1, 256);
	radianceCacheInputs.ProbeAtlasResolutionInProbes = Vector2int32(128, 128);
	radianceCacheInputs.NumRadianceProbeClipmaps = iClamp(4, 1, MaxClipmaps);
	radianceCacheInputs.RadianceProbeResolution = 16;
	radianceCacheInputs.FinalProbeResolution = 16 + 2 * (1 << (numMipMaps - 1));
	radianceCacheInputs.FinalRadianceAtlasMaxMip = numMipMaps - 1;
	radianceCacheInputs.CalculateIrradiance = 0;
	radianceCacheInputs.IrradianceProbeResolution = 6;
	radianceCacheInputs.OcclusionProbeResolution = 16;
	radianceCacheInputs.NumProbesToTraceBudget = 200;
	radianceCacheInputs.RadianceCacheStats = 0;
	radianceCacheInputs.InvClipmapFadeSize = 1.0f / clamp(1.f, .001f, 16.0f);
	
	AdaptiveProbeWSPosition = screenProbeWSAdaptivePositionTexture;
	AdaptiveProbeSSPosition = screenProbeSSAdaptivePositionTexture;
	AdaptiveProbeNum = numAdaptiveScreenProbesTexture;
	m_gbuffer = gbuffer;
}

void RadianceCache::UpdateRadianceCache(RenderDevice* rd) {
	Array<RadianceCacheClipmap> lastFrameClipmap = radianceCacheState.clipmaps;
	bool resizedHistoryState = UpdateRadianceCacheState(activeCamera, radianceCacheInputs, radianceCacheState);

	//TODO: depthProbeAtlas texture


	const Vector3int32 RadianceProbeIndirectionTextureSize = Vector3int32(
		radianceCacheInputs.RadianceProbeClipmapResolution * radianceCacheInputs.NumRadianceProbeClipmaps,
		radianceCacheInputs.RadianceProbeClipmapResolution,
		radianceCacheInputs.RadianceProbeClipmapResolution);

	if (m_finalRadianceAtlas) {}
	else {
		GBuffer::Specification gbufferRTSpec;
		gbufferRTSpec.encoding[GBuffer::Field::LAMBERTIAN].format = ImageFormat::RGBA32F();
		gbufferRTSpec.encoding[GBuffer::Field::GLOSSY].format = ImageFormat::RGBA32F();
		gbufferRTSpec.encoding[GBuffer::Field::EMISSIVE].format = ImageFormat::RGBA32F();
		gbufferRTSpec.encoding[GBuffer::Field::TRANSMISSIVE].format = ImageFormat::RGBA32F();
		gbufferRTSpec.encoding[GBuffer::Field::WS_POSITION].format = ImageFormat::RGBA32F();
		gbufferRTSpec.encoding[GBuffer::Field::WS_NORMAL] = Texture::Encoding(ImageFormat::RGBA32F(), FrameName::CAMERA, 1.0f, 0.0f);
		gbufferRTSpec.encoding[GBuffer::Field::DEPTH_AND_STENCIL].format = nullptr;
		gbufferRTSpec.encoding[GBuffer::Field::CS_NORMAL] = nullptr;
		gbufferRTSpec.encoding[GBuffer::Field::CS_POSITION] = nullptr;

		Vector2int32 extent = m_specification.finalRadianceAtlasExtent;

		m_finalRadianceAtlas = GBuffer::create(gbufferRTSpec, "RadianceCache::m_radianceCacheFinalGBuffer");
		m_finalRadianceAtlas->setSpecification(gbufferRTSpec);
		m_finalRadianceAtlas->resize(extent.x, extent.y);
	}
	if (radianceCacheInputs.CalculateIrradiance) {
		//radiance irradiance occlusion

	}
	else {
		const Vector2int32 FinalRadianceAtlasSize = m_specification.finalRadianceAtlasExtent;
		float ScreenProbeDownsampleFactor = 16;
		float fraction = 0.5;

		if(!m_radianceProbeIndirectionTexture)
			m_radianceProbeIndirectionTexture = Texture::createEmpty(
				"RadianceCache::m_radianceProbeIndirect",
				RadianceProbeIndirectionTextureSize.x,
				RadianceProbeIndirectionTextureSize.y,
				ImageFormat::R32UI(),
				Texture::DIM_3D,
				false,
				RadianceProbeIndirectionTextureSize.z,
				1);
		if(!testRadianceIndirect)
			testRadianceIndirect = Texture::createEmpty("RadianceCache::testRadianceProbeIndirect",
				RadianceProbeIndirectionTextureSize.x,
				RadianceProbeIndirectionTextureSize.y * RadianceProbeIndirectionTextureSize.z,
				ImageFormat::R32UI(),
				Texture::DIM_2D,
				false,
				1,
				1);
		//clear indirection
		{
			Args args;
			Vector3int32 extent(m_radianceProbeIndirectionTexture->width(), m_radianceProbeIndirectionTexture->height(), m_radianceProbeIndirectionTexture->depth());
			Vector3int32 groupsize(4, 4, 4); //groupsize = 4
			args.setComputeGroupSize(groupsize);
			args.setComputeGridDim(extent / groupsize);
			args.setImageUniform("RWRadianceProbeIndirectionTexture", m_radianceProbeIndirectionTexture, Access::READ_WRITE, false);
			LAUNCH_SHADER("shaders/WorldSpaceProbe_ClearProbeIndirect.glc", args);
		}

		const int ClipmapCount = radianceCacheState.clipmaps.size();
		auto Clipmaps = radianceCacheState.clipmaps;
		Array<Vector4> world2probeData;
		Array<Vector4> probe2worldData;
		world2probeData.resize(ClipmapCount);
		probe2worldData.resize(ClipmapCount);
		for (int i = 0; i < ClipmapCount; i++)
		{
			auto value1 = Vector4(Clipmaps[i].WorldPositionToProbeCoordBias, Clipmaps[i].WorldPositionToProbeCoordScale);;
			world2probeData[i] = Vector4(Clipmaps[i].WorldPositionToProbeCoordBias, Clipmaps[i].WorldPositionToProbeCoordScale);
			probe2worldData[i] = Vector4(Clipmaps[i].ProbeCoordToWorldCenterBias, Clipmaps[i].ProbeCoordToWorldCenterScale);
		}
		//mark used probes
		{
			/*AdaptiveProbeWSPosition = Texture::createEmpty("AdaptiveProbeWsPosition", rd->viewport().width() / ScreenProbeDownsampleFactor, rd->viewport().height() / ScreenProbeDownsampleFactor * fraction, ImageFormat::RGB32F());

			AdaptiveProbeSSPosition = Texture::createEmpty("AdaptiveProbeSsPosition", rd->viewport().width() / ScreenProbeDownsampleFactor, rd->viewport().height() / ScreenProbeDownsampleFactor * fraction, ImageFormat::RGB32F());

			AdaptiveProbeNum = Texture::createEmpty("NumAdaptiveScreenProbes", 1, 1, ImageFormat::RGB32F());*/

			shared_ptr<GLPixelTransferBuffer> adaptiveProbeWSPosition = AdaptiveProbeWSPosition->toPixelTransferBuffer();
			shared_ptr<GLPixelTransferBuffer> adaptiveProbeSSPosition = AdaptiveProbeSSPosition->toPixelTransferBuffer();
			shared_ptr<GLPixelTransferBuffer> adaptiveProbeNum = AdaptiveProbeNum->toPixelTransferBuffer();
			shared_ptr<GLPixelTransferBuffer> worldPositionToRadianceProbeCoordForMark = GLPixelTransferBuffer::create(ClipmapCount, 1, ImageFormat::RGBA32F(), world2probeData.getCArray());
			shared_ptr<GLPixelTransferBuffer> radianceProbeCoordToWorldPosition = GLPixelTransferBuffer::create(ClipmapCount, 1, ImageFormat::RGBA32F(), probe2worldData.getCArray());



			adaptiveProbeWSPosition->bindAsShaderStorageBuffer(0);
			adaptiveProbeSSPosition->bindAsShaderStorageBuffer(1);
			adaptiveProbeNum->bindAsShaderStorageBuffer(2);
			worldPositionToRadianceProbeCoordForMark->bindAsShaderStorageBuffer(3);
			radianceProbeCoordToWorldPosition->bindAsShaderStorageBuffer(4);


			Vector2int32 sceneTextureExtent(2048, 2048);
			Vector2int32 ScreenProbeViewSize = Vector2int32(iCeil(rd->viewport().extent().x / ScreenProbeDownsampleFactor), iCeil(rd->viewport().extent().y / ScreenProbeDownsampleFactor));
			Vector2int32 ScreenProbeAtlasViewSize = ScreenProbeViewSize;
			ScreenProbeAtlasViewSize.y += ScreenProbeViewSize.y * fraction; //ScreenProbeGatherAdaptiveProbeAllocationFraction

			Vector2int32 ScreenProbeAtlasBufferSize = Vector2int32(iCeil(sceneTextureExtent.x / ScreenProbeDownsampleFactor), iCeil(sceneTextureExtent.y / ScreenProbeDownsampleFactor));
			ScreenProbeAtlasBufferSize.y += ScreenProbeAtlasBufferSize.y * fraction;
			int	 NumUniformScreenProbes = ScreenProbeViewSize.x * ScreenProbeViewSize.y;
			int	 MaxNumAdaptiveProbes = NumUniformScreenProbes * fraction;

			Args args;
			Vector3int32 groupSize(16, 16, 1);
			args.setComputeGroupSize(groupSize);
			args.setComputeGridDim(Vector3int32(ScreenProbeAtlasViewSize, 1) / groupSize);
			args.setImageUniform("RadianceProbeIndirectionTexture", m_radianceProbeIndirectionTexture, Access::READ_WRITE, false);
			args.setImageUniform("testRadianceProbeIndirectionTexture", testRadianceIndirect, Access::READ_WRITE, false);
			args.setUniform("ScreenProbeAtlasViewSize", ScreenProbeAtlasViewSize);
			args.setUniform("ScreenProbeViewSize", ScreenProbeViewSize);
			args.setUniform("ScreenProbeDownsampleFactor", int(ScreenProbeDownsampleFactor));
			args.setUniform("NumUniformScreenProbes", NumUniformScreenProbes);
			args.setUniform("MaxNumAdaptiveProbes", MaxNumAdaptiveProbes);

			args.setUniform("NumRadianceProbeClipmapsForMark", ClipmapCount);
			args.setUniform("RadianceProbeClipmapResolutionForMark", radianceCacheInputs.RadianceProbeClipmapResolution);
			args.setUniform("InvClipmapFadeSizeForMark", radianceCacheInputs.InvClipmapFadeSize);

			args.setUniform("ws_positionTexture", m_gbuffer->texture(GBuffer::Field::WS_POSITION), Sampler::buffer());
			args.setUniform("depthTexture", m_gbuffer->texture(GBuffer::Field::DEPTH_AND_STENCIL), Sampler::buffer());
			args.setUniform("ws_normalTexture", m_gbuffer->texture(GBuffer::Field::WS_NORMAL), Sampler::buffer());
			/*
			uniform ivec2 ScreenProbeAtlasViewSize;
			uniform ivec2 ScreenProbeViewSize;
			uniform int ScreenProbeDownsampleFactor;
			uniform int NumUniformScreenProbes;
			uniform int MaxNumAdaptiveProbes;


			uniform int NumRadianceProbeClipmapsForMark;
			uniform uint RadianceProbeClipmapResolutionForMark;
			uniform float InvClipmapFadeSizeForMark;

			// Texture
			uniform sampler2D   ws_positionTexture;
			uniform sampler2D   depthTexture;
			uniform sampler2D   ws_normalTexture;

			*/

			LAUNCH_SHADER("shaders/WorldSpaceProbePlacement.glc", args);
		}

		{
			shared_ptr<GLPixelTransferBuffer>& worldProbePosition = GLPixelTransferBuffer::create(2048, 1, ImageFormat::RGBA32F());
			//shared_ptr<GLPixelTransferBuffer>& numWorldProbe = GLPixelTransferBuffer::create(1, 1, ImageFormat::RGB32I());

			shared_ptr<GLPixelTransferBuffer>& worldPositionToRadianceProbeCoordForMark = GLPixelTransferBuffer::create(ClipmapCount, 1, ImageFormat::RGBA32F(), world2probeData.getCArray());
			shared_ptr<GLPixelTransferBuffer>& radianceProbeCoordToWorldPosition = GLPixelTransferBuffer::create(ClipmapCount, 1, ImageFormat::RGBA32F(), probe2worldData.getCArray());

			/*
			layout( local_size_variable ) in;
			layout(std430, binding = 0) buffer worldSpacePosition{vec3 worldspacePositionData[];};

			layout(r32ui) uniform uimage3D RadianceProbeIndirectionTexture;

			layout (binding = 1, offset = 0) uniform atomic_uint  probeCount;
			layout(std430, binding=2) buffer worldPositionToRadianceProbeCoordForMark {vec4 WorldPositionToRadianceProbeCoordForMark[];};

			layout(std430, binding=3) buffer radianceProbeCoordToWorldPosition {vec4 RadianceProbeCoordToWorldPosition[];};

			uniform int clipmapResolution;
			*/
			if (!RadianceProbeWorldPosition) {
				RadianceProbeWorldPosition = Texture::createEmpty("RadianceCache::RadianceProbeWorldPosition", 2048, 1, ImageFormat::RGBA32F());

			}
			if (!NumRadianceProbe) {
				NumRadianceProbe = Texture::createEmpty("RadianceCache::NumRadianceProbe", 1, 1, ImageFormat::R32UI());
			}
			worldProbePosition->bindAsShaderStorageBuffer(0);
			worldPositionToRadianceProbeCoordForMark->bindAsShaderStorageBuffer(2);
			radianceProbeCoordToWorldPosition->bindAsShaderStorageBuffer(3);
			//numWorldProbe->bindAsShaderStorageBuffer(4);
			Args args;
			Vector3int32 groupSize(4,4,4);
			Vector3int32 extent(m_radianceProbeIndirectionTexture->width(), m_radianceProbeIndirectionTexture->height(), m_radianceProbeIndirectionTexture->depth());
			args.setComputeGroupSize(groupSize);
			args.setComputeGridDim(extent / groupSize);
			args.setUniform("probeCount", 0);
			args.setUniform("clipmapResolution", radianceCacheInputs.RadianceProbeClipmapResolution);
			args.setImageUniform("RadianceProbeIndirectionTexture", m_radianceProbeIndirectionTexture, Access::READ_WRITE, false);
			args.setImageUniform("numWorldSpacePosition", NumRadianceProbe, Access::READ_WRITE, false);
			LAUNCH_SHADER("shaders/WorldSpaceProbe_Gather.glc", args);
			
			RadianceProbeWorldPosition->update(worldProbePosition);
			//NumRadianceProbe->update(numWorldProbe);
		}




	}
}