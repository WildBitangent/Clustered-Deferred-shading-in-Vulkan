#include "Renderer.h"

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


// todo refactor structs
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

Renderer::Renderer(GLFWwindow* window, ThreadPool& pool)
	: mContext(window)
	, mUtility(mContext)
	, mResource(mContext.getDevice())
{
	vk::PhysicalDeviceSubgroupProperties subgroupProperties;
	vk::PhysicalDeviceProperties2 properties2;
	properties2.pNext = &subgroupProperties;

	mContext.getPhysicalDevice().getProperties2(&properties2);

	mSubGroupSize = subgroupProperties.subgroupSize;

	setTileCount();

	createSwapChain();
	createSwapChainImageViews();
	createGBuffers();
	createRenderPasses();
	createDescriptorSetLayouts();
	createPipelineCache();
	createGraphicsPipelines();
	createComputePipeline();
	createUniformBuffers();
	createClusteredBuffers();
	createLights();
	createDescriptorPool();
	// mModel.loadModel(mContext, "data/models/sponza.obj", *mGBufferAttachments.sampler, *mDescriptorPool, mResource, pool);
	mModel.loadModel(mContext, "data/models/dust2/de_dust2.obj", *mGBufferAttachments.sampler, *mDescriptorPool, mResource, pool);
	createDescriptorSets();
	createGraphicsCommandBuffers();
	createComputeCommandBuffer();
	createSyncPrimitives();
}

void Renderer::resize()
{
	recreateSwapChain();
}

void Renderer::requestDraw(float deltatime)
{
	updateUniformBuffers(/*deltatime*/);
	submitLightSortingCmds(mCurrentFrame);
	drawFrame();
}

void Renderer::cleanUp()
{
	mContext.getDevice().waitIdle(); // finish everything before destroying
}

void Renderer::setCamera(const glm::mat4& view, const glm::vec3 campos)
{
	// update camera ubo
	{
		auto data = reinterpret_cast<CameraUBO*>(mContext.getDevice().mapMemory(*mCameraStagingBuffer.memory, 0, sizeof(CameraUBO)));
		data->view = view;
		data->projection = glm::perspective(glm::radians(45.0f), mSwapchainExtent.width / static_cast<float>(mSwapchainExtent.height), 0.5f, 100.0f);
		data->projection[1][1] *= -1; //since the Y axis of Vulkan NDC points down TODO extension viewport -1
		data->invProj = glm::inverse(data->projection);
		data->cameraPosition = campos;
		data->screenSize = {mSwapchainExtent.width, mSwapchainExtent.height};

		mContext.getDevice().unmapMemory(*mCameraStagingBuffer.memory);
		mUtility.copyBuffer(*mCameraStagingBuffer.handle, *mCameraUniformBuffer.handle, sizeof(CameraUBO));
	}
}

void Renderer::reloadShaders(size_t size)
{
	mCurrentTileSize = size;
	setTileCount();

	vkDeviceWaitIdle(mContext.getDevice());

	createGraphicsPipelines();
	createGraphicsCommandBuffers();
	createComputePipeline();
	createComputeCommandBuffer();
}

