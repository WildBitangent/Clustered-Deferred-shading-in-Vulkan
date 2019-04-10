#pragma once
#include <vector>

#include "Context.h"
#include "Util.h"
#include "Model.h"
#include "Resource.h"

struct GLFWwindow;
struct PointLight;

class Renderer
{
public:
	Renderer(GLFWwindow* window, ThreadPool& pool); // todo move thread pool to utility when it's singleton

	void resize();
	void requestDraw(float deltatime);
	void cleanUp();

	void setCamera(const glm::mat4& view, const glm::vec3 campos);
	void updateLights(const std::vector<PointLight>& lights); 
	void reloadShaders(size_t size);

private:
	void recreateSwapChain();

	void createSwapChain();
	void createSwapChainImageViews();
	void createRenderPasses();
	void createDescriptorSetLayouts();
	void createPipelineCache();
	void createGraphicsPipelines();
	void createGBuffers();
	void createUniformBuffers();
	void createClusteredBuffers();
	void createLights();
	void createDescriptorPool();
	void createDescriptorSets();
	void createGraphicsCommandBuffers();
	void createSyncPrimitives();

	void createComputePipeline();
	void createComputeCommandBuffer();

	void updateUniformBuffers();
	void drawFrame();

	void submitLightCullingCmds(size_t imageIndex);

	void submitLightSortingCmds(size_t imageIndex);

	void setTileCount();

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
	size_t mCurrentFrame = 0;

	vk::UniquePipelineCache mPipelineCache;
	
	// G Buffer
	GBuffer mGBufferAttachments;
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
	vk::DeviceSize mSplittersOffset;

	vk::DeviceSize mLightsOutSize;
	vk::DeviceSize mPointLightsSize;
	vk::DeviceSize mLightsOutSwap;
	vk::DeviceSize mSplittersSize;

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
	size_t mCurrentTileSize = 32;
	size_t mSubGroupSize;
	
	// params for light culling created at light sorting
	uint32_t mMaxLevel;
	std::string mLightBufferSwapUsed = "lightculling_01";
	std::vector<std::pair<uint32_t, uint32_t>> mLevelParam;

	friend class UI;
};

