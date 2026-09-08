// Exercise worker ownership without requiring a running TouchDesigner host.
#include "../TOP_CPlusPlusBase.h"
#include "../Parameters.h"
#include <thread>
#include <condition_variable>
#include <atomic>
#include <array>
#include <queue>

// Limit test access to this class; SDK and standard headers are already loaded.
#define private public
#include "../BasicFilterTOP.h"
#undef private
#include "../ThreadManager.h"

#include <mach/mach.h>
#include <chrono>
#include <cstdlib>
#include <iostream>

static size_t threadCount()
{
	thread_act_array_t threads = nullptr;
	mach_msg_type_number_t count = 0;
	if (task_threads(mach_task_self(), &threads, &count) != KERN_SUCCESS)
	{
		std::cerr << "Could not inspect task threads\n";
		std::exit(1);
	}
	for (mach_msg_type_number_t i = 0; i < count; ++i)
		mach_port_deallocate(mach_task_self(), threads[i]);
	vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threads),
		count * sizeof(thread_t));
	return count;
}

int main()
{
	// Immediate destruction also exercises the race between startup and wait.
	for (int i = 0; i < 500; ++i)
	{
		ThreadManager worker;
		if (i % 2)
			std::this_thread::yield();
	}
	const size_t baseline = threadCount();
	for (int i = 0; i < 5; ++i)
	{
		BasicFilterTOP node(nullptr, nullptr);
		node.switchToMultiThreaded();
		node.switchToSingleThreaded();
		node.switchToMultiThreaded();
		// Destroy while multithreaded is still enabled, as when deleting a TOP.
	}
	for (int i = 0; i < 100; ++i)
	{
		if (threadCount() == baseline)
		{
			std::cout << "Worker startup, mode changes, and node deletion passed\n";
			return 0;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	std::cerr << "Node deletion left worker threads running\n";
	return 1;
}