void Renderer::recreateSwapChain()
{
    for (int width = 0, height = 0; width == 0 || height == 0; ) 
	{
        glfwGetFramebufferSize(mContext.getWindow(), &width, &height);
        glfwWaitEvents();
    }

	setTileCount();
	vkDeviceWaitIdle(mContext.getDevice());

	createSwapChain();
	createSwapChainImageViews();
	createGBuffers();
	createRenderPasses();
	createGraphicsPipelines();
	createGraphicsCommandBuffers();
	createComputePipeline();
	createComputeCommandBuffer();
	BaseApp::getInstance().getUI().resize();
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
			description.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

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
		bindings.emplace_back(bindings.size(), vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute);

		// lights out
		bindings.emplace_back(bindings.size(), vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute);
		
		// lights indirection
		bindings.emplace_back(bindings.size(), vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute);
		
		// depth binding
		bindings.emplace_back(bindings.size(), vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute);
		
		// page table
		bindings.emplace_back(bindings.size(), vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute);

		// page pool
		bindings.emplace_back(bindings.size(), vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute);

		// unique clusters
		bindings.emplace_back(bindings.size(), vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute);

		// splitters
		bindings.emplace_back(bindings.size(), vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute);

		vk::DescriptorSetLayoutCreateInfo createInfo;
		createInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		createInfo.pBindings = bindings.data();

		mResource.descriptorSetLayout.add("lightculling", createInfo);
	}

	// Composition
	{
		std::vector<vk::DescriptorSetLayoutBinding> bindings;
		
		// point lights
		bindings.emplace_back(bindings.size(), vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eFragment);

		// lights out
		bindings.emplace_back(bindings.size(), vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eFragment);

		// lights indirection
		bindings.emplace_back(bindings.size(), vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eFragment);

		// position
		bindings.emplace_back(bindings.size(), vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment);

		// albedo
		bindings.emplace_back(bindings.size(), vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment);

		// normal
		bindings.emplace_back(bindings.size(), vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment);

		// depth
		bindings.emplace_back(bindings.size(), vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment);
		
		// page table
		bindings.emplace_back(bindings.size(), vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eFragment);

		// page pool
		bindings.emplace_back(bindings.size(), vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eFragment);

		// unique clusters
		bindings.emplace_back(bindings.size(), vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eFragment);

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

		// create specialization constants
		std::vector<vk::SpecializationMapEntry> entries;
		entries.emplace_back(entries.size(), entries.size() * 4, 4); // Tile Size
		entries.emplace_back(entries.size(), entries.size() * 4, 4); // Y_slices

		float ySlices = std::log(1.0 + 2.0 / mTileCount.y);
		std::vector<uint32_t> constantData = {static_cast<uint32_t>(mCurrentTileSize), *reinterpret_cast<uint32_t*>(&ySlices)};
		
		vk::SpecializationInfo specializationInfo;
		specializationInfo.mapEntryCount = entries.size();
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
	}

	// normal
	{
		mGBufferAttachments.normal = mUtility.createImage(
			mSwapchainExtent.width, mSwapchainExtent.height,
			vk::Format::eR16G16Sfloat,
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
			vk::MemoryPropertyFlagBits::eDeviceLocal
		);

		mGBufferAttachments.normal.view = mUtility.createImageView(*mGBufferAttachments.normal.handle, mGBufferAttachments.normal.format, vk::ImageAspectFlagBits::eColor);
	}

	// sampler for attachments
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

		mGBufferAttachments.sampler = mContext.getDevice().createSamplerUnique(samplerInfo);
	}
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

	// Adding data to scene object buffer
	{ 
		ObjectUBO ubo;
		ubo.model = glm::scale(glm::mat4(1.f), glm::vec3(0.01f)); // TODO update uniform for objects
	
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

void Renderer::createClusteredBuffers()
{
	auto alignedMemorySize = [this](vk::DeviceSize size)
	{
		const auto align = mContext.getPhysicalDevice().getProperties().limits.minStorageBufferOffsetAlignment;
		return ((size - 1) / align + 1) * align;
	};
	
	// page table
	mPageTableOffset = 0;
	mPageTableSize = alignedMemorySize((32'768 + 3) * sizeof(uint32_t)); 

	// physical page pool
	constexpr vk::DeviceSize pageCount = 2048;
	constexpr vk::DeviceSize pageSize = 512 * sizeof(uint32_t);
	
	mPagePoolOffset = mPageTableSize;
	mPagePoolSize = alignedMemorySize(pageCount * pageSize);

	// compacted unique clusters 
	mUniqueClustersOffset = mPagePoolOffset + mPagePoolSize;
	mUniqueClustersSize = alignedMemorySize(4096 * sizeof(uint32_t)); // default value, every scene should tweak this value

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
	mLightsOutSwap = MAX_LIGHTS * 20; // every sceene need to tweak this value
	mLightsOutSize = mLightsOutSwap;
	mSplittersSize = 1024 * sizeof(uint32_t) * 2; // todo remove in final prob

	mLightsOutOffset = 0;
	mPointLightsOffset = mLightsOutSize;
	mLightsOutSwapOffset = mPointLightsOffset + mPointLightsSize;
	mSplittersOffset = mLightsOutSwapOffset + mLightsOutSwap;

	vk::DeviceSize bufferSize = mPointLightsSize + mLightsOutSize + mLightsOutSwap + mSplittersSize;

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
	std::vector<vk::WriteDescriptorSet> descriptorWrites;
	
	// world transform
	vk::DescriptorBufferInfo transformBufferInfo;
	transformBufferInfo.buffer = *mCameraUniformBuffer.handle;
	transformBufferInfo.offset = 0;
	transformBufferInfo.range = sizeof(CameraUBO);
	{
		vk::DescriptorSetAllocateInfo allocInfo;
		allocInfo.descriptorPool = *mDescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &mResource.descriptorSetLayout.get("camera");

		auto targetSet = mResource.descriptorSet.add("camera", allocInfo);

		// refer to the uniform object buffer
		auto write = util::createDescriptorWriteBuffer(targetSet, 0, vk::DescriptorType::eUniformBuffer, transformBufferInfo);
		descriptorWrites.emplace_back(write);
	}
	
	// Model
	vk::DescriptorBufferInfo modelBufferInfo;
	modelBufferInfo.buffer = *mObjectUniformBuffer.handle;
	modelBufferInfo.offset = 0;
	modelBufferInfo.range = sizeof(ObjectUBO);
	{
		vk::DescriptorSetAllocateInfo allocInfo;
		allocInfo.descriptorPool = *mDescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &mResource.descriptorSetLayout.get("model");

		const auto targetSet = mResource.descriptorSet.add("model", allocInfo);

		descriptorWrites.emplace_back(util::createDescriptorWriteBuffer(targetSet, 0, vk::DescriptorType::eUniformBuffer, modelBufferInfo));
	}
	
	// Light culling
	vk::DescriptorBufferInfo pointLightsInfo;
	pointLightsInfo.buffer = *mLightsBuffers.handle;
	pointLightsInfo.offset = mPointLightsOffset;
	pointLightsInfo.range = mPointLightsSize;

	vk::DescriptorBufferInfo lightsOutInfo;
	lightsOutInfo.buffer = *mLightsBuffers.handle;
	lightsOutInfo.offset = mLightsOutOffset;
	lightsOutInfo.range = mLightsOutSize;

	vk::DescriptorBufferInfo lightsIndirectionInfo;
	lightsIndirectionInfo.buffer = *mLightsBuffers.handle;
	lightsIndirectionInfo.offset = mLightsOutSwapOffset;
	lightsIndirectionInfo.range = mLightsOutSwap;

	vk::DescriptorBufferInfo pageTableInfo;
	pageTableInfo.buffer = *mClusteredBuffer.handle;
	pageTableInfo.offset = mPageTableOffset;
	pageTableInfo.range = mPageTableSize;

	vk::DescriptorBufferInfo pagePoolInfo;
	pagePoolInfo.buffer = *mClusteredBuffer.handle;
	pagePoolInfo.offset = mPagePoolOffset;
	pagePoolInfo.range = mPagePoolSize;

	vk::DescriptorBufferInfo uniqueClustersInfo;
	uniqueClustersInfo.buffer = *mClusteredBuffer.handle;
	uniqueClustersInfo.offset = mUniqueClustersOffset;
	uniqueClustersInfo.range = mUniqueClustersSize;

	vk::DescriptorBufferInfo splittersInfo;
	splittersInfo.buffer = *mLightsBuffers.handle;
	splittersInfo.offset = mSplittersOffset;
	splittersInfo.range = mSplittersSize;

	vk::DescriptorImageInfo depthInfo;
	depthInfo.sampler = *mGBufferAttachments.sampler;
	depthInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	depthInfo.imageView = *mGBufferAttachments.depth.view;
	{
		vk::DescriptorSetAllocateInfo allocInfo;
		allocInfo.descriptorPool = *mDescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &mResource.descriptorSetLayout.get("lightculling");

		auto targetSet = mResource.descriptorSet.add("lightculling_01", allocInfo);

		std::vector<vk::WriteDescriptorSet> writes;

		uint32_t binding = 0;
		writes.emplace_back(util::createDescriptorWriteBuffer(targetSet, binding++, vk::DescriptorType::eStorageBuffer, pointLightsInfo));
		writes.emplace_back(util::createDescriptorWriteBuffer(targetSet, binding++, vk::DescriptorType::eStorageBuffer, lightsOutInfo));
		writes.emplace_back(util::createDescriptorWriteBuffer(targetSet, binding++, vk::DescriptorType::eStorageBuffer, lightsIndirectionInfo));
		writes.emplace_back(util::createDescriptorWriteImage(targetSet, binding++, depthInfo));
		writes.emplace_back(util::createDescriptorWriteBuffer(targetSet, binding++, vk::DescriptorType::eStorageBuffer, pageTableInfo));
		writes.emplace_back(util::createDescriptorWriteBuffer(targetSet, binding++, vk::DescriptorType::eStorageBuffer, pagePoolInfo));
		writes.emplace_back(util::createDescriptorWriteBuffer(targetSet, binding++, vk::DescriptorType::eStorageBuffer, uniqueClustersInfo));
		writes.emplace_back(util::createDescriptorWriteBuffer(targetSet, binding++, vk::DescriptorType::eStorageBuffer, splittersInfo));

		descriptorWrites.insert(descriptorWrites.end(), writes.begin(), writes.end());

		// swapped light buffers
		targetSet = mResource.descriptorSet.add("lightculling_10", allocInfo);
		for (auto& item : writes)
			item.dstSet = targetSet;

		std::swap(writes[1].dstBinding, writes[2].dstBinding);
	
		descriptorWrites.insert(descriptorWrites.end(), writes.begin(), writes.end());
	}
	
	// composition
	vk::DescriptorImageInfo positionInfo;
	positionInfo.sampler = *mGBufferAttachments.sampler;
	positionInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	positionInfo.imageView = *mGBufferAttachments.position.view;

	vk::DescriptorImageInfo albedoInfo;
	albedoInfo.sampler = *mGBufferAttachments.sampler;
	albedoInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	albedoInfo.imageView = *mGBufferAttachments.color.view;

	vk::DescriptorImageInfo normalInfo;
	normalInfo.sampler = *mGBufferAttachments.sampler;
	normalInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	normalInfo.imageView = *mGBufferAttachments.normal.view;
	{
		vk::DescriptorSetAllocateInfo allocInfo;
		allocInfo.descriptorPool = *mDescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &mResource.descriptorSetLayout.get("composition");

		auto targetSet = mResource.descriptorSet.add("composition_01", allocInfo);

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
		targetSet = mResource.descriptorSet.add("composition_10", allocInfo);
		for (auto& item : writes)
			item.dstSet = targetSet;

		std::swap(writes[1].dstBinding, writes[2].dstBinding);
	
		descriptorWrites.insert(descriptorWrites.end(), writes.begin(), writes.end());
	}

	// debug
	vk::DescriptorBufferInfo uboInfo;
	uboInfo.buffer = *mDebugUniformBuffer.handle;
	uboInfo.offset = 0;
	uboInfo.range = mDebugUniformBuffer.size;
	{
		vk::DescriptorSetAllocateInfo allocInfo;
		allocInfo.descriptorPool = *mDescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &mResource.descriptorSetLayout.get("debug");
		
		auto targetSet = mResource.descriptorSet.add("debug", allocInfo);
		
		descriptorWrites.emplace_back(util::createDescriptorWriteBuffer(targetSet, 0, vk::DescriptorType::eUniformBuffer, uboInfo));
	}

	mContext.getDevice().updateDescriptorSets(descriptorWrites, nullptr);
}

void Renderer::createGraphicsCommandBuffers()
{
	// Gbuffers
	{
		vk::CommandBufferAllocateInfo allocInfo;
		allocInfo.commandPool = mContext.getStaticCommandPool();
		allocInfo.level = vk::CommandBufferLevel::ePrimary;
		allocInfo.commandBufferCount = 1;

		auto& cmd = mResource.cmd.add("gBuffer", allocInfo);

		vk::CommandBufferBeginInfo beginInfo;
		beginInfo.flags = vk::CommandBufferUsageFlagBits::eSimultaneousUse;

		std::array<vk::ClearValue, 4> clearValues;
		clearValues[0].color.setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f });
		clearValues[1].color.setFloat32({ 1.0f, 0.8f, 0.4f, 1.0f });
		clearValues[2].color.setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f });
		clearValues[3].depthStencil.setDepth(1.0f).setStencil(0.0f);

		vk::RenderPassBeginInfo renderpassInfo;
		renderpassInfo.renderPass = *mGBufferRenderpass;
		renderpassInfo.framebuffer = *mGBufferFramebuffer;
		renderpassInfo.renderArea.offset = vk::Offset2D{ 0, 0 };
		renderpassInfo.renderArea.extent = mSwapchainExtent;
		renderpassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderpassInfo.pClearValues = clearValues.data();

		std::array<vk::DescriptorSet, 2> descriptorSets = {
			mResource.descriptorSet.get("camera"),
			mResource.descriptorSet.get("model")
		};

		auto pipelineLayout = mResource.pipelineLayout.get("gbuffers");
		
		cmd.begin(beginInfo);
		cmd.beginRenderPass(renderpassInfo, vk::SubpassContents::eInline);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, mResource.pipeline.get("gbuffers"));
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
		allocInfo.commandPool = mContext.getStaticCommandPool();
		allocInfo.level = vk::CommandBufferLevel::eSecondary;
		allocInfo.commandBufferCount = static_cast<uint32_t>(mSwapchainFramebuffers.size());
		mResource.cmd.add("composition", allocInfo);
		
		allocInfo.commandPool = mContext.getDynamicCommandPool();
		allocInfo.level = vk::CommandBufferLevel::ePrimary;
		mResource.cmd.add("primaryComposition", allocInfo);
	}

	// debug
	{
		vk::CommandBufferAllocateInfo allocInfo;
		allocInfo.commandPool = mContext.getStaticCommandPool();
		allocInfo.level = vk::CommandBufferLevel::eSecondary;
		allocInfo.commandBufferCount = static_cast<uint32_t>(mSwapchainFramebuffers.size());
		mResource.cmd.add("debug", allocInfo);

		allocInfo.commandPool = mContext.getDynamicCommandPool();
		allocInfo.level = vk::CommandBufferLevel::ePrimary;
		mResource.cmd.add("primaryDebug", allocInfo);
		
		vk::CommandBufferInheritanceInfo inheritanceInfo;
		inheritanceInfo.renderPass = *mCompositionRenderpass;
		inheritanceInfo.subpass = 0;

		vk::CommandBufferBeginInfo beginInfo;
		beginInfo.flags = vk::CommandBufferUsageFlagBits::eRenderPassContinue;
		beginInfo.pInheritanceInfo = &inheritanceInfo;

		// record command buffers
		for (auto& cmd : mResource.cmd.getAll("debug"))
		{
			cmd->begin(beginInfo);
			cmd->bindPipeline(vk::PipelineBindPoint::eGraphics, mResource.pipeline.get("debug"));
			
			std::array<vk::DescriptorSet, 2> descriptorSets = {
				mResource.descriptorSet.get("debug"),
				mResource.descriptorSet.get("composition_01"),
			};
			
			cmd->bindDescriptorSets(vk::PipelineBindPoint::eGraphics, mResource.pipelineLayout.get("debug"), 0, descriptorSets, nullptr);
			cmd->draw(4, 1, 0, 0);
			
			cmd->end();
		}
	}
}

