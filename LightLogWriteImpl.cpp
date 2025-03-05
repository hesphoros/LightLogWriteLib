
#include <iostream>
#include <string>
#include <chrono>
#include <mutex>
#include <fstream>
#include <queue>
#include <thread>
#include <string>
#include <condition_variable>
#include <atomic>

// 日志写入接口
struct LightLogWrite_Info {
	std::wstring		sLogTagNameVal;
	std::wstring		sLogContentVal;
};

class LightLogWrite_Impl {

private:
	std::tm GetCurrsTimerTm() {
		auto		sCurrentTime = std::chrono::system_clock::now();
		std::time_t sCurrTimerTm = std::chrono::system_clock::to_time_t(sCurrentTime);
		std::tm		sCurrTmDatas;
#ifdef _WIN32
		localtime_s(&sCurrTmDatas, &sCurrTimerTm);
#else
		localtime_r(&sCurrTimerTm, &sCurrTmDatas);
#endif
		return sCurrTmDatas;
	}

	std::wofstream						pLogFileStream;	// 日志文件流
	std::mutex							pLogWriteMutex;	// 日志写入锁
	std::queue<LightLogWrite_Info>		pLogWriteQueue;	// 日志消息队列
	std::condition_variable				pWritedCondVar;	// 条件变量
	std::thread							sWritedThreads;	// 日志处理线程
	std::atomic<bool>					bIsStopLogging;	// 停止标志
	std::wstring						sLogLastingDir;	// 持久化日志路径
	std::wstring						sLogsBasedName; // 持久化日志选项
	std::atomic<bool>					bHasLogLasting;	// 是否日志持久化输出
	std::atomic<bool>					bLastingTmTags;	// 判断时间是上午还是下午

};

int main()
{
    std::cout << "Hello World!\n";
}

