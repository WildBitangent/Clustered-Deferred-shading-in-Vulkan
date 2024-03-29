/**
 * @file 'Renderer.cpp'
 * @brief Vulkan renderer
 * @copyright The MIT license 
 * @author Matej Karas
 */

#include "Renderer.h"

#include <glm/common.hpp>
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.hpp>
#include <algorithm> 

#include "Context.h"
#include "Model.h"
#include "BaseApp.h"
#include "imgui.h"
#include <valarray>
#include <random>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE

struct CameraUBO
{
	glm::mat4 view;
	glm::mat4 projection;
	glm::mat4 invProj;
	glm::vec3 cameraPosition;
	alignas(16) glm::uvec2 screenSize;
};

struct ObjectUBO
{
	glm::mat4 model;
};

struct DebugUBO
{
	uint32_t debugState = 0;
};

namespace
{
	std::pair<vk::AttachmentDescription, vk::AttachmentReference> createAttachmentDescription(vk::Format format, vk::ImageLayout layout, uint32_t index)
	{
		vk::AttachmentDescription description;
		description.format = format;
		description.samples = vk::SampleCountFlagBits::e1;
		description.loadOp = vk::AttachmentLoadOp::eClear;
		description.storeOp = vk::AttachmentStoreOp::eStore;
		description.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		description.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		description.initialLayout = vk::ImageLayout::eUndefined;
		description.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

		vk::AttachmentReference reference;
		reference.attachment = index;
		reference.layout = layout;

		return std::make_pair(description, reference);
	}
}

Renderer::Renderer(GLFWwindow* window, Scene& scene)
	: mContext(window)
	, mUtility(mContext)
	, mScene(scene)
	, mResource(mContext.getDevice())
{
	vk::PhysicalDeviceSubgroupProperties subgroupProperties;
	vk::PhysicalDeviceProperties2 properties2;
	properties2.pNext = &subgroupProperties;

	mContext.getPhysicalDevice().getProperties2(&properties2);

	mSubGroupSize = subgroupProperties.subgroupSize;
	mLevelParam.reserve(6);

	setTileCount();

	createSwapChain();
	createSwapChainImageViews();
	createGBuffers();
	createSampler();
	createRenderPasses();
	createFrameBuffers();
	createDescriptorSetLayouts();
	createPipelineCache();
	createGraphicsPipelines();
	createComputePipeline();
	createUniformBuffers();
	createClusteredBuffers();
	createLights();
	createDescriptorPool();
	createDescriptorSets();
	updateDescriptorSets();
	// createGraphicsCommandBuffers();
	createComputeCommandBuffer();
	createSyncPrimitives();
}

void Renderer::draw()
{
	updateUniformBuffers();

	if (BaseApp::getInstance().getUI().mContext.cullingMethod == CullingMethod::clustered)
		submitBVHCreationCmds(mCurrentFrame);

	drawFrame();
}

void Renderer::cleanUp()
{
	mContext.getDevice().waitIdle(); // finish everything before destroying
}

void Renderer::reloadShaders(uint32_t tileSize)
{
	mCurrentTileSize = tileSize;
	setTileCount();

	vkDeviceWaitIdle(mContext.getDevice());
	mResource.pipeline.clear();

	createGraphicsPipelines();
	createGraphicsCommandBuffers();
	createComputePipeline();
	createComputeCommandBuffer();
	BaseApp::getInstance().getUI().resize();
}

void Renderer::onSceneChange()
{
	createGraphicsCommandBuffers();

	// update scale
	{ 
		auto data = static_cast<ObjectUBO*>(mContext.getDevice().mapMemory(*mObjectStagingBuffer.memory, 0, sizeof(ObjectUBO), {}));
		data->model = glm::scale(glm::mat4(1.f), mScene.getScale()); 
		mContext.getDevice().unmapMemory(*mObjectStagingBuffer.memory);
		mUtility.copyBuffer(*mObjectStagingBuffer.handle, *mObjectUniformBuffer.handle, sizeof(ObjectUBO));
	}
}

void Renderer::recreateSwapChain()
{
    for (int width = 0, height = 0; width == 0 || height == 0; ) 
	{
        glfwGetFramebufferSize(mContext.getWindow(), &width, &height);
        glfwWaitEvents();
    }

	vkDeviceWaitIdle(mContext.getDevice());
	mResource.pipeline.clear();
	
	setTileCount();
	createSwapChain();
	createSwapChainImageViews();
	createGBuffers();
	createFrameBuffers();
	updateDescriptorSets();
	createGraphicsPipelines();
	createGraphicsCommandBuffers();
	createComputePipeline();
	createComputeCommandBuffer();
	BaseApp::getInstance().getUI().resize();
}

void Renderer::createSwapChain()
{
	auto capabilities = mContext.getPhysicalDevice().getSurfaceCapabilitiesKHR(mContext.getWindowSurface());
	auto formats = mContext.getPhysicalDevice().getSurfaceFormatsKHR(mContext.getWindowSurface());
	auto presentModes = mContext.getPhysicalDevice().getSurfacePresentModesKHR(mContext.getWindowSurface());

	auto surfaceFormat = mUtility.chooseSwapSurfaceFormat(formats);
	auto presentMode = mUtility.chooseSwapPresentMode(presentModes);
	auto extent = mUtility.chooseSwapExtent(capabilities);

	vk::SwapchainCreateInfoKHR swapchainInfo;
	swapchainInfo.surface = mContext.getWindowSurface();
	swapchainInfo.minImageCount = 2; // only double buffer 
	swapchainInfo.imageFormat = surfaceFormat.format;
	swapchainInfo.imageColorSpace = surfaceFormat.colorSpace;
	swapchainInfo.imageExtent = extent;
	swapchainInfo.imageArrayLayers = 1;
	swapchainInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
	swapchainInfo.imageSharingMode = vk::SharingMode::eExclusive; // present is inside graphics

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
	// Gbuffer renderpass
	{
		// attachment descriptions
		auto position = createAttachmentDescription(mGBufferAttachments.position.format, vk::ImageLayout::eColorAttachmentOptimal, 0);
		auto color = createAttachmentDescription(mGBufferAttachments.color.format, vk::ImageLayout::eColorAttachmentOptimal, 1);
		auto normal = createAttachmentDescription(mGBufferAttachments.normal.format, vk::ImageLayout::eColorAttachmentOptimal, 2);
		auto depth = createAttachmentDescription(mGBufferAttachments.depth.format, vk::ImageLayout::eDepthStencilAttachmentOptimal, 3);
		
		std::vector<vk::AttachmentDescription> attachmentDescriptions = {position.first, color.first, normal.first, depth.first};
		std::vector<vk::AttachmentReference> attachmentReferences = {position.second, color.second, normal.second, depth.second};

		vk::SubpassDescription subpass;
		subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass.colorAttachmentCount = static_cast<uint32_t>(attachmentDescriptions.size() - 1);
		subpass.pColorAttachments = attachmentReferences.data();
		subpass.pDepthStencilAttachment = &attachmentReferences.back();

		std::array<vk::SubpassDependency, 2> dependencies;
		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
		dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependencies[0].srcAccessMask = vk::AccessFlagBits::eMemoryRead;
		dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
		dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;
		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
		dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
		dependencies[1].dstAccessMask = vk::AccessFlagBits::eMemoryRead;
		dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

		vk::RenderPassCreateInfo renderpassInfo;
		renderpassInfo.attachmentCount = static_cast<uint32_t>(attachmentDescriptions.size());
		renderpassInfo.pAttachments = attachmentDescriptions.data();
		renderpassInfo.subpassCount = 1;
		renderpassInfo.pSubpasses = &subpass;
		renderpassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderpassInfo.pDependencies = dependencies.data();

		mGBufferRenderpass = mContext.getDevice().createRenderPassUnique(renderpassInfo);
	}

	// composition + UI
	{
		vk::AttachmentDescription colorAttachmentComposition;
		colorAttachmentComposition.format = mSwapchainImageFormat;
		colorAttachmentComposition.samples = vk::SampleCountFlagBits::e1;
		colorAttachmentComposition.loadOp = vk::AttachmentLoadOp::eDontCare; // before rendering
		colorAttachmentComposition.storeOp = vk::AttachmentStoreOp::eStore; // after rendering
		colorAttachmentComposition.stencilLoadOp = vk::AttachmentLoadOp::eDontCare; // no stencil
		colorAttachmentComposition.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		colorAttachmentComposition.initialLayout = vk::ImageLayout::eUndefined;
		colorAttachmentComposition.finalLayout = vk::ImageLayout::ePresentSrcKHR; // to be directly used in swap chain

		vk::AttachmentReference colorAttachmentRef;
		colorAttachmentRef.attachment = 0;
		colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;
		
		std::array<vk::SubpassDescription, 2> subpass;
		subpass[0].pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass[0].colorAttachmentCount = 1;
		subpass[0].pColorAttachments = &colorAttachmentRef;
		// subpass.pDepthStencilAttachment = &depthAttachmentRef;

		subpass[1].pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass[1].colorAttachmentCount = 1;
		subpass[1].pColorAttachments = &colorAttachmentRef;

		std::array<vk::SubpassDependency, 3> dependencies;
		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
		dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependencies[0].srcAccessMask = vk::AccessFlagBits::eMemoryRead;
		dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
		dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;

		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = 1;
		dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;
		dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		dependencies[1].dstAccessMask = vk::AccessFlagBits::eShaderRead;
		dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

		dependencies[2].srcSubpass = 1;
		dependencies[2].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[2].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependencies[2].dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
		dependencies[2].srcAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
		dependencies[2].dstAccessMask = vk::AccessFlagBits::eMemoryRead;
		dependencies[2].dependencyFlags = vk::DependencyFlagBits::eByRegion;

		std::array<vk::AttachmentDescription, 1> attachmentDescriptions = { colorAttachmentComposition/*, depthAttachment*/ };

		vk::RenderPassCreateInfo renderpassInfo;
		renderpassInfo.attachmentCount = static_cast<uint32_t>(attachmentDescriptions.size());
		renderpassInfo.pAttachments = attachmentDescriptions.data();
		renderpassInfo.subpassCount = static_cast<uint32_t>(subpass.size());
		renderpassInfo.pSubpasses = subpass.data();
		renderpassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderpassInfo.pDependencies = dependencies.data();

		mCompositionRenderpass = mContext.getDevice().createRenderPassUnique(renderpassInfo);
	}
}

