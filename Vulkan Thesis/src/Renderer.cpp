#include "Renderer.h"

#include <GLFW/glfw3.h>

#include "Context.h"
#include "Model.h"
#include "ShaderCompiler.h"

struct PushConstantObject
{
};

struct Camera
{
	glm::mat4 view;
	glm::mat4 projection;
};

struct SceneObject
{
	glm::mat4 model;
};

Renderer::Renderer(GLFWwindow* window)
	: mContext(window)
	, mUtility(mContext)
{
	createSwapChain();
	createSwapChainImageViews();
	createRenderPasses();
	//createDescriptorSetLayouts();
	createGraphicsPipelines();
	//createComputePipeline();
	createDepthResources();
	createFrameBuffers();
	//createTextureSampler();
	//createUniformBuffers();
	////createLights();
	//createDescriptorPool();
	mModel.loadModel(mContext, "", *mTextureSampler, *mDescriptorPool, *mObjectDescriptorSetLayout);
	////model = VModel::loadModelFromFile(vulkan_context, getGlobalTestSceneConfiguration().model_file, texture_sampler.get(), descriptor_pool.get(), material_descriptor_set_layout.get());
	//createSceneObjectDescriptorSet();
	//createCameraDescriptorSet();
	////createIntermediateDescriptorSet();
	////updateIntermediateDescriptorSet();
	////createLigutCullingDescriptorSet();
	////createLightVisibilityBuffer(); 
	createGraphicsCommandBuffers();
	////createLightCullingCommandBuffer();
	////createDepthPrePassCommandBuffer();
	createSemaphores();
}

void Renderer::requestDraw(float deltatime)
{
	//updateUniformBuffers(/*deltatime*/);
	drawFrame();
}

void Renderer::cleanUp()
{
	mContext.getDevice().waitIdle();
}

void Renderer::setCamera(const glm::mat4& view, const glm::vec3 campos)
{
	mViewMatrix = view;
	//mCameraPosition = campos;
}

void Renderer::recreateSwapChain()
{
	vkDeviceWaitIdle(mContext.getDevice());

	createSwapChain();
	createSwapChainImageViews();
	createRenderPasses();
	createGraphicsPipelines();
	createDepthResources();
	createFrameBuffers();
	//createLightVisibilityBuffer(); // since it's size will scale with window;
	//updateIntermediateDescriptorSet();
	createGraphicsCommandBuffers();
	//createLightCullingCommandBuffer(); // it needs light_visibility_buffer_size, which is changed on resize
	//createDepthPrePassCommandBuffer();
}

void Renderer::createSwapChain()
{
	//auto support_details = SwapChainSupportDetails::querySwapChainSupport(physical_device, vulkan_context.getWindowSurface());
	auto capabilities = mContext.getPhysicalDevice().getSurfaceCapabilitiesKHR(mContext.getWindowSurface());
	auto formats = mContext.getPhysicalDevice().getSurfaceFormatsKHR(mContext.getWindowSurface());
	auto presentModes = mContext.getPhysicalDevice().getSurfacePresentModesKHR(mContext.getWindowSurface());

	auto surfaceFormat = mUtility.chooseSwapSurfaceFormat(formats);
	auto presentMode = mUtility.chooseSwapPresentMode(presentModes);
	auto extent = mUtility.chooseSwapExtent(capabilities);

	// 0 for maxImageCount means no limit
	uint32_t queueSize = capabilities.minImageCount + 1;
	if (capabilities.maxImageCount > 0 && queueSize > capabilities.maxImageCount)
		queueSize = capabilities.maxImageCount;

	vk::SwapchainCreateInfoKHR swapchainInfo;
	swapchainInfo.surface = mContext.getWindowSurface();
	swapchainInfo.minImageCount = queueSize;
	swapchainInfo.imageFormat = surfaceFormat.format;
	swapchainInfo.imageColorSpace = surfaceFormat.colorSpace;
	swapchainInfo.imageExtent = extent;
	swapchainInfo.imageArrayLayers = 1;
	swapchainInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;

	QueueFamilyIndices indices = QueueFamilyIndices::findQueueFamilies(mContext.getPhysicalDevice(), mContext.getWindowSurface());
	uint32_t queueFamilyIndices[] = { static_cast<uint32_t>(indices.graphicsFamily), static_cast<uint32_t>(indices.presentFamily) };

	if (indices.graphicsFamily != indices.presentFamily)
	{
		swapchainInfo.imageSharingMode = vk::SharingMode::eConcurrent;
		swapchainInfo.queueFamilyIndexCount = 2;
		swapchainInfo.pQueueFamilyIndices = queueFamilyIndices;
	}
	else
		swapchainInfo.imageSharingMode = vk::SharingMode::eExclusive;

	auto oldSwapchain = std::move(mSwapchain); //which will be destroyed when out of scope

	swapchainInfo.preTransform = capabilities.currentTransform;
	swapchainInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
	swapchainInfo.presentMode = presentMode;
	swapchainInfo.clipped = VK_TRUE;
	swapchainInfo.oldSwapchain = *oldSwapchain; // required when recreating a swap chain (like resizing windows)
	
	mSwapchain = mContext.getDevice().createSwapchainKHRUnique(swapchainInfo);
	mSwapchainImages = mContext.getDevice().getSwapchainImagesKHR(*mSwapchain);
	mSwapchainImageFormat = surfaceFormat.format;
	mSwapchainExtent = extent;
}

