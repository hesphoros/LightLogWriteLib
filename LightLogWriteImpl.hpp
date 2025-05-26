#include <iostream>
#include <string>
#include <chrono>
#include <mutex>
#include <fstream>
#include <queue>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <locale>
#include <codecvt>
#include <ostream>
#include <sstream>
#include <iomanip>
#include <memory>
#include <filesystem>
#include <vector>
#include <stdexcept>
#include "iconv.h"
#include "convert_tools.h"
#include "LightLogWriteCommon.h"

#pragma comment ( lib,"libiconv.lib" )




class LightLogWrite_Impl {
public:
	LightLogWrite_Impl(size_t maxQueueSize = 10000,LogQueueFullStrategy  strategy = LogQueueFullStrategy::Block , size_t reportInterval = 100)
		: 
		kMaxQueueSize(maxQueueSize),
		discardCount(0),
		lastReportedDiscardCount(0),
		bIsStopLogging{false},
		bNeedReport{false},
		queueFullStrategy(strategy),
		reportInterval(reportInterval),
		bHasLogLasting{false}
	{
		
		sWritedThreads = std::thread(&LightLogWrite_Impl::RunWriteThread, this);
	}

	~LightLogWrite_Impl() {
		CloseLogStream();
	}

	/// <summary>
	/// 设置日志文件名  如果不存在将会创建
	/// </summary>
	/// <param name="sFilename"></param>
	void SetLogsFileName(const std::wstring& sFilename) {
		std::lock_guard<std::mutex> sWriteLock(pLogWriteMutex);
		if (pLogFileStream.is_open()) pLogFileStream.close();
		ChecksDirectory(sFilename);  //确保目录存在
		pLogFileStream.open(sFilename, std::ios::app);
	}

	void SetLogsFileName(const std::string& sFilename) {
		SetLogsFileName(Utf8ConvertsToUcs4(sFilename));
	}

	void SetLogsFileName(const std::u16string& sFilename) {
		SetLogsFileName(U16StringToWString(sFilename));
	}

	/// <summary>
	/// 设置日志持久化输出
	/// </summary>
	/// <param name="sFilePath"></param>
	/// <param name="sBaseName"></param>
	void SetLastingsLogs(const std::wstring& sFilePath, const std::wstring& sBaseName) {
		sLogLastingDir = sFilePath;
		sLogsBasedName = sBaseName;
		bHasLogLasting = true;
		CreateLogsFile();
	}

	void SetLastingsLogs(const std::u16string& sFilePath, const std::u16string& sBaseName) {
		SetLastingsLogs(U16StringToWString(sFilePath), U16StringToWString(sBaseName));
	}
	void SetLastingsLogs(const std::string& sFilePath, const std::string& sBaseName) {
		SetLastingsLogs(Utf8ConvertsToUcs4(sFilePath), Utf8ConvertsToUcs4(sBaseName));
	}

	/// <summary>
	/// 
	/// </summary>
	/// <param name="sTypeVal"></param>
	/// <param name="sMessage"></param>
	void WriteLogContent(const std::wstring& sTypeVal, const std::wstring& sMessage) {
		bool bNeedReport = false;
		size_t currentDiscard = 0;
		static thread_local bool inErrorReport = false;

		if (queueFullStrategy == LogQueueFullStrategy::Block) {
			std::unique_lock<std::mutex> sWriteLock(pLogWriteMutex);
			//std::wcerr << L"[WriteLogContent] Try push (Block), queue size: " << pLogWriteQueue.size() << std::endl;
		
			pWritedCondVar.wait(sWriteLock, [this] {
				return pLogWriteQueue.size() < kMaxQueueSize;
			});
			
			pLogWriteQueue.push({ sTypeVal, sMessage });
			//std::wcerr << L"[WriteLogContent] Pushed (Block), queue size: " << pLogWriteQueue.size() << std::endl;
		}
		else if (queueFullStrategy == LogQueueFullStrategy::DropOldest) {
			std::lock_guard<std::mutex> sWriteLock(pLogWriteMutex);
			std::wcerr << L"[WriteLogContent] Try push (DropOldest), queue size: " << pLogWriteQueue.size() << std::endl;
			if (pLogWriteQueue.size() >= kMaxQueueSize) {
				std::wcerr << L"[WriteLogContent] Drop oldest, queue full: " << pLogWriteQueue.size() << std::endl;
				pLogWriteQueue.pop();
				++discardCount;
				if (discardCount - lastReportedDiscardCount >= reportInterval) {
					bNeedReport = true;
					currentDiscard = discardCount;
					lastReportedDiscardCount.store(discardCount.load());
				}
			}
			pLogWriteQueue.push({ sTypeVal, sMessage });
			std::wcout << L"[WriteLogContent] Pushed (DropOldest), queue size: " << pLogWriteQueue.size() << std::endl;
		}
		pWritedCondVar.notify_one();

		if (bNeedReport && !inErrorReport) {
			inErrorReport = true;
			std::wstring overflowMsg = L"The log queue overflows and has been discarded "
				+ std::to_wstring(currentDiscard) + L" logs";
			std::wcerr << L"[WriteLogContent] Report overflow: " << overflowMsg << std::endl;
			WriteLogContent(L"LOG_OVERFLOW", overflowMsg);
			inErrorReport = false;
		}
	}

	void WriteLogContent(const std::string& sTypeVal, const std::string& sMessage)
	{
		WriteLogContent(Utf8ConvertsToUcs4(sTypeVal), Utf8ConvertsToUcs4(sMessage));
	}

