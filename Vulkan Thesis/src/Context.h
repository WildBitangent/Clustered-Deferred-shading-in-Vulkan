#pragma once
#include <vulkan/vulkan.hpp>


struct GLFWwindow;

struct QueueFamilyIndices
{
	std::pair<int, int> graphicsFamily = {-1, -1};
	std::pair<int, int> presentFamily = {-1, -1};
	std::pair<int, int> computeFamily = {-1, -1};

	bool sharedComputeGraphics = false;

	bool isComplete() const;
	bool isSingleQueue() const;
	static QueueFamilyIndices findQueueFamilies(vk::PhysicalDevice device, vk::SurfaceKHR surface);
};

class Context
{
public:
	Context(GLFWwindow* window);
	~Context()
	{
		
	}

	Context(const Context&) = delete;
	void operator=(const Context&) = delete;

	auto getQueueFamilyIndices() const
	{
		return mQueueFamilyIndices;
	}

	vk::Instance getInstance() const
	{
		return *mInstance;
	}

	vk::PhysicalDevice getPhysicalDevice() const
	{
		return mPhysicalDevice;
	}

	vk::Device getDevice() const
	{
		return *mDevice;
	}

	vk::Queue getGraphicsQueue() const
	{
		return mGraphicsQueue;
	}

	vk::Queue getPresentQueue() const
	{
		return mPresentQueue;
	}

	vk::Queue getComputeQueue() const
	{
		return mComputeQueue;
	}

	vk::SurfaceKHR getWindowSurface() const
	{
		return *mSurface;
	}

	vk::CommandPool getStaticCommandPool() const
	{
		return *mStaticCommandPool;
	}

	vk::CommandPool getDynamicCommandPool() const
	{
		return *mDynamicCommandPool;
	}

private:
	void createInstance();
	void setupDebugCallback();
	void createWindowSurface();
	void pickPhysicalDevice();
	void findQueueFamilyIndices();
	void createLogicalDevice();
	void createCommandPools();

private:
	using UniqueMessengerDLD = vk::UniqueHandle<vk::DebugUtilsMessengerEXT, vk::DispatchLoaderDynamic>;

	GLFWwindow*				mWindow;

	vk::UniqueInstance		mInstance;
	// vk::UniqueDebugUtilsMessengerEXT		mMessenger;
	UniqueMessengerDLD		mMessenger;
	vk::UniqueDevice		mDevice;
	vk::UniqueSurfaceKHR	mSurface;

	QueueFamilyIndices		mQueueFamilyIndices;
	vk::PhysicalDevice		mPhysicalDevice;
	vk::Queue				mGraphicsQueue;
	vk::Queue				mPresentQueue;
	vk::Queue				mComputeQueue;

	vk::UniqueCommandPool	mStaticCommandPool;
	vk::UniqueCommandPool	mDynamicCommandPool;
	//vk::PhysicalDeviceProperties	mPhyisicalDeviceProperties;
};