void Renderer::createSwapChainImageViews()
{
	mSwapchainImageViews.clear();
	for (const auto& image : mSwapchainImages) 
		mSwapchainImageViews.emplace_back(mUtility.createImageView(image, mSwapchainImageFormat, vk::ImageAspectFlagBits::eColor));
}

void Renderer::createRenderPasses()
{
	// the render pass
	{
		vk::AttachmentDescription colorAttachment;
		colorAttachment.format = mSwapchainImageFormat;
		colorAttachment.samples = vk::SampleCountFlagBits::e1;
		colorAttachment.loadOp = vk::AttachmentLoadOp::eClear; // before rendering
		colorAttachment.storeOp = vk::AttachmentStoreOp::eStore; // after rendering
		colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare; // no stencil
		colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
		colorAttachment.finalLayout = vk::ImageLayout::ePresentSrcKHR; // to be directly used in swap chain

		auto formatCandidates = { vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint };

		vk::AttachmentDescription depthAttachment;
		depthAttachment.format = mUtility.findSupportedFormat(formatCandidates, vk::ImageTiling::eOptimal, vk::FormatFeatureFlagBits::eDepthStencilAttachment);
		depthAttachment.samples = vk::SampleCountFlagBits::e1;
		depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
		depthAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;
		depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		depthAttachment.initialLayout = vk::ImageLayout::eUndefined;
		depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

		vk::AttachmentReference colorAttachmentRef;
		colorAttachmentRef.attachment = 0;
		colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

		vk::AttachmentReference depthAttachmentRef;
		depthAttachmentRef.attachment = 1;
		depthAttachmentRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

		vk::SubpassDescription subpass;
		subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorAttachmentRef;
		subpass.pDepthStencilAttachment = &depthAttachmentRef;

		// overwrite subpass dependency to make it wait until VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
		//vk::SubpassDependency dependency;
		//dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		//dependency.dstSubpass = 0; // 0  refers to the subpass
		//dependency.srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
		//dependency.srcAccessMask = vk::AccessFlagBits::eMemoryRead;
		//dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		//dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;

		std::array<vk::SubpassDependency, 2> dependencies;
		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0; 
		dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
		dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependencies[0].srcAccessMask = vk::AccessFlagBits::eMemoryRead;
		dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;
		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
		dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		dependencies[1].dstAccessMask = vk::AccessFlagBits::eMemoryRead;
		dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;


		std::array<vk::AttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };

		vk::RenderPassCreateInfo renderpassInfo;
		renderpassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		renderpassInfo.pAttachments = attachments.data();
		renderpassInfo.subpassCount = 1;
		renderpassInfo.pSubpasses = &subpass;
		renderpassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderpassInfo.pDependencies = dependencies.data();

		mRenderpass = mContext.getDevice().createRenderPassUnique(renderpassInfo);
	}
}

