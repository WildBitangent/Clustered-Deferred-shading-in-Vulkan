#pragma once
#include <vector>

#include "Context.h"
#include "Util.h"
#include "Model.h"
#include "Resource.h"

struct GLFWwindow;

class Renderer
{
public:
	Renderer(GLFWwindow* window);

	//void resize(int width, int height);
	void requestDraw(float deltatime);
	void cleanUp();

	void setCamera(const glm::mat4& view, const glm::vec3 campos);

	void reloadShaders();

private:
	void recreateSwapChain();

	void createSwapChain();
	void createSwapChainImageViews();
	void createRenderPasses();
	void createDescriptorSetLayouts();
	void createPipelineCache();
	void createGraphicsPipelines();
	void createGBuffers();
	// void createFrameBuffers();
	// void createTextureSampler();
	void createUniformBuffers();
	void createLights();
	void createDescriptorPool();
	// void createSceneObjectDescriptorSet();
	void createDescriptorSets();
	//void createIntermediateDescriptorSet();
	//void updateIntermediateDescriptorSet();
	void createGraphicsCommandBuffers();
	void createSemaphores();

	void createComputePipeline();
	//void createLigutCullingDescriptorSet();
	//void createLightVisibilityBuffer();
	void createComputeCommandBuffer();

	//void createDepthPrePassCommandBuffer();

	void updateUniformBuffers();
	void drawFrame();

private:
	Context mContext;
	Utility mUtility;
	Model mModel;
	vk::UniqueDescriptorPool mDescriptorPool;
	resource::Resources mResource;

	vk::UniqueSwapchainKHR mSwapchain;
	std::vector<vk::Image> mSwapchainImages;
	std::vector<vk::UniqueImageView> mSwapchainImageViews;
	vk::Format mSwapchainImageFormat;
	vk::Extent2D mSwapchainExtent;

	vk::UniquePipelineCache mPipelineCache;

	// command buffers
	std::vector<vk::UniqueCommandBuffer> mCommandBuffers;
	vk::UniqueCommandBuffer mLightCullingCommandBuffer;
	vk::UniqueCommandBuffer mGBufferCommandBuffer;
	std::vector<vk::UniqueCommandBuffer> mDebugCommandBuffer;

	// semaphores // TODO move to resource handler
	vk::UniqueSemaphore mImageAvailableSemaphore;
	vk::UniqueSemaphore mGBufferFinishedSemaphore;
	vk::UniqueSemaphore mLightCullingFinishedSemaphore;
	vk::UniqueSemaphore mRenderFinishedSemaphore;

	// G Buffer
	GBuffer mGBufferAttachments;
	vk::UniqueRenderPass mGBufferRenderpass;
	vk::UniqueFramebuffer mGBufferFramebuffer;

	// composition
	vk::UniqueRenderPass mCompositionRenderpass;
	std::vector<vk::UniqueFramebuffer> mSwapchainFramebuffers;

	// uniform buffers
	BufferParameters mObjectStagingBuffer; // TODO cahnge to host visible/coherent?
	BufferParameters mObjectUniformBuffer;
	BufferParameters mCameraStagingBuffer;
	BufferParameters mCameraUniformBuffer;
	
	BufferParameters mDebugUniformBuffer;

	// TODO refactor
	BufferParameters mLightsOutBuffer;

	BufferParameters mPointLightsStagingBuffer;
	BufferParameters mPointLightsBuffer;
};

