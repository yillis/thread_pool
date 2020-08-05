#pragma once

#include <thread>
#include <condition_variable>
#include <mutex>
#include <future>
#include <queue>
#include <memory>
#include <functional>
#include <vector>
#include <stdexcept>

namespace wm {

	class ThreadPool {
	private:
		std::vector<std::thread> workers_;
		std::queue<std::function<void()> > tasks_;

		std::mutex mutex_;
		std::condition_variable condition_;
		bool stop_;
	public:
		ThreadPool(int size);
		template<class F, class ...Args>
		auto add(F&& f, Args&& ...args)->std::future<typename std::result_of<F(Args ...)>::type>;
		void stop();
		~ThreadPool();
	};


	inline ThreadPool::ThreadPool(int size = 100) : stop_(false)
	{
		for (size_t i = 0; i < size; ++i) {
			workers_.emplace_back(
				[this]
			{
				while (!stop_) {
					std::function<void()> task;

					{
						std::unique_lock<std::mutex> lock(this->mutex_);
						this->condition_.wait(lock, [this] {return this->stop_ || !this->tasks_.empty(); });
						if (this->stop_ && this->tasks_.empty()) {
							return;
						}
						task = std::move(this->tasks_.front());
						this->tasks_.pop();
					}

					task();
				}
			}
			);
			
		}
	}

	ThreadPool::~ThreadPool()
	{
		if (!stop_) {
			stop();
		}
	}
	template<class F, class ...Args>
	inline auto ThreadPool::add(F&& f, Args&& ...args)
		->std::future<typename std::result_of<F(Args ...)>::type>
	{
		using return_type = typename std::result_of<F(Args ...)>::type;

		// must use pointer here, if use object, the object will be destruct when exist this function
		// then, in other thread, the variable *task* in lambda is disabled.
		// std::packaged_task<return_type()> task(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
		auto task = std::make_shared<std::packaged_task<return_type()> >(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
		std::future<return_type> res = task->get_future();

		{
			std::unique_lock<std::mutex> lock(mutex_);
			tasks_.emplace(
				[task] {
				(*task)();
			}
			);
			condition_.notify_one();
		}
		return res;
	}

	inline void wm::ThreadPool::stop()
	{
		{
			std::unique_lock<std::mutex> lock(mutex_);
			stop_ = true;
			condition_.notify_all();
		}
		for (auto& worker : workers_) {
			worker.join();
		}
	}
}