void Renderer::createDescriptorSetLayouts()
{
	// random
	{
		// Transform information
		// create descriptor for uniform buffer objects
		vk::DescriptorSetLayoutBinding samplerBinding;
		samplerBinding.binding = 0;
		samplerBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
		samplerBinding.descriptorCount = 1;
		samplerBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

		vk::DescriptorSetLayoutBinding uboBinding;
		uboBinding.binding = 1;
		uboBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
		uboBinding.descriptorCount = 1;
		uboBinding.stageFlags = vk::ShaderStageFlagBits::eVertex;

		std::array<vk::DescriptorSetLayoutBinding, 2> bindings = { samplerBinding, uboBinding };

		vk::DescriptorSetLayoutCreateInfo layoutInfo;
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();

		mObjectDescriptorSetLayout = mContext.getDevice().createDescriptorSetLayoutUnique(layoutInfo);
	}

	// camera
	{
		vk::DescriptorSetLayoutBinding cameraBinding;
		cameraBinding.binding = 0;
		cameraBinding.descriptorType = vk::DescriptorType::eStorageBuffer;
		cameraBinding.descriptorCount = 1;
		cameraBinding.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute;

		vk::DescriptorSetLayoutCreateInfo createInfo;
		createInfo.bindingCount = 1;
		createInfo.pBindings = &cameraBinding;

		mCameraDescriptorSetLayout = mContext.getDevice().createDescriptorSetLayoutUnique(createInfo);
	}
}

void Renderer::createGraphicsPipelines()
{
	// create main pipeline
	{
		auto vertShader = createShaderModule("data/shader.vert");
		auto fragShader = createShaderModule("data/shader.frag");

		vk::PipelineShaderStageCreateInfo vertexStageInfo;
		vertexStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
		vertexStageInfo.module = *vertShader;
		vertexStageInfo.pName = "main";

		vk::PipelineShaderStageCreateInfo fragmentStageInfo;
		fragmentStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
		fragmentStageInfo.module = *fragShader;
		fragmentStageInfo.pName = "main";

		vk::PipelineShaderStageCreateInfo shaderStages[] = { vertexStageInfo, fragmentStageInfo };

		// vertex data info
		vk::PipelineVertexInputStateCreateInfo vertexInputInfo;

		auto bindingDescription = util::getVertexBindingDesciption();
		auto attrDescription = util::getVertexAttributeDescriptions();

		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescription.size());
		vertexInputInfo.pVertexAttributeDescriptions = attrDescription.data(); // Optional

		// input assembler
		vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo;
		inputAssemblyInfo.topology = vk::PrimitiveTopology::eTriangleList;
		inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;

		// viewport
		vk::Viewport viewport;
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(mSwapchainExtent.width);
		viewport.height = static_cast<float>(mSwapchainExtent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		vk::Rect2D scissor;
		scissor.offset = vk::Offset2D{ 0, 0 };
		scissor.extent = mSwapchainExtent;
		
		vk::PipelineViewportStateCreateInfo viewportInfo;
		viewportInfo.viewportCount = 1;
		viewportInfo.pViewports = &viewport;
		viewportInfo.scissorCount = 1;
		viewportInfo.pScissors = &scissor;

		vk::PipelineRasterizationStateCreateInfo rasterizer;
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = vk::PolygonMode::eFill;
		rasterizer.lineWidth = 1.0f; // requires wideLines feature enabled when larger than one
		rasterizer.cullMode = vk::CullModeFlagBits::eNone;
		rasterizer.frontFace = vk::FrontFace::eCounterClockwise; // inverted Y during projection matrix
		rasterizer.depthBiasEnable = VK_FALSE;
		rasterizer.depthBiasConstantFactor = 0.0f; // Optional
		rasterizer.depthBiasClamp = 0.0f; // Optional
		rasterizer.depthBiasSlopeFactor = 0.0f; // Optional

		// no multisampling
		vk::PipelineMultisampleStateCreateInfo multisampling;
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;
		multisampling.minSampleShading = 1.0f; // Optional
		multisampling.pSampleMask = nullptr; /// Optional
		multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
		multisampling.alphaToOneEnable = VK_FALSE; // Optional

		// depth and stencil
		vk::PipelineDepthStencilStateCreateInfo depthStencil;
		depthStencil.depthTestEnable = VK_TRUE;
		depthStencil.depthWriteEnable = VK_TRUE; // not VK_TRUE since we have a depth prepass
		depthStencil.depthCompareOp = vk::CompareOp::eLess;
		//depthStencil.depthCompareOp = vk::CompareOp::eLessOrEqual; //not VK_COMPARE_OP_LESS since we have a depth prepass;
		depthStencil.depthBoundsTestEnable = VK_FALSE;
		depthStencil.stencilTestEnable = VK_FALSE;

		vk::PipelineColorBlendAttachmentState colorblendAttachment;
		colorblendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
		colorblendAttachment.blendEnable = VK_FALSE;
		colorblendAttachment.srcColorBlendFactor = vk::BlendFactor::eOne;
		colorblendAttachment.dstColorBlendFactor = vk::BlendFactor::eZero;
		//colorblendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
		//colorblendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
		colorblendAttachment.colorBlendOp = vk::BlendOp::eAdd;
		colorblendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
		colorblendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
		colorblendAttachment.alphaBlendOp = vk::BlendOp::eAdd;

		vk::PipelineColorBlendStateCreateInfo blendingInfo;
		blendingInfo.logicOpEnable = VK_FALSE;
		blendingInfo.logicOp = vk::LogicOp::eCopy; // Optional
		blendingInfo.attachmentCount = 1;
		blendingInfo.pAttachments = &colorblendAttachment;
		blendingInfo.blendConstants[0] = 0.0f; // Optional
		blendingInfo.blendConstants[1] = 0.0f; // Optional
		blendingInfo.blendConstants[2] = 0.0f; // Optional
		blendingInfo.blendConstants[3] = 0.0f; // Optional

		// parameters allowed to be changed without recreating a pipeline
		vk::DynamicState dynamicStates[] = { vk::DynamicState::eViewport, vk::DynamicState::eLineWidth };

		vk::PipelineDynamicStateCreateInfo dynamicStateInfo;
		dynamicStateInfo.dynamicStateCount = 2;
		dynamicStateInfo.pDynamicStates = dynamicStates;

		//vk::PushConstantRange pushconstantRange;
		//pushconstantRange.offset = 0;
		//pushconstantRange.size = sizeof(PushConstantObject);
		//pushconstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex;

		// no uniform variables or push constants
		vk::PipelineLayoutCreateInfo layoutInfo;
		//std::vector<vk::DescriptorSetLayout> setLayouts = { *mObjectDescriptorSetLayout, *mCameraDescriptorSetLayout };
		//layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
		//layoutInfo.pSetLayouts = setLayouts.data(); 
		//layoutInfo.pushConstantRangeCount = 1; 
		//layoutInfo.pPushConstantRanges = &pushconstantRange; 

		mPipelineLayout = mContext.getDevice().createPipelineLayoutUnique(layoutInfo);

		vk::GraphicsPipelineCreateInfo pipelineInfo;
		pipelineInfo.stageCount = 2;
		pipelineInfo.pStages = shaderStages;
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
		pipelineInfo.pViewportState = &viewportInfo;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pDepthStencilState = &depthStencil;
		pipelineInfo.pColorBlendState = &blendingInfo;
		pipelineInfo.pDynamicState = nullptr; // Optional
		pipelineInfo.layout = *mPipelineLayout;
		pipelineInfo.renderPass = *mRenderpass;
		pipelineInfo.subpass = 0;
		pipelineInfo.basePipelineHandle = nullptr; // not deriving from existing pipeline
		pipelineInfo.basePipelineIndex = -1;
		//pipelineInfo.flags = vk::PipelineCreateFlagBits::eAllowDerivatives;

		mGraphicsPipeline = mContext.getDevice().createGraphicsPipelineUnique({}, pipelineInfo);
	}
}