	void WriteLogContent(const std::u16string& sTypeVal, const std::u16string& sMessage) {
		WriteLogContent(U16StringToWString(sTypeVal), U16StringToWString(sMessage));
	}

	size_t GetDiscardCount() const {
		return discardCount;
	}

	void ResetDiscardCount() {
		discardCount = 0;
	}

private:

	std::wstring BuildLogFileOut() {
		std::tm                 sTmPartsInfo = GetCurrsTimerTm();
		std::wostringstream     sWosStrStream;

		sWosStrStream << std::put_time(&sTmPartsInfo, L"%Y_%m_%d") << (sTmPartsInfo.tm_hour >= 12 ? L"_AM" : L"_PM") << L".log";

		bLastingTmTags = (sTmPartsInfo.tm_hour > 12);

		std::filesystem::path   sLotOutPaths = sLogLastingDir;
		std::filesystem::path   sLogOutFiles = sLotOutPaths / (sLogsBasedName + sWosStrStream.str());

		return sLogOutFiles.wstring();
	}

	void CloseLogStream()
	{
		bIsStopLogging = true;
		pWritedCondVar.notify_all();
		WriteLogContent(L"<================================              Stop log write thread    ", L"================================>");
		if (sWritedThreads.joinable()) sWritedThreads.join();//等待线程结束
	}

	void CreateLogsFile()
	{
		std::wstring  sOutFileName = BuildLogFileOut();
		std::lock_guard<std::mutex> sLock(pLogWriteMutex);
		ChecksDirectory(sOutFileName);
		pLogFileStream.close();	//关闭之前提交的文件流
		pLogFileStream.open(sOutFileName, std::ios::app);
	}

	void RunWriteThread() {
		while (true) {
			if (bHasLogLasting)
				if (bLastingTmTags != (GetCurrsTimerTm().tm_hour > 12))
					CreateLogsFile();
			LightLogWrite_Info sLogMessageInf;
			{
				auto sLock = std::unique_lock<std::mutex>(pLogWriteMutex);
				pWritedCondVar.wait(sLock, [this] {return !pLogWriteQueue.empty() || bIsStopLogging; });

				if (bIsStopLogging && pLogWriteQueue.empty()) break;  //如果停止标志为真且队列为空，则退出线程
				if (!pLogWriteQueue.empty()) {
					sLogMessageInf = pLogWriteQueue.front();
					pLogWriteQueue.pop();
					pWritedCondVar.notify_one();
					
				}
			}
			if (!sLogMessageInf.sLogContentVal.empty() && pLogFileStream.is_open())
			{
				pLogFileStream << sLogMessageInf.sLogTagNameVal << L"-//>>>" << GetCurrentTimer() << " : " << sLogMessageInf.sLogContentVal << "\n";
			}
		}
		pLogFileStream.close();
		std::cerr << "Log write thread Exit\n";
	}

	void ChecksDirectory(const std::wstring& sFilename) {
		
		std::filesystem::path sFullFileName(sFilename);
		std::filesystem::path sOutFilesPath = sFullFileName.parent_path();
		if (!sOutFilesPath.empty() && !std::filesystem::exists(sOutFilesPath))
		{
			std::filesystem::create_directories(sOutFilesPath);
		}
	}

	std::wstring GetCurrentTimer() const {
		std::tm              sTmPartsInfo = GetCurrsTimerTm();
		std::wostringstream  sWosStrStream;
		sWosStrStream << std::put_time(&sTmPartsInfo, L"%Y-%m-%d %H:%M:%S");
		return	sWosStrStream.str();
	}

	std::tm GetCurrsTimerTm() const {
		auto        sCurrentTime = std::chrono::system_clock::now();
		std::time_t sCurrTimerTm = std::chrono::system_clock::to_time_t(sCurrentTime);
		std::tm     sCurrTmDatas;
#ifdef _WIN32
		localtime_s(&sCurrTmDatas, &sCurrTimerTm);
#else
		localtime_r(&sCurrTmDatas, &sCurrTimerTm);
#endif
		return sCurrTmDatas;
	}

private:
	std::wofstream                                 pLogFileStream;  // 日志文件流
	std::mutex                                     pLogWriteMutex;  // 日志写入锁
	std::queue<LightLogWrite_Info>                 pLogWriteQueue;  // 日志写入队列 FIFO
	std::condition_variable	                       pWritedCondVar;  // 条件变量 唤醒日志写线程

	std::thread                                    sWritedThreads;  // 日志处理线程
	std::atomic<bool>                              bIsStopLogging;  // 停止标志  default false 控制线程停止
	std::wstring                                   sLogLastingDir;  // 持久化日志路径
	std::wstring                                   sLogsBasedName;  // 持久化日志选项
	std::atomic<bool>                              bHasLogLasting;  // 是否持久化输出
	std::atomic<bool>                              bLastingTmTags;  // 当前日志文件AM/PM标识
	const size_t                                    kMaxQueueSize;  // 队列的最大长度
	LogQueueFullStrategy                        queueFullStrategy;  // 队列满时处理策略
	std::atomic<size_t>                              discardCount;  // 日志丢弃计数
	std::atomic<size_t>                   lastReportedDiscardCount;  // 上次报告后丢弃条数
	std::atomic<size_t>                             reportInterval;  // 报告间隔
	std::atomic<bool>                                  bNeedReport;  // 是否需要报告
	
};