void Renderer::createFrameBuffers()
{
	// gbuffers
	{
		std::vector<vk::ImageView> attachments = {
			*mGBufferAttachments.position.view,
			*mGBufferAttachments.color.view,
			*mGBufferAttachments.normal.view,
			*mGBufferAttachments.depth.view
		};

		vk::FramebufferCreateInfo framebufferInfo;
		framebufferInfo.renderPass = *mGBufferRenderpass;
		framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		framebufferInfo.pAttachments = attachments.data();
		framebufferInfo.width = mSwapchainExtent.width;
		framebufferInfo.height = mSwapchainExtent.height;
		framebufferInfo.layers = 1;

		mGBufferFramebuffer = mContext.getDevice().createFramebufferUnique(framebufferInfo);
	}

	// composition
	{
		mSwapchainFramebuffers.clear();
		mSwapchainFramebuffers.reserve(mSwapchainImageViews.size());

		for (const auto& view : mSwapchainImageViews)
		{
			std::vector<vk::ImageView> attachments = { *view };

			vk::FramebufferCreateInfo framebufferInfo;
			framebufferInfo.renderPass = *mCompositionRenderpass;
			framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
			framebufferInfo.pAttachments = attachments.data();
			framebufferInfo.width = mSwapchainExtent.width;
			framebufferInfo.height = mSwapchainExtent.height;
			framebufferInfo.layers = 1;

			mSwapchainFramebuffers.emplace_back(mContext.getDevice().createFramebufferUnique(framebufferInfo));
		}
	}
}

void Renderer::createDescriptorSetLayouts()
{
	// Camera UBO
	{
		// View, Proj
		vk::DescriptorSetLayoutBinding uboBinding;
		uboBinding.binding = 0;
		uboBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
		uboBinding.descriptorCount = 1;
		uboBinding.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute;

		vk::DescriptorSetLayoutCreateInfo createInfo;
		createInfo.bindingCount = 1;
		createInfo.pBindings = &uboBinding;

		mResource.descriptorSetLayout.add("camera", createInfo);
	}

	// Model transform
	{
		// Model transform
		vk::DescriptorSetLayoutBinding uboBinding;
		uboBinding.binding = 0;
		uboBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
		uboBinding.descriptorCount = 1;
		uboBinding.stageFlags = vk::ShaderStageFlagBits::eVertex;

		vk::DescriptorSetLayoutCreateInfo createInfo;
		createInfo.bindingCount = 1;
		createInfo.pBindings = &uboBinding;

		mResource.descriptorSetLayout.add("model", createInfo);
	}

	// Material
	{
		vk::DescriptorSetLayoutBinding uboBinding;
		uboBinding.binding = 0;
		uboBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
		uboBinding.descriptorCount = 1;
		uboBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

		vk::DescriptorSetLayoutBinding albedoBinding;
		albedoBinding.binding = 1;
		albedoBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
		albedoBinding.descriptorCount = 1;
		albedoBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

		vk::DescriptorSetLayoutBinding normalBinding;
		normalBinding.binding = 2;
		normalBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
		normalBinding.descriptorCount = 1;
		normalBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

		vk::DescriptorSetLayoutBinding specularBinding;
		specularBinding.binding = 3;
		specularBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
		specularBinding.descriptorCount = 1;
		specularBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

		std::array<vk::DescriptorSetLayoutBinding, 4> bindings = { uboBinding, albedoBinding, normalBinding, specularBinding };

		vk::DescriptorSetLayoutCreateInfo createInfo;
		createInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		createInfo.pBindings = bindings.data();

		mResource.descriptorSetLayout.add("material", createInfo);
	}

	// Light culling
	{
		std::vector<vk::DescriptorSetLayoutBinding> bindings;

		// point lights
		bindings.emplace_back(static_cast<uint32_t>(bindings.size()), vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute);

		// lights out
		bindings.emplace_back(static_cast<uint32_t>(bindings.size()), vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute);
		
		// lights indirection
		bindings.emplace_back(static_cast<uint32_t>(bindings.size()), vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute);
		
		// depth binding
		bindings.emplace_back(static_cast<uint32_t>(bindings.size()), vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute);
		
		// page table
		bindings.emplace_back(static_cast<uint32_t>(bindings.size()), vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute);

		// page pool
		bindings.emplace_back(static_cast<uint32_t>(bindings.size()), vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute);

		// unique clusters
		bindings.emplace_back(static_cast<uint32_t>(bindings.size()), vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute);

		vk::DescriptorSetLayoutCreateInfo createInfo;
		createInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		createInfo.pBindings = bindings.data();

		mResource.descriptorSetLayout.add("lightculling", createInfo);
	}

	// Composition
	{
		std::vector<vk::DescriptorSetLayoutBinding> bindings;
		
		// point lights
		bindings.emplace_back(static_cast<uint32_t>(bindings.size()), vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eFragment);

		// lights out
		bindings.emplace_back(static_cast<uint32_t>(bindings.size()), vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eFragment);

		// lights indirection
		bindings.emplace_back(static_cast<uint32_t>(bindings.size()), vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eFragment);

		// position
		bindings.emplace_back(static_cast<uint32_t>(bindings.size()), vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment);

		// albedo
		bindings.emplace_back(static_cast<uint32_t>(bindings.size()), vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment);

		// normal
		bindings.emplace_back(static_cast<uint32_t>(bindings.size()), vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment);

		// depth
		bindings.emplace_back(static_cast<uint32_t>(bindings.size()), vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment);
		
		// page table
		bindings.emplace_back(static_cast<uint32_t>(bindings.size()), vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eFragment);

		// page pool
		bindings.emplace_back(static_cast<uint32_t>(bindings.size()), vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eFragment);

		// unique clusters
		bindings.emplace_back(static_cast<uint32_t>(bindings.size()), vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eFragment);

		vk::DescriptorSetLayoutCreateInfo createInfo;
		createInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		createInfo.pBindings = bindings.data();

		mResource.descriptorSetLayout.add("composition", createInfo);
	}

	// Debug 
	{
		vk::DescriptorSetLayoutBinding uboBinding;
		uboBinding.binding = 0;
		uboBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
		uboBinding.descriptorCount = 1;
		uboBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

		vk::DescriptorSetLayoutCreateInfo createInfo;
		createInfo.bindingCount = 1;
		createInfo.pBindings = &uboBinding;

		mResource.descriptorSetLayout.add("debug", createInfo);
	}
}

void Renderer::createPipelineCache()
{
	vk::PipelineCacheCreateInfo createInfo;
	mPipelineCache = mContext.getDevice().createPipelineCacheUnique(createInfo);
}

