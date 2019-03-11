#include "ThreadPool.h"

Thread::Thread(vk::Device device, uint32_t familyIndex)
	: mDevice(device)
{
	vk::CommandPoolCreateInfo poolInfo;
	poolInfo.queueFamilyIndex = familyIndex;
	poolInfo.flags |= vk::CommandPoolCreateFlagBits::eTransient;
	
	mCommandPool = mDevice.createCommandPoolUnique(poolInfo);
	
	mThread = std::thread(&Thread::run, this);
}

Thread::~Thread()
{
	mDestroy = true;
	if (mThread.joinable())
	{
		{
			std::lock_guard<std::mutex> lock(mWorkMutex);
			mWorkCondition.notify_one();
		}
		
		mThread.join();
	}
}

void Thread::run()
{
	while(true)
	{
		{
			std::unique_lock<std::mutex> lock(mWorkMutex);
			mWorkCondition.wait(lock, [this]() { return mWork != nullptr; });

			if (mDestroy)
				break;

			createCmdBuffer();
			auto work = std::move(mWork);
			
			work(*mCommandBuffer);
		}

		{
			std::lock_guard<std::mutex> lock(mWorkMutex);
			mFinished = true;
			mWorkCondition.notify_one();
		}
	}
}

void Thread::createCmdBuffer()
{
	vk::CommandBufferAllocateInfo allocInfo;
	allocInfo.commandPool = *mCommandPool;
	allocInfo.level = vk::CommandBufferLevel::eSecondary;
	allocInfo.commandBufferCount = 1;

	mCommandBuffer = std::move(mDevice.allocateCommandBuffersUnique(allocInfo)[0]);
}

void Thread::addWork(ThreadFuncPtr work)
{
	std::lock_guard<std::mutex> lock(mWorkMutex);
	mWork = std::move(work);
	mWorkCondition.notify_one();
}

void Thread::wait()
{
	std::unique_lock<std::mutex> lock(mWorkMutex);
	mWorkCondition.wait(lock, [this]() { return mFinished; });
	mFinished = false;
}

vk::UniqueCommandBuffer&& Thread::getCommandBuffer()
{
	return std::move(mCommandBuffer);
}

ThreadPool::ThreadPool(vk::Device device, uint32_t familyIndex)
{
	for (size_t i = 0; i < std::thread::hardware_concurrency(); i++)
		mThreads.emplace_back(std::make_unique<Thread>(device, familyIndex));
}

void ThreadPool::addWorkMultiplex(const ThreadFuncPtr& work)
{
	for (auto& thread : mThreads)
		thread->addWork(work);
}

void ThreadPool::wait()
{
	for (auto& thread : mThreads)
		thread->wait();
}

std::vector<vk::UniqueCommandBuffer> ThreadPool::getCommandBuffers()
{
	std::vector<vk::UniqueCommandBuffer> buffers;
	for (auto& thread : mThreads)
		buffers.emplace_back(thread->getCommandBuffer());

	return buffers;
}