void Renderer::createDepthResources()
{
	auto formatCandidates = { vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint };
	auto depthFormat = mUtility.findSupportedFormat(formatCandidates, vk::ImageTiling::eOptimal, vk::FormatFeatureFlagBits::eDepthStencilAttachment);

	// for depth pre pass and output as texture
	mDepthImage = mUtility.createImage(
		mSwapchainExtent.width, mSwapchainExtent.height,
		depthFormat,
		vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eDepthStencilAttachment/* | vk::ImageUsageFlagBits::eSampled*/,
		vk::MemoryPropertyFlagBits::eDeviceLocal
	);

	mDepthImage.view = mUtility.createImageView(*mDepthImage.handle, depthFormat, vk::ImageAspectFlagBits::eDepth);
	mUtility.transitImageLayout(*mDepthImage.handle, vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal);
}

void Renderer::createFrameBuffers()
{
	// swap chain frame buffers
	{
		mSwapchainFramebuffers.clear();
		mSwapchainFramebuffers.reserve(mSwapchainImageViews.size());

		for (const auto& view : mSwapchainImageViews)
		{
			std::array<vk::ImageView, 2> attachments = { *view, *mDepthImage.view };
			
			vk::FramebufferCreateInfo framebufferInfo;
			framebufferInfo.renderPass = *mRenderpass;
			framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
			framebufferInfo.pAttachments = attachments.data();
			framebufferInfo.width = mSwapchainExtent.width;
			framebufferInfo.height = mSwapchainExtent.height;
			framebufferInfo.layers = 1;

			mSwapchainFramebuffers.emplace_back(mContext.getDevice().createFramebufferUnique(framebufferInfo));
		}
	}
}

