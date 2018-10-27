#include "Renderer.h"

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.hpp>

#include "Context.h"
#include "Model.h"
#include "BaseApp.h"

constexpr auto WIDTH = 1024;
constexpr auto HEIGHT = 726;

constexpr auto TILE_SIZE = 32;

constexpr auto TILE_COUNT_X = (WIDTH - 1) / TILE_SIZE + 1;
constexpr auto TILE_COUNT_Y = (HEIGHT - 1) / TILE_SIZE + 1;
constexpr auto MAX_LIGHTS = 1024;

struct CameraUBO
{
	glm::mat4 view;
	glm::mat4 projection;
	glm::vec3 cameraPosition;
};

struct ObjectUBO
{
	glm::mat4 model;
};

struct DebugUBO
{
	uint32_t debugState = 0;
};

struct PointLight
{
	glm::vec3 position;
	glm::vec3 intensity;
	float radius;
	float padding; 
};

Renderer::Renderer(GLFWwindow* window)
	: mContext(window)
	, mUtility(mContext)
	, mResource(mContext.getDevice())
{
	createSwapChain();
	createSwapChainImageViews();
	createGBuffers();
	createRenderPasses();
	createDescriptorSetLayouts();
	createPipelineCache();
	createGraphicsPipelines();
	createComputePipeline();
	// createTextureSampler();
	createUniformBuffers();
	createLights();
	createDescriptorPool();
	mModel.loadModel(mContext, "data/models/sponza.obj", *mGBufferAttachments.sampler, *mDescriptorPool, mResource);
	createDescriptorSets();
	////createIntermediateDescriptorSet();
	////updateIntermediateDescriptorSet();
	////createLigutCullingDescriptorSet();
	////createLightVisibilityBuffer(); 
	createGraphicsCommandBuffers();
	createComputeCommandBuffer();
	////createLightCullingCommandBuffer();
	////createDepthPrePassCommandBuffer();
	createSemaphores();
}

void Renderer::requestDraw(float deltatime)
{
	updateUniformBuffers(/*deltatime*/);
	drawFrame();
}

void Renderer::cleanUp()
{
	mContext.getDevice().waitIdle();
}

void Renderer::setCamera(const glm::mat4& view, const glm::vec3 campos)
{
	// update camera ubo
	{
		auto data = reinterpret_cast<CameraUBO*>(mContext.getDevice().mapMemory(*mCameraStagingBuffer.memory, 0, sizeof(CameraUBO)));
		data->view = view;
		data->projection = glm::perspective(glm::radians(45.0f), mSwapchainExtent.width / static_cast<float>(mSwapchainExtent.height), 0.5f, 100.0f);
		data->projection[1][1] *= -1; //since the Y axis of Vulkan NDC points down
		data->cameraPosition = campos;

		mContext.getDevice().unmapMemory(*mCameraStagingBuffer.memory);
		mUtility.copyBuffer(*mCameraStagingBuffer.handle, *mCameraUniformBuffer.handle, sizeof(CameraUBO));
	}
}

void Renderer::reloadShaders()
{
	vkDeviceWaitIdle(mContext.getDevice());

	createGraphicsPipelines();
	createGraphicsCommandBuffers();
	createComputePipeline();
	createComputeCommandBuffer();
}

