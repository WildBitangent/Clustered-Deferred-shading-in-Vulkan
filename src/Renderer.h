/**
 * @file 'Renderer.h'
 * @brief Vulkan renderer
 * @copyright The MIT license 
 * @author Matej Karas
 */

#pragma once
#include <vector>

#include "Context.h"
#include "Util.h"
#include "Model.h"
#include "Resource.h"

class Scene;
struct GLFWwindow;
struct PointLight;

class Renderer
{
public:
	Renderer(GLFWwindow* window, Scene& scene);
	
	void draw();
	void cleanUp();

	void updateLights(const std::vector<PointLight>& lights); 
	void reloadShaders(uint32_t tileSize);
	void onSceneChange();

private:
	void recreateSwapChain();

	void createSwapChain();
	void createSwapChainImageViews();
	void createRenderPasses();
	void createFrameBuffers();
	void createDescriptorSetLayouts();
	void createPipelineCache();
	void createGraphicsPipelines();
	void createGBuffers();
	void createSampler();
	void createUniformBuffers();
	void createClusteredBuffers();
	void createLights();
	void createDescriptorPool();
	void createDescriptorSets();
	void updateDescriptorSets();
	void createGraphicsCommandBuffers();
	void createSyncPrimitives();

	void createComputePipeline();
	void createComputeCommandBuffer();

	void updateUniformBuffers();
	void drawFrame();

	void submitClusteredLightCullingCmds(size_t imageIndex);
	void submitClusteredCompositionCmds(size_t imageIndex);
	void submitBVHCreationCmds(size_t imageIndex);
	void submitTiledLightCullingCmds(size_t imageIndex);
	void submitTiledCompositionCmds(size_t imageIndex);
	void submitDeferredCompositionCmds(size_t imageIndex);
	void submitGbufferCmds();
	void submitDebugCmds(size_t imageIndex);
	
	void setTileCount();
	GBuffer generateGBuffer();

private:
	Context mContext;
	Utility mUtility;
	Scene& mScene;
	vk::UniqueDescriptorPool mDescriptorPool;
	resource::Resources mResource;

	vk::UniqueSwapchainKHR mSwapchain;
	std::vector<vk::Image> mSwapchainImages;
	std::vector<vk::UniqueImageView> mSwapchainImageViews;

	vk::Format mSwapchainImageFormat;
	vk::Extent2D mSwapchainExtent;
	size_t mCurrentFrame = 0;

	vk::UniquePipelineCache mPipelineCache;
	
	// G Buffer
	GBuffer mGBufferAttachments;
	vk::UniqueSampler mSampler;
	vk::UniqueRenderPass mGBufferRenderpass;
	vk::UniqueFramebuffer mGBufferFramebuffer;

	// composition
	vk::UniqueRenderPass mCompositionRenderpass;
	std::vector<vk::UniqueFramebuffer> mSwapchainFramebuffers;

	// uniform buffers
	BufferParameters mObjectStagingBuffer;
	BufferParameters mObjectUniformBuffer;
	BufferParameters mCameraStagingBuffer;
	BufferParameters mCameraUniformBuffer;
	BufferParameters mDebugUniformBuffer;

	// Lights buffer
	BufferParameters mLightsBuffers;
	BufferParameters mPointLightsStagingBuffer;
	vk::DeviceSize mLightsOutOffset;
	vk::DeviceSize mPointLightsOffset;
	vk::DeviceSize mLightsOutSwapOffset;

	vk::DeviceSize mLightsOutSize;
	vk::DeviceSize mPointLightsSize;
	vk::DeviceSize mLightsOutSwap;

	// Cluster buffer
	BufferParameters mClusteredBuffer;
	vk::DeviceSize mPageTableOffset;
	vk::DeviceSize mPagePoolOffset;
	vk::DeviceSize mUniqueClustersOffset;

	vk::DeviceSize mPageTableSize;
	vk::DeviceSize mPagePoolSize;
	vk::DeviceSize mUniqueClustersSize;
	
	glm::uvec2 mTileCount;
	uint32_t mLightsCount;
	uint32_t mCurrentTileSize = 32;
	uint32_t mSubGroupSize;
	
	// params for light culling created at light sorting
	uint32_t mMaxBVHLevel;
	std::string mLightBufferSwapUsed = "lightculling_01";
	std::vector<std::pair<uint32_t, uint32_t>> mLevelParam;

	friend class UI;
	friend class Scene;
};