void Renderer::createTextureSampler()
{
	vk::SamplerCreateInfo samplerInfo = {};
	samplerInfo.magFilter = vk::Filter::eLinear;
	samplerInfo.minFilter = vk::Filter::eLinear;

	samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
	samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
	samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;

	samplerInfo.anisotropyEnable = VK_TRUE;
	samplerInfo.maxAnisotropy = 16;

	samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
	samplerInfo.unnormalizedCoordinates = VK_FALSE;

	samplerInfo.compareEnable = VK_FALSE;
	samplerInfo.compareOp = vk::CompareOp::eAlways;

	samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
	samplerInfo.mipLodBias = 0.0f;
	samplerInfo.minLod = 0.0f;
	samplerInfo.maxLod = 0.0f;

	mTextureSampler = mContext.getDevice().createSamplerUnique(samplerInfo);
}

void Renderer::createUniformBuffers()
{
	// create buffers for scene object
	{
		vk::DeviceSize bufferSize = sizeof(SceneObject);

		mObjectStagingBuffer = mUtility.createBuffer(
			bufferSize, 
			vk::BufferUsageFlagBits::eTransferSrc, 
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
		);

		mObjectUniformBuffer = mUtility.createBuffer(
			bufferSize,
			vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eUniformBuffer,
			vk::MemoryPropertyFlagBits::eDeviceLocal
		);
	}

	// Adding data to scene object buffer
	{
		SceneObject ubo;
		ubo.model = glm::mat4(1.0f);

		auto data = mContext.getDevice().mapMemory(*mObjectStagingBuffer.memory, 0, sizeof(ubo), {});
		memcpy(data, &ubo, sizeof(ubo));
		mContext.getDevice().unmapMemory(*mObjectStagingBuffer.memory);
		mUtility.copyBuffer(*mObjectStagingBuffer.handle, *mObjectUniformBuffer.handle, sizeof(ubo));
	}

	// camera 
	{
		vk::DeviceSize bufferSize = sizeof(Camera);

		mCameraStagingBuffer = mUtility.createBuffer(
			bufferSize,
			vk::BufferUsageFlagBits::eTransferSrc,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
		);
		
		mCameraUniformBuffer = mUtility.createBuffer(
			bufferSize,
			vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
			vk::MemoryPropertyFlagBits::eDeviceLocal
		);
	}
}

void Renderer::createDescriptorPool()
{
	// Create descriptor pool for uniform buffer
	std::array<vk::DescriptorPoolSize, 2> poolSizes;
	poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
	poolSizes[0].descriptorCount = 100; // transform buffer & light buffer & camera buffer & light buffer in compute pipeline
	poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
	poolSizes[1].descriptorCount = 100; // sampler for color map and normal map and depth map from depth prepass... and so many from scene materials

	vk::DescriptorPoolCreateInfo poolInfo;
	poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	poolInfo.pPoolSizes = poolSizes.data();
	poolInfo.maxSets = 200;
	
	mDescriptorPool = mContext.getDevice().createDescriptorPoolUnique(poolInfo);
}

void Renderer::createSceneObjectDescriptorSet()
{
	vk::DescriptorSetLayout layouts[] = { *mObjectDescriptorSetLayout };
	vk::DescriptorSetAllocateInfo allocInfo;
	allocInfo.descriptorPool = *mDescriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = layouts;

	mObjectDescriptorSet = mContext.getDevice().allocateDescriptorSets(allocInfo)[0];
	
	// refer to the uniform object buffer
	vk::DescriptorBufferInfo bufferInfo;
	bufferInfo.buffer = *mObjectUniformBuffer.handle;
	bufferInfo.offset = 0;
	bufferInfo.range = sizeof(SceneObject);

	vk::WriteDescriptorSet descriptorWrites;
	descriptorWrites.dstSet = mObjectDescriptorSet;
	descriptorWrites.dstBinding = 0;
	descriptorWrites.dstArrayElement = 0;
	descriptorWrites.descriptorType = vk::DescriptorType::eUniformBuffer;
	descriptorWrites.descriptorCount = 1;
	descriptorWrites.pBufferInfo = &bufferInfo;
	descriptorWrites.pImageInfo = nullptr; // Optional
	descriptorWrites.pTexelBufferView = nullptr; // Optional

	mContext.getDevice().updateDescriptorSets(descriptorWrites, nullptr);
}