void Renderer::recreateSwapChain()
{
	vkDeviceWaitIdle(mContext.getDevice());

	createSwapChain();
	createSwapChainImageViews();
	createRenderPasses();
	createGraphicsPipelines();
	createGBuffers();
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
	// Gbuffer renderpass
	{
		// attachment descriptions
		std::vector<vk::AttachmentDescription> attachmentDescriptions;
		std::vector<vk::AttachmentReference> attachmentReferences;

		// position
		{
			vk::AttachmentDescription description;
			description.format = mGBufferAttachments.position.format;
			description.samples = vk::SampleCountFlagBits::e1;
			description.loadOp = vk::AttachmentLoadOp::eClear;
			description.storeOp = vk::AttachmentStoreOp::eStore;
			description.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
			description.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
			description.initialLayout = vk::ImageLayout::eUndefined;
			description.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

			vk::AttachmentReference reference;
			reference.attachment = 0;
			reference.layout = vk::ImageLayout::eColorAttachmentOptimal;

			attachmentDescriptions.emplace_back(description);
			attachmentReferences.emplace_back(reference);
		}

		// color
		{
			vk::AttachmentDescription description;
			description.format = mGBufferAttachments.color.format;
			description.samples = vk::SampleCountFlagBits::e1;
			description.loadOp = vk::AttachmentLoadOp::eClear;
			description.storeOp = vk::AttachmentStoreOp::eStore;
			description.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
			description.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
			description.initialLayout = vk::ImageLayout::eUndefined;
			description.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

			vk::AttachmentReference reference;
			reference.attachment = 1;
			reference.layout = vk::ImageLayout::eColorAttachmentOptimal;

			attachmentDescriptions.emplace_back(description);
			attachmentReferences.emplace_back(reference);
		}

		// normal
		{
			vk::AttachmentDescription description;
			description.format = mGBufferAttachments.normal.format;
			description.samples = vk::SampleCountFlagBits::e1;
			description.loadOp = vk::AttachmentLoadOp::eClear;
			description.storeOp = vk::AttachmentStoreOp::eStore;
			description.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
			description.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
			description.initialLayout = vk::ImageLayout::eUndefined;
			description.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

			vk::AttachmentReference reference;
			reference.attachment = 2;
			reference.layout = vk::ImageLayout::eColorAttachmentOptimal;

			attachmentDescriptions.emplace_back(description);
			attachmentReferences.emplace_back(reference);			
		}

		// depth 
		{
			vk::AttachmentDescription description;
			description.format = mGBufferAttachments.depth.format;
			description.samples = vk::SampleCountFlagBits::e1;
			description.loadOp = vk::AttachmentLoadOp::eClear;
			description.storeOp = vk::AttachmentStoreOp::eStore;
			description.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
			description.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
			description.initialLayout = vk::ImageLayout::eUndefined;
			description.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

			vk::AttachmentReference reference;
			reference.attachment = 3;
			reference.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

			attachmentDescriptions.emplace_back(description);
			attachmentReferences.emplace_back(reference);
		}


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
		vk::AttachmentDescription colorAttachment;
		colorAttachment.format = mSwapchainImageFormat;
		colorAttachment.samples = vk::SampleCountFlagBits::e1;
		colorAttachment.loadOp = vk::AttachmentLoadOp::eDontCare; // before rendering
		colorAttachment.storeOp = vk::AttachmentStoreOp::eStore; // after rendering
		colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare; // no stencil
		colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
		colorAttachment.finalLayout = vk::ImageLayout::ePresentSrcKHR; // to be directly used in swap chain

		// vk::AttachmentDescription depthAttachment;
		// depthAttachment.format = mGBufferAttachments.depth.format;
		// depthAttachment.samples = vk::SampleCountFlagBits::e1;
		// depthAttachment.loadOp = vk::AttachmentLoadOp::eLoad;
		// depthAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;
		// depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		// depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		// depthAttachment.initialLayout = vk::ImageLayout::eUndefined;
		// depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

		vk::AttachmentReference colorAttachmentRef;
		colorAttachmentRef.attachment = 0;
		colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

		// vk::AttachmentReference depthAttachmentRef;
		// depthAttachmentRef.attachment = 1;
		// depthAttachmentRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

		vk::SubpassDescription subpass;
		subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorAttachmentRef;
		// subpass.pDepthStencilAttachment = &depthAttachmentRef;

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

		std::array<vk::AttachmentDescription, 1> attachmentDescriptions = { colorAttachment/*, depthAttachment*/ };

		vk::RenderPassCreateInfo renderpassInfo;
		renderpassInfo.attachmentCount = static_cast<uint32_t>(attachmentDescriptions.size());
		renderpassInfo.pAttachments = attachmentDescriptions.data();
		renderpassInfo.subpassCount = 1;
		renderpassInfo.pSubpasses = &subpass;
		renderpassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderpassInfo.pDependencies = dependencies.data();

		mCompositionRenderpass = mContext.getDevice().createRenderPassUnique(renderpassInfo);

		mSwapchainFramebuffers.clear();
		mSwapchainFramebuffers.reserve(mSwapchainImageViews.size());

		for (const auto& view : mSwapchainImageViews)
		{
			std::array<vk::ImageView, 1> attachments = { *view/*, *mDepthImage.view*/ };

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
	// World transform (desc 0)
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
		vk::DescriptorSetLayoutBinding pointLightsBinding;
		pointLightsBinding.binding = 0;
		pointLightsBinding.descriptorType = vk::DescriptorType::eStorageBuffer;
		pointLightsBinding.descriptorCount = 1;
		pointLightsBinding.stageFlags = vk::ShaderStageFlagBits::eCompute;

		vk::DescriptorSetLayoutBinding lightsOutBinding;
		lightsOutBinding.binding = 1;
		lightsOutBinding.descriptorType = vk::DescriptorType::eStorageBuffer;
		lightsOutBinding.descriptorCount = 1;
		lightsOutBinding.stageFlags = vk::ShaderStageFlagBits::eCompute;

		vk::DescriptorSetLayoutBinding depthBinding;
		depthBinding.binding = 2;
		depthBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
		depthBinding.descriptorCount = 1;
		depthBinding.stageFlags = vk::ShaderStageFlagBits::eCompute;

		std::array<vk::DescriptorSetLayoutBinding, 3> bindings = { pointLightsBinding, lightsOutBinding, depthBinding };

		vk::DescriptorSetLayoutCreateInfo createInfo;
		createInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		createInfo.pBindings = bindings.data();

		mResource.descriptorSetLayout.add("lightculling", createInfo);
	}

	// Composition
	{
		vk::DescriptorSetLayoutBinding pointLightsBinding;
		pointLightsBinding.binding = 0;
		pointLightsBinding.descriptorType = vk::DescriptorType::eStorageBuffer;
		pointLightsBinding.descriptorCount = 1;
		pointLightsBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

		vk::DescriptorSetLayoutBinding lightsOutBinding;
		lightsOutBinding.binding = 1;
		lightsOutBinding.descriptorType = vk::DescriptorType::eStorageBuffer;
		lightsOutBinding.descriptorCount = 1;
		lightsOutBinding.stageFlags = vk::ShaderStageFlagBits::eCompute;

		vk::DescriptorSetLayoutBinding positionBinding;
		positionBinding.binding = 2;
		positionBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
		positionBinding.descriptorCount = 1;
		positionBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

		vk::DescriptorSetLayoutBinding albedoBinding;
		albedoBinding.binding = 3;
		albedoBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
		albedoBinding.descriptorCount = 1;
		albedoBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

		vk::DescriptorSetLayoutBinding normalBiding;
		normalBiding.binding = 4;
		normalBiding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
		normalBiding.descriptorCount = 1;
		normalBiding.stageFlags = vk::ShaderStageFlagBits::eFragment;

		vk::DescriptorSetLayoutBinding specularBiding;
		specularBiding.binding = 5;
		specularBiding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
		specularBiding.descriptorCount = 1;
		specularBiding.stageFlags = vk::ShaderStageFlagBits::eFragment;

		std::array<vk::DescriptorSetLayoutBinding, 6> bindings = { 
			pointLightsBinding, lightsOutBinding, 
			positionBinding, 
			albedoBinding, normalBiding, specularBiding 
		};

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
	// create main pipeline

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

		vk::PipelineShaderStageCreateInfo shaderStages[] = { vertexStageInfo, fragmentStageInfo };

		// vertex data info
		vk::PipelineVertexInputStateCreateInfo vertexInputInfo; // empty - positions will be created in VS

		// depth and stencil
		vk::PipelineDepthStencilStateCreateInfo depthStencil;

		// blending
		vk::PipelineColorBlendStateCreateInfo blendingInfo;
		blendingInfo.attachmentCount = 1;
		blendingInfo.pAttachments = &colorblendAttachment;

		//vk::PushConstantRange pushconstantRange;
		//pushconstantRange.offset = 0;
		//pushconstantRange.size = sizeof(PushConstantObject);
		//pushconstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex;

		std::vector<vk::DescriptorSetLayout> setLayouts = {
			mResource.descriptorSetLayout.get("camera"),
			mResource.descriptorSetLayout.get("composition")
		};

		vk::PipelineLayoutCreateInfo layoutInfo;
		layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
		layoutInfo.pSetLayouts = setLayouts.data();
		//layoutInfo.pushConstantRangeCount = 1; 
		//layoutInfo.pPushConstantRanges = &pushconstantRange; 

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

		//vk::PushConstantRange pushconstantRange;
		//pushconstantRange.offset = 0;
		//pushconstantRange.size = sizeof(PushConstantObject);
		//pushconstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex;

		std::vector<vk::DescriptorSetLayout> setLayouts = {
			mResource.descriptorSetLayout.get("camera"),
			mResource.descriptorSetLayout.get("model"), 
			mResource.descriptorSetLayout.get("material")
		};

		vk::PipelineLayoutCreateInfo layoutInfo;
		layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
		layoutInfo.pSetLayouts = setLayouts.data();
		//layoutInfo.pushConstantRangeCount = 1; 
		//layoutInfo.pPushConstantRanges = &pushconstantRange; 

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
	// depth buffer
	{
		auto formatCandidates = { vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint };
		auto depthFormat = mUtility.findSupportedFormat(formatCandidates, vk::ImageTiling::eOptimal, vk::FormatFeatureFlagBits::eDepthStencilAttachment);

		// for depth pre pass and output as texture
		mGBufferAttachments.depth = mUtility.createImage(
			mSwapchainExtent.width, mSwapchainExtent.height,
			depthFormat,
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
			vk::MemoryPropertyFlagBits::eDeviceLocal
		);

		mGBufferAttachments.depth.view = mUtility.createImageView(*mGBufferAttachments.depth.handle, depthFormat, vk::ImageAspectFlagBits::eDepth);
		// mUtility.transitImageLayout(*mGBufferAttachments.depth.handle, vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal);
	}

	// position
	{
		mGBufferAttachments.position = mUtility.createImage(
			mSwapchainExtent.width, mSwapchainExtent.height,
			vk::Format::eR16G16B16A16Sfloat,
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
			vk::MemoryPropertyFlagBits::eDeviceLocal
		);

		mGBufferAttachments.position.view = mUtility.createImageView(*mGBufferAttachments.position.handle, mGBufferAttachments.position.format, vk::ImageAspectFlagBits::eColor);
		// mUtility.transitImageLayout(*mGBufferAttachments.position.handle, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal);
	}

	// color
	{
		mGBufferAttachments.color = mUtility.createImage(
			mSwapchainExtent.width, mSwapchainExtent.height,
			vk::Format::eR8G8B8A8Unorm,
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
			vk::MemoryPropertyFlagBits::eDeviceLocal
		);

		mGBufferAttachments.color.view = mUtility.createImageView(*mGBufferAttachments.color.handle, mGBufferAttachments.color.format, vk::ImageAspectFlagBits::eColor);
		// mUtility.transitImageLayout(*mGBufferAttachments.color.handle, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal);
	}

	// normal
	{
		mGBufferAttachments.normal = mUtility.createImage(
			mSwapchainExtent.width, mSwapchainExtent.height,
			vk::Format::eR16G16B16A16Sfloat,
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
			vk::MemoryPropertyFlagBits::eDeviceLocal
		);

		mGBufferAttachments.normal.view = mUtility.createImageView(*mGBufferAttachments.normal.handle, mGBufferAttachments.normal.format, vk::ImageAspectFlagBits::eColor);
		// mUtility.transitImageLayout(*mGBufferAttachments.normal.handle, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal);
	}

	// sampler for attachments
	{
		vk::SamplerCreateInfo samplerInfo;
		samplerInfo.magFilter = vk::Filter::eLinear;
		samplerInfo.minFilter = vk::Filter::eLinear;
		// samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
		// samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
		// samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
		samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
		samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
		samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = 1.0f;
		samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;

		mGBufferAttachments.sampler = mContext.getDevice().createSamplerUnique(samplerInfo);
	}
}

// void Renderer::createTextureSampler()
// {
// 	vk::SamplerCreateInfo samplerInfo = {};
// 	samplerInfo.magFilter = vk::Filter::eLinear;
// 	samplerInfo.minFilter = vk::Filter::eLinear;
//
// 	samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
// 	samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
// 	samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
//
// 	samplerInfo.anisotropyEnable = VK_TRUE;
// 	samplerInfo.maxAnisotropy = 16;
//
// 	samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
// 	samplerInfo.unnormalizedCoordinates = VK_FALSE;
//
// 	samplerInfo.compareEnable = VK_FALSE;
// 	samplerInfo.compareOp = vk::CompareOp::eAlways;
//
// 	samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
// 	samplerInfo.mipLodBias = 0.0f;
// 	samplerInfo.minLod = 0.0f;
// 	samplerInfo.maxLod = 0.0f;
//
// 	mTextureSampler = mContext.getDevice().createSamplerUnique(samplerInfo);
// }

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

	// Adding data to scene object buffer
	{ // TODO refactor this
		ObjectUBO ubo;
		ubo.model = glm::scale(glm::mat4(1.f), glm::vec3(0.01f)); // TODO update uniform for objects
		//ubo.model[3][3] = 1.0f;

		auto data = mContext.getDevice().mapMemory(*mObjectStagingBuffer.memory, 0, sizeof(ubo), {});
		memcpy(data, &ubo, sizeof(ubo));
		mContext.getDevice().unmapMemory(*mObjectStagingBuffer.memory);
		mUtility.copyBuffer(*mObjectStagingBuffer.handle, *mObjectUniformBuffer.handle, sizeof(ubo));
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

void Renderer::createLights()
{
	// create buffers
	{
		mLightsOutBuffer = mUtility.createBuffer(
			sizeof(uint32_t) * (MAX_LIGHTS + 1) * TILE_COUNT_X * TILE_COUNT_Y,
			vk::BufferUsageFlagBits::eStorageBuffer,
			vk::MemoryPropertyFlagBits::eDeviceLocal
		);

		vk::DeviceSize bufferSize = sizeof(PointLight) * MAX_LIGHTS + sizeof(glm::vec4);

		mPointLightsStagingBuffer = mUtility.createBuffer(
			bufferSize,
			vk::BufferUsageFlagBits::eTransferSrc,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
		);

		mPointLightsBuffer = mUtility.createBuffer(
			bufferSize,
			vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
			vk::MemoryPropertyFlagBits::eDeviceLocal
		);
	}

	// TODO fill buffer with lights
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
	poolSizes[2].descriptorCount = 2; // used for lightculling

	vk::DescriptorPoolCreateInfo poolInfo;
	poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	poolInfo.pPoolSizes = poolSizes.data();
	poolInfo.maxSets = 202;
	
	mDescriptorPool = mContext.getDevice().createDescriptorPoolUnique(poolInfo);
}

void Renderer::createDescriptorSets()
{
	std::vector<vk::WriteDescriptorSet> descriptorWrites;

	// world transform
	{
		vk::DescriptorSetAllocateInfo allocInfo;
		allocInfo.descriptorPool = *mDescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &mResource.descriptorSetLayout.get("camera");

		auto targetSet = mResource.descriptorSet.add("camera", allocInfo);

		// refer to the uniform object buffer
		vk::DescriptorBufferInfo bufferInfo;
		bufferInfo.buffer = *mCameraUniformBuffer.handle;
		bufferInfo.offset = 0;
		bufferInfo.range = sizeof(CameraUBO);

		vk::WriteDescriptorSet writes;
		writes.dstSet = targetSet;
		writes.dstBinding = 0;
		writes.dstArrayElement = 0;
		writes.descriptorType = vk::DescriptorType::eUniformBuffer;
		writes.descriptorCount = 1;
		writes.pBufferInfo = &bufferInfo;

		descriptorWrites.emplace_back(writes);
	}

	// Model
	{
		vk::DescriptorSetAllocateInfo allocInfo;
		allocInfo.descriptorPool = *mDescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &mResource.descriptorSetLayout.get("model");

		auto targetSet = mResource.descriptorSet.add("model", allocInfo);

		vk::DescriptorBufferInfo bufferInfo;
		bufferInfo.buffer = *mObjectUniformBuffer.handle;
		bufferInfo.offset = 0;
		bufferInfo.range = sizeof(ObjectUBO);
		
		vk::WriteDescriptorSet writes;
		writes.dstSet = targetSet;
		writes.dstBinding = 0;
		writes.dstArrayElement = 0;
		writes.descriptorType = vk::DescriptorType::eUniformBuffer;
		writes.descriptorCount = 1;
		writes.pBufferInfo = &bufferInfo;

		descriptorWrites.emplace_back(writes);
	}

	// Light culling
	{
		vk::DescriptorSetAllocateInfo allocInfo;
		allocInfo.descriptorPool = *mDescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &mResource.descriptorSetLayout.get("lightculling");

		auto targetSet = mResource.descriptorSet.add("lightculling", allocInfo);
		
		vk::DescriptorBufferInfo pointLightsInfo;
		pointLightsInfo.buffer = *mPointLightsBuffer.handle;
		pointLightsInfo.offset = 0;
		pointLightsInfo.range = mPointLightsBuffer.size;

		vk::DescriptorBufferInfo lightsOutInfo;
		lightsOutInfo.buffer = *mLightsOutBuffer.handle;
		lightsOutInfo.offset = 0;
		lightsOutInfo.range = mLightsOutBuffer.size;

		vk::DescriptorImageInfo depthInfo;
		depthInfo.sampler = *mGBufferAttachments.sampler;
		depthInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		depthInfo.imageView = *mGBufferAttachments.depth.view;

		std::array<vk::WriteDescriptorSet, 3> writes;
		writes[0].dstSet = targetSet;
		writes[0].descriptorCount = 1;
		writes[0].dstBinding = 0;
		writes[0].descriptorType = vk::DescriptorType::eStorageBuffer;
		writes[0].pBufferInfo = &pointLightsInfo;

		writes[1].dstSet = targetSet;
		writes[1].descriptorCount = 1;
		writes[1].dstBinding = 1;
		writes[1].descriptorType = vk::DescriptorType::eStorageBuffer;
		writes[1].pBufferInfo = &lightsOutInfo;

		writes[2].dstSet = targetSet;
		writes[2].descriptorCount = 1;
		writes[2].dstBinding = 2;
		writes[2].descriptorType = vk::DescriptorType::eCombinedImageSampler;
		writes[2].pImageInfo = &depthInfo;

		descriptorWrites.insert(descriptorWrites.end(), writes.begin(), writes.end());
	}

	// composition
	{
		vk::DescriptorSetAllocateInfo allocInfo;
		allocInfo.descriptorPool = *mDescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &mResource.descriptorSetLayout.get("composition");

		auto targetSet = mResource.descriptorSet.add("composition", allocInfo);

		vk::DescriptorBufferInfo pointLightsInfo;
		pointLightsInfo.buffer = *mPointLightsBuffer.handle;
		pointLightsInfo.offset = 0;
		pointLightsInfo.range = mPointLightsBuffer.size;

		vk::DescriptorBufferInfo lightsOutInfo;
		lightsOutInfo.buffer = *mLightsOutBuffer.handle;
		lightsOutInfo.offset = 0;
		lightsOutInfo.range = mLightsOutBuffer.size;

		vk::DescriptorImageInfo positionInfo;
		positionInfo.sampler = *mGBufferAttachments.sampler;
		positionInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		positionInfo.imageView = *mGBufferAttachments.position.view;

		vk::DescriptorImageInfo colorInfo;
		colorInfo.sampler = *mGBufferAttachments.sampler;
		colorInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		colorInfo.imageView = *mGBufferAttachments.color.view;

		vk::DescriptorImageInfo normalInfo;
		normalInfo.sampler = *mGBufferAttachments.sampler;
		normalInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		normalInfo.imageView = *mGBufferAttachments.normal.view;

		std::array<vk::WriteDescriptorSet, 5> writes;
		writes[0].dstSet = targetSet;
		writes[0].descriptorCount = 1;
		writes[0].dstBinding = 0;
		writes[0].descriptorType = vk::DescriptorType::eStorageBuffer;
		writes[0].pBufferInfo = &pointLightsInfo;

		writes[1].dstSet = targetSet;
		writes[1].descriptorCount = 1;
		writes[1].dstBinding = 1;
		writes[1].descriptorType = vk::DescriptorType::eStorageBuffer;
		writes[1].pBufferInfo = &lightsOutInfo;

		writes[2].dstSet = targetSet;
		writes[2].descriptorCount = 1;
		writes[2].dstBinding = 2;
		writes[2].descriptorType = vk::DescriptorType::eCombinedImageSampler;
		writes[2].pImageInfo = &positionInfo;

		writes[3].dstSet = targetSet;
		writes[3].descriptorCount = 1;
		writes[3].dstBinding = 3;
		writes[3].descriptorType = vk::DescriptorType::eCombinedImageSampler;
		writes[3].pImageInfo = &colorInfo;

		writes[4].dstSet = targetSet;
		writes[4].descriptorCount = 1;
		writes[4].dstBinding = 4;
		writes[4].descriptorType = vk::DescriptorType::eCombinedImageSampler;
		writes[4].pImageInfo = &normalInfo;

		descriptorWrites.insert(descriptorWrites.end(), writes.begin(), writes.end());
	}

	// debug
	{
		vk::DescriptorSetAllocateInfo allocInfo;
		allocInfo.descriptorPool = *mDescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &mResource.descriptorSetLayout.get("debug");

		auto targetSet = mResource.descriptorSet.add("debug", allocInfo);

		vk::DescriptorBufferInfo uboInfo;
		uboInfo.buffer = *mDebugUniformBuffer.handle;
		uboInfo.offset = 0;
		uboInfo.range = mDebugUniformBuffer.size;

		vk::WriteDescriptorSet write;
		write.dstSet = targetSet;
		write.descriptorCount = 1;
		write.dstBinding = 0;
		write.descriptorType = vk::DescriptorType::eUniformBuffer;
		write.pBufferInfo = &uboInfo;
		
		descriptorWrites.emplace_back(write);
	}

	mContext.getDevice().updateDescriptorSets(descriptorWrites, nullptr);
}

void Renderer::createGraphicsCommandBuffers()
{
	// Gbuffers
	{
		vk::CommandBufferAllocateInfo allocInfo;
		allocInfo.commandPool = mContext.getGraphicsCommandPool();
		allocInfo.level = vk::CommandBufferLevel::ePrimary;
		allocInfo.commandBufferCount = 1;

		mGBufferCommandBuffer = std::move(mContext.getDevice().allocateCommandBuffersUnique(allocInfo)[0]);

		auto& cmd = *mGBufferCommandBuffer;

		cmd.begin(vk::CommandBufferBeginInfo{});

		vk::RenderPassBeginInfo renderpassInfo;
		renderpassInfo.renderPass = *mGBufferRenderpass;
		renderpassInfo.framebuffer = *mGBufferFramebuffer;
		renderpassInfo.renderArea.offset = vk::Offset2D{ 0, 0 };
		renderpassInfo.renderArea.extent = mSwapchainExtent;

		std::array<vk::ClearValue, 4> clearValues;
		clearValues[0].color.setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f });
		clearValues[1].color.setFloat32({ 1.0f, 0.8f, 0.4f, 1.0f });
		clearValues[2].color.setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f });
		clearValues[3].depthStencil.setDepth(1.0f).setStencil(0.0f);

		renderpassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderpassInfo.pClearValues = clearValues.data();

		cmd.beginRenderPass(renderpassInfo, vk::SubpassContents::eInline);

		//PushConstantObject pco = {};
		//cmdBuffer.pushConstants(mResource.pipelineLayout.get("composite"), vk::ShaderStageFlagBits::eFragment, 0, sizeof(pco), &pco);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, mResource.pipeline.get("gbuffers"));

		std::array<vk::DescriptorSet, 2> descriptorSets = {
			mResource.descriptorSet.get("camera"),
			mResource.descriptorSet.get("model")
		};
		auto pipelineLayout = mResource.pipelineLayout.get("gbuffers");

		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSets, nullptr);

		for (const auto& part : mModel.getMeshParts())
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

	// composition
	{ 
		vk::CommandBufferAllocateInfo allocInfo;
		allocInfo.commandPool = mContext.getGraphicsCommandPool();
		allocInfo.level = vk::CommandBufferLevel::ePrimary;
		allocInfo.commandBufferCount = static_cast<uint32_t>(mSwapchainFramebuffers.size());

		mCommandBuffers = mContext.getDevice().allocateCommandBuffersUnique(allocInfo);

		// record command buffers
		for (size_t i = 0; i < mCommandBuffers.size(); i++)
		{
			vk::RenderPassBeginInfo renderpassInfo;
			renderpassInfo.renderPass = *mCompositionRenderpass;
			renderpassInfo.framebuffer = *mSwapchainFramebuffers[i];
			renderpassInfo.renderArea.offset = vk::Offset2D{ 0, 0 };
			renderpassInfo.renderArea.extent = mSwapchainExtent;

			std::array<vk::ClearValue, 1> clearValues;
			clearValues[0].color.setFloat32({ 1.0f, 0.8f, 0.4f, 1.0f });

			renderpassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
			renderpassInfo.pClearValues = clearValues.data();

			auto& cmd = *mCommandBuffers[i];

			cmd.begin(vk::CommandBufferBeginInfo{});
			cmd.beginRenderPass(renderpassInfo, vk::SubpassContents::eInline);
			cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, mResource.pipeline.get("composition"));

			std::array<vk::DescriptorSet, 2> descriptorSets = {
				mResource.descriptorSet.get("camera"),
				mResource.descriptorSet.get("composition")
			};

			cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, mResource.pipelineLayout.get("composition"), 0, descriptorSets, nullptr);
			cmd.draw(4, 1, 0, 0);

			cmd.endRenderPass();
			cmd.end();
		}
	}

	// debug
	{
		vk::CommandBufferAllocateInfo allocInfo;
		allocInfo.commandPool = mContext.getGraphicsCommandPool();
		allocInfo.level = vk::CommandBufferLevel::ePrimary;
		allocInfo.commandBufferCount = static_cast<uint32_t>(mSwapchainFramebuffers.size());

		mDebugCommandBuffer = mContext.getDevice().allocateCommandBuffersUnique(allocInfo);

		// record command buffers
		for (size_t i = 0; i < mDebugCommandBuffer.size(); i++)
		{
			vk::RenderPassBeginInfo renderpassInfo;
			renderpassInfo.renderPass = *mCompositionRenderpass;
			renderpassInfo.framebuffer = *mSwapchainFramebuffers[i];
			renderpassInfo.renderArea.offset = vk::Offset2D{ 0, 0 };
			renderpassInfo.renderArea.extent = mSwapchainExtent;

			std::array<vk::ClearValue, 1> clearValues;
			clearValues[0].color.setFloat32({ 1.0f, 0.8f, 0.4f, 1.0f });

			renderpassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
			renderpassInfo.pClearValues = clearValues.data();

			auto& cmd = *mDebugCommandBuffer[i];

			cmd.begin(vk::CommandBufferBeginInfo{});
			cmd.beginRenderPass(renderpassInfo, vk::SubpassContents::eInline);
			cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, mResource.pipeline.get("debug"));

			std::array<vk::DescriptorSet, 2> descriptorSets = {
				mResource.descriptorSet.get("debug"),
				mResource.descriptorSet.get("composition"),
			};

			cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, mResource.pipelineLayout.get("debug"), 0, descriptorSets, nullptr);
			cmd.draw(4, 1, 0, 0);

			cmd.endRenderPass();
			cmd.end();
		}
	}
}