void Renderer::createGraphicsPipelines()
{
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
	rasterizer.cullMode = vk::CullModeFlagBits::eBack;
	rasterizer.frontFace = vk::FrontFace::eClockwise;

	// no multisampling
	vk::PipelineMultisampleStateCreateInfo multisampling;
	multisampling.sampleShadingEnable = VK_FALSE;
	multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;
	multisampling.minSampleShading = 1.0f; // Optional
	multisampling.pSampleMask = nullptr; /// Optional
	multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
	multisampling.alphaToOneEnable = VK_FALSE; // Optional

	vk::PipelineColorBlendAttachmentState colorblendAttachment;
	colorblendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
	colorblendAttachment.srcColorBlendFactor = vk::BlendFactor::eOne;
	colorblendAttachment.dstColorBlendFactor = vk::BlendFactor::eZero;

	// composition pipeline
	{
		vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo;
		inputAssemblyInfo.topology = vk::PrimitiveTopology::eTriangleStrip;
		inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;

		// create specialization constants
		std::vector<vk::SpecializationMapEntry> entries;
		entries.emplace_back(static_cast<uint32_t>(entries.size()), static_cast<uint32_t>(entries.size() * 4), 4); // Tile Size
		entries.emplace_back(static_cast<uint32_t>(entries.size()), static_cast<uint32_t>(entries.size() * 4), 4); // Y_slices

		float ySlices = std::log(1.0f + (2.f * std::tanf(glm::radians(45.f / 2.f))) / mTileCount.y); // todo FOV as parameter
		std::vector<uint32_t> constantData = {mCurrentTileSize, *reinterpret_cast<uint32_t*>(&ySlices)}; 
		
		vk::SpecializationInfo specializationInfo;
		specializationInfo.mapEntryCount = static_cast<uint32_t>(entries.size());
		specializationInfo.pMapEntries = entries.data();
		specializationInfo.dataSize = constantData.size() * 4;
		specializationInfo.pData = constantData.data();

		// shader stages
		auto vertShader = mResource.shaderModule.add("data/composite.vert");
		auto fragShader = mResource.shaderModule.add("data/composite.frag");

		vk::PipelineShaderStageCreateInfo vertexStageInfo;
		vertexStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
		vertexStageInfo.module = vertShader;
		vertexStageInfo.pName = "main";

		vk::PipelineShaderStageCreateInfo fragmentStageInfo;
		fragmentStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
		fragmentStageInfo.module = fragShader;
		fragmentStageInfo.pName = "main";
		fragmentStageInfo.pSpecializationInfo = &specializationInfo;

		vk::PipelineShaderStageCreateInfo shaderStages[] = { vertexStageInfo, fragmentStageInfo };

		// vertex data info
		vk::PipelineVertexInputStateCreateInfo vertexInputInfo; // empty - positions will be created in VS

		// depth and stencil
		vk::PipelineDepthStencilStateCreateInfo depthStencil;

		// blending
		vk::PipelineColorBlendStateCreateInfo blendingInfo;
		blendingInfo.attachmentCount = 1;
		blendingInfo.pAttachments = &colorblendAttachment;

		std::vector<vk::DescriptorSetLayout> setLayouts = {
			mResource.descriptorSetLayout.get("camera"),
			mResource.descriptorSetLayout.get("composition")
		};

		vk::PipelineLayoutCreateInfo layoutInfo;
		layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
		layoutInfo.pSetLayouts = setLayouts.data();

		auto layout = mResource.pipelineLayout.add("composition", layoutInfo);

		vk::GraphicsPipelineCreateInfo pipelineInfo;
		pipelineInfo.flags = vk::PipelineCreateFlagBits::eAllowDerivatives;
		pipelineInfo.stageCount = 2;
		pipelineInfo.pStages = shaderStages;
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
		pipelineInfo.pViewportState = &viewportInfo;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pDepthStencilState = &depthStencil;
		pipelineInfo.pColorBlendState = &blendingInfo;
		pipelineInfo.layout = layout;
		pipelineInfo.renderPass = *mCompositionRenderpass;
		pipelineInfo.subpass = 0;
		pipelineInfo.basePipelineHandle = nullptr; // not deriving from existing pipeline

		mResource.pipeline.add("composition", *mPipelineCache, pipelineInfo);

		// tiled composition
		fragmentStageInfo.module = mResource.shaderModule.add("data/composite_tiled.frag");
		shaderStages[1] = fragmentStageInfo;
		
		pipelineInfo.layout = mResource.pipelineLayout.add("composition_tiled", layoutInfo);
		mResource.pipeline.add("composition_tiled", *mPipelineCache, pipelineInfo);
		
		// deferred composition
		vk::PushConstantRange pushConstantRange;
		pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eFragment;
		pushConstantRange.size = sizeof(uint32_t);
		
		layoutInfo.pPushConstantRanges = &pushConstantRange;
		layoutInfo.pushConstantRangeCount = 1;
		
		fragmentStageInfo.module = mResource.shaderModule.add("data/composite_deferred.frag");
		shaderStages[1] = fragmentStageInfo;
		
		pipelineInfo.layout = mResource.pipelineLayout.add("composition_deferred", layoutInfo);
		mResource.pipeline.add("composition_deferred", *mPipelineCache, pipelineInfo);
	}

	// debug pipeline
	{
		vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo;
		inputAssemblyInfo.topology = vk::PrimitiveTopology::eTriangleStrip;
		inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;

		// shader stages
		auto vertShader = mResource.shaderModule.add("data/debug.vert");
		auto fragShader = mResource.shaderModule.add("data/debug.frag");

		vk::PipelineShaderStageCreateInfo vertexStageInfo;
		vertexStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
		vertexStageInfo.module = vertShader;
		vertexStageInfo.pName = "main";

		vk::PipelineShaderStageCreateInfo fragmentStageInfo;
		fragmentStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
		fragmentStageInfo.module = fragShader;
		fragmentStageInfo.pName = "main";

		vk::PipelineShaderStageCreateInfo shaderStages[] = { vertexStageInfo, fragmentStageInfo };

		// vertex data info
		vk::PipelineVertexInputStateCreateInfo vertexInputInfo; // empty - positions will be created in VS

		// depth and stencil
		vk::PipelineDepthStencilStateCreateInfo depthStencil;

		// blending
		vk::PipelineColorBlendStateCreateInfo blendingInfo;
		blendingInfo.attachmentCount = 1;
		blendingInfo.pAttachments = &colorblendAttachment;

		std::vector<vk::DescriptorSetLayout> setLayouts = {
			mResource.descriptorSetLayout.get("debug"),
			mResource.descriptorSetLayout.get("composition"),
		};

		vk::PipelineLayoutCreateInfo layoutInfo;
		layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
		layoutInfo.pSetLayouts = setLayouts.data();

		auto layout = mResource.pipelineLayout.add("debug", layoutInfo);

		vk::GraphicsPipelineCreateInfo pipelineInfo;
		pipelineInfo.flags = vk::PipelineCreateFlagBits::eDerivative;
		pipelineInfo.stageCount = 2;
		pipelineInfo.pStages = shaderStages;
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
		pipelineInfo.pViewportState = &viewportInfo;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pDepthStencilState = &depthStencil;
		pipelineInfo.pColorBlendState = &blendingInfo;
		pipelineInfo.layout = layout;
		pipelineInfo.renderPass = *mCompositionRenderpass;
		pipelineInfo.subpass = 0;
		pipelineInfo.basePipelineHandle = mResource.pipeline.get("composition"); // derive from composition pipeline
		pipelineInfo.basePipelineIndex = -1;

		mResource.pipeline.add("debug", *mPipelineCache, pipelineInfo);
	}

	// create G buffer construction pipeline
	{
		auto vertShader = mResource.shaderModule.add("data/gbuffers.vert");
		auto fragShader = mResource.shaderModule.add("data/gbuffers.frag");

		vk::PipelineShaderStageCreateInfo vertexStageInfo;
		vertexStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
		vertexStageInfo.module = vertShader;
		vertexStageInfo.pName = "main";

		vk::PipelineShaderStageCreateInfo fragmentStageInfo;
		fragmentStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
		fragmentStageInfo.module = fragShader;
		fragmentStageInfo.pName = "main";

		vk::PipelineShaderStageCreateInfo shaderStages[] = { vertexStageInfo, fragmentStageInfo };

		// vertex data info
		auto bindingDescription = util::getVertexBindingDesciption();
		auto attrDescription = util::getVertexAttributeDescriptions();

		vk::PipelineVertexInputStateCreateInfo vertexInputInfo;
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescription.size());
		vertexInputInfo.pVertexAttributeDescriptions = attrDescription.data();

		// depth and stencil
		vk::PipelineDepthStencilStateCreateInfo depthStencil;
		depthStencil.depthTestEnable = VK_TRUE;
		depthStencil.depthWriteEnable = VK_TRUE;
		depthStencil.depthCompareOp = vk::CompareOp::eLess;
		depthStencil.depthBoundsTestEnable = VK_FALSE;
		depthStencil.stencilTestEnable = VK_FALSE;

		// blending 
		std::vector<vk::PipelineColorBlendAttachmentState> blendAttachments(3, colorblendAttachment);
		vk::PipelineColorBlendStateCreateInfo blendingInfo;
		blendingInfo.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
		blendingInfo.pAttachments = blendAttachments.data();

		rasterizer.frontFace = vk::FrontFace::eCounterClockwise;

		std::vector<vk::DescriptorSetLayout> setLayouts = {
			mResource.descriptorSetLayout.get("camera"),
			mResource.descriptorSetLayout.get("model"), 
			mResource.descriptorSetLayout.get("material")
		};

		vk::PipelineLayoutCreateInfo layoutInfo;
		layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
		layoutInfo.pSetLayouts = setLayouts.data();

		auto layout = mResource.pipelineLayout.add("gbuffers", layoutInfo);

		vk::GraphicsPipelineCreateInfo pipelineInfo;
		pipelineInfo.flags = vk::PipelineCreateFlagBits::eDerivative;
		pipelineInfo.stageCount = 2;
		pipelineInfo.pStages = shaderStages;
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
		pipelineInfo.pViewportState = &viewportInfo;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pDepthStencilState = &depthStencil;
		pipelineInfo.pColorBlendState = &blendingInfo;
		pipelineInfo.layout = layout;
		pipelineInfo.renderPass = *mGBufferRenderpass;
		pipelineInfo.subpass = 0;
		pipelineInfo.basePipelineHandle = mResource.pipeline.get("composition"); // derive from composition pipeline
		pipelineInfo.basePipelineIndex = -1;

		mResource.pipeline.add("gbuffers", *mPipelineCache, pipelineInfo);
	}
}

void Renderer::createGBuffers()
{
	mGBufferAttachments = generateGBuffer();
}

void Renderer::createSampler()
{
	vk::SamplerCreateInfo samplerInfo;
	samplerInfo.magFilter = vk::Filter::eLinear;
	samplerInfo.minFilter = vk::Filter::eLinear;
	samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
	samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
	samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
	samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
	samplerInfo.minLod = 0.0f;
	samplerInfo.maxLod = 1.0f;
	samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;

	mSampler = mContext.getDevice().createSamplerUnique(samplerInfo);
}

