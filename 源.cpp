#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <string>
#include "LightLogWriteImpl.hpp"        // 假设这是原始实现头文件
#include "LockFreeLogWriteImpl.hpp"     // 假设这是无锁队列实现头文件

/*
 * 修正要点:
 * 原错误提示是因为 LockFreeLogWriteImpl 中包含 const 数据成员 (kMaxQueueSize)，
 * 导致编译器隐式删除了拷贝赋值运算符。若在模板函数中使用了类似 "logger = tmpLogger" 的方式，
 * 则会触发尝试引用已删除的函数。
 *
 * 下面的示例避免了在模板里对 logger 进行多次赋值，而是直接通过构造函数创建最终对象，
 * 从而避免 “= std::move(...)” 的操作。
 */

 // 配置测试参数
    static const int NUM_THREADS = 4;       // 并发线程数
static const int LINES_PER_THREAD = 100000;   // 每个线程写入日志条数

// 根据需要切换使用哪种策略
static const bool USE_BLOCK_STRATEGY = false; // 或 false

template <typename LogImplClass>
double MeasureLogPerformance(const std::wstring& testName, const std::wstring& outFilename)
{
    
    LogImplClass logger(
        /*maxQueueSize=*/100,
        (USE_BLOCK_STRATEGY ? LogQueueFullStrategy::Block : LogQueueFullStrategy::DropOldest),
        /*reportInterval=*/100
    );

    logger.SetLogsFileName(outFilename);

    auto writeTask = [&](int threadIndex) {
        std::wstring sTag = L"Thread_" + std::to_wstring(threadIndex);
        for (int i = 0; i < LINES_PER_THREAD; ++i) {
            std::wstring message = L"Log message " + std::to_wstring(i) + L" from " + sTag;
            logger.WriteLogContent(sTag, message);
        }
        };

    auto startTime = std::chrono::high_resolution_clock::now();

    // 启动多线程进行日志写入
    std::vector<std::thread> threads;
    threads.reserve(NUM_THREADS);
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(writeTask, i);
    }

    // 等待所有线程结束
    for (auto& t : threads) {
        t.join();
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    double durationSec =
        std::chrono::duration<double>(endTime - startTime).count();

    std::wcout << L"[" << testName << L"] Completed in "
        << durationSec << L" seconds." << std::endl;

    return durationSec;
}

int main()
{
    // 测试 LightLogWriteImpl 性能
    double timeLightLog = MeasureLogPerformance<LightLogWrite_Impl>(
        L"LightLogWriteImpl",
        L"LightLogWriteImpl_result.log"
    );

    // 测试 LockFreeLogWriteImpl 性能
    double timeLockFree = MeasureLogPerformance<LockFreeLogWriteImpl>(
        L"LockFreeLogWriteImpl",
        L"LockFreeLogWriteImpl_result.log"
    );

    // 打印简要对比结果
    std::wcout << L"------ Performance Summary ------" << std::endl
        << L"LightLogWriteImpl       : " << timeLightLog << L" seconds" << std::endl
        << L"LockFreeLogWriteImpl    : " << timeLockFree << L" seconds" << std::endl;

	if (timeLockFree < timeLightLog) {
		std::wcout << L"LockFreeLogWriteImpl is faster by ~"
			<< (timeLightLog - timeLockFree) << L" seconds." << std::endl;
	}
	else {
		std::wcout << L"LightLogWriteImpl is faster by ~"
			<< (timeLockFree - timeLightLog) << L" seconds." << std::endl;
	}

    std::wcout << L"Note: Actual performance may vary depending on environment and log specifics.\n";
    return 0;
}