void Renderer::createSemaphores()
{
	vk::SemaphoreCreateInfo semaphoreInfo;

	mRenderFinishedSemaphore = mContext.getDevice().createSemaphoreUnique(semaphoreInfo);
	mImageAvailableSemaphore = mContext.getDevice().createSemaphoreUnique(semaphoreInfo);
	mLightCullingFinishedSemaphore = mContext.getDevice().createSemaphoreUnique(semaphoreInfo);
	mGBufferFinishedSemaphore = mContext.getDevice().createSemaphoreUnique(semaphoreInfo);
}

void Renderer::createComputePipeline()
{
	// vk::PushConstantRange pushConstantRange;
	// pushConstantRange.offset = 0;
	// pushConstantRange.size = sizeof(PushConstantObject);
	// pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eCompute;

	std::array<vk::DescriptorSetLayout, 2> setLayouts = { 
		mResource.descriptorSetLayout.get("camera"),
		mResource.descriptorSetLayout.get("lightculling")
	};

	vk::PipelineLayoutCreateInfo layoutInfo;
	layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
	layoutInfo.pSetLayouts = setLayouts.data();
	// layoutInfo.pushConstantRangeCount = 1;
	// layoutInfo.pPushConstantRanges = &pushConstantRange;

	mResource.pipelineLayout.add("lightculling", layoutInfo);

	auto shader = mResource.shaderModule.add("data/lightculling.comp");

	vk::PipelineShaderStageCreateInfo stageInfo;
	stageInfo.stage = vk::ShaderStageFlagBits::eCompute;
	stageInfo.module = shader;
	stageInfo.pName = "main";

	vk::ComputePipelineCreateInfo pipelineInfo;
	pipelineInfo.stage = stageInfo;
	pipelineInfo.layout = mResource.pipelineLayout.get("lightculling");

	mResource.pipeline.add("lightculling", *mPipelineCache, pipelineInfo);
}

