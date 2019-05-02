#pragma once
#include <thread>
#include <vulkan/vulkan.hpp>
#include <mutex>

using ThreadFuncPtr = std::function<void(size_t)>;

class Thread
{
public:
	Thread(size_t id);
	~Thread();

	void addWork(ThreadFuncPtr work);
	void wait();

private:
	void run();

private:
	std::thread mThread;
	std::mutex mWorkMutex;
	std::condition_variable mWorkCondition;
	ThreadFuncPtr mWork;
	size_t mID;

	bool mDestroy = false;
	bool mFinished = false;
};

class ThreadPool
{
public:
	ThreadPool();

	void addWorkMultiplex(const ThreadFuncPtr& work); // all threads do the same task
	void addWork(std::vector<ThreadFuncPtr>&& work); // assign work to only some threads
	void wait();

private:
	std::vector<std::unique_ptr<Thread>> mThreads;
};