void Renderer::createUniformBuffers()
{
	// create buffers for scene object
	{
		vk::DeviceSize bufferSize = sizeof(ObjectUBO);

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

	// camera 
	{
		vk::DeviceSize bufferSize = sizeof(CameraUBO);

		mCameraStagingBuffer = mUtility.createBuffer(
			bufferSize,
			vk::BufferUsageFlagBits::eTransferSrc,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
		);
		
		mCameraUniformBuffer = mUtility.createBuffer(
			bufferSize,
			vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eUniformBuffer,
			vk::MemoryPropertyFlagBits::eDeviceLocal
		);
	}

	// debug
	{
		mDebugUniformBuffer = mUtility.createBuffer(
			sizeof(DebugUBO),
			vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferSrc,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached
		);
	}
}

void Renderer::createClusteredBuffers()
{
	auto alignedMemorySize = [this](vk::DeviceSize size)
	{
		const auto align = mContext.getPhysicalDevice().getProperties().limits.minStorageBufferOffsetAlignment;
		return ((size - 1) / align + 1) * align;
	};
	
	// page table
	mPageTableOffset = 0;
	mPageTableSize = alignedMemorySize((2'048 + 3) * sizeof(uint32_t)); 

	// physical page pool
	constexpr vk::DeviceSize pageCount = 512; // requires manual tweaking
	constexpr vk::DeviceSize pageSize = 4'096 * sizeof(uint32_t);
	
	mPagePoolOffset = mPageTableSize;
	mPagePoolSize = alignedMemorySize(pageCount * pageSize);

	// compacted unique clusters 
	// default value is 32k of unique clusters, every scene should tweak this value
	// rule of thumb is, the less depth discontiunities, the less unique clusters will be created
	mUniqueClustersOffset = mPagePoolOffset + mPagePoolSize;
	mUniqueClustersSize = alignedMemorySize(32'768 * sizeof(uint32_t));

	// allocate buffer
	mClusteredBuffer = mUtility.createBuffer(
		mPageTableSize + mPagePoolSize + mUniqueClustersSize,
		vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndirectBuffer,
		vk::MemoryPropertyFlagBits::eDeviceLocal
	);
}

void Renderer::createLights()
{
	mPointLightsSize = sizeof(PointLight) * MAX_LIGHTS;
	mLightsOutSwap = MAX_LIGHTS * 20 * 4; // every sceene need to tweak this value
	mLightsOutSize = mLightsOutSwap;

	mLightsOutOffset = 0;
	mPointLightsOffset = mLightsOutSize;
	mLightsOutSwapOffset = mPointLightsOffset + mPointLightsSize;

	vk::DeviceSize bufferSize = mPointLightsSize + mLightsOutSize + mLightsOutSwap;

	// allocate buffer
	mPointLightsStagingBuffer = mUtility.createBuffer(
		mPointLightsSize,
		vk::BufferUsageFlagBits::eTransferSrc,
		vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
	);

	mLightsBuffers = mUtility.createBuffer(
		bufferSize,
		vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc,
		vk::MemoryPropertyFlagBits::eDeviceLocal
	);

	// write random stuff to set queue ownership, so there won't be stupid if/else in lights update for first queue acquisition
	auto cmd = mUtility.beginSingleTimeCommands();
	cmd.fillBuffer(*mLightsBuffers.handle, 0, 4, 0);

	// release ownership
	vk::BufferMemoryBarrier after;
	after.buffer = *mLightsBuffers.handle;
	after.size = VK_WHOLE_SIZE;
	after.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
	after.srcQueueFamilyIndex = mContext.getQueueFamilyIndices().generalFamily;
	after.dstQueueFamilyIndex = mContext.getQueueFamilyIndices().computeFamily;
	
	cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe, vk::DependencyFlagBits::eByRegion, nullptr, after, nullptr);
	mUtility.endSingleTimeCommands(cmd);
}

void Renderer::createDescriptorPool()
{
	// Create descriptor pool for uniform buffer
	std::array<vk::DescriptorPoolSize, 3> poolSizes;
	poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
	poolSizes[0].descriptorCount = 100; 
	poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
	poolSizes[1].descriptorCount = 100; 
	poolSizes[2].type = vk::DescriptorType::eStorageBuffer;
	poolSizes[2].descriptorCount = 100;

	vk::DescriptorPoolCreateInfo poolInfo;
	poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	poolInfo.pPoolSizes = poolSizes.data();
	poolInfo.maxSets = 300;
	poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
	
	mDescriptorPool = mContext.getDevice().createDescriptorPoolUnique(poolInfo);
}

void Renderer::createDescriptorSets()
{
	vk::DescriptorSetAllocateInfo allocInfo;
	allocInfo.descriptorPool = *mDescriptorPool;
	allocInfo.descriptorSetCount = 1;
	
	// light culling
	allocInfo.pSetLayouts = &mResource.descriptorSetLayout.get("lightculling");
	mResource.descriptorSet.add("lightculling_front", allocInfo);
	mResource.descriptorSet.add("lightculling_back", allocInfo);

	// composition
	allocInfo.pSetLayouts = &mResource.descriptorSetLayout.get("composition");
	mResource.descriptorSet.add("composition_front", allocInfo);
	mResource.descriptorSet.add("composition_back", allocInfo);

	// world transform
	allocInfo.pSetLayouts = &mResource.descriptorSetLayout.get("camera");
	mResource.descriptorSet.add("camera", allocInfo);

	// model
	allocInfo.pSetLayouts = &mResource.descriptorSetLayout.get("model");
	mResource.descriptorSet.add("model", allocInfo);

	//debug
	allocInfo.pSetLayouts = &mResource.descriptorSetLayout.get("debug");
	mResource.descriptorSet.add("debug", allocInfo);
}

void Renderer::updateDescriptorSets()
{	
	// world transform
	{
		vk::DescriptorBufferInfo transformBufferInfo;
		transformBufferInfo.buffer = *mCameraUniformBuffer.handle;
		transformBufferInfo.offset = 0;
		transformBufferInfo.range = sizeof(CameraUBO);
		
		auto targetSet = mResource.descriptorSet.get("camera");
		auto write = util::createDescriptorWriteBuffer(targetSet, 0, vk::DescriptorType::eUniformBuffer, transformBufferInfo);
		mContext.getDevice().updateDescriptorSets(write, nullptr);
	}
	
	// Model
	{
		vk::DescriptorBufferInfo modelBufferInfo;
		modelBufferInfo.buffer = *mObjectUniformBuffer.handle;
		modelBufferInfo.offset = 0;
		modelBufferInfo.range = sizeof(ObjectUBO);

		auto targetSet = mResource.descriptorSet.get("model");
		auto write = util::createDescriptorWriteBuffer(targetSet, 0, vk::DescriptorType::eUniformBuffer, modelBufferInfo);
		mContext.getDevice().updateDescriptorSets(write, nullptr);
	}

	// debug
	{
		vk::DescriptorBufferInfo uboInfo;
		uboInfo.buffer = *mDebugUniformBuffer.handle;
		uboInfo.offset = 0;
		uboInfo.range = mDebugUniformBuffer.size;

		auto targetSet = mResource.descriptorSet.get("debug");
		auto write = util::createDescriptorWriteBuffer(targetSet, 0, vk::DescriptorType::eUniformBuffer, uboInfo);
		mContext.getDevice().updateDescriptorSets(write, nullptr);
	}
	
	vk::DescriptorBufferInfo pointLightsInfo{ *mLightsBuffers.handle, mPointLightsOffset, mPointLightsSize };
	vk::DescriptorBufferInfo lightsOutInfo{ *mLightsBuffers.handle, mLightsOutOffset, mLightsOutSize };
	vk::DescriptorBufferInfo lightsIndirectionInfo{ *mLightsBuffers.handle, mLightsOutSwapOffset, mLightsOutSwap };
	vk::DescriptorBufferInfo pageTableInfo{ *mClusteredBuffer.handle, mPageTableOffset, mPageTableSize };
	vk::DescriptorBufferInfo pagePoolInfo{ *mClusteredBuffer.handle, mPagePoolOffset, mPagePoolSize };
	vk::DescriptorBufferInfo uniqueClustersInfo{ *mClusteredBuffer.handle, mUniqueClustersOffset, mUniqueClustersSize };
	
	vk::DescriptorImageInfo depthInfo{ *mSampler, *mGBufferAttachments.depth.view, vk::ImageLayout::eShaderReadOnlyOptimal };
	vk::DescriptorImageInfo positionInfo{ *mSampler, *mGBufferAttachments.position.view, vk::ImageLayout::eShaderReadOnlyOptimal };
	vk::DescriptorImageInfo albedoInfo{ *mSampler, *mGBufferAttachments.color.view, vk::ImageLayout::eShaderReadOnlyOptimal };
	vk::DescriptorImageInfo normalInfo{ *mSampler, *mGBufferAttachments.normal.view, vk::ImageLayout::eShaderReadOnlyOptimal };

	std::vector<vk::WriteDescriptorSet> descriptorWrites;

	// Light culling
	{
		auto targetSet = mResource.descriptorSet.get("lightculling_front");
		std::vector<vk::WriteDescriptorSet> writes;

		uint32_t binding = 0;
		writes.emplace_back(util::createDescriptorWriteBuffer(targetSet, binding++, vk::DescriptorType::eStorageBuffer, pointLightsInfo));
		writes.emplace_back(util::createDescriptorWriteBuffer(targetSet, binding++, vk::DescriptorType::eStorageBuffer, lightsOutInfo));
		writes.emplace_back(util::createDescriptorWriteBuffer(targetSet, binding++, vk::DescriptorType::eStorageBuffer, lightsIndirectionInfo));
		writes.emplace_back(util::createDescriptorWriteImage(targetSet, binding++, depthInfo));
		writes.emplace_back(util::createDescriptorWriteBuffer(targetSet, binding++, vk::DescriptorType::eStorageBuffer, pageTableInfo));
		writes.emplace_back(util::createDescriptorWriteBuffer(targetSet, binding++, vk::DescriptorType::eStorageBuffer, pagePoolInfo));
		writes.emplace_back(util::createDescriptorWriteBuffer(targetSet, binding++, vk::DescriptorType::eStorageBuffer, uniqueClustersInfo));

		descriptorWrites.insert(descriptorWrites.end(), writes.begin(), writes.end());

		// swapped light buffers
		targetSet = mResource.descriptorSet.get("lightculling_back");
		for (auto& item : writes)
			item.dstSet = targetSet;

		std::swap(writes[1].dstBinding, writes[2].dstBinding);
	
		descriptorWrites.insert(descriptorWrites.end(), writes.begin(), writes.end());
	}
		
	// composition
	{
		auto targetSet = mResource.descriptorSet.get("composition_front");
		std::vector<vk::WriteDescriptorSet> writes;

		uint32_t binding = 0;
		writes.emplace_back(util::createDescriptorWriteBuffer(targetSet, binding++, vk::DescriptorType::eStorageBuffer, pointLightsInfo));
		writes.emplace_back(util::createDescriptorWriteBuffer(targetSet, binding++, vk::DescriptorType::eStorageBuffer, lightsOutInfo));
		writes.emplace_back(util::createDescriptorWriteBuffer(targetSet, binding++, vk::DescriptorType::eStorageBuffer, lightsIndirectionInfo));
		writes.emplace_back(util::createDescriptorWriteImage(targetSet, binding++, positionInfo));
		writes.emplace_back(util::createDescriptorWriteImage(targetSet, binding++, albedoInfo));
		writes.emplace_back(util::createDescriptorWriteImage(targetSet, binding++, normalInfo));
		writes.emplace_back(util::createDescriptorWriteImage(targetSet, binding++, depthInfo));
		writes.emplace_back(util::createDescriptorWriteBuffer(targetSet, binding++, vk::DescriptorType::eStorageBuffer, pageTableInfo));
		writes.emplace_back(util::createDescriptorWriteBuffer(targetSet, binding++, vk::DescriptorType::eStorageBuffer, pagePoolInfo));
		writes.emplace_back(util::createDescriptorWriteBuffer(targetSet, binding++, vk::DescriptorType::eStorageBuffer, uniqueClustersInfo));
		
		descriptorWrites.insert(descriptorWrites.end(), writes.begin(), writes.end());
		
		// swapped light buffers
		targetSet = mResource.descriptorSet.get("composition_back");
		for (auto& item : writes)
			item.dstSet = targetSet;

		std::swap(writes[1].dstBinding, writes[2].dstBinding);
	
		descriptorWrites.insert(descriptorWrites.end(), writes.begin(), writes.end());
	}
		
	mContext.getDevice().updateDescriptorSets(descriptorWrites, nullptr);
}

void Renderer::createGraphicsCommandBuffers()
{
	// allocate buffers
	{
		vk::CommandBufferAllocateInfo allocInfo;
		allocInfo.commandPool = mContext.getStaticCommandPool();
		allocInfo.level = vk::CommandBufferLevel::eSecondary;
		allocInfo.commandBufferCount = static_cast<uint32_t>(mSwapchainFramebuffers.size());
		
		mResource.cmd.add("composition", allocInfo);
		mResource.cmd.add("composition_tiled", allocInfo);
		mResource.cmd.add("debug", allocInfo);
		
		allocInfo.commandPool = mContext.getDynamicCommandPool();
		allocInfo.level = vk::CommandBufferLevel::ePrimary;

		mResource.cmd.add("primaryComposition", allocInfo);
		mResource.cmd.add("primaryComposition_tiled", allocInfo);
		mResource.cmd.add("primaryDebug", allocInfo);

		allocInfo.commandPool = mContext.getStaticCommandPool();
		allocInfo.commandBufferCount = 1;

		mResource.cmd.add("gBuffer", allocInfo);
	}

	// Gbuffers
	{
		vk::CommandBufferBeginInfo beginInfo;
		beginInfo.flags = vk::CommandBufferUsageFlagBits::eSimultaneousUse;

		std::array<vk::ClearValue, 4> clearValues;
		clearValues[0].color.setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f });
		clearValues[1].color.setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f });
		clearValues[2].color.setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f });
		clearValues[3].depthStencil.setDepth(1.0f).setStencil(0);

		std::array<vk::DescriptorSet, 2> descriptorSets = {
			mResource.descriptorSet.get("camera"),
			mResource.descriptorSet.get("model")
		};
		
		vk::RenderPassBeginInfo renderpassInfo;
		renderpassInfo.renderPass = *mGBufferRenderpass;
		renderpassInfo.framebuffer = *mGBufferFramebuffer;
		renderpassInfo.renderArea.offset = vk::Offset2D{ 0, 0 };
		renderpassInfo.renderArea.extent = mSwapchainExtent;
		renderpassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderpassInfo.pClearValues = clearValues.data();

		auto pipelineLayout = mResource.pipelineLayout.get("gbuffers");
		auto& cmd = mResource.cmd.get("gBuffer");
		
		cmd.begin(beginInfo);
		cmd.beginRenderPass(renderpassInfo, vk::SubpassContents::eInline);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, mResource.pipeline.get("gbuffers"));
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSets, nullptr);

		for (const auto& part : mScene.getGeometry())
		{
			auto materialSet = mResource.descriptorSet.get(part.materialDescriptorSetKey);

			cmd.bindVertexBuffers(0, part.vertexBufferSection.handle, part.vertexBufferSection.offset);
			cmd.bindIndexBuffer(part.indexBufferSection.handle, part.indexBufferSection.offset, vk::IndexType::eUint32);
			cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, static_cast<uint32_t>(descriptorSets.size()), materialSet, nullptr);
			cmd.drawIndexed(part.indexCount, 1, 0, 0, 0);
		}

		cmd.endRenderPass();
		cmd.end();
	}

	// debug
	{
		vk::CommandBufferInheritanceInfo inheritanceInfo;
		inheritanceInfo.renderPass = *mCompositionRenderpass;
		inheritanceInfo.subpass = 0;

		vk::CommandBufferBeginInfo beginInfo;
		beginInfo.flags = vk::CommandBufferUsageFlagBits::eRenderPassContinue;
		beginInfo.pInheritanceInfo = &inheritanceInfo;
		
		std::array<vk::DescriptorSet, 2> descriptorSets = {
			mResource.descriptorSet.get("debug"),
			mResource.descriptorSet.get("composition_front"),
		};

		// record command buffers
		for (auto& cmd : mResource.cmd.getAll("debug"))
		{
			cmd->begin(beginInfo);
			cmd->bindPipeline(vk::PipelineBindPoint::eGraphics, mResource.pipeline.get("debug"));
			cmd->bindDescriptorSets(vk::PipelineBindPoint::eGraphics, mResource.pipelineLayout.get("debug"), 0, descriptorSets, nullptr);
			cmd->draw(4, 1, 0, 0);
			cmd->end();
		}
	}
}