void Renderer::createComputeCommandBuffer()
{
	// Create command buffer
	{
		vk::CommandBufferAllocateInfo allocInfo;
		allocInfo.commandPool = mContext.getComputeCommandPool();
		allocInfo.level = vk::CommandBufferLevel::ePrimary;
		allocInfo.commandBufferCount = 1;

		mLightCullingCommandBuffer = std::move(mContext.getDevice().allocateCommandBuffersUnique(allocInfo)[0]);
	}

	// Record command buffer
	{
		auto& cmd = *mLightCullingCommandBuffer;

		cmd.begin(vk::CommandBufferBeginInfo{});

		// TODO Barries might be needed when updating lights inbetween rendering
		// std::array<vk::BufferMemoryBarrier, 2> barriersBefore;
		// barriersBefore[0].srcAccessMask = vk::AccessFlagBits::eShaderRead;
		// barriersBefore[0].dstAccessMask = vk::AccessFlagBits::eShaderWrite;
		// barriersBefore[0].buffer = *mLightsOutBuffer.handle;
		// barriersBefore[0].size = mLightsOutBuffer.size;
		
		//cmd.pipelineBarrier(
		//	vk::PipelineStageFlagBits::eFragmentShader,  // srcStageMask
		//	vk::PipelineStageFlagBits::eComputeShader,  // dstStageMask
		//	vk::DependencyFlags(),  // dependencyFlags
		//	nullptr,  // pBUfferMemoryBarriers
		//	barriersBefore,  // pBUfferMemoryBarriers
		//	nullptr // pImageMemoryBarriers
		//);


		std::array<vk::DescriptorSet, 2> descriptorSets{ 
			mResource.descriptorSet.get("camera"),
			mResource.descriptorSet.get("lightculling")
		};

		cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, mResource.pipelineLayout.get("lightculling"), 0, descriptorSets, nullptr);

		// PushConstantObject pco;
		// cmd.pushConstants(mResource.pipelineLayout.get("lightculling"), vk::ShaderStageFlagBits::eCompute, 0, sizeof(pco), &pco);
		cmd.bindPipeline(vk::PipelineBindPoint::eCompute, mResource.pipeline.get("lightculling"));
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
		//	static_cast<uint32_t>(barriersAfter.size()), barriersAfter.data(),
		//	0, nullptr
		//);

		cmd.end();
	}
}

