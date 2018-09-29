#pragma once
#include <vector>

#include "Context.h"
#include "Util.h"
#include "Model.h"

struct GLFWwindow;

class Renderer
{
public:
	Renderer(GLFWwindow* window);

	//void resize(int width, int height);
	void requestDraw(float deltatime);
	void cleanUp();

	void setCamera(const glm::mat4 & view, const glm::vec3 campos);

private:
	void recreateSwapChain();

	void createSwapChain();
	void createSwapChainImageViews();
	void createRenderPasses();
	void createDescriptorSetLayouts();
	void createGraphicsPipelines();
	void createDepthResources();
	void createFrameBuffers();
	void createTextureSampler();
	void createUniformBuffers();
	//void createLights();
	void createDescriptorPool();
	void createSceneObjectDescriptorSet();
	void createCameraDescriptorSet();
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

	vk::UniqueShaderModule createShaderModule(const std::string& filename);

private:
	Context mContext;
	Utility mUtility;
	Model mModel;

	vk::UniqueSwapchainKHR mSwapchain;
	std::vector<vk::Image> mSwapchainImages;
	vk::Format mSwapchainImageFormat;
	vk::Extent2D mSwapchainExtent;
	std::vector<vk::UniqueImageView> mSwapchainImageViews;
	std::vector<vk::UniqueFramebuffer> mSwapchainFramebuffers;
	std::vector<vk::CommandBuffer> mCommandBuffers;

	vk::UniqueRenderPass mRenderpass;

	vk::UniqueDescriptorSetLayout mObjectDescriptorSetLayout;
	vk::UniqueDescriptorSetLayout mCameraDescriptorSetLayout;
	vk::UniqueDescriptorSetLayout mMaterialDescriptorSetLayout;
	vk::UniquePipelineLayout mPipelineLayout;
	vk::UniquePipeline mGraphicsPipeline;

	vk::UniquePipeline mComputePipeline;
	vk::UniquePipelineLayout mComputePipelineLayout;
	vk::CommandBuffer mComputeCommandBuffer;

	vk::UniqueSemaphore mImageAvailableSemaphore;
	vk::UniqueSemaphore mRenderFinishedSemaphore;

	// depth image
	ImageParameters mDepthImage;

	// texture image
	vk::UniqueImage mTextureImage; // TODO image parameters
	vk::UniqueDeviceMemory mTextureImageMemory;
	vk::UniqueImageView mTextureImageView;
	//VRaii<VkImage normalmap_image;
	//VRaii<VkDeviceMemory normalmap_image_memory;
	//VRaii<VkImageView normalmap_image_view;
	vk::UniqueSampler mTextureSampler;

	// uniform buffers
	BufferParameters mObjectStagingBuffer;
	BufferParameters mObjectUniformBuffer;
	BufferParameters mCameraStagingBuffer;
	BufferParameters mCameraUniformBuffer;

	vk::UniqueDescriptorPool mDescriptorPool;
	vk::DescriptorSet mObjectDescriptorSet;
	vk::DescriptorSet mCameraDescriptorSet;

	glm::mat4 mViewMatrix = glm::lookAt(glm::vec3(-2.5f, 0.f, 2.f), glm::vec3(0.f, 0.f, 0.75f), glm::vec3(0.f, 0.f, 1.f));;
};

