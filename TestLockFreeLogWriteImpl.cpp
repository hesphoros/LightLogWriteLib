#include "LightLogWriteImpl.hpp"
#include "LockFreeLogWriteImpl.hpp"

/*
 * 该示例演示多线程并发向 LockFreeLogWriteImpl 写入日志。
 *
 */

 // 指定线程数量与写日志次数
static const int NUM_THREADS = 4;        // 线程数量
static const int LINES_PER_THREAD = 100; // 每个线程的日志条数

void LogThreadFunc(int threadId, LockFreeLogWriteImpl* pLogger) {
	std::wstring tag = L"Thread_" + std::to_wstring(threadId);
	for (int i = 0; i < LINES_PER_THREAD; ++i) {
		// 构造要写入的日志信息
		std::wstring message = L"Log entry " + std::to_wstring(i) + L" from thread "
			+ std::to_wstring(threadId);
		// 写入日志
		pLogger->WriteLogContent(tag, message);

		// 简单休眠，模拟业务处理
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

int TestLockFreeLogWriteImpl() {
	// 创建日志写入对象，并设置日志文件名
	LockFreeLogWriteImpl logger(/*maxQueueSize=*/5000,
		LogQueueFullStrategy::Block,
		/*reportInterval=*/10);

	// 设置日志文件名称（可换成你的路径或文件前缀）
	logger.SetLogsFileName(L"lockfree_test_output.log");

	// 创建并启动线程进行并发写日志
	std::vector<std::thread> threads;
	threads.reserve(NUM_THREADS);
	for (int i = 0; i < NUM_THREADS; ++i) {
		threads.emplace_back(LogThreadFunc, i, &logger);
	}

	// 等待所有线程完成
	for (auto& t : threads) {
		t.join();
	}

	// 程序结束时会调用 ~LockFreeLogWriteImpl()，确保日志线程正常退出
	std::wcout << L"All threads finished writing. Check 'lockfree_test_output.log' for output.\n";

	return 0;
}