void Renderer::updateUniformBuffers()
{

	// // update model
	// {
	// 	auto data = reinterpret_cast<ObjectUBO*>(mContext.getDevice().mapMemory(*mObjectStagingBuffer.memory, 0, sizeof(ObjectUBO)));
	// 	data->model = glm::mat4();
	//
	// 	mContext.getDevice().unmapMemory(*mObjectStagingBuffer.memory);
	// 	mUtility.copyBuffer(*mObjectStagingBuffer.handle, *mObjectUniformBuffer.handle, sizeof(ObjectUBO));
	// }

	// update debug buffer, if dirty bit is set
	if (BaseApp::getInstance().getUI().debugStateUniformNeedsUpdate())
	{
		auto state = static_cast<uint32_t>(BaseApp::getInstance().getUI().getDebugIndex());
		auto data = reinterpret_cast<DebugUBO*>(mContext.getDevice().mapMemory(*mDebugUniformBuffer.memory, 0, sizeof(DebugUBO)));

		data->debugState = state;

		vk::MappedMemoryRange range;
		range.size = sizeof(DebugUBO);
		range.memory = *mDebugUniformBuffer.memory;

		mContext.getDevice().unmapMemory(*mDebugUniformBuffer.memory);
		mContext.getDevice().flushMappedMemoryRanges(range);
	}
}

