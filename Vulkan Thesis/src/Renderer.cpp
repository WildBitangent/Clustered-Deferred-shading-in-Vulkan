#include "Renderer.h"

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.hpp>
#include <algorithm> 

#include "Context.h"
#include "Model.h"
#include "BaseApp.h"
#include "imgui.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE

// TODO refactor this
constexpr auto WIDTH = 1024;
constexpr auto HEIGHT = 726;

constexpr auto TILE_SIZE = 32;

constexpr auto TILE_COUNT_X = (WIDTH - 1) / TILE_SIZE + 1;
constexpr auto TILE_COUNT_Y = (HEIGHT - 1) / TILE_SIZE + 1;
constexpr auto MAX_LIGHTS_PER_TILE = 1024;
constexpr auto MAX_POINTLIGHTS = 50000;

struct CameraUBO
{
	glm::mat4 view;
	glm::mat4 projection;
	glm::mat4 invProj;
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

struct LightParams // used with size of glm::vec4
{
	uint32_t lightsCount;
	glm::uvec2 screenSize;
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
	createUniformBuffers();
	createClusteredBuffers();
	createLights();
	createDescriptorPool();
	mModel.loadModel(mContext, "data/models/sponza.obj", *mGBufferAttachments.sampler, *mDescriptorPool, mResource);
	createDescriptorSets();
	createGraphicsCommandBuffers();
	createComputeCommandBuffer();
	createSyncPrimitives();
}

// Renderer::~Renderer()
// {
// 	mContext.getDevice().waitIdle(); // finish everything before destroying
// }

void Renderer::requestDraw(float deltatime)
{
	updateUniformBuffers(/*deltatime*/);
	drawFrame();
}

void Renderer::cleanUp()
{
	mContext.getDevice().waitIdle(); // finish everything before destroying
}

// void Renderer::cleanUp()
// {
// 	mContext.getDevice().waitIdle();
// }

void Renderer::setCamera(const glm::mat4& view, const glm::vec3 campos)
{
	// update camera ubo
	{
		auto data = reinterpret_cast<CameraUBO*>(mContext.getDevice().mapMemory(*mCameraStagingBuffer.memory, 0, sizeof(CameraUBO)));
		data->view = view;
		data->projection = glm::perspective(glm::radians(45.0f), mSwapchainExtent.width / static_cast<float>(mSwapchainExtent.height), 0.5f, 100.0f);
		data->projection[1][1] *= -1; //since the Y axis of Vulkan NDC points down
		data->invProj = glm::inverse(data->projection);
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
	createGraphicsCommandBuffers();
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

	QueueFamilyIndices indices = mContext.getQueueFamilyIndices();
	uint32_t queueFamilyIndices[] = { static_cast<uint32_t>(indices.graphicsFamily.first), static_cast<uint32_t>(indices.presentFamily.first) };

	if (indices.graphicsFamily.first != indices.presentFamily.first)
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
		// colorAttachmentComposition.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
		colorAttachmentComposition.finalLayout = vk::ImageLayout::ePresentSrcKHR; // to be directly used in swap chain

		// vk::AttachmentDescription colorAttachmentUI;
		// colorAttachmentUI.format = mSwapchainImageFormat;
		// colorAttachmentUI.samples = vk::SampleCountFlagBits::e1;
		// colorAttachmentUI.loadOp = vk::AttachmentLoadOp::eLoad; // before rendering
		// colorAttachmentUI.storeOp = vk::AttachmentStoreOp::eStore; // after rendering
		// colorAttachmentUI.stencilLoadOp = vk::AttachmentLoadOp::eDontCare; // no stencil
		// colorAttachmentUI.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		// colorAttachmentUI.initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
		// colorAttachmentUI.finalLayout = vk::ImageLayout::ePresentSrcKHR; // to be directly used in swap chain
		

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
		// mUtility.transitImageLayout(*mGBufferAttachments.depth.handle, vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal);
		
		// auto cmd = mUtility.beginSingleTimeCommands();
		// vk::ImageMemoryBarrier barrier;
		// barrier.oldLayout = vk::ImageLayout::eUndefined;
		// barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		// barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		// barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		// barrier.image = *mGBufferAttachments.depth.handle;
		//
		// barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
		// barrier.subresourceRange.baseMipLevel = 0;
		// barrier.subresourceRange.levelCount = 1;
		// barrier.subresourceRange.baseArrayLayer = 0;
		// barrier.subresourceRange.layerCount = 1;
		//
		// barrier.srcAccessMask = {};
		// barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
		//
		// vk::PipelineStageFlags srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
		// vk::PipelineStageFlags dstStage = vk::PipelineStageFlagBits::eTopOfPipe;
		//
		// cmd.pipelineBarrier(srcStage, dstStage, {}, nullptr, nullptr, barrier);
		//
		// mUtility.endSingleTimeCommands(cmd);
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
			vk::Format::eR16G16Sfloat,
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

void Renderer::createClusteredBuffers()
{
	// key size [10, 7, 7]	24b
	//			[ z, y, x]
	// page size 2^9		512B
	// key count / page size ... 2^24 / 2^9
	
	// page table
	constexpr vk::DeviceSize pageTableSize = (32'768 + 1) * sizeof(uint32_t) + 32 - (32'769 * 4 % 0x20);
	mPageTableOffset = 0;
	mPageTableSize = pageTableSize;

	// physical page pool
	constexpr vk::DeviceSize pageCount = 2048;
	constexpr vk::DeviceSize pageSize = 512 * sizeof(uint32_t) + 32 - (512 * 4 % 0x20);
	
	constexpr vk::DeviceSize pagePoolSize = pageCount * pageSize;
	mPagePoolOffset = pageTableSize;
	mPagePoolSize = pagePoolSize;

	// compacted clusters range
	constexpr vk::DeviceSize compactedClustersSize = (2048 * sizeof(uint16_t)); // tile can have max 255 unique clusters + number of clusters
	constexpr vk::DeviceSize compactedRangeSize = compactedClustersSize * TILE_COUNT_X * TILE_COUNT_Y + sizeof(uint16_t); // + cluster index counter
	mUniqueClustersOffset = mPagePoolOffset + pagePoolSize;
	mUniqueClustersSize = compactedRangeSize;

	// allocate buffer
	constexpr vk::DeviceSize bufferSize = pageTableSize + pagePoolSize + compactedRangeSize;

	mClusteredBuffer = mUtility.createBuffer(
		bufferSize,
		vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
		vk::MemoryPropertyFlagBits::eDeviceLocal
	);
}

void Renderer::createLights()
{
	constexpr vk::DeviceSize lightsOutSize = sizeof(uint32_t) * (MAX_LIGHTS_PER_TILE + 1) * TILE_COUNT_X * TILE_COUNT_Y; // ... + 1 => storing light counter for tile
	constexpr vk::DeviceSize pointLightsSize = sizeof(PointLight) * MAX_POINTLIGHTS/* + sizeof(glm::vec4)*/; // ... + size of LightParams struct
	constexpr vk::DeviceSize indirectionSize = pointLightsSize; // swap buffer
	mLightsOutOffset = 0;
	mPointLightsOffset = lightsOutSize;
	mLightsIndirectionOffset = mPointLightsOffset + pointLightsSize;

	mLightsOutSize = lightsOutSize;
	mPointLightsSize = pointLightsSize;
	mLightsIndirectionSize = indirectionSize;

	constexpr vk::DeviceSize bufferSize = pointLightsSize + lightsOutSize + indirectionSize;

	// allocate buffer
	mPointLightsStagingBuffer = mUtility.createBuffer(
		pointLightsSize,
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

	{
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
		lightsIndirectionInfo.offset = mLightsIndirectionOffset;
		lightsIndirectionInfo.range = mLightsIndirectionSize;

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

		vk::DescriptorImageInfo depthInfo;
		depthInfo.sampler = *mGBufferAttachments.sampler;
		depthInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		depthInfo.imageView = *mGBufferAttachments.depth.view;

		// Light culling
		{
			vk::DescriptorSetAllocateInfo allocInfo;
			allocInfo.descriptorPool = *mDescriptorPool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &mResource.descriptorSetLayout.get("lightculling");

			auto targetSet = mResource.descriptorSet.add("lightculling_01", allocInfo);

			std::vector<vk::WriteDescriptorSet> writes;
			writes.emplace_back(targetSet, writes.size(), 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &pointLightsInfo);
			writes.emplace_back(targetSet, writes.size(), 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &lightsOutInfo);
			writes.emplace_back(targetSet, writes.size(), 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &lightsIndirectionInfo);
			writes.emplace_back(targetSet, writes.size(), 0, 1, vk::DescriptorType::eCombinedImageSampler, &depthInfo);
			writes.emplace_back(targetSet, writes.size(), 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &pageTableInfo);
			writes.emplace_back(targetSet, writes.size(), 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &pagePoolInfo);
			writes.emplace_back(targetSet, writes.size(), 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &uniqueClustersInfo);

			descriptorWrites.insert(descriptorWrites.end(), writes.begin(), writes.end());

			// swapped light buffers
			targetSet = mResource.descriptorSet.add("lightculling_10", allocInfo);
			for (auto& item : writes)
				item.dstSet = targetSet;

			std::swap(writes[1].dstBinding, writes[2].dstBinding);
		
			descriptorWrites.insert(descriptorWrites.end(), writes.begin(), writes.end());
		}

		// composition
		{
			vk::DescriptorSetAllocateInfo allocInfo;
			allocInfo.descriptorPool = *mDescriptorPool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &mResource.descriptorSetLayout.get("composition");

			auto targetSet = mResource.descriptorSet.add("composition", allocInfo);

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

			std::vector<vk::WriteDescriptorSet> writes;
			writes.emplace_back(targetSet, writes.size(), 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &pointLightsInfo);
			writes.emplace_back(targetSet, writes.size(), 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &lightsOutInfo);
			writes.emplace_back(targetSet, writes.size(), 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &lightsIndirectionInfo);
			writes.emplace_back(targetSet, writes.size(), 0, 1, vk::DescriptorType::eCombinedImageSampler, &positionInfo);
			writes.emplace_back(targetSet, writes.size(), 0, 1, vk::DescriptorType::eCombinedImageSampler, &albedoInfo);
			writes.emplace_back(targetSet, writes.size(), 0, 1, vk::DescriptorType::eCombinedImageSampler, &normalInfo);
			writes.emplace_back(targetSet, writes.size(), 0, 1, vk::DescriptorType::eCombinedImageSampler, &depthInfo);
			writes.emplace_back(targetSet, writes.size(), 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &pageTableInfo);
			writes.emplace_back(targetSet, writes.size(), 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &pagePoolInfo);
			writes.emplace_back(targetSet, writes.size(), 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &uniqueClustersInfo);
			
			descriptorWrites.insert(descriptorWrites.end(), writes.begin(), writes.end());
		}
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
		allocInfo.commandPool = mContext.getStaticCommandPool();
		allocInfo.level = vk::CommandBufferLevel::ePrimary;
		allocInfo.commandBufferCount = 1;

		mGBufferCommandBuffer = std::move(mContext.getDevice().allocateCommandBuffersUnique(allocInfo)[0]);

		auto& cmd = *mGBufferCommandBuffer;

		vk::CommandBufferBeginInfo beginInfo;
		beginInfo.flags = vk::CommandBufferUsageFlagBits::eSimultaneousUse;

		cmd.begin(beginInfo);

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
		allocInfo.commandPool = mContext.getStaticCommandPool();
		allocInfo.level = vk::CommandBufferLevel::eSecondary;
		allocInfo.commandBufferCount = static_cast<uint32_t>(mSwapchainFramebuffers.size());
		mCompositionCommandBuffers = mContext.getDevice().allocateCommandBuffersUnique(allocInfo);

		allocInfo.commandPool = mContext.getDynamicCommandPool();
		allocInfo.level = vk::CommandBufferLevel::ePrimary;
		mPrimaryCompositionCommandBuffers = mContext.getDevice().allocateCommandBuffersUnique(allocInfo);

		vk::CommandBufferInheritanceInfo inheritanceInfo;
		inheritanceInfo.renderPass = *mCompositionRenderpass;
		inheritanceInfo.subpass = 0;

		vk::CommandBufferBeginInfo beginInfo;
		beginInfo.flags = vk::CommandBufferUsageFlagBits::eRenderPassContinue;
		beginInfo.pInheritanceInfo = &inheritanceInfo;

		// record command buffers
		for (auto& cmd : mCompositionCommandBuffers)
		{
			cmd->begin(beginInfo);
			cmd->bindPipeline(vk::PipelineBindPoint::eGraphics, mResource.pipeline.get("composition"));

			std::array<vk::DescriptorSet, 2> descriptorSets = {
				mResource.descriptorSet.get("camera"),
				mResource.descriptorSet.get("composition")
			};

			cmd->bindDescriptorSets(vk::PipelineBindPoint::eGraphics, mResource.pipelineLayout.get("composition"), 0, descriptorSets, nullptr);
			cmd->draw(4, 1, 0, 0);

			cmd->end();
		}
	}

	// debug
	{
		vk::CommandBufferAllocateInfo allocInfo;
		allocInfo.commandPool = mContext.getStaticCommandPool();
		allocInfo.level = vk::CommandBufferLevel::eSecondary;
		allocInfo.commandBufferCount = static_cast<uint32_t>(mSwapchainFramebuffers.size());
		mDebugCommandBuffers = mContext.getDevice().allocateCommandBuffersUnique(allocInfo);

		allocInfo.commandPool = mContext.getDynamicCommandPool();
		allocInfo.level = vk::CommandBufferLevel::ePrimary;
		mPrimaryDebugCommandBuffers = mContext.getDevice().allocateCommandBuffersUnique(allocInfo);
		
		vk::CommandBufferInheritanceInfo inheritanceInfo;
		inheritanceInfo.renderPass = *mCompositionRenderpass;
		inheritanceInfo.subpass = 0;

		vk::CommandBufferBeginInfo beginInfo;
		beginInfo.flags = vk::CommandBufferUsageFlagBits::eRenderPassContinue;
		beginInfo.pInheritanceInfo = &inheritanceInfo;

		// record command buffers
		for (auto& cmd : mDebugCommandBuffers)
		{
			cmd->begin(beginInfo);
			cmd->bindPipeline(vk::PipelineBindPoint::eGraphics, mResource.pipeline.get("debug"));
			
			std::array<vk::DescriptorSet, 2> descriptorSets = {
				mResource.descriptorSet.get("debug"),
				mResource.descriptorSet.get("composition"),
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

	mLightCullingFinishedSemaphore = mContext.getDevice().createSemaphoreUnique({});
	mGBufferFinishedSemaphore = mContext.getDevice().createSemaphoreUnique({});

	for (size_t i = 0; i < mSwapchainImages.size(); i++)
	{
		mRenderFinishedSemaphore.emplace_back(mContext.getDevice().createSemaphoreUnique({}));
		mImageAvailableSemaphore.emplace_back(mContext.getDevice().createSemaphoreUnique({}));

		mFences.emplace_back(mContext.getDevice().createFenceUnique(fenceInfo));
	}
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

	mResource.pipelineLayout.add("lightculling", layoutInfo);

	vk::PipelineShaderStageCreateInfo stageInfo;
	stageInfo.stage = vk::ShaderStageFlagBits::eCompute;
	stageInfo.pName = "main";


	// page flag
	{
		stageInfo.module = mResource.shaderModule.add("data/pageflag.comp");

		vk::ComputePipelineCreateInfo pipelineInfo;
		pipelineInfo.stage = stageInfo;
		pipelineInfo.layout = mResource.pipelineLayout.get("lightculling");
		pipelineInfo.basePipelineIndex = -1;

		mResource.pipeline.add("pageflag", *mPipelineCache, pipelineInfo);
	}

	auto pipelineBase = mResource.pipeline.get("pageflag");
	for (const auto& name : { "pagealloc", "pagestore", "pagecompact", "lightculling", "sort_bitonic" })
	{
		stageInfo.module = mResource.shaderModule.add(std::string("data/" ) + name + ".comp");

		vk::ComputePipelineCreateInfo pipelineInfo;
		pipelineInfo.stage = stageInfo;
		pipelineInfo.layout = mResource.pipelineLayout.get("lightculling");
		pipelineInfo.basePipelineHandle = pipelineBase;

		mResource.pipeline.add(name, *mPipelineCache, pipelineInfo);
	}
}

void Renderer::createComputeCommandBuffer()
{
	// Create command buffer
	{
		vk::CommandBufferAllocateInfo allocInfo;
		allocInfo.commandPool = mContext.getDynamicCommandPool();
		allocInfo.level = vk::CommandBufferLevel::ePrimary;
		allocInfo.commandBufferCount = static_cast<uint32_t>(mSwapchainFramebuffers.size());

		mPrimaryLightCullingCommandBuffer = mContext.getDevice().allocateCommandBuffersUnique(allocInfo);

		allocInfo.level = vk::CommandBufferLevel::eSecondary;
		allocInfo.commandPool = mContext.getStaticCommandPool();
		allocInfo.commandBufferCount = 2;
		mSecondaryLightCullingCommandBuffers = mContext.getDevice().allocateCommandBuffersUnique(allocInfo);
	}

	// Record command buffer
	{
		vk::MemoryBarrier barrier;
		barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
		barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

		std::array<vk::DescriptorSet, 2> descriptorSets{ 
			mResource.descriptorSet.get("camera"),
			mResource.descriptorSet.get("lightculling_01")
		};

		vk::CommandBufferInheritanceInfo inheritanceInfo;

		vk::CommandBufferBeginInfo beginInfo;
		beginInfo.pInheritanceInfo = &inheritanceInfo;
		beginInfo.flags = vk::CommandBufferUsageFlagBits::eSimultaneousUse;
		
		{
			auto& cmd = *mSecondaryLightCullingCommandBuffers[0];
			cmd.begin(beginInfo);

				cmd.fillBuffer(*mClusteredBuffer.handle, 0, VK_WHOLE_SIZE, 0);
				cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlagBits::eByRegion , nullptr, nullptr, nullptr);

				cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, mResource.pipelineLayout.get("lightculling"), 0, descriptorSets, nullptr);

				cmd.bindPipeline(vk::PipelineBindPoint::eCompute, mResource.pipeline.get("pageflag"));
				cmd.dispatch(TILE_COUNT_X, TILE_COUNT_Y, 1);

				cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlagBits::eByRegion, barrier, nullptr, nullptr);
				cmd.bindPipeline(vk::PipelineBindPoint::eCompute, mResource.pipeline.get("pagealloc"));
				cmd.dispatch(1, 1, 1);
				
				cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlagBits::eByRegion, barrier, nullptr, nullptr);
				cmd.bindPipeline(vk::PipelineBindPoint::eCompute, mResource.pipeline.get("pagestore"));
				cmd.dispatch(TILE_COUNT_X, TILE_COUNT_Y, 1);
				
				cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlagBits::eByRegion, barrier, nullptr, nullptr);
				cmd.bindPipeline(vk::PipelineBindPoint::eCompute, mResource.pipeline.get("pagecompact"));
				cmd.dispatch(40, 1, 1);

			cmd.end();
		}

		{
			auto &cmd = *mSecondaryLightCullingCommandBuffers[1];
			cmd.begin(beginInfo);

				cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, mResource.pipelineLayout.get("lightculling"), 0, descriptorSets, nullptr);

				cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlagBits::eByRegion, barrier, nullptr, nullptr);
				cmd.bindPipeline(vk::PipelineBindPoint::eCompute, mResource.pipeline.get("lightculling"));
				cmd.dispatch(30, 1, 1);

			cmd.end();
		}
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
	auto memorySize = sizeof(PointLight) * mLightsCount + sizeof(glm::vec4);
	auto lightBuffer = reinterpret_cast<uint8_t*>(mContext.getDevice().mapMemory(*mPointLightsStagingBuffer.memory, 0, memorySize));

	*reinterpret_cast<LightParams*>(lightBuffer) = { static_cast<uint32_t>(mLightsCount), {WIDTH, HEIGHT} };
	memcpy(lightBuffer + sizeof(glm::vec4), lights.data(), memorySize);
	mContext.getDevice().unmapMemory(*mPointLightsStagingBuffer.memory);

	mUtility.copyBuffer(*mPointLightsStagingBuffer.handle, *mLightsBuffers.handle, memorySize, 0, mPointLightsOffset); // TODO use transfer queue
	mUtility.copyBuffer(*mPointLightsStagingBuffer.handle, *mLightsBuffers.handle, sizeof(glm::vec4), 0, mLightsIndirectionOffset); // TODO use transfer queue
}

void Renderer::drawFrame()
{
	// wait for fences
	mContext.getDevice().waitForFences({ *mFences[mCurrentFrame] }, true, std::numeric_limits<uint64_t>::max());
	mContext.getDevice().resetFences({ *mFences[mCurrentFrame] });

	// Acquire an image from the swap chain
	uint32_t imageIndex;
	{
		auto result = mContext.getDevice().acquireNextImageKHR(*mSwapchain, std::numeric_limits<uint64_t>::max(), *mImageAvailableSemaphore[mCurrentFrame], nullptr);

		imageIndex = result.value;

		if (result.result == vk::Result::eErrorOutOfDateKHR)
		{
			// when swap chain needs recreation
			recreateSwapChain();
			return;
		}
		if (result.result != vk::Result::eSuccess && result.result != vk::Result::eSuboptimalKHR)
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
		submitInfo.pWaitSemaphores = &*mImageAvailableSemaphore[mCurrentFrame];
		submitInfo.pWaitDstStageMask = &waitStages;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &*mGBufferCommandBuffer;
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &*mGBufferFinishedSemaphore;

		// submitInfos.emplace_back(submitInfo);
		mContext.getGraphicsQueue().submit(submitInfo, nullptr);
	}

	if (BaseApp::getInstance().getUI().getDebugIndex() == DebugStates::disabled)
	{
		// submit light culling
		{
			auto& cmd = *mPrimaryLightCullingCommandBuffer[imageIndex];
			
			// build light culling cmd
			{

				std::array<vk::DescriptorSet, 2> descriptorSets{ 
			mResource.descriptorSet.get("camera"),
			mResource.descriptorSet.get("lightculling_01")
				};

				vk::MemoryBarrier barrier;
				barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
				barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;


				// record 
				cmd.begin(vk::CommandBufferBeginInfo{});
				cmd.executeCommands(1, &*mSecondaryLightCullingCommandBuffers[0]);
				
				// light sorting
				{
					size_t lightWindowsCount = (1023 + mLightsCount) >> 10;

					// cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, mResource.pipelineLayout.get("lightculling"), 0, descriptorSets, nullptr);
					//
					// cmd.bindPipeline(vk::PipelineBindPoint::eCompute, mResource.pipeline.get("sort_bitonic"));
					// cmd.dispatch(lightWindowsCount, 1, 1);
					// cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlagBits::eByRegion, barrier, nullptr, nullptr);

					if (mLightsCount > 256)
					{
						// todo remove this
						// cmd.fillBuffer(*mLightsBuffers.handle, mLightsIndirectionOffset, mLightsIndirectionSize, 0);
						// cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlagBits::eByRegion, barrier, nullptr, nullptr);

					// 	std::string mLightBufferSwapUsed = "lightculling_01";
					// 	for (size_t i = lightWindowsCount; i > 1; i = (i + 1) >> 1) 
					// 	{
					// 		cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, mResource.pipelineLayout.get("lightculling"), 1, mResource.descriptorSet.get(mLightBufferSwapUsed), nullptr);
					//
					// 		cmd.bindPipeline(vk::PipelineBindPoint::eCompute, mResource.pipeline.get("sort_splitterSort2_8"));
					// 		cmd.dispatch(1, 1, 1);
					// 		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlagBits::eByRegion, barrier, nullptr, nullptr);
					//
					// 		cmd.bindPipeline(vk::PipelineBindPoint::eCompute, mResource.pipeline.get("sort_mergeSeq"));
					// 		cmd.dispatch((lightWindowsCount + 1) >> 1, 1, 1);
					//
					// 		if (i > 2 || mLightBufferSwapUsed == "lightculling_01")
					// 			cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlagBits::eByRegion, barrier, nullptr, nullptr);
					// 		
					// 		mLightBufferSwapUsed = (mLightBufferSwapUsed == "lightculling_01") ? "lightculling_10" : "lightculling_01";
					// 	}
					//
					// 	if (mLightBufferSwapUsed == "lightculling_10")
					// 	{
					// 		vk::DeviceSize size = (mLightsCount + 1) * sizeof(PointLight) - sizeof(glm::vec4);
					//
					// 		vk::BufferCopy copy;
					// 		copy.srcOffset = mLightsIndirectionOffset + sizeof(glm::vec4);
					// 		copy.dstOffset = mPointLightsOffset + sizeof(glm::vec4);
					// 		copy.size = size;
					//
					// 		vk::BufferMemoryBarrier barrier;
					// 		barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
					// 		barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
					// 		barrier.size = size;
					// 		barrier.offset = mLightsIndirectionOffset + sizeof(glm::vec4);
					// 		barrier.buffer = *mLightsBuffers.handle;
					//
					// 		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlagBits::eByRegion, nullptr, barrier, nullptr);
					// 		cmd.copyBuffer(*mLightsBuffers.handle, *mLightsBuffers.handle, copy);
					//
					// 		barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
					// 		barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
					// 		barrier.offset = mPointLightsOffset + sizeof(glm::vec4);
					//
					// 		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlagBits::eByRegion, nullptr, barrier, nullptr);
					// 		
					//
					// 		// cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlagBits::eByRegion, barrier, nullptr, nullptr);
					// 	}
					}



					// cmd.dispatch(1, 1, 1);
					// cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlagBits::eByRegion, barrier, nullptr, nullptr);
				}

				cmd.executeCommands(1, &*mSecondaryLightCullingCommandBuffers[1]);
				cmd.end();
			}
			vk::PipelineStageFlags waitStages = vk::PipelineStageFlagBits::eComputeShader;

			vk::SubmitInfo submitInfo;
			submitInfo.waitSemaphoreCount = 1;
			submitInfo.pWaitSemaphores = &*mGBufferFinishedSemaphore;
			submitInfo.pWaitDstStageMask = &waitStages;
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &cmd;
			submitInfo.signalSemaphoreCount = 1;
			submitInfo.pSignalSemaphores = &*mLightCullingFinishedSemaphore;

			mContext.getComputeQueue().submit(submitInfo, nullptr); // TODO compute queue in graphics queue
		}

		// build composition
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

			auto& cmd = *mPrimaryCompositionCommandBuffers[imageIndex];

			cmd.begin(vk::CommandBufferBeginInfo{});
			cmd.beginRenderPass(renderpassInfo, vk::SubpassContents::eSecondaryCommandBuffers);
			cmd.executeCommands(1, &*mCompositionCommandBuffers[imageIndex]);
			cmd.nextSubpass(vk::SubpassContents::eSecondaryCommandBuffers);
			cmd.executeCommands(1, &*BaseApp::getInstance().getUI().getCommandBuffer());
			cmd.endRenderPass();
			cmd.end();
		}

		// submit composition
		{
			vk::PipelineStageFlags waitStages = vk::PipelineStageFlagBits::eFragmentShader;

			vk::SubmitInfo submitInfo;
			submitInfo.waitSemaphoreCount = 1;
			submitInfo.pWaitSemaphores = &*mLightCullingFinishedSemaphore;
			submitInfo.pWaitDstStageMask = &waitStages;
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &*mPrimaryCompositionCommandBuffers[imageIndex];
			submitInfo.signalSemaphoreCount = 1;
			submitInfo.pSignalSemaphores = &*mRenderFinishedSemaphore[mCurrentFrame];

			submitInfos.emplace_back(submitInfo);
		}
	}
	else
	{
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

			auto& cmd = *mPrimaryDebugCommandBuffers[imageIndex];

			cmd.begin(vk::CommandBufferBeginInfo{});
			cmd.beginRenderPass(renderpassInfo, vk::SubpassContents::eSecondaryCommandBuffers);
			cmd.executeCommands(1, &*mDebugCommandBuffers[imageIndex]);
			cmd.nextSubpass(vk::SubpassContents::eSecondaryCommandBuffers);
			cmd.executeCommands(1, &*BaseApp::getInstance().getUI().getCommandBuffer());
			cmd.endRenderPass();
			cmd.end();
		}
		// debugging
		vk::PipelineStageFlags waitStages = vk::PipelineStageFlagBits::eFragmentShader;

		vk::SubmitInfo submitInfo;
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &*mGBufferFinishedSemaphore;
		submitInfo.pWaitDstStageMask = &waitStages;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &*mPrimaryDebugCommandBuffers[imageIndex];
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &*mRenderFinishedSemaphore[mCurrentFrame];

		submitInfos.emplace_back(submitInfo);
	}

	mContext.getGraphicsQueue().submit(submitInfos, *mFences[mCurrentFrame]);

	// 3. Submitting the result back to the swap chain to show it on screen
	{
		vk::PresentInfoKHR presentInfo;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &*mRenderFinishedSemaphore[mCurrentFrame];
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &*mSwapchain;
		presentInfo.pImageIndices = &imageIndex;

		auto presentResult = mContext.getPresentQueue().presentKHR(presentInfo);

		if (presentResult == vk::Result::eErrorOutOfDateKHR || presentResult == vk::Result::eSuboptimalKHR)
			recreateSwapChain();
		else if (presentResult != vk::Result::eSuccess)
			throw std::runtime_error("Failed to present swap chain image");
	}

	mCurrentFrame = (mCurrentFrame + 1) % mSwapchainImages.size();
}