void Renderer::createSyncPrimitives()
{
	vk::FenceCreateInfo fenceInfo;
	fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;

	mResource.semaphore.add("lightCullingFinished");
	mResource.semaphore.add("lightSortingFinished");
	mResource.semaphore.add("lightCopyFinished");
	mResource.semaphore.add("gBufferFinished");
	mResource.semaphore.add("renderFinished", mSwapchainImages.size());
	mResource.semaphore.add("imageAvailable", mSwapchainImages.size());

	mResource.fence.add("lightCopy");
	// mResource.fence.add("renderFinished", mSwapchainImages.size());
}

void Renderer::createComputePipeline()
{
	std::array<vk::DescriptorSetLayout, 2> setLayouts = { 
		mResource.descriptorSetLayout.get("camera"),
		mResource.descriptorSetLayout.get("lightculling")
	};

	vk::PipelineLayoutCreateInfo layoutInfo;
	layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
	layoutInfo.pSetLayouts = setLayouts.data();

	mResource.pipelineLayout.add("pageflag", layoutInfo);

	// create specialization constants
	std::vector<vk::SpecializationMapEntry> entries;
	entries.emplace_back(entries.size(), entries.size() * 4, 4); // Tile Size
	entries.emplace_back(entries.size(), entries.size() * 4, 4); // Y_slices
	entries.emplace_back(entries.size(), entries.size() * 4, 4); // WG size

	uint32_t groupSize = mCurrentTileSize <= 32 ? mCurrentTileSize : 32;
	float ySlices = std::log(1.0 + 2.0 / mTileCount.y);
	std::vector<uint32_t> constantData = {
		static_cast<uint32_t>(mCurrentTileSize), 
		*reinterpret_cast<uint32_t*>(&ySlices),
		groupSize,
	};
	
	vk::SpecializationInfo specializationInfo;
	specializationInfo.mapEntryCount = entries.size();
	specializationInfo.pMapEntries = entries.data();
	specializationInfo.dataSize = constantData.size() * 4;
	specializationInfo.pData = constantData.data();

	vk::PipelineShaderStageCreateInfo stageInfo;
	stageInfo.stage = vk::ShaderStageFlagBits::eCompute;
	stageInfo.pName = "main";
	stageInfo.pSpecializationInfo = &specializationInfo;

	// page flag
	{
		stageInfo.module = mResource.shaderModule.add("data/pageflag.comp");

		vk::ComputePipelineCreateInfo pipelineInfo;
		pipelineInfo.stage = stageInfo;
		pipelineInfo.layout = mResource.pipelineLayout.get("pageflag");
		pipelineInfo.basePipelineIndex = -1;

		mResource.pipeline.add("pageflag", *mPipelineCache, pipelineInfo);
	}

	auto pipelineBase = mResource.pipeline.get("pageflag");
	for (const auto& name : { "pagealloc", "pagestore", "pagecompact" }) // todo remove unused pipelines
	{
		stageInfo.module = mResource.shaderModule.add(std::string("data/" ) + name + ".comp");

		vk::ComputePipelineCreateInfo pipelineInfo;
		pipelineInfo.stage = stageInfo;
		pipelineInfo.layout = mResource.pipelineLayout.get("pageflag");
		pipelineInfo.basePipelineHandle = pipelineBase;

		mResource.pipeline.add(name, *mPipelineCache, pipelineInfo);
	}

	// merge bitonic (push constants needed)
	vk::PushConstantRange pushConstantRange;
	pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
	
	layoutInfo.pPushConstantRanges = &pushConstantRange;
	layoutInfo.pushConstantRangeCount = 1;

	auto createPipeline = [&](const std::string name, const size_t pcSize)
	{
		pushConstantRange.size = pcSize;
		auto layout = mResource.pipelineLayout.add(name, layoutInfo);
		stageInfo.module = mResource.shaderModule.add(std::string("data/" ) + name + ".comp");
		
		vk::ComputePipelineCreateInfo pipelineInfo;
		pipelineInfo.stage = stageInfo;
		pipelineInfo.layout = layout;
		pipelineInfo.basePipelineHandle = pipelineBase;
	
		mResource.pipeline.add(name, *mPipelineCache, pipelineInfo);
	};

	createPipeline("sort_bitonic", sizeof(uint32_t));
	createPipeline("sort_mergeBitonic", 2 * sizeof(uint32_t));
	createPipeline("bvh", 3 * sizeof(uint32_t));
	createPipeline("lightculling", 11 * sizeof(uint32_t));
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
			mResource.descriptorSet.get("lightculling_01")
		};

		vk::CommandBufferInheritanceInfo inheritanceInfo;

		vk::CommandBufferBeginInfo beginInfo;
		beginInfo.pInheritanceInfo = &inheritanceInfo;
		beginInfo.flags = vk::CommandBufferUsageFlagBits::eSimultaneousUse;
		
		{
			auto& cmd = mResource.cmd.get("secondaryLightCulling");
			cmd.begin(beginInfo);
				cmd.fillBuffer(*mClusteredBuffer.handle, 0, VK_WHOLE_SIZE, 0); // todo weird barrier
				
				cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlagBits::eByRegion , nullptr, nullptr, nullptr); 
				cmd.fillBuffer(*mClusteredBuffer.handle, mPageTableOffset + 4, 8, 1);
				cmd.fillBuffer(*mClusteredBuffer.handle, mUniqueClustersOffset, 16, 1);
				cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, mResource.pipelineLayout.get("pageflag"), 0, descriptorSets, nullptr);
				
				cmd.bindPipeline(vk::PipelineBindPoint::eCompute, mResource.pipeline.get("pageflag"));
				cmd.dispatch(mTileCount.x, mTileCount.y, 1);
				
				cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlagBits::eByRegion, barrier, nullptr, nullptr);
				cmd.bindPipeline(vk::PipelineBindPoint::eCompute, mResource.pipeline.get("pagealloc"));
				cmd.dispatch(64, 1, 1);
				
				cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlagBits::eByRegion, barrier, nullptr, nullptr);
				cmd.bindPipeline(vk::PipelineBindPoint::eCompute, mResource.pipeline.get("pagestore"));
				cmd.dispatch(mTileCount.x, mTileCount.y, 1);

				cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eDrawIndirect, vk::DependencyFlagBits::eByRegion, indirectBarrier, nullptr, nullptr);
				cmd.bindPipeline(vk::PipelineBindPoint::eCompute, mResource.pipeline.get("pagecompact"));
				// cmd.dispatch(40, 1, 1); // TODO 1 smth to 1 WG
				cmd.dispatchIndirect(*mClusteredBuffer.handle, mPageTableOffset);
			cmd.end();
		}
	}
}

