#pragma once
#include <vector>

#include "Context.h"
#include "Util.h"
#include "Model.h"
#include "Resource.h"

struct GLFWwindow;
struct PointLight;

// enum class TileSize
// {
// 	_8x8 = 8,
// 	_16x16 = 16,
// 	_32x32 = 32,
// 	_64x64 = 64,
// };

class Renderer
{
public:
	Renderer(GLFWwindow* window, ThreadPool& pool); // todo move thread pool to utility when it's singleton
	// ~Renderer();

	//void resize(int width, int height);
	void requestDraw(float deltatime);
	void cleanUp();

	void setCamera(const glm::mat4& view, const glm::vec3 campos);
	void updateLights(const std::vector<PointLight>& lights); 

	void reloadShaders(size_t size);
	// void changeTileSize(TileSize size);

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

	// command buffers
	std::vector<vk::UniqueCommandBuffer> mPrimaryCompositionCommandBuffers;
	std::vector<vk::UniqueCommandBuffer> mCompositionCommandBuffers;
	std::vector<vk::UniqueCommandBuffer> mPrimaryLightCullingCommandBuffer;
	std::vector<vk::UniqueCommandBuffer> mSecondaryLightCullingCommandBuffers;
	std::vector<vk::UniqueCommandBuffer> mLightSortingCommandBuffers;
	vk::UniqueCommandBuffer mGBufferCommandBuffer;
	std::vector<vk::UniqueCommandBuffer> mPrimaryDebugCommandBuffers;
	std::vector<vk::UniqueCommandBuffer> mDebugCommandBuffers;

	// semaphores // TODO move to resource handler
	std::vector<vk::UniqueSemaphore> mImageAvailableSemaphore; // todo alloc
	vk::UniqueSemaphore mGBufferFinishedSemaphore;
	vk::UniqueSemaphore mLightCullingFinishedSemaphore;
	vk::UniqueSemaphore mLightSortingFinishedSemaphore;
	std::vector<vk::UniqueSemaphore> mRenderFinishedSemaphore; // todo

	std::vector<vk::UniqueFence> mFences;

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
	BufferParameters mLightsBuffers;
	BufferParameters mPointLightsStagingBuffer;
	vk::DeviceSize mLightsOutOffset;
	vk::DeviceSize mPointLightsOffset;
	vk::DeviceSize mLightsIndirectionOffset;
	vk::DeviceSize mSplittersOffset;

	vk::DeviceSize mLightsOutSize;
	vk::DeviceSize mPointLightsSize;
	vk::DeviceSize mLightsIndirectionSize;
	vk::DeviceSize mSplittersSize;

	// Cluster buffer
	BufferParameters mClusteredBuffer;
	vk::DeviceSize mPageTableOffset;
	vk::DeviceSize mPagePoolOffset;
	vk::DeviceSize mUniqueClustersOffset;

	vk::DeviceSize mPageTableSize;
	vk::DeviceSize mPagePoolSize;
	vk::DeviceSize mUniqueClustersSize;

	size_t mLightsCount; // for sorting
	size_t mCurrentTileSize = 32;
	size_t mSubGroupSize;
	std::string mLightBufferSwapUsed = "lightculling_01";

	// todo rewrite this
	uint32_t maxLevel;
	std::vector<std::pair<uint32_t, uint32_t>> levelParam;
	
	glm::uvec2 mTileCount;

	friend class UI;
};

