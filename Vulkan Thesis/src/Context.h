#pragma once
#include <vulkan/vulkan.hpp>
#include "ThreadPool.h"


struct GLFWwindow;

struct QueueFamilyIndices
{
	int generalFamily = -1;
	int computeFamily = -1;
	int computeQueueIndex = 0;

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

	vk::Queue getGeneralQueue() const
	{
		return mGeneralQueue;
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
	// vk::UniqueDebugUtilsMessengerEXT		mMessenger;
	UniqueMessengerDLD		mMessenger;
	vk::UniqueDevice		mDevice;
	vk::UniqueSurfaceKHR	mSurface;

	QueueFamilyIndices		mQueueFamilyIndices;
	vk::PhysicalDevice		mPhysicalDevice;
	vk::Queue				mGeneralQueue;
	vk::Queue				mComputeQueue;

	vk::UniqueCommandPool	mStaticCommandPool;
	vk::UniqueCommandPool	mDynamicCommandPool;
	vk::UniqueCommandPool	mComputeCommandPool;

	//vk::PhysicalDeviceProperties	mPhyisicalDeviceProperties;
};