void Renderer::updateUniformBuffers()
{
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
	auto memorySize = sizeof(PointLight) * mLightsCount;

	auto data = reinterpret_cast<uint8_t*>(mContext.getDevice().mapMemory(*mPointLightsStagingBuffer.memory, 0, memorySize));
	memcpy(data, lights.data(), memorySize);
	mContext.getDevice().unmapMemory(*mPointLightsStagingBuffer.memory);
		
	vk::CommandBufferBeginInfo beginInfo;
	beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

	auto& cmd = mResource.cmd.get("lightCopy");

	cmd.begin(beginInfo);
	mUtility.recordCopyBuffer(cmd, *mPointLightsStagingBuffer.handle, *mLightsBuffers.handle, memorySize, 0, mPointLightsOffset);
	cmd.end();

	vk::SubmitInfo submitInfo;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &cmd;
	submitInfo.pSignalSemaphores = &mResource.semaphore.get("lightCopyFinished");

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
		catch(const vk::OutOfDateKHRError& e)
		{
			recreateSwapChain();
			return;
		}
		catch(...)
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
		submitInfo.pWaitSemaphores = &mResource.semaphore.get("imageAvailable", imageIndex);
		submitInfo.pWaitDstStageMask = &waitStages;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &mResource.cmd.get("gBuffer");
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &mResource.semaphore.get("gBufferFinished");

		// submitInfos.emplace_back(submitInfo);
		mContext.getGeneralQueue().submit(submitInfo, nullptr);
	}

	auto cmdUI = BaseApp::getInstance().getUI().recordCommandBuffer(imageIndex);

	if (BaseApp::getInstance().getUI().getDebugIndex() == DebugStates::disabled)
	{
		// submit light culling
		submitLightCullingCmds(imageIndex);
	
		auto& cmd = mResource.cmd.get("primaryComposition", imageIndex);

		// build composition
		{
			vk::RenderPassBeginInfo renderpassInfo;
			renderpassInfo.renderPass = *mCompositionRenderpass;
			renderpassInfo.framebuffer = *mSwapchainFramebuffers[imageIndex];
			renderpassInfo.renderArea.offset = vk::Offset2D{ 0, 0 };
			renderpassInfo.renderArea.extent = mSwapchainExtent;
			
			std::array<vk::DescriptorSet, 2> descriptorSets = {
				mResource.descriptorSet.get("camera"),
				mResource.descriptorSet.get(mLightBufferSwapUsed == "lightculling_01" ? "composition_01" : "composition_10")
			};
			
			std::array<vk::ClearValue, 1> clearValues;
			clearValues[0].color.setFloat32({ 1.0f, 0.8f, 0.4f, 1.0f });

			renderpassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
			renderpassInfo.pClearValues = clearValues.data();

			cmd.begin(vk::CommandBufferBeginInfo{});
			cmd.beginRenderPass(renderpassInfo, vk::SubpassContents::eInline);

			cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, mResource.pipeline.get("composition"));
			cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, mResource.pipelineLayout.get("composition"), 0, descriptorSets, nullptr);
			cmd.draw(4, 1, 0, 0);

			cmd.nextSubpass(vk::SubpassContents::eSecondaryCommandBuffers);
			cmd.executeCommands(1, &cmdUI);
			cmd.endRenderPass();
			cmd.end();
		}

		// submit composition
		{
			vk::PipelineStageFlags waitStages = vk::PipelineStageFlagBits::eFragmentShader;

			vk::SubmitInfo submitInfo;
			submitInfo.waitSemaphoreCount = 1;
			submitInfo.pWaitSemaphores = &mResource.semaphore.get("lightCullingFinished");
			submitInfo.pWaitDstStageMask = &waitStages;
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &cmd;
			submitInfo.signalSemaphoreCount = 1;
			submitInfo.pSignalSemaphores = &mResource.semaphore.get("renderFinished", imageIndex);

			submitInfos.emplace_back(submitInfo);
		}
	}
	else
	{
		auto& cmd = mResource.cmd.get("primaryDebug", imageIndex);

		// build debug
		{
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
			cmd.beginRenderPass(renderpassInfo, vk::SubpassContents::eSecondaryCommandBuffers);
			cmd.executeCommands(1, &mResource.cmd.get("debug", imageIndex));
			cmd.nextSubpass(vk::SubpassContents::eSecondaryCommandBuffers);
			cmd.executeCommands(1, &cmdUI);
			cmd.endRenderPass();
			cmd.end();
		}
		// debugging
		std::array<vk::PipelineStageFlags, 2> waitStages = {vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eTopOfPipe};
		std::array<vk::Semaphore, 2> semaphores = {
			mResource.semaphore.get("gBufferFinished"),
			mResource.semaphore.get("lightSortingFinished")
		};

		vk::SubmitInfo submitInfo;
		submitInfo.waitSemaphoreCount = 2;
		submitInfo.pWaitSemaphores = semaphores.data();
		submitInfo.pWaitDstStageMask = waitStages.data();
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &cmd;
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &mResource.semaphore.get("renderFinished", imageIndex);

		submitInfos.emplace_back(submitInfo);
	}

	mContext.getGeneralQueue().submit(submitInfos, nullptr);

	// 3. Submitting the result back to the swap chain to show it on screen
	{
		vk::PresentInfoKHR presentInfo;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &mResource.semaphore.get("renderFinished", imageIndex);
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &*mSwapchain;
		presentInfo.pImageIndices = &imageIndex;

		try
		{
			mContext.getGeneralQueue().presentKHR(presentInfo);
		}
		catch(const vk::OutOfDateKHRError& e)
		{
			recreateSwapChain();
			return;
		}
		catch(...)
		{
			throw std::runtime_error("Failed to present swap chain image!");
		}

	}

	mContext.getGeneralQueue().waitIdle();
	mCurrentFrame = (mCurrentFrame + 1) % mSwapchainImages.size();
}

