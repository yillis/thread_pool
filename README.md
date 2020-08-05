---
title: c++线程池实现
date: 2020-08-04 23:34:00
tags:
	- c++
	- 线程管理
toc: true
---

阐述线程池模型，详细讲解并使用code实现。

<!-- more -->

## 概述

多线程是一个编程语言必要的功能，如果一个程序中的线程变多之后，杂乱的线程会增加代码的维护成本，使用线程池则能对大量的线程进行有效的管理。

## 线程池模型

我们将每一个线程里的任务称为**task**

task的工作方式很容易让我们联想到队列，我们可以使用一个队列来管理所有的task

```c++
std::queue<std::function<void()> > tasks;
```

由于task都可以由lambda或函数来表示，于是可以使用std::bind将所有的task封装成一个std::function<void()>对象，注：这里的void与函数的void要进行区分，这里的void可以绑定所有类型返回值的函数。

封装好后，线程内部需要做的事情可以用如下伪码表示

```c++
thread
{
    while(true){
        get and lock mutex;
        wait until tasks not empty;
        task = tasks.front();
        tasks.pop();
        unlock mutex;
        
        task();  // execute task
    }
}
```

本质上就是不停的循环取出队首的任务来执行。

到这个地方，结合线程池的池很容易想到，线程池其实就是管理了很多线程的一个像池子一样的数据结构，每一个线程都由上述伪码实现，我们定义这样的线程在线程池里面担当**worker**的角色，所谓worker就应该不停地取出tasks里的task来完成，而worker的数量则为人为设定的线程池的池子大小，异步操作通过c++11的future库来实现。

那么线程池整体可以抽象为以下代码：

```c++
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
```

- workers储存所有的线程
- tasks为线程的队列，若有调度算法的实现需求，可另加一个函数get_task（），在其中实现任务调度算法
- mutex为线程池内共享变量的互斥锁，保证线程安全
- condition为条件变量，通过管程来阻塞和唤醒worker
- stop用于控制线程池是否停止
- add()为对外接口，用于添加任务
- stop()接口用于暂停线程池，并等待所有线程执行完毕

到此线程池的结构功能以及介绍清楚，现在还剩下一个问题，如何异步地获取每个task的结果

我们可以借助future库里的std::package_task来实现，该类可以接受一个函数对象，通过std::future与之绑定

调用std::future.get()会阻塞当前进程，直到std::package_task执行完毕，并传递返回值给std::future.get()

将上述worker的伪码实现如下：

```c++
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
```

思路在于不断取出task执行，task具体实现add函数，注意好共享变量的互斥操作就ok

add接口实现如下：

```c++
template<class F, class ...Args>
    inline auto ThreadPool::add(F&& f, Args&& ...args)
    ->std::future<typename std::result_of<F(Args ...)>::type>
{
    using return_type = typename std::result_of<F(Args ...)>::type;

    // must use pointer here, if use object, the object will be destruct when exist this function
    // then, in other thread, the variable *task* in lambda is disabled.
    // std::packaged_task<return_type()> task(
    // std::bind(std::forward<F>(f), std::forward<Args>(args)...));
    auto task = std::make_shared<std::packaged_task<return_type()> >(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));
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
```

std::package_task<void()>和std::function<void()>虽然类似，但并不能互相转化

我们将task设为一个匿名函数，执行传递进入的std::package_task就好了

很有趣的一点是，task必须使用指针，如果task使用对象的话，在add函数周期结束后，task便会被析构，那么引用传递给worker里的task便会成为一个空对象，如果对并发开发接触得少的话，这个错误是很容易犯的

## 测试

```c++
// main.cpp
#include "ThreadPool.h"
#include <iostream>

int main()
{
	wm::ThreadPool thread_pool(100);
	
	std::mutex m;
	int result = 0;
	for (int i = 1; i <= 100; ++i) {
		thread_pool.add([&m, &result, i] {
			std::unique_lock<std::mutex> lock(m);
			result += i;
		});
	}

	thread_pool.stop();

	std::cout << result << std::endl;

	return 0;
}
// output: 5050
```

至此已经成功实现了线程池，完整代码可[到此]()查看

## 参考资料

- https://blog.csdn.net/caoshangpa/article/details/80374651
- https://en.cppreference.com/w/

