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