void Renderer::submitLightCullingCmds(size_t imageIndex)
{
	auto& cmd = mResource.cmd.get("primaryLightCulling", imageIndex);

	std::array<vk::DescriptorSet, 2> descriptorSets{ 
mResource.descriptorSet.get("camera"),
mResource.descriptorSet.get(mLightBufferSwapUsed)
	};
	// record 
	cmd.begin(vk::CommandBufferBeginInfo{});
	cmd.executeCommands(1, &mResource.cmd.get("secondaryLightCulling"));

	// light culling
	{
		uint32_t data = 0;
		auto buffer = (mLightBufferSwapUsed == "lightculling_01") ? mLightsOutOffset : mLightsOutSwapOffset;
	
		vk::BufferMemoryBarrier before;
		before.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
		before.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
		before.size = 4;
		before.offset = buffer;
		before.buffer = *mLightsBuffers.handle;
	
		vk::BufferMemoryBarrier after = before;
		after.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		after.dstAccessMask = vk::AccessFlagBits::eIndirectCommandRead;
	
		cmd.updateBuffer(*mLightsBuffers.handle, buffer, 4, &data);
		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eDrawIndirect, vk::DependencyFlagBits::eByRegion, nullptr, after, nullptr);
		
		cmd.bindPipeline(vk::PipelineBindPoint::eCompute, mResource.pipeline.get("lightculling"));
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, mResource.pipelineLayout.get("pageflag"), 0, descriptorSets, nullptr);
		cmd.pushConstants(mResource.pipelineLayout.get("lightculling"), vk::ShaderStageFlagBits::eCompute, 0, 4, &mMaxLevel);
		cmd.pushConstants(mResource.pipelineLayout.get("lightculling"), vk::ShaderStageFlagBits::eCompute, 4, mLevelParam.size() * 8, mLevelParam.data());
		cmd.dispatchIndirect(*mClusteredBuffer.handle, mUniqueClustersOffset + 4);
	}

	cmd.end();
	
	std::vector<vk::PipelineStageFlags> waitStages = {vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eTransfer};
	std::vector<vk::Semaphore> waitSemaphores = { 
		mResource.semaphore.get("gBufferFinished"), 
		mResource.semaphore.get("lightSortingFinished")
	};

	vk::SubmitInfo submitInfo;
	submitInfo.waitSemaphoreCount = 2;
	submitInfo.pWaitSemaphores = waitSemaphores.data();
	submitInfo.pWaitDstStageMask = waitStages.data();
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &cmd;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &mResource.semaphore.get("lightCullingFinished");

	mContext.getGeneralQueue().submit(submitInfo, nullptr);
}

