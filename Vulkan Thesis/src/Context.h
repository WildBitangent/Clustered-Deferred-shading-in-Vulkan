#pragma once
#include <vulkan/vulkan.hpp>


struct GLFWwindow;

struct QueueFamilyIndices
{
	int graphicsFamily = -1;
	int presentFamily = -1;

	bool isComplete();
	bool isSingleQueue();
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

	vk::CommandPool getGraphicsCommandPool() const
	{
		return *mGraphicCommandPool;
	}

	vk::CommandPool getComputeCommandPool() const
	{
		return *mComputeCommandPool;
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
	UniqueMessengerDLD		mMessenger;
	vk::UniqueDevice		mDevice;
	vk::UniqueSurfaceKHR	mSurface;

	QueueFamilyIndices		mQueueFamilyIndices;
	vk::PhysicalDevice		mPhysicalDevice;
	vk::Queue				mGraphicsQueue;
	vk::Queue				mPresentQueue;
	vk::Queue				mComputeQueue;

	vk::UniqueCommandPool	mGraphicCommandPool;
	vk::UniqueCommandPool	mComputeCommandPool;
	//vk::PhysicalDeviceProperties	mPhyisicalDeviceProperties;
};