void Renderer::createCameraDescriptorSet()
{
	// Create descriptor set
	{
		vk::DescriptorSetAllocateInfo allocInfo;
		allocInfo.descriptorPool = *mDescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &(*mCameraDescriptorSetLayout);

		mCameraDescriptorSet = mContext.getDevice().allocateDescriptorSets(allocInfo)[0];
	}

	// Write desciptor set
	{
		// refer to the uniform object buffer
		vk::DescriptorBufferInfo cameraInfo;
		cameraInfo.buffer = *mCameraUniformBuffer.handle;
		cameraInfo.offset = 0;
		cameraInfo.range = sizeof(Camera);

		vk::WriteDescriptorSet descriptorWrites;
		descriptorWrites.dstSet = mCameraDescriptorSet;
		descriptorWrites.dstBinding = 0;
		descriptorWrites.dstArrayElement = 0;
		descriptorWrites.descriptorCount = 1;
		descriptorWrites.descriptorType = vk::DescriptorType::eUniformBuffer;
		descriptorWrites.pBufferInfo = &cameraInfo;
		
		mContext.getDevice().updateDescriptorSets(descriptorWrites, nullptr);
	}
}

void Renderer::createGraphicsCommandBuffers()
{
	// Free old command buffers, if any
	if (!mCommandBuffers.empty())
		mContext.getDevice().freeCommandBuffers(mContext.getGraphicsCommandPool(), mCommandBuffers);
	
	vk::CommandBufferAllocateInfo allocInfo;
	allocInfo.commandPool = mContext.getGraphicsCommandPool();
	allocInfo.level = vk::CommandBufferLevel::ePrimary;
	allocInfo.commandBufferCount = static_cast<uint32_t>(mSwapchainFramebuffers.size());

	mCommandBuffers = mContext.getDevice().allocateCommandBuffers(allocInfo);

	// record command buffers
	for (size_t i = 0; i < mCommandBuffers.size(); i++)
	{
		auto& cmdBuffer = mCommandBuffers[i];

		vk::CommandBufferBeginInfo beginInfo;
		beginInfo.flags = vk::CommandBufferUsageFlagBits::eSimultaneousUse;

		cmdBuffer.begin(beginInfo);

		// render pass
		{
			vk::RenderPassBeginInfo renderpassInfo;
			renderpassInfo.renderPass = *mRenderpass;
			renderpassInfo.framebuffer = *mSwapchainFramebuffers[i];
			renderpassInfo.renderArea.offset = vk::Offset2D{ 0, 0 };
			renderpassInfo.renderArea.extent = mSwapchainExtent;

			std::array<vk::ClearValue, 2> clearValues;
			clearValues[0].color.setFloat32({ 1.0f, 0.8f, 0.4f, 1.0f });
			clearValues[1].depthStencil.setDepth(1.0f).setStencil(0.0f);

			renderpassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
			renderpassInfo.pClearValues = clearValues.data();

			cmdBuffer.beginRenderPass(renderpassInfo, vk::SubpassContents::eInline);

			//PushConstantObject pco = {};
			//cmdBuffer.pushConstants(*mPipelineLayout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(pco), &pco);
			cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *mGraphicsPipeline);

			//std::array<vk::DescriptorSet, 2> descriptorSets = { mObjectDescriptorSet, mCameraDescriptorSet };
			//cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *mPipelineLayout, 0, descriptorSets, nullptr);

			for (const auto& part : mModel.getMeshParts())
			{
				cmdBuffer.bindVertexBuffers(0, part.vertexBufferSection.handle, part.vertexBufferSection.offset);
				cmdBuffer.bindIndexBuffer(part.indexBufferSection.handle, part.indexBufferSection.offset, vk::IndexType::eUint32);
				//cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *mPipelineLayout, static_cast<uint32_t>(descriptorSets.size()), part.materialDescriptorSet, nullptr);
				cmdBuffer.drawIndexed(part.indexCount, 1, 0, 0, 0);
			}
			cmdBuffer.endRenderPass();
		}
	
		cmdBuffer.end();
	}
}

void Renderer::createSemaphores()
{
	vk::SemaphoreCreateInfo semaphoreInfo;

	mRenderFinishedSemaphore = mContext.getDevice().createSemaphoreUnique(semaphoreInfo);
	mImageAvailableSemaphore = mContext.getDevice().createSemaphoreUnique(semaphoreInfo);
}

void Renderer::createComputePipeline()
{
	vk::PushConstantRange pushConstantRange;
	pushConstantRange.offset = 0;
	pushConstantRange.size = sizeof(PushConstantObject);
	pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eCompute;

	std::array<vk::DescriptorSetLayout, 1> setLayouts = { *mCameraDescriptorSetLayout };
	vk::PipelineLayoutCreateInfo layoutInfo;
	layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
	layoutInfo.pSetLayouts = setLayouts.data();
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges = &pushConstantRange;

	mComputePipelineLayout = mContext.getDevice().createPipelineLayoutUnique(layoutInfo);

	auto shader = createShaderModule("data/shader.comp"); // TODO make compute shader

	vk::PipelineShaderStageCreateInfo stageInfo;
	stageInfo.stage = vk::ShaderStageFlagBits::eCompute;
	stageInfo.module = *shader;
	stageInfo.pName = "main";

	vk::ComputePipelineCreateInfo pipelineInfo;
	pipelineInfo.stage = stageInfo;
	pipelineInfo.layout = *mComputePipelineLayout;
	//pipelineInfo.basePipelineIndex = -1; 

	mComputePipeline = mContext.getDevice().createComputePipelineUnique(nullptr, pipelineInfo);
}

void Renderer::createComputeCommandBuffer()
{
	if (mComputeCommandBuffer)
		mContext.getDevice().freeCommandBuffers(mContext.getComputeCommandPool(), mComputeCommandBuffer);

	// Create command buffer
	{
		vk::CommandBufferAllocateInfo allocInfo;
		allocInfo.commandPool = mContext.getComputeCommandPool();
		allocInfo.level = vk::CommandBufferLevel::ePrimary;
		allocInfo.commandBufferCount = 1;

		mComputeCommandBuffer = mContext.getDevice().allocateCommandBuffers(allocInfo)[0];
	}

	// Record command buffer
	{
		vk::CommandBufferBeginInfo beginInfo;
		beginInfo.flags = vk::CommandBufferUsageFlagBits::eSimultaneousUse;

		auto& cmd = mComputeCommandBuffer;

		cmd.begin(beginInfo);

		// using barrier since the sharing mode when allocating memory is exclusive
		// begin after fragment shader finished reading from storage buffer

		//std::array<vk::BufferMemoryBarrier, 2> barriersBefore;
		//barriersBefore[0].srcAccessMask = vk::AccessFlagBits::eShaderRead;
		//barriersBefore[0].dstAccessMask = vk::AccessFlagBits::eShaderWrite;
		//barriersBefore[0].buffer = 
		
		//cmd.pipelineBarrier(
		//	vk::PipelineStageFlagBits::eFragmentShader,  // srcStageMask
		//	vk::PipelineStageFlagBits::eComputeShader,  // dstStageMask
		//	vk::DependencyFlags(),  // dependencyFlags
		//	nullptr,  // pBUfferMemoryBarriers
		//	barriersBefore,  // pBUfferMemoryBarriers
		//	nullptr // pImageMemoryBarriers
		//);


		std::array<vk::DescriptorSet, 1> descriptorSets{ mCameraDescriptorSet };

		cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *mComputePipelineLayout, 0, descriptorSets, nullptr);

		PushConstantObject pco;
		cmd.pushConstants(*mComputePipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(pco), &pco);
		cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *mComputePipeline);
		cmd.dispatch(1, 1, 1);


		//std::array<vk::BufferMemoryBarrier, 2> barriersAfter;
		//barriersAfter[0] = 
		//barriersAfter.emplace_back
		//(
		//	vk::AccessFlagBits::eShaderWrite,  // srcAccessMask
		//	vk::AccessFlagBits::eShaderRead,  // dstAccessMask
		//	0,//static_cast<uint32_t>(queue_family_indices.compute_family), // srcQueueFamilyIndex
		//	0,//static_cast<uint32_t>(queue_family_indices.graphics_family),  // dstQueueFamilyIndex
		//	static_cast<vk::Buffer>(light_visibility_buffer.get()),  // buffer
		//	0,  // offset
		//	light_visibility_buffer_size  // size
		//);
		//barriersAfter.emplace_back
		//(
		//	vk::AccessFlagBits::eShaderWrite,  // srcAccessMask 
		//	vk::AccessFlagBits::eShaderRead,  // dstAccessMask
		//	0, //static_cast<uint32_t>(queue_family_indices.compute_family), // srcQueueFamilyIndex
		//	0, //static_cast<uint32_t>(queue_family_indices.graphics_family),  // dstQueueFamilyIndex
		//	static_cast<vk::Buffer>(pointlight_buffer.get()),  // buffer
		//	0,  // offset
		//	pointlight_buffer_size  // size
		//);

		//cmd.pipelineBarrier(
		//	vk::PipelineStageFlagBits::eComputeShader,
		//	vk::PipelineStageFlagBits::eFragmentShader,
		//	vk::DependencyFlags(),
		//	0, nullptr,
		//	static_cast<uint32_t>(barriersAfter.size()), barriersAfter.data(), // TODO
		//	0, nullptr
		//);

		cmd.end();
	}
}