void Renderer::createSyncPrimitives()
{
	size_t count = mSwapchainImages.size();
	mResource.semaphore.add("lightCullingFinished");
	mResource.semaphore.add("lightSortingFinished");
	mResource.semaphore.add("lightCopyFinished");
	mResource.semaphore.add("gBufferFinished");
	mResource.semaphore.add("renderFinished", count);
	mResource.semaphore.add("imageAvailable", count);

	mResource.fence.add("renderFinished", count);
}

void Renderer::createComputePipeline()
{
	std::array<vk::DescriptorSetLayout, 2> setLayouts = { 
		mResource.descriptorSetLayout.get("camera"),
		mResource.descriptorSetLayout.get("lightculling")
	};
	
	// create specialization constants
	std::vector<vk::SpecializationMapEntry> entries;
	entries.emplace_back(static_cast<uint32_t>(entries.size()), static_cast<uint32_t>(entries.size() * 4), 4); // Tile Size
	entries.emplace_back(static_cast<uint32_t>(entries.size()), static_cast<uint32_t>(entries.size() * 4), 4); // Y_slices
	entries.emplace_back(static_cast<uint32_t>(entries.size()), static_cast<uint32_t>(entries.size() * 4), 4); // WG size

	uint32_t groupSize = mCurrentTileSize <= 32 ? mCurrentTileSize : 32;
	float ySlices = std::log(1.0f + (2.f * std::tanf(glm::radians(45.f / 2.f))) / mTileCount.y);  // todo FOV as parameter
	std::vector<uint32_t> constantData = {
		mCurrentTileSize, 
		*reinterpret_cast<uint32_t*>(&ySlices),
		groupSize,
	};
	
	vk::SpecializationInfo specializationInfo;
	specializationInfo.mapEntryCount = static_cast<uint32_t>(entries.size());
	specializationInfo.pMapEntries = entries.data();
	specializationInfo.dataSize = constantData.size() * 4;
	specializationInfo.pData = constantData.data();

	vk::PipelineShaderStageCreateInfo stageInfo;
	stageInfo.stage = vk::ShaderStageFlagBits::eCompute;
	stageInfo.pName = "main";
	stageInfo.pSpecializationInfo = &specializationInfo;

	auto createPipeline = [this, setLayouts, &stageInfo](const std::string name, const uint32_t pcSize)
	{	
		stageInfo.module = mResource.shaderModule.add(std::string("data/" ) + name + ".comp");

		vk::PushConstantRange pushConstantRange;
		pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
		pushConstantRange.size = pcSize;

		vk::PipelineLayoutCreateInfo layoutInfo;
		layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
		layoutInfo.pSetLayouts = setLayouts.data();

		if (pcSize > 0)
		{
			layoutInfo.pPushConstantRanges = &pushConstantRange;
			layoutInfo.pushConstantRangeCount = 1;
		}
				
		vk::ComputePipelineCreateInfo pipelineInfo;
		pipelineInfo.stage = stageInfo;
		pipelineInfo.layout = mResource.pipelineLayout.add(name, layoutInfo);
		pipelineInfo.basePipelineIndex = -1;
			
		mResource.pipeline.add(name, *mPipelineCache, pipelineInfo);
	};
	
	createPipeline("pt_flag", 0);
	createPipeline("pt_alloc", 0);
	createPipeline("pt_store", 0);
	createPipeline("pt_compact", 0);
	createPipeline("sort_bitonic", sizeof(uint32_t));
	createPipeline("sort_mergeBitonic", 2 * sizeof(uint32_t));
	createPipeline("bvh", 3 * sizeof(uint32_t));
	createPipeline("lightculling", 11 * sizeof(uint32_t));
	createPipeline("lightculling_tiled", sizeof(uint32_t));
}

void Renderer::createComputeCommandBuffer()
{
	// Create command buffer
	{
		vk::CommandBufferAllocateInfo allocInfo;
		allocInfo.commandPool = mContext.getDynamicCommandPool();
		allocInfo.level = vk::CommandBufferLevel::ePrimary;
		allocInfo.commandBufferCount = static_cast<uint32_t>(mSwapchainFramebuffers.size());
		mResource.cmd.add("primaryLightCulling", allocInfo);

		allocInfo.level = vk::CommandBufferLevel::eSecondary;
		allocInfo.commandPool = mContext.getStaticCommandPool();
		allocInfo.commandBufferCount = 1;
		mResource.cmd.add("secondaryLightCulling", allocInfo);

		// light sorting buffers
		allocInfo.commandPool = mContext.getComputeCommandPool();
		allocInfo.level = vk::CommandBufferLevel::ePrimary;
		allocInfo.commandBufferCount = static_cast<uint32_t>(mSwapchainFramebuffers.size());
		mResource.cmd.add("lightSorting", allocInfo);

		allocInfo.commandBufferCount = 1;
		mResource.cmd.add("lightCopy", allocInfo);

		// lightculling tiled
		allocInfo.commandPool = mContext.getDynamicCommandPool();
		mResource.cmd.add("lightculling_tiled", allocInfo);
	}

	// Record command buffer
	{
		vk::MemoryBarrier barrier;
		barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
		barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

		vk::MemoryBarrier indirectBarrier = barrier;
		indirectBarrier.dstAccessMask = vk::AccessFlagBits::eIndirectCommandRead;

		std::array<vk::DescriptorSet, 2> descriptorSets{ 
			mResource.descriptorSet.get("camera"),
			mResource.descriptorSet.get("lightculling_front")
		};

		vk::MemoryBarrier copyBarrier;
		copyBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		copyBarrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

		vk::MemoryBarrier transferToComputeBarrier;
		transferToComputeBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		transferToComputeBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

		vk::CommandBufferInheritanceInfo inheritanceInfo;

		vk::CommandBufferBeginInfo beginInfo;
		beginInfo.pInheritanceInfo = &inheritanceInfo;
		beginInfo.flags = vk::CommandBufferUsageFlagBits::eSimultaneousUse;
		
		{
			auto& cmd = mResource.cmd.get("secondaryLightCulling");
			cmd.begin(beginInfo);
			cmd.fillBuffer(*mClusteredBuffer.handle, 0, VK_WHOLE_SIZE, 0); 
			
			cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlagBits::eByRegion , copyBarrier, nullptr, nullptr); 
			cmd.fillBuffer(*mClusteredBuffer.handle, mPageTableOffset + 4, 8, 1);
			cmd.fillBuffer(*mClusteredBuffer.handle, mUniqueClustersOffset, 16, 1);
			
			cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, mResource.pipelineLayout.get("pt_flag"), 0, descriptorSets, nullptr);
			cmd.bindPipeline(vk::PipelineBindPoint::eCompute, mResource.pipeline.get("pt_flag"));
			cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlagBits::eByRegion , transferToComputeBarrier, nullptr, nullptr); 
			cmd.dispatch(mTileCount.x, mTileCount.y, 1);
			
			cmd.bindPipeline(vk::PipelineBindPoint::eCompute, mResource.pipeline.get("pt_alloc"));
			cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlagBits::eByRegion, barrier, nullptr, nullptr);
			cmd.dispatch(4, 1, 1);
			
			cmd.bindPipeline(vk::PipelineBindPoint::eCompute, mResource.pipeline.get("pt_store"));
			cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlagBits::eByRegion, barrier, nullptr, nullptr);
			cmd.dispatch(mTileCount.x, mTileCount.y, 1);

			cmd.bindPipeline(vk::PipelineBindPoint::eCompute, mResource.pipeline.get("pt_compact"));
			cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eDrawIndirect, vk::DependencyFlagBits::eByRegion, indirectBarrier, nullptr, nullptr);
			cmd.dispatchIndirect(*mClusteredBuffer.handle, mPageTableOffset);
			cmd.end(); 
		}
	}
}

