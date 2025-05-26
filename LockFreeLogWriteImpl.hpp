#pragma once

#include <iostream>
#include <string>
#include <chrono>
#include <fstream>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <filesystem>
#include <vector>
#include <stdexcept>
#include "iconv.h"
#include "convert_tools.h"
#include "LightLogWriteCommon.h"
#include "LockFreeQueue.hpp" // 引入 lockfree 队列

#pragma comment ( lib,"libiconv.lib" )

class LockFreeLogWriteImpl {
public:
	LockFreeLogWriteImpl(size_t maxQueueSize = 10000, LogQueueFullStrategy strategy = LogQueueFullStrategy::Block, size_t reportInterval = 100)
		: kMaxQueueSize(maxQueueSize),
		discardCount(0),
		lastReportedDiscardCount(0),
		bIsStopLogging{ false },
		bNeedReport{ false },
		queueFullStrategy(strategy),
		reportInterval(reportInterval),
		bHasLogLasting{ false },
		pLogWriteQueue(maxQueueSize) // 初始化无锁队列容量
	{
		sWritedThreads = std::thread(&LockFreeLogWriteImpl::RunWriteThread, this);
	}

	~LockFreeLogWriteImpl() {
		CloseLogStream();
	}

	void SetLogsFileName(const std::wstring& sFilename) {
		std::lock_guard<std::mutex> sWriteLock(fileMutex);
		if (pLogFileStream.is_open()) pLogFileStream.close();
		ChecksDirectory(sFilename);
		pLogFileStream.open(sFilename, std::ios::app);
	}

	void SetLogsFileName(const std::string& sFilename) {
		SetLogsFileName(Utf8ConvertsToUcs4(sFilename));
	}

	void SetLogsFileName(const std::u16string& sFilename) {
		SetLogsFileName(U16StringToWString(sFilename));
	}

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

	void WriteLogContent(const std::wstring& sTypeVal, const std::wstring& sMessage) {
		bool bNeedReport = false;
		size_t currentDiscard = 0;
		static thread_local bool inErrorReport = false;

		if (queueFullStrategy == LogQueueFullStrategy::Block) {
			// 阻塞直到成功写入
			while (!pLogWriteQueue.push({ sTypeVal, sMessage })) {
				if (bIsStopLogging) return;
				std::this_thread::yield();
			}
		}
		else if (queueFullStrategy == LogQueueFullStrategy::DropOldest) {
			if (!pLogWriteQueue.push({ sTypeVal, sMessage })) {
				// 队列满，丢弃最老的再插入
				LightLogWrite_Info dummy;
				pLogWriteQueue.pop(dummy);
				pLogWriteQueue.push({ sTypeVal, sMessage });
				++discardCount;
				if (discardCount - lastReportedDiscardCount >= reportInterval) {
					bNeedReport = true;
					currentDiscard = discardCount;
					lastReportedDiscardCount.store(discardCount.load());
				}
			}
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

	void WriteLogContent(const std::string& sTypeVal, const std::string& sMessage) {
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
		std::tm sTmPartsInfo = GetCurrsTimerTm();
		std::wostringstream sWosStrStream;
		sWosStrStream << std::put_time(&sTmPartsInfo, L"%Y_%m_%d") << (sTmPartsInfo.tm_hour >= 12 ? L"_AM" : L"_PM") << L".log";
		bLastingTmTags = (sTmPartsInfo.tm_hour > 12);
		std::filesystem::path sLotOutPaths = sLogLastingDir;
		std::filesystem::path sLogOutFiles = sLotOutPaths / (sLogsBasedName + sWosStrStream.str());
		return sLogOutFiles.wstring();
	}

	void CloseLogStream() {
		bIsStopLogging = true;
		pWritedCondVar.notify_all();
		WriteLogContent(L"<================================              Stop log write thread    ", L"================================>");
		if (sWritedThreads.joinable()) sWritedThreads.join();
	}

	void CreateLogsFile() {
		std::wstring sOutFileName = BuildLogFileOut();
		std::lock_guard<std::mutex> sLock(fileMutex);
		ChecksDirectory(sOutFileName);
		pLogFileStream.close();
		pLogFileStream.open(sOutFileName, std::ios::app);
	}

	void RunWriteThread() {
		while (true) {
			if (bHasLogLasting)
				if (bLastingTmTags != (GetCurrsTimerTm().tm_hour > 12))
					CreateLogsFile();

			LightLogWrite_Info sLogMessageInf;

			// pop 等待日志数据
			while (!pLogWriteQueue.pop(sLogMessageInf)) {
				if (bIsStopLogging)
					break;
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}

			if (bIsStopLogging && pLogWriteQueue.size() == 0)
				break;

			if (!sLogMessageInf.sLogContentVal.empty() && pLogFileStream.is_open()) {
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
		std::tm sTmPartsInfo = GetCurrsTimerTm();
		std::wostringstream sWosStrStream;
		sWosStrStream << std::put_time(&sTmPartsInfo, L"%Y-%m-%d %H:%M:%S");
		return sWosStrStream.str();
	}

	std::tm GetCurrsTimerTm() const {
		auto sCurrentTime = std::chrono::system_clock::now();
		std::time_t sCurrTimerTm = std::chrono::system_clock::to_time_t(sCurrentTime);
		std::tm sCurrTmDatas;
#ifdef _WIN32
		localtime_s(&sCurrTmDatas, &sCurrTimerTm);
#else
		localtime_r(&sCurrTimerTm, &sCurrTmDatas);
#endif
		return sCurrTmDatas;
	}

private:
	std::wofstream pLogFileStream; // 日志文件流
	std::mutex fileMutex; // 文件写入保护锁（只有文件相关，队列无锁）
	LockFreeQueue<LightLogWrite_Info> pLogWriteQueue; // 无锁队列
	std::condition_variable pWritedCondVar; // 条件变量，线程同步

	std::thread sWritedThreads;
	std::atomic<bool> bIsStopLogging;
	std::wstring sLogLastingDir;
	std::wstring sLogsBasedName;
	std::atomic<bool> bHasLogLasting;
	std::atomic<bool> bLastingTmTags;
	const size_t kMaxQueueSize;
	LogQueueFullStrategy queueFullStrategy;
	std::atomic<size_t> discardCount;
	std::atomic<size_t> lastReportedDiscardCount;
	std::atomic<size_t> reportInterval;
	std::atomic<bool> bNeedReport;
};