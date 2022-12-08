#include "App.h"

G3D_START_AT_MAIN();

int main(int argc, const char* argv[])
{
	initGLG3D(G3DSpecification());

	GApp::Settings settings(argc, argv);

	settings.window.caption = argv[0];

	settings.window.fullScreen = false;
	settings.window.width = 1400;
	settings.window.height = 1000;
	settings.window.resizable = !settings.window.fullScreen;
	settings.window.framed = !settings.window.fullScreen;
	settings.window.defaultIconFilename = "icon.png";

	settings.window.asynchronous = true;

	settings.hdrFramebuffer.colorGuardBandThickness = Vector2int16(0, 0);
	settings.hdrFramebuffer.depthGuardBandThickness = Vector2int16(0, 0);

	settings.renderer.deferredShading = true;
	settings.renderer.orderIndependentTransparency = true;

	settings.dataDir = FileSystem::currentDirectory();

	settings.screenCapture.includeAppRevision = false;
	settings.screenCapture.includeG3DRevision = false;
	settings.screenCapture.filenamePrefix = "_";

	return App(settings).run();
}


App::App(const GApp::Settings& settings) : GApp(settings)
{
}

void App::onInit()
{
	GApp::onInit();

	setFrameDuration(1.0f / 240.0f);

	m_gbufferSpecification.encoding[GBuffer::Field::LAMBERTIAN].format = ImageFormat::RGBA32F();
	m_gbufferSpecification.encoding[GBuffer::Field::GLOSSY].format = ImageFormat::RGBA32F();
	m_gbufferSpecification.encoding[GBuffer::Field::EMISSIVE].format = ImageFormat::RGBA32F();
	m_gbufferSpecification.encoding[GBuffer::Field::WS_POSITION].format = ImageFormat::RGBA32F();
	m_gbufferSpecification.encoding[GBuffer::Field::WS_NORMAL] = Texture::Encoding(ImageFormat::RGBA32F(), FrameName::CAMERA, 1.0f, 0.0f);

	m_pGIRenderer = dynamic_pointer_cast<CGIRenderer>(CGIRenderer::create());
	m_pGIRenderer->setDeferredShading(true);
	m_pGIRenderer->setOrderIndependentTransparency(true);

	String SceneName = "Dragon (Dynamic Light Source)";
	loadScene(SceneName);

	m_renderer = m_pGIRenderer;
	
	makeGUI();
}

void App::onGraphics3D(RenderDevice * rd, Array<shared_ptr<Surface>>& surface3D)
{
	if (m_pIrradianceField)
	{
		m_pIrradianceField->onGraphics3D(rd, surface3D);
		if (!m_firstFrame) {
			screenProbeAdaptivePlacement(rd);
			m_pRadianceCache->setupInputs(activeCamera(),
				screenProbeWSAdaptivePositionTexture,
				screenProbeSSAdaptivePositionTexture,
				numAdaptiveScreenProbesTexture,
				m_gbuffer);
			//screenProbeDebugDraw();
			m_pRadianceCache->onGraphics3D(rd, surface3D);
			m_pRadianceCache->debugDraw();
		}
	}
	
	GApp::onGraphics3D(rd, surface3D);
	if (m_firstFrame) {
		m_firstFrame = false;

		//show(m_gbuffer_ws_position);
	}
}

void App::onAfterLoadScene(const Any & any, const String & sceneName)
{
	m_pIrradianceField = IrradianceField::create(sceneName, scene());
	m_pIrradianceField->onSceneChanged(scene());
	m_pGIRenderer->setIrradianceField(m_pIrradianceField);

	m_pRadianceCache = std::make_shared<RadianceCache>();
	
}


void App::makeGUI()
{
	debugWindow->setVisible(true);
	developerWindow->videoRecordDialog->setEnabled(true);

	debugWindow->pack();
	debugWindow->setRect(Rect2D::xywh(0, 0, (float)window()->width(), debugWindow->rect().height()));
}


void App::screenProbeDebugDraw() {

	int probeCountX = screenProbeWSUniformPositionTexture->width();
	int probeCountY = screenProbeWSUniformPositionTexture->height();
	shared_ptr<Image> screenProbeWSPositionImg = screenProbeWSUniformPositionTexture->toImage();

	const float radius = 0.01f;

	for (int i = 0; i < probeCountX; ++i)
	{
		for (int j = 0; j < probeCountY; ++j) {
			Color4 wsPosition;
			Color3 color = Color3(1.0f, 1.0f, 1.0f);
			Point2int32 index = Point2int32(i, j);
			screenProbeWSPositionImg->get(index, wsPosition);
			::debugDraw(std::make_shared<SphereShape>((Vector3)wsPosition.rgb(), radius), 0.0f, color * 0.8f, Color4::clear());
		}
	}
}


void App::screenProbeAdaptivePlacement(RenderDevice* rd) {

	if (m_staticProbe) {
		int placementDownsampleFactor = 16;
		int screenProbeDownsampleFactor = placementDownsampleFactor;
		screenProbeUniformPlacement(rd, placementDownsampleFactor);

		int minDownsampleFactor = 4;
		float maxAdaptiveFactor = 0.5f; // adaptive数量最多为uniform的0.5倍

		// TODO： View Change Clean

		// RW Buffer
		// World position
		screenProbeWSAdaptivePositionTexture = Texture::createEmpty("AdaptiveProbeWsPosition", rd->viewport().width() / placementDownsampleFactor, rd->viewport().height() / placementDownsampleFactor * maxAdaptiveFactor, ImageFormat::RGBA32F());
		shared_ptr<GLPixelTransferBuffer>& adaptiveProbeWSPosBuffer = GLPixelTransferBuffer::create(rd->viewport().width() / placementDownsampleFactor, rd->viewport().height() / placementDownsampleFactor * maxAdaptiveFactor, ImageFormat::RGBA32F());
		// Screen position
		screenProbeSSAdaptivePositionTexture = Texture::createEmpty("AdaptiveProbeSsPosition", rd->viewport().width() / placementDownsampleFactor, rd->viewport().height() / placementDownsampleFactor * maxAdaptiveFactor, ImageFormat::RGBA32F());
		shared_ptr<GLPixelTransferBuffer>& adaptiveProbeSSPosBuffer = GLPixelTransferBuffer::create(rd->viewport().width() / placementDownsampleFactor, rd->viewport().height() / placementDownsampleFactor * maxAdaptiveFactor, ImageFormat::RGBA32F());
		// Header
		screenTileAdaptiveProbeHeaderTexture = Texture::createEmpty("ScreenTileAdaptiveProbeHeader", rd->viewport().width() / placementDownsampleFactor, rd->viewport().height() / placementDownsampleFactor, ImageFormat::R32UI());
		shared_ptr<GLPixelTransferBuffer>& screenTileAdaptiveProbeHeaderBuffer = GLPixelTransferBuffer::create(rd->viewport().width() / placementDownsampleFactor, rd->viewport().height() / placementDownsampleFactor, ImageFormat::R32UI());
		// Index
		screenTileAdaptiveProbeIndicesTexture = Texture::createEmpty("ScreenTileAdaptiveProbeIndices", rd->viewport().width(), rd->viewport().height(), ImageFormat::R32UI());
		shared_ptr<GLPixelTransferBuffer>& screenTileAdaptiveProbeIndicesBuffer = GLPixelTransferBuffer::create(rd->viewport().width(), rd->viewport().height(), ImageFormat::R32UI());
		// Num
		numAdaptiveScreenProbesTexture = Texture::createEmpty("NumAdaptiveScreenProbes", 1, 1, ImageFormat::R32UI());
		shared_ptr<GLPixelTransferBuffer>& numAdaptiveScreenProbesBuffer = GLPixelTransferBuffer::create(1, 1, ImageFormat::R32UI());

		//do {
		placementDownsampleFactor /= 2;
		Args args;

		// GroupSize & GroupNum
		const Vector3int32 blockSize(16, 16, 1);
		args.setComputeGridDim(Vector3int32(iCeil(rd->viewport().width() / (float(blockSize.x) * placementDownsampleFactor)),
			iCeil(rd->viewport().height() / (float(blockSize.y) * placementDownsampleFactor)), 1));
		args.setComputeGroupSize(blockSize);



		adaptiveProbeWSPosBuffer->bindAsShaderStorageBuffer(0);
		adaptiveProbeSSPosBuffer->bindAsShaderStorageBuffer(1);
		screenTileAdaptiveProbeHeaderBuffer->bindAsShaderStorageBuffer(2);
		screenTileAdaptiveProbeIndicesBuffer->bindAsShaderStorageBuffer(3);
		numAdaptiveScreenProbesBuffer->bindAsShaderStorageBuffer(4);

		args.setUniform("placementDownsampleFactor", placementDownsampleFactor);
		args.setUniform("screenProbeDownsampleFactor", screenProbeDownsampleFactor);
		args.setUniform("viewport_width", rd->viewport().width());
		args.setUniform("viewport_height", rd->viewport().height());
		args.setUniform("ws_positionTexture", m_gbuffer->texture(GBuffer::Field::WS_POSITION), Sampler::buffer());
		args.setUniform("depthTexture", m_gbuffer->texture(GBuffer::Field::DEPTH_AND_STENCIL), Sampler::buffer());
		args.setUniform("ws_normalTexture", m_gbuffer->texture(GBuffer::Field::WS_NORMAL), Sampler::buffer());

		LAUNCH_SHADER("shaders/ScreenProbeAdaptivePlacement.glc", args);

		screenProbeWSAdaptivePositionTexture->update(adaptiveProbeWSPosBuffer);
		screenProbeSSAdaptivePositionTexture->update(adaptiveProbeSSPosBuffer);
		screenTileAdaptiveProbeHeaderTexture->update(screenTileAdaptiveProbeHeaderBuffer);
		screenTileAdaptiveProbeIndicesTexture->update(screenTileAdaptiveProbeIndicesBuffer);
		numAdaptiveScreenProbesTexture->update(numAdaptiveScreenProbesBuffer);

		//} while (placementDownsampleFactor > minDownsampleFactor);


		m_staticProbe = false;

		
	}



}