void Renderer::updateUniformBuffers()
{
	// update camera ubo
	{
		Camera cameraUbo = {};
		cameraUbo.view = mViewMatrix;
		cameraUbo.projection = glm::perspective(glm::radians(45.0f), mSwapchainExtent.width / static_cast<float>(mSwapchainExtent.height), 0.5f, 100.0f);
		cameraUbo.projection[1][1] *= -1; //since the Y axis of Vulkan NDC points down
		
		auto data = mContext.getDevice().mapMemory(*mCameraStagingBuffer.memory, 0, sizeof(cameraUbo));
		memcpy(data, &cameraUbo, sizeof(cameraUbo));
		mContext.getDevice().unmapMemory(*mCameraStagingBuffer.memory);
		
		mUtility.copyBuffer(*mCameraStagingBuffer.handle, *mCameraUniformBuffer.handle, sizeof(cameraUbo));
	}
}

void Renderer::drawFrame()
{
	// 1. Acquiring an image from the swap chain
	uint32_t imageIndex;
	{
		auto result = mContext.getDevice().acquireNextImageKHR(*mSwapchain, std::numeric_limits<uint64_t>::max(), *mImageAvailableSemaphore, nullptr);

		imageIndex = result.value;

		if (result.result == vk::Result::eErrorOutOfDateKHR)
		{
			// when swap chain needs recreation
			recreateSwapChain();
			return;
		}
		else if (result.result != vk::Result::eSuccess && result.result != vk::Result::eSuboptimalKHR)
		{
			throw std::runtime_error("Failed to acquire swap chain image!");
		}
	}

	// 2. Submitting the command buffer
	{
		vk::SubmitInfo submitInfo;
		vk::PipelineStageFlags waitStages = vk::PipelineStageFlagBits::eColorAttachmentOutput; // which stage to execute
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &(*mImageAvailableSemaphore);
		submitInfo.pWaitDstStageMask = &waitStages;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &mCommandBuffers[imageIndex];
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &(*mRenderFinishedSemaphore);

		mContext.getGraphicsQueue().submit(submitInfo, nullptr);
	}
	// TODO: use Fence and we can have cpu start working at a earlier time

	// 3. Submitting the result back to the swap chain to show it on screen
	{
		vk::PresentInfoKHR presentInfo;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &(*mRenderFinishedSemaphore);
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &(*mSwapchain);
		presentInfo.pImageIndices = &imageIndex;

		auto presentResult = mContext.getPresentQueue().presentKHR(presentInfo);

		if (presentResult == vk::Result::eErrorOutOfDateKHR || presentResult == vk::Result::eSuboptimalKHR)
			recreateSwapChain();
		else if (presentResult != vk::Result::eSuccess)
			throw std::runtime_error("Failed to present swap chain image");
	}
}

vk::UniqueShaderModule Renderer::createShaderModule(const std::string& filename)
{
	auto spirv = compileShader(filename);

	vk::ShaderModuleCreateInfo shaderInfo;
	shaderInfo.codeSize = spirv.size() * sizeof(uint32_t);
	shaderInfo.pCode = spirv.data();

	return mContext.getDevice().createShaderModuleUnique(shaderInfo);
}
