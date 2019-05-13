/**
 * @file 'ThreadPool.cpp'
 * @brief Implementation of thread pool design pattern
 * @copyright The MIT license 
 * @author Matej Karas
 */

#include "ThreadPool.h"

Thread::Thread(size_t id)
	: mID(id)
{
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
			mWorkCondition.wait(lock, [this]() { return mWork != nullptr || mDestroy; });

			if (mDestroy)
				break;

			auto work = std::move(mWork);
			
			work(mID);
		}

		{
			std::lock_guard<std::mutex> lock(mWorkMutex);
			mFinished = true;
			mWorkCondition.notify_one();
		}
	}
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

ThreadPool::ThreadPool()
{
	for (size_t i = 0; i < std::thread::hardware_concurrency(); i++)
		mThreads.emplace_back(std::make_unique<Thread>(i));
}

void ThreadPool::addWorkMultiplex(const ThreadFuncPtr& work)
{
	for (auto& thread : mThreads)
		thread->addWork(work);
}

void ThreadPool::addWork(std::vector<ThreadFuncPtr>&& work)
{
	for (size_t i = 0; i < work.size(); i++)
		mThreads[i % mThreads.size()]->addWork(work[i]);
}

void ThreadPool::wait()
{
	for (auto& thread : mThreads)
		thread->wait();
}
