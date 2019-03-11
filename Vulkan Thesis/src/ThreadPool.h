#pragma once
#include <thread>
#include <vulkan/vulkan.hpp>
#include <mutex>
#include <queue>
#include <atomic>


using ThreadFuncPtr = std::function<void(vk::CommandBuffer&)>;

class Thread
{
public:
	Thread(vk::Device device, uint32_t familyIndex);
	~Thread();


	void addWork(ThreadFuncPtr work);
	void wait();

	vk::UniqueCommandBuffer&& getCommandBuffer();

private:
	void run();
	void createCmdBuffer();

private:
	std::thread mThread;
	vk::Device mDevice;
	vk::UniqueCommandPool mCommandPool;
	vk::UniqueCommandBuffer mCommandBuffer;
		
	std::mutex mWorkMutex;
	std::condition_variable mWorkCondition;
	ThreadFuncPtr mWork;
	bool mDestroy = false;
	bool mFinished = false;

	// std::deque<std::function<void()>> mWorkQueue; // todo mby
};

class ThreadPool
{
public:
	ThreadPool(vk::Device device, uint32_t familyIndex);

	void addWorkMultiplex(const ThreadFuncPtr& work); // all threads do the same task
	void wait();
	std::vector<vk::UniqueCommandBuffer> getCommandBuffers();

private:
	std::vector<std::unique_ptr<Thread>> mThreads;
};