void Renderer::submitLightSortingCmds(size_t imageIndex)
{
	auto& cmd = mResource.cmd.get("lightSorting", imageIndex);

	std::array<vk::DescriptorSet, 2> descriptorSets{ 
mResource.descriptorSet.get("camera"),
mResource.descriptorSet.get("lightculling_01")
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

	mLevelParam.reserve(6);
	mLevelParam.clear();

	mLightBufferSwapUsed = "lightculling_01";

	cmd.begin(vk::CommandBufferBeginInfo{});

	// wait for lights update
	{
		vk::BufferMemoryBarrier barrier;
		barrier.size = mPointLightsSize;
		barrier.buffer = *mLightsBuffers.handle;
		barrier.offset = mPointLightsOffset;
		barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

		cmd.pipelineBarrier(
			vk::PipelineStageFlagBits::eTransfer, 
			vk::PipelineStageFlagBits::eComputeShader, 
			vk::DependencyFlagBits::eByRegion, 
			nullptr, barrier, nullptr
		);
	}

	// light sorting
	{
		size_t lightWindowsCount = (1023 + mLightsCount) >> 10;

		cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, mResource.pipelineLayout.get("pageflag"), 0, descriptorSets, nullptr);	
		cmd.bindPipeline(vk::PipelineBindPoint::eCompute, mResource.pipeline.get("sort_bitonic"));
		cmd.pushConstants(mResource.pipelineLayout.get("sort_bitonic"), vk::ShaderStageFlagBits::eCompute, 0, 4, &mLightsCount);
		cmd.dispatch(lightWindowsCount, 1, 1);
		
		if (mLightsCount > 128)
		{
			cmd.bindPipeline(vk::PipelineBindPoint::eCompute, mResource.pipeline.get("sort_mergeBitonic"));
			
			for (size_t i = 0; mLightsCount > (128u << i); i++)
			{
				size_t mergeCount = (mLightsCount - 1) / ((256 / mSubGroupSize) * (256 << i)) + 1; // every warp merges 256^i items
				uint32_t pc = i + 1;
			
				cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, mResource.pipelineLayout.get("pageflag"), 1, mResource.descriptorSet.get(mLightBufferSwapUsed), nullptr);
				cmd.pushConstants(mResource.pipelineLayout.get("sort_mergeBitonic"), vk::ShaderStageFlagBits::eCompute, 4, 4, &pc);
				barrier(cmd);
				cmd.dispatch(mergeCount, 1, 1);
				
				mLightBufferSwapUsed = (mLightBufferSwapUsed == "lightculling_01") ? "lightculling_10" : "lightculling_01";
			}
		}		
		
		mLightBufferSwapUsed = (mLightBufferSwapUsed == "lightculling_01") ? "lightculling_10" : "lightculling_01";
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, mResource.pipelineLayout.get("pageflag"), 1, mResource.descriptorSet.get(mLightBufferSwapUsed), nullptr);
	}

	// BVH
	{
		auto& layout = mResource.pipelineLayout.get("bvh");
		const uint32_t subgroupAlignedLightCount = ((mLightsCount - 1) / mSubGroupSize + 1) * mSubGroupSize - 1;
		mMaxLevel = (mLightsCount > mSubGroupSize) ? 1 : 0;

		struct
		{
			uint32_t count;
			uint32_t offset;
			uint32_t nextOffset;
		} pc = {
			mLightsCount,
			0,
			subgroupAlignedLightCount / 6 + 1,
		};
		
		cmd.bindPipeline(vk::PipelineBindPoint::eCompute, mResource.pipeline.get("bvh"));
		cmd.pushConstants(layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(pc), &pc);
		barrier(cmd);
		cmd.dispatch((mLightsCount - 1) / 512 + 1, 1, 1);

		mLevelParam.emplace_back(mLightsCount, 0);
		mLevelParam.emplace_back((mLightsCount - 1) / mSubGroupSize + 1, pc.nextOffset);
		
		for ( ;mLevelParam.back().first > mSubGroupSize; mMaxLevel++)
		{
			pc = {mLevelParam.back().first, pc.nextOffset, pc.nextOffset + mLevelParam.back().first };
						
			cmd.pushConstants(layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(pc), &pc);
			barrier(cmd);
			cmd.dispatch((pc.count - 1) / 512 + 1, 1, 1);
			
			mLevelParam.emplace_back((mLevelParam.back().first - 1) / mSubGroupSize + 1, pc.nextOffset);
		}
	}

	cmd.end();

	vk::SubmitInfo submitInfo;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &cmd;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &mResource.semaphore.get("lightSortingFinished");

	mContext.getComputeQueue().submit(submitInfo, nullptr);
}

void Renderer::setTileCount()
{
	int width, height;
    glfwGetFramebufferSize(mContext.getWindow(), &width, &height);
	mTileCount = {(width - 1) / mCurrentTileSize + 1, (height - 1) / mCurrentTileSize + 1};
}