void Renderer::updateUniformBuffers()
{
	// update camera ubo
	{
		auto data = reinterpret_cast<CameraUBO*>(mContext.getDevice().mapMemory(*mCameraStagingBuffer.memory, 0, sizeof(CameraUBO)));
		data->view = mScene.getCamera().getViewMatrix();
		data->projection = glm::perspective(glm::radians(45.0f), mSwapchainExtent.width / static_cast<float>(mSwapchainExtent.height), 0.05f, 100.0f);
		data->projection[1][1] *= -1; //since the Y axis of Vulkan NDC points down
		data->invProj = glm::inverse(data->projection);
		data->cameraPosition = mScene.getCamera().getPosition();
		data->screenSize = {mSwapchainExtent.width, mSwapchainExtent.height};

		mContext.getDevice().unmapMemory(*mCameraStagingBuffer.memory);
		mUtility.copyBuffer(*mCameraStagingBuffer.handle, *mCameraUniformBuffer.handle, sizeof(CameraUBO));
	}

	// update debug buffer, if dirty bit is set
	if (BaseApp::getInstance().getUI().debugStateUniformNeedsUpdate())
	{
		auto state = static_cast<uint32_t>(BaseApp::getInstance().getUI().getDebugIndex());
		auto data = reinterpret_cast<DebugUBO*>(mContext.getDevice().mapMemory(*mDebugUniformBuffer.memory, 0, VK_WHOLE_SIZE)); // TODO refactor this

		data->debugState = state;

		vk::MappedMemoryRange range;
		range.size = VK_WHOLE_SIZE;
		range.memory = *mDebugUniformBuffer.memory;

		mContext.getDevice().unmapMemory(*mDebugUniformBuffer.memory);
		mContext.getDevice().flushMappedMemoryRanges(range);
	}
}

void Renderer::updateLights(const std::vector<PointLight>& lights)
{	
	mLightsCount = BaseApp::getInstance().getUI().mContext.lightsCount;

	const auto memorySize = sizeof(PointLight) * mLightsCount;
	auto data = reinterpret_cast<uint8_t*>(mContext.getDevice().mapMemory(*mPointLightsStagingBuffer.memory, 0, memorySize));
	auto& cmd = mResource.cmd.get("lightCopy");
	auto& context = BaseApp::getInstance().getUI().mContext;

	vk::CommandBufferBeginInfo beginInfo;
	beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

	vk::BufferMemoryBarrier before;
	before.buffer = *mLightsBuffers.handle;
	before.size = VK_WHOLE_SIZE;
	before.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
	before.srcQueueFamilyIndex = mContext.getQueueFamilyIndices().generalFamily;
	before.dstQueueFamilyIndex = mContext.getQueueFamilyIndices().computeFamily;
	
	vk::BufferMemoryBarrier after;
	after.buffer = *mLightsBuffers.handle;
	after.size = VK_WHOLE_SIZE;
	after.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
	after.srcQueueFamilyIndex = mContext.getQueueFamilyIndices().computeFamily;
	after.dstQueueFamilyIndex = mContext.getQueueFamilyIndices().generalFamily;

	// copy lights to host memory
	memcpy(data, lights.data(), memorySize);

	// used as fence, to wait for rendering finished before start of recording buffer for new frame
	mContext.getGeneralQueue().waitIdle();

	cmd.begin(beginInfo);

	// acquire ownership
	cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlagBits::eByRegion, nullptr, before, nullptr);
	
	mUtility.recordCopyBuffer(cmd, *mPointLightsStagingBuffer.handle, *mLightsBuffers.handle, memorySize, 0, mPointLightsOffset);
		
	// release ownership
	if (context.cullingMethod != CullingMethod::clustered)
		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe, vk::DependencyFlagBits::eByRegion, nullptr, after, nullptr);

	cmd.end();

	vk::SubmitInfo submitInfo;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &cmd;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &mResource.semaphore.get("lightCopyFinished");
	
	mContext.getDevice().unmapMemory(*mPointLightsStagingBuffer.memory);
	mContext.getComputeQueue().submit(1, &submitInfo, nullptr);
}

void Renderer::drawFrame()
{
	// Acquire an image from the swap chain
	uint32_t imageIndex;
	{
		try
		{
			auto result = mContext.getDevice().acquireNextImageKHR(*mSwapchain, std::numeric_limits<uint64_t>::max(), mResource.semaphore.get("imageAvailable", mCurrentFrame), nullptr);
			imageIndex = result.value;
		}
		catch(const vk::OutOfDateKHRError&)
		{
			recreateSwapChain();
			return;
		}
		catch(...)
		{
			throw std::runtime_error("Failed to acquire swap chain image!");
		}
	}

	submitGbufferCmds(); 

	if (BaseApp::getInstance().getUI().getDebugIndex() == DebugStates::disabled)
	{
		if (BaseApp::getInstance().getUI().mContext.cullingMethod == CullingMethod::clustered)
		{
			submitClusteredLightCullingCmds(imageIndex);
			submitClusteredCompositionCmds(imageIndex);
		}
		else if (BaseApp::getInstance().getUI().mContext.cullingMethod == CullingMethod::tiled)
		{
			submitTiledLightCullingCmds(imageIndex);
			submitTiledCompositionCmds(imageIndex);
		}
		else
			submitDeferredCompositionCmds(imageIndex);
	}
	else
		submitDebugCmds(imageIndex);
	
	// Present on screen
	vk::PresentInfoKHR presentInfo;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &mResource.semaphore.get("renderFinished", mCurrentFrame);
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &*mSwapchain;
	presentInfo.pImageIndices = &imageIndex;

	try
	{
		mContext.getGeneralQueue().presentKHR(presentInfo);
	}
	catch(const vk::OutOfDateKHRError&)
	{
		recreateSwapChain();
		return;
	}
	catch(...)
	{
		throw std::runtime_error("Failed to present swap chain image!");
	}

	mCurrentFrame = (mCurrentFrame + 1) % mSwapchainImages.size();
}

void Renderer::submitClusteredLightCullingCmds(size_t imageIndex)
{
	auto& cmd = mResource.cmd.get("primaryLightCulling", imageIndex);
	const auto bufferUsed = (mLightBufferSwapUsed == "lightculling_front") ? mLightsOutOffset : mLightsOutSwapOffset;

	std::array<vk::DescriptorSet, 2> descriptorSets{ 
mResource.descriptorSet.get("camera"),
mResource.descriptorSet.get(mLightBufferSwapUsed)
	};
	
	vk::BufferMemoryBarrier acquisitionBarrier;
	acquisitionBarrier.buffer = *mLightsBuffers.handle;
	acquisitionBarrier.size = VK_WHOLE_SIZE;
	acquisitionBarrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
	acquisitionBarrier.srcQueueFamilyIndex = mContext.getQueueFamilyIndices().computeFamily;
	acquisitionBarrier.dstQueueFamilyIndex = mContext.getQueueFamilyIndices().generalFamily;

	vk::BufferMemoryBarrier copyBarrier;
	copyBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
	copyBarrier.dstAccessMask = vk::AccessFlagBits::eIndirectCommandRead;
	copyBarrier.size = 4;
	copyBarrier.offset = bufferUsed;
	copyBarrier.buffer = *mLightsBuffers.handle;
	copyBarrier.srcQueueFamilyIndex = mContext.getQueueFamilyIndices().generalFamily;
	copyBarrier.dstQueueFamilyIndex = mContext.getQueueFamilyIndices().generalFamily;

	vk::MemoryBarrier barrier;
	barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
	barrier.dstAccessMask = vk::AccessFlagBits::eIndirectCommandRead;

	// record 
	cmd.begin(vk::CommandBufferBeginInfo{});
	// acquire ownershup
	cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlagBits::eByRegion, nullptr, acquisitionBarrier, nullptr);

	// page tables
	cmd.executeCommands(1, &mResource.cmd.get("secondaryLightCulling"));
	
	cmd.fillBuffer(*mLightsBuffers.handle, bufferUsed, 4, 0);
	
	cmd.bindPipeline(vk::PipelineBindPoint::eCompute, mResource.pipeline.get("lightculling"));
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, mResource.pipelineLayout.get("lightculling"), 0, descriptorSets, nullptr);
	cmd.pushConstants(mResource.pipelineLayout.get("lightculling"), vk::ShaderStageFlagBits::eCompute, 0, 4, &mMaxBVHLevel);
	cmd.pushConstants(mResource.pipelineLayout.get("lightculling"), vk::ShaderStageFlagBits::eCompute, 4, static_cast<uint32_t>(mLevelParam.size() * 8), mLevelParam.data());
	
	cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eDrawIndirect, vk::DependencyFlagBits::eByRegion, nullptr, copyBarrier, nullptr);
	cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eDrawIndirect, vk::DependencyFlagBits::eByRegion, barrier, nullptr, nullptr);
	
	cmd.dispatchIndirect(*mClusteredBuffer.handle, mUniqueClustersOffset + 4);

	cmd.end();
	
	std::vector<vk::PipelineStageFlags> waitStages = {vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eDrawIndirect};
	std::vector<vk::Semaphore> waitSemaphores = { 
		mResource.semaphore.get("gBufferFinished"), 
		mResource.semaphore.get("lightSortingFinished")
	};

	vk::SubmitInfo submitInfo;
	submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
	submitInfo.pWaitSemaphores = waitSemaphores.data();
	submitInfo.pWaitDstStageMask = waitStages.data();
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &cmd;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &mResource.semaphore.get("lightCullingFinished");

	mContext.getGeneralQueue().submit(submitInfo, nullptr);
}

