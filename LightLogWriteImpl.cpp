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
#include <locale>
#include <codecvt>
#include <ostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include "convert.h"

// 日志写入接口
struct LightLogWrite_Info {
	std::wstring		sLogTagNameVal;
	std::wstring		sLogContentVal;
};

class LightLogWrite_Impl {


public:
	LightLogWrite_Impl() :
		bIsStopLogging{ false },
		bHasLogLasting{ false }
	{
		sWritedThreads = std::jthread(&LightLogWrite_Impl::RunWriteThread, this);
	}

	void CreateLogsFile();
private:
	void RunWriteThread() {
		while (true) {
			if (bHasLogLasting) if (bLastingTmTags != GetCurrsTimerTm().tm_hour > 12) CreateLogsFile();
			LightLogWrite_Info sLogMessageInf;
			{

				//std::unique_lock<std::mutex> sLock(pLogWriteMutex);
				auto sLock = std::unique_lock<std::mutex>(pLogWriteMutex);
				pWritedCondVar.wait(sLock, [this] {return !pLogWriteQueue.empty() || bIsStopLogging; });
				if (bIsStopLogging && pLogWriteQueue.empty()) break;//如果停止标志为真且队列为空，则退出线程
				if (!pLogWriteQueue.empty()) {
					sLogMessageInf = pLogWriteQueue.front();
					pLogWriteQueue.pop();
					std::cerr << "pop:" << Ucs4ConvertToUtf8(sLogMessageInf.sLogContentVal) <<"\n";
				}
			}
			if (!sLogMessageInf.sLogContentVal.empty() && pLogFileStream.is_open())
			{
				pLogFileStream << sLogMessageInf.sLogTagNameVal << L"-/>>>" << GetCurrentTimer() << " : " <<sLogMessageInf.sLogContentVal << "\n";

			}

		}
		pLogFileStream.close();
		std::cerr << "Log write thread Exit\n";
	}
	void ChecksDirectory(const std::wstring& sFilename) const{
		std::filesystem::path sFullFileName(sFilename);
		std::filesystem::path sOutFilesPath = sFullFileName.parent_path();
		if (!sOutFilesPath.empty() && !std::filesystem::exists(sOutFilesPath))
		{
			std::filesystem::create_directories(sOutFilesPath);
		}

	}

	std::wstring GetCurrentTimer() const {
		std::tm				sTmPartsInfo = GetCurrsTimerTm();
		std::wostringstream sWosStrStream;
		sWosStrStream << std::put_time(&sTmPartsInfo, L"%Y-%m-%d %H:%M:%S");
		return	sWosStrStream.str();

	}

	std::tm GetCurrsTimerTm() const{
		auto		sCurrentTime = std::chrono::system_clock::now();
		std::time_t sCurrTimerTm = std::chrono::system_clock::to_time_t(sCurrentTime);
		std::tm		sCurrTmDatas;
#ifdef _WIN32
		localtime_s(&sCurrTmDatas, &sCurrTimerTm);
#else
		localtime_r(&sCurrTmDatas, &sCurrTimerTm);
#endif
		return sCurrTmDatas;
	}

	std::wofstream						pLogFileStream;	// 日志文件流
	std::mutex							pLogWriteMutex;	// 日志写入锁
	std::queue<LightLogWrite_Info>		pLogWriteQueue;	// 日志消息队列
	std::condition_variable				pWritedCondVar;	// 条件变量
	//change to jthread
	std::jthread						sWritedThreads;	// 日志处理线程
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