void App::screenProbeUniformPlacement(RenderDevice* rd, int downsampleFactor) {

	//m_gbuffer_ws_position = m_gbuffer->texture(GBuffer::Field::WS_POSITION);
	Args args;

	// GroupSize & GroupNum
	const Vector3int32 blockSize(16, 16, 1);
	args.setComputeGridDim(Vector3int32(iCeil(rd->viewport().width() / (float(blockSize.x) * downsampleFactor)),
		iCeil(rd->viewport().height() / (float(blockSize.y) * downsampleFactor)), 1));
	args.setComputeGroupSize(blockSize);

	// IO Variable
	screenProbeWSUniformPositionTexture = Texture::createEmpty("UniformProbeWsPosition", rd->viewport().width() / downsampleFactor, rd->viewport().height() / downsampleFactor, ImageFormat::RGBA32F());
	const shared_ptr<GLPixelTransferBuffer>& outputBuffer = GLPixelTransferBuffer::create(rd->viewport().width() / downsampleFactor, rd->viewport().height() / downsampleFactor, ImageFormat::RGBA32F());

	outputBuffer->bindAsShaderStorageBuffer(0);
	args.setUniform("placementDownsampleFactor", downsampleFactor);
	args.setUniform("viewport_width", rd->viewport().width());
	args.setUniform("viewport_height", rd->viewport().height());
	args.setUniform("ws_positionTexture", m_gbuffer->texture(GBuffer::Field::WS_POSITION), Sampler::buffer());
	args.setUniform("depthTexture", m_gbuffer->texture(GBuffer::Field::DEPTH_AND_STENCIL), Sampler::buffer());
	args.setUniform("ws_normalTexture", m_gbuffer->texture(GBuffer::Field::WS_NORMAL), Sampler::buffer());

	// Run the uniform shader
	LAUNCH_SHADER("shaders/ScreenProbeUniformPlacement.glc", args);

	screenProbeWSUniformPositionTexture->update(outputBuffer);
}