void Renderer::submitClusteredCompositionCmds(size_t imageIndex)
{
	auto& cmd = mResource.cmd.get("primaryComposition", imageIndex);

	vk::RenderPassBeginInfo renderpassInfo;
	renderpassInfo.renderPass = *mCompositionRenderpass;
	renderpassInfo.framebuffer = *mSwapchainFramebuffers[imageIndex];
	renderpassInfo.renderArea.offset = vk::Offset2D{ 0, 0 };
	renderpassInfo.renderArea.extent = mSwapchainExtent;
	
	std::array<vk::DescriptorSet, 2> descriptorSets = {
		mResource.descriptorSet.get("camera"),
		mResource.descriptorSet.get(mLightBufferSwapUsed == "lightculling_front" ? "composition_front" : "composition_back")
	};
	
	std::array<vk::ClearValue, 1> clearValues;
	clearValues[0].color.setFloat32({ 1.0f, 0.8f, 0.4f, 1.0f });

	renderpassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
	renderpassInfo.pClearValues = clearValues.data();

	cmd.begin(vk::CommandBufferBeginInfo{});
	BaseApp::getInstance().getUI().copyDrawData(cmd);

	cmd.beginRenderPass(renderpassInfo, vk::SubpassContents::eInline);
	
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, mResource.pipeline.get("composition"));
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, mResource.pipelineLayout.get("composition"), 0, descriptorSets, nullptr);
	cmd.draw(4, 1, 0, 0);
	
	cmd.nextSubpass(vk::SubpassContents::eInline);
	BaseApp::getInstance().getUI().recordCommandBuffer(cmd);
	cmd.endRenderPass();


	// release ownership
	vk::BufferMemoryBarrier after;
	after.buffer = *mLightsBuffers.handle;
	after.size = VK_WHOLE_SIZE;
	after.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
	after.srcQueueFamilyIndex = mContext.getQueueFamilyIndices().generalFamily;
	after.dstQueueFamilyIndex = mContext.getQueueFamilyIndices().computeFamily;
	
	cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eBottomOfPipe, vk::DependencyFlagBits::eByRegion, nullptr, after, nullptr);
	cmd.end();


	vk::PipelineStageFlags waitStages = vk::PipelineStageFlagBits::eFragmentShader;

	vk::SubmitInfo submitInfo;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &mResource.semaphore.get("lightCullingFinished");
	submitInfo.pWaitDstStageMask = &waitStages;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &cmd;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &mResource.semaphore.get("renderFinished", mCurrentFrame);

	mContext.getGeneralQueue().submit(submitInfo, nullptr);
}

void Renderer::submitBVHCreationCmds(size_t imageIndex)
{
	auto& cmd = mResource.cmd.get("lightSorting", imageIndex);

	std::array<vk::DescriptorSet, 2> descriptorSets{ 
mResource.descriptorSet.get("camera"),
mResource.descriptorSet.get("lightculling_front")
	};
	
	auto barrier = [](vk::CommandBuffer cmd)
	{
		vk::MemoryBarrier barrier;
		barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
		barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
		
		cmd.pipelineBarrier(
			vk::PipelineStageFlagBits::eComputeShader, 
			vk::PipelineStageFlagBits::eComputeShader, 
			vk::DependencyFlagBits::eByRegion, 
			barrier, nullptr, nullptr
		);
	};

	// set params
	mLevelParam.clear();
	mLightBufferSwapUsed = "lightculling_front";

	cmd.begin(vk::CommandBufferBeginInfo{});

	// light sorting
	uint32_t sortingKernelsCount = (1023 + mLightsCount) / 1024;

	cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, mResource.pipelineLayout.get("pt_flag"), 0, descriptorSets, nullptr);	
	cmd.bindPipeline(vk::PipelineBindPoint::eCompute, mResource.pipeline.get("sort_bitonic"));
	cmd.pushConstants(mResource.pipelineLayout.get("sort_bitonic"), vk::ShaderStageFlagBits::eCompute, 0, 4, &mLightsCount);
	cmd.dispatch(sortingKernelsCount, 1, 1);
	
	if (const auto lightSublistSize = 128u; mLightsCount > lightSublistSize)
	{
		cmd.bindPipeline(vk::PipelineBindPoint::eCompute, mResource.pipeline.get("sort_mergeBitonic"));
		
		for (uint32_t i = 0; mLightsCount > (lightSublistSize << i); i++)
		{
			const uint32_t elementsPerWarp = 256 << i;
			const uint32_t warpCount = 8;
			const uint32_t blocksCount = (mLightsCount - 1) / (warpCount * elementsPerWarp) + 1;
			const uint32_t currentPhase = i + 1;
		
			cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, mResource.pipelineLayout.get("pt_flag"), 1, mResource.descriptorSet.get(mLightBufferSwapUsed), nullptr);
			cmd.pushConstants(mResource.pipelineLayout.get("sort_mergeBitonic"), vk::ShaderStageFlagBits::eCompute, 4, 4, &currentPhase);
			barrier(cmd);
			cmd.dispatch(blocksCount, 1, 1);
			
			mLightBufferSwapUsed = (mLightBufferSwapUsed == "lightculling_front") ? "lightculling_back" : "lightculling_front";
		}
	}		
	
	mLightBufferSwapUsed = (mLightBufferSwapUsed == "lightculling_front") ? "lightculling_back" : "lightculling_front";
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, mResource.pipelineLayout.get("pt_flag"), 1, mResource.descriptorSet.get(mLightBufferSwapUsed), nullptr);

	// BVH
	auto& bvhLayout = mResource.pipelineLayout.get("bvh");
	mMaxBVHLevel = (mLightsCount > mSubGroupSize) ? 1 : 0;
	const uint32_t subgroupAlignedLightCount = ((mLightsCount - 1) / mSubGroupSize + 1) * mSubGroupSize - 1;

	auto createdNodes = [this](uint32_t elementsCount) { return (elementsCount - 1) / mSubGroupSize + 1; };
	auto groupsCount = [](uint32_t levelNodeCount)
	{
		const uint32_t bvhThreadCount = 512;
		return (levelNodeCount - 1) / bvhThreadCount + 1;
	};


	struct
	{
		uint32_t count;
		uint32_t offset;
		uint32_t nextOffset;
	} pushConstants = {
		mLightsCount,
		0,
		subgroupAlignedLightCount / 6 + 1,
	};
	
	cmd.bindPipeline(vk::PipelineBindPoint::eCompute, mResource.pipeline.get("bvh"));
	cmd.pushConstants(bvhLayout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(pushConstants), &pushConstants);
	barrier(cmd);
	cmd.dispatch(groupsCount(mLightsCount), 1, 1);

	mLevelParam.emplace_back(mLightsCount, 0);
	mLevelParam.emplace_back(createdNodes(mLightsCount), pushConstants.nextOffset);
	
	for ( ;mLevelParam.back().first > mSubGroupSize; mMaxBVHLevel++)
	{
		pushConstants = {mLevelParam.back().first, pushConstants.nextOffset, pushConstants.nextOffset + mLevelParam.back().first };
					
		cmd.pushConstants(bvhLayout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(pushConstants), &pushConstants);
		barrier(cmd);
		cmd.dispatch(groupsCount(pushConstants.count), 1, 1);
		
		mLevelParam.emplace_back(createdNodes(mLevelParam.back().first), pushConstants.nextOffset);
	}

	// release ownership
	vk::BufferMemoryBarrier after;
	after.buffer = *mLightsBuffers.handle;
	after.size = VK_WHOLE_SIZE;
	after.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
	after.srcQueueFamilyIndex = mContext.getQueueFamilyIndices().computeFamily;
	after.dstQueueFamilyIndex = mContext.getQueueFamilyIndices().generalFamily;
	
	cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eBottomOfPipe, vk::DependencyFlagBits::eByRegion, nullptr, after, nullptr);
	cmd.end();

	vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eComputeShader;

	vk::SubmitInfo submitInfo;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &cmd;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &mResource.semaphore.get("lightSortingFinished");
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitDstStageMask = &waitStage;
	submitInfo.pWaitSemaphores = &mResource.semaphore.get("lightCopyFinished");

	mContext.getComputeQueue().submit(submitInfo, nullptr);
}

void Renderer::submitTiledLightCullingCmds(size_t imageIndex)
{
	std::array<vk::DescriptorSet, 2> descriptorSets{ 
mResource.descriptorSet.get("camera"),
mResource.descriptorSet.get("lightculling_front")
	};
	
	auto& cmd = mResource.cmd.get("lightculling_tiled");
	cmd.begin(vk::CommandBufferBeginInfo{});

	// acquire ownership
	vk::BufferMemoryBarrier before;
	before.buffer = *mLightsBuffers.handle;
	before.size = VK_WHOLE_SIZE;
	before.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
	before.srcQueueFamilyIndex = mContext.getQueueFamilyIndices().computeFamily;
	before.dstQueueFamilyIndex = mContext.getQueueFamilyIndices().generalFamily;
	
	cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlagBits::eByRegion, nullptr, before, nullptr);

	cmd.fillBuffer(*mClusteredBuffer.handle, 0, VK_WHOLE_SIZE, 0);
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, mResource.pipelineLayout.get("lightculling_tiled"), 0, descriptorSets, nullptr);
	cmd.bindPipeline(vk::PipelineBindPoint::eCompute, mResource.pipeline.get("lightculling_tiled"));
	cmd.pushConstants(mResource.pipelineLayout.get("lightculling_tiled"), vk::ShaderStageFlagBits::eCompute, 0, 4, &mLightsCount);
	cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlagBits::eByRegion , nullptr, nullptr, nullptr); 
	cmd.dispatch(mTileCount.x, mTileCount.y, 1);
	cmd.end();

	
	std::vector<vk::PipelineStageFlags> waitStages = {vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTopOfPipe};
	std::vector<vk::Semaphore> waitSemaphores = { 
		mResource.semaphore.get("gBufferFinished"), 
		mResource.semaphore.get("lightCopyFinished")
	};

	vk::SubmitInfo submitInfo;
	submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
	submitInfo.pWaitSemaphores = waitSemaphores.data();
	submitInfo.pWaitDstStageMask = waitStages.data();
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &cmd;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &mResource.semaphore.get("lightCullingFinished");

	mContext.getGeneralQueue().submit(submitInfo, nullptr);
}