void Renderer::drawFrame()
{
	// Acquire an image from the swap chain
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

	std::vector<vk::SubmitInfo> submitInfos;

	// Submit GBUffer creation
	{
		vk::PipelineStageFlags waitStages = vk::PipelineStageFlagBits::eColorAttachmentOutput;

		vk::SubmitInfo submitInfo;
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &(*mImageAvailableSemaphore);
		submitInfo.pWaitDstStageMask = &waitStages;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &*mGBufferCommandBuffer;
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &(*mGBufferFinishedSemaphore);

		// submitInfos.emplace_back(submitInfo);
		mContext.getGraphicsQueue().submit(submitInfo, nullptr);
	}
	// TODO: use Fence and we can have cpu start working at a earlier time


	if (BaseApp::getInstance().getUI().getDebugIndex() == DebugStates::disabled)
	{
		// submit light culling
		{
			vk::PipelineStageFlags waitStages = vk::PipelineStageFlagBits::eComputeShader;

			vk::SubmitInfo submitInfo;
			submitInfo.waitSemaphoreCount = 1;
			submitInfo.pWaitSemaphores = &(*mGBufferFinishedSemaphore);
			submitInfo.pWaitDstStageMask = &waitStages;
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &*mLightCullingCommandBuffer;
			submitInfo.signalSemaphoreCount = 1;
			submitInfo.pSignalSemaphores = &(*mLightCullingFinishedSemaphore);

			mContext.getComputeQueue().submit(submitInfo, nullptr); // TODO compute queue in graphics queue
		}

		// submit composition
		{
			vk::PipelineStageFlags waitStages = vk::PipelineStageFlagBits::eFragmentShader;

			vk::SubmitInfo submitInfo;
			submitInfo.waitSemaphoreCount = 1;
			submitInfo.pWaitSemaphores = &(*mLightCullingFinishedSemaphore);
			submitInfo.pWaitDstStageMask = &waitStages;
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &*mCommandBuffers[imageIndex];
			submitInfo.signalSemaphoreCount = 1;
			submitInfo.pSignalSemaphores = &(*mRenderFinishedSemaphore);

			submitInfos.emplace_back(submitInfo);
		}
	}
	else
	{
		// debugging
		vk::PipelineStageFlags waitStages = vk::PipelineStageFlagBits::eFragmentShader;

		vk::SubmitInfo submitInfo;
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &(*mGBufferFinishedSemaphore);
		submitInfo.pWaitDstStageMask = &waitStages;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &*mDebugCommandBuffer[imageIndex];
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &(*mRenderFinishedSemaphore);

		submitInfos.emplace_back(submitInfo);
	}

	mContext.getGraphicsQueue().submit(submitInfos, nullptr);



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