void Renderer::submitTiledCompositionCmds(size_t imageIndex)
{
	auto& cmd = mResource.cmd.get("primaryComposition", imageIndex);

	vk::RenderPassBeginInfo renderpassInfo;
	renderpassInfo.renderPass = *mCompositionRenderpass;
	renderpassInfo.framebuffer = *mSwapchainFramebuffers[imageIndex];
	renderpassInfo.renderArea.offset = vk::Offset2D{ 0, 0 };
	renderpassInfo.renderArea.extent = mSwapchainExtent;
	
	std::array<vk::DescriptorSet, 2> descriptorSets = {
		mResource.descriptorSet.get("camera"),
		mResource.descriptorSet.get("composition_front")
	};
	
	std::array<vk::ClearValue, 1> clearValues;
	clearValues[0].color.setFloat32({ 1.0f, 0.8f, 0.4f, 1.0f });

	renderpassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
	renderpassInfo.pClearValues = clearValues.data();

	cmd.begin(vk::CommandBufferBeginInfo{});
	BaseApp::getInstance().getUI().copyDrawData(cmd);

	cmd.beginRenderPass(renderpassInfo, vk::SubpassContents::eInline);

	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, mResource.pipeline.get("composition_tiled"));
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, mResource.pipelineLayout.get("composition_tiled"), 0, descriptorSets, nullptr);
	cmd.draw(4, 1, 0, 0);

	cmd.nextSubpass(vk::SubpassContents::eInline);
	BaseApp::getInstance().getUI().recordCommandBuffer(cmd);
	cmd.endRenderPass();

	
	// release ownership
	vk::BufferMemoryBarrier after;
	after.buffer = *mLightsBuffers.handle;
	after.size = VK_WHOLE_SIZE;
	after.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
	after.srcQueueFamilyIndex = mContext.getQueueFamilyIndices().generalFamily;
	after.dstQueueFamilyIndex = mContext.getQueueFamilyIndices().computeFamily;
	
	cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eBottomOfPipe, vk::DependencyFlagBits::eByRegion, nullptr, after, nullptr);
	cmd.end();


	vk::PipelineStageFlags waitStages = vk::PipelineStageFlagBits::eFragmentShader;

	vk::SubmitInfo submitInfo;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &mResource.semaphore.get("lightCullingFinished");
	submitInfo.pWaitDstStageMask = &waitStages;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &cmd;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &mResource.semaphore.get("renderFinished", mCurrentFrame);

	mContext.getGeneralQueue().submit(submitInfo, nullptr);
}

void Renderer::submitDeferredCompositionCmds(size_t imageIndex)
{
	auto& cmd = mResource.cmd.get("primaryComposition", imageIndex);

	vk::RenderPassBeginInfo renderpassInfo;
	renderpassInfo.renderPass = *mCompositionRenderpass;
	renderpassInfo.framebuffer = *mSwapchainFramebuffers[imageIndex];
	renderpassInfo.renderArea.offset = vk::Offset2D{ 0, 0 };
	renderpassInfo.renderArea.extent = mSwapchainExtent;
	
	std::array<vk::DescriptorSet, 2> descriptorSets = {
		mResource.descriptorSet.get("camera"),
		mResource.descriptorSet.get("composition_front")
	};
	
	std::array<vk::ClearValue, 1> clearValues;
	clearValues[0].color.setFloat32({ 1.0f, 0.8f, 0.4f, 1.0f });

	renderpassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
	renderpassInfo.pClearValues = clearValues.data();

	cmd.begin(vk::CommandBufferBeginInfo{});
	BaseApp::getInstance().getUI().copyDrawData(cmd);

	// acquire ownership
	vk::BufferMemoryBarrier before;
	before.buffer = *mLightsBuffers.handle;
	before.size = VK_WHOLE_SIZE;
	before.dstAccessMask = vk::AccessFlagBits::eShaderRead;
	before.srcQueueFamilyIndex = mContext.getQueueFamilyIndices().computeFamily;
	before.dstQueueFamilyIndex = mContext.getQueueFamilyIndices().generalFamily;
	
	cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eFragmentShader, vk::DependencyFlagBits::eByRegion, nullptr, before, nullptr);
	cmd.beginRenderPass(renderpassInfo, vk::SubpassContents::eInline);

	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, mResource.pipeline.get("composition_deferred"));
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, mResource.pipelineLayout.get("composition_deferred"), 0, descriptorSets, nullptr);
	cmd.pushConstants(mResource.pipelineLayout.get("composition_deferred"), vk::ShaderStageFlagBits::eFragment, 0, 4, &mLightsCount);
	cmd.draw(4, 1, 0, 0);

	cmd.nextSubpass(vk::SubpassContents::eInline);
	BaseApp::getInstance().getUI().recordCommandBuffer(cmd);
	cmd.endRenderPass();

	// release ownership
	vk::BufferMemoryBarrier after;
	after.buffer = *mLightsBuffers.handle;
	after.size = VK_WHOLE_SIZE;
	after.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
	after.srcQueueFamilyIndex = mContext.getQueueFamilyIndices().generalFamily;
	after.dstQueueFamilyIndex = mContext.getQueueFamilyIndices().computeFamily;
	
	cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eBottomOfPipe, vk::DependencyFlagBits::eByRegion, nullptr, after, nullptr);
	cmd.end();


	std::vector<vk::PipelineStageFlags> waitStages = {vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTopOfPipe};
	std::vector<vk::Semaphore> waitSemaphores = { 
		mResource.semaphore.get("gBufferFinished"), 
		mResource.semaphore.get("lightCopyFinished")
	};

	vk::SubmitInfo submitInfo;
	submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
	submitInfo.pWaitSemaphores = waitSemaphores.data();
	submitInfo.pWaitDstStageMask = waitStages.data();
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &cmd;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &mResource.semaphore.get("renderFinished", mCurrentFrame);

	mContext.getGeneralQueue().submit(submitInfo, nullptr);
}

void Renderer::submitGbufferCmds()
{
	vk::PipelineStageFlags waitStages = vk::PipelineStageFlagBits::eColorAttachmentOutput;

	vk::SubmitInfo submitInfo;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &mResource.semaphore.get("imageAvailable", mCurrentFrame);
	submitInfo.pWaitDstStageMask = &waitStages;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &mResource.cmd.get("gBuffer");
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &mResource.semaphore.get("gBufferFinished");

	mContext.getGeneralQueue().submit(submitInfo, nullptr);
}

void Renderer::submitDebugCmds(size_t imageIndex)
{
	auto& cmd = mResource.cmd.get("primaryDebug", imageIndex);

	vk::RenderPassBeginInfo renderpassInfo;
	renderpassInfo.renderPass = *mCompositionRenderpass;
	renderpassInfo.framebuffer = *mSwapchainFramebuffers[imageIndex];
	renderpassInfo.renderArea.offset = vk::Offset2D{ 0, 0 };
	renderpassInfo.renderArea.extent = mSwapchainExtent;

	std::array<vk::ClearValue, 1> clearValues;
	clearValues[0].color.setFloat32({ 1.0f, 0.8f, 0.4f, 1.0f });

	renderpassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
	renderpassInfo.pClearValues = clearValues.data();


	cmd.begin(vk::CommandBufferBeginInfo{});
	BaseApp::getInstance().getUI().copyDrawData(cmd);

	cmd.beginRenderPass(renderpassInfo, vk::SubpassContents::eSecondaryCommandBuffers);
	cmd.executeCommands(1, &mResource.cmd.get("debug", imageIndex));
	cmd.nextSubpass(vk::SubpassContents::eInline);
	BaseApp::getInstance().getUI().recordCommandBuffer(cmd);
	cmd.endRenderPass();
	cmd.end();

	std::vector<vk::PipelineStageFlags> waitStages = { vk::PipelineStageFlagBits::eFragmentShader };
	std::vector<vk::Semaphore> semaphores = { mResource.semaphore.get("gBufferFinished") };

	if (BaseApp::getInstance().getUI().mContext.cullingMethod == CullingMethod::clustered)
	{
		waitStages.emplace_back(vk::PipelineStageFlagBits::eTopOfPipe);
		semaphores.emplace_back(mResource.semaphore.get("lightSortingFinished"));
	}

	vk::SubmitInfo submitInfo;
	submitInfo.waitSemaphoreCount = static_cast<uint32_t>(semaphores.size());
	submitInfo.pWaitSemaphores = semaphores.data();
	submitInfo.pWaitDstStageMask = waitStages.data();
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &cmd;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &mResource.semaphore.get("renderFinished", mCurrentFrame);

	mContext.getGeneralQueue().submit(submitInfo, nullptr);
}

void Renderer::setTileCount()
{
	int width, height;
    glfwGetFramebufferSize(mContext.getWindow(), &width, &height);
	mTileCount = {(width - 1) / mCurrentTileSize + 1, (height - 1) / mCurrentTileSize + 1};
}

GBuffer Renderer::generateGBuffer()
{
	GBuffer buffer;

	// depth buffer
	{
		auto formatCandidates = { vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint };
		auto depthFormat = mUtility.findSupportedFormat(formatCandidates, vk::ImageTiling::eOptimal, vk::FormatFeatureFlagBits::eDepthStencilAttachment);

		// for depth pre pass and output as texture
		buffer.depth = mUtility.createImage(
			mSwapchainExtent.width, mSwapchainExtent.height,
			depthFormat,
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
			vk::MemoryPropertyFlagBits::eDeviceLocal
		);

		buffer.depth.view = mUtility.createImageView(*buffer.depth.handle, depthFormat, vk::ImageAspectFlagBits::eDepth);
	}

	// position
	{
		buffer.position = mUtility.createImage(
			mSwapchainExtent.width, mSwapchainExtent.height,
			vk::Format::eR16G16B16A16Sfloat,
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
			vk::MemoryPropertyFlagBits::eDeviceLocal
		);

		buffer.position.view = mUtility.createImageView(*buffer.position.handle, buffer.position.format, vk::ImageAspectFlagBits::eColor);
	}

	// color
	{
		buffer.color = mUtility.createImage(
			mSwapchainExtent.width, mSwapchainExtent.height,
			vk::Format::eR8G8B8A8Unorm,
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
			vk::MemoryPropertyFlagBits::eDeviceLocal
		);

		buffer.color.view = mUtility.createImageView(*buffer.color.handle, buffer.color.format, vk::ImageAspectFlagBits::eColor);
	}

	// normal
	{
		buffer.normal = mUtility.createImage(
			mSwapchainExtent.width, mSwapchainExtent.height,
			vk::Format::eR16G16Sfloat,
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
			vk::MemoryPropertyFlagBits::eDeviceLocal
		);

		buffer.normal.view = mUtility.createImageView(*buffer.normal.handle, buffer.normal.format, vk::ImageAspectFlagBits::eColor);
	}

	return buffer;
}
