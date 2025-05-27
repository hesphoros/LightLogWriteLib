#ifndef LOCK_FREE_LOG_WRITE_IMPL_HPP
#define LOCK_FREE_LOG_WRITE_IMPL_HPP

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
#include <cstddef>
#include "iconv.h"
#include "convert_tools.h"
#pragma comment ( lib,"libiconv.lib" )



/**
 * @brief Structure for log message information.
 * @param sLogTagNameVal The tag name of the log.
 * * It can be used to categorize or identify the log message.
 * * such as INFO , WARNING, ERROR, etc.
 * @param sLogContentVal The content of the log message.
 * * It contains the actual log message that will be written to the log file.
 * * This can include any relevant information that needs to be logged, such as error messages, status updates, etc.
 */
struct LightLogWrite_Info {
	std::wstring                   sLogTagNameVal;  /*!< Log tag name */
	std::wstring                   sLogContentVal;  /*!< Log content */
};

/**
 * @brief Enum for strategies to handle full log queues.
 * @details
 * * This enum defines the strategies that can be used when the log queue is full.
 * * It provides options for blocking until space is available or dropping the oldest log entry.
 * @param Block Blocked waiting for space in the queue.
 * * When the queue is full, the logging operation will block until space becomes available.
 * @param DropOldest Drop the oldest log entry when the queue is full.
 * * When the queue is full, the oldest log entry will be removed to make space for the new log entry.
 * * This strategy allows for continuous logging without blocking, but may result in loss of older log entries.
 */
enum class LogQueueFullStrategy {
	Block,      /*!< Blocked waiting           */
	DropOldest  /*!< Drop the oldest log entry */
};


/**
 * @brief Converts a UTF-8 encoded string to UCS-4 (UTF-32) encoded wide string
 * @param utf8str The UTF-8 encoded string to be converted
 * @return A wide string (std::wstring) representing the UCS-4 encoded string
 * @details This function uses std::wstring_convert with std::codecvt_utf8<wchar_t> to perform the conversion.
 */
inline std::wstring Utf8ConvertsToUcs4(const std::string& utf8str) {
	try {

		std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
		return converter.from_bytes(utf8str);
	} catch (const std::range_error& e) {
		throw std::runtime_error("Failed to convert UTF-8 to UCS-4: " + std::string(e.what()));
	}
}

/**
 * @brief Converts a UCS-4 (UTF-32) encoded wide string to UTF-8 encoded string
 * @param wstr The UCS-4 encoded wide string to be converted
 * @return A UTF-8 encoded string (std::string) representing the converted wide string
 * @details This function uses std::wstring_convert with std::codecvt_utf8<wchar_t> to perform the conversion.
 */
inline std::string Ucs4ConvertToUtf8(const std::wstring& wstr) {
	try {
		std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
		return converter.to_bytes(wstr);
	}
	catch (const std::range_error& e) {
		throw std::runtime_error("Failed to convert UCS-4 to UTF-8: " + std::string(e.what()));
	}
}

/**
 * @brief Converts a UTF-16 encoded string to a wide string (UCS-4)
 * @param u16str The UTF-16 encoded string to be converted
 * @return A wide string (std::wstring) representing the UCS-4 encoded string
 * @details This function uses std::wstring_convert with std::codecvt_utf16<wchar_t, 0x10ffff, std::little_endian> to perform the conversion.
 */
inline std::wstring U16StringToWString(const std::u16string& u16str) {
	std::wstring wstr;
#ifdef _WIN32
	wstr.assign(u16str.begin(), u16str.end());
#else
	std::wstring_convert<
	std::codecvt_utf16<wchar_t, 0x10ffff, std::little_endian>>
	converter;
	wstr = converter.from_bytes(
		reinterpret_cast<const char*>(u16str.data()),
		reinterpret_cast<const char*>(u16str.data() + u16str.size()));
#endif
	return wstr;
}



/**
 * @brief A lock-free queue implementation using atomic operations
 * * This queue is designed to be used in a multi-threaded environment where multiple threads can push and pop elements concurrently without locks.
 * @param T The type of elements stored in the queue
 * @details The queue uses a fixed-size array of nodes, each containing an atomic tail and head index to manage the queue state.
 * * The queue supports push and pop operations, where push adds an element to the end of the queue and pop removes an element from the front.
 * * The queue is designed to be lock-free, meaning that it does not use mutexes or other locking mechanisms to ensure thread safety.
 * * The queue is implemented using a circular buffer, where the capacity is a power of two to optimize index calculations.
 */
template <typename T>
class LockFreeQueue {
public:
	explicit LockFreeQueue(size_t capacity)
	{
		_capacityMask = capacity - 1;
		for (size_t i = 1; i <= sizeof(void*) * 4; i <<= 1)
			_capacityMask |= _capacityMask >> i;
		_capacity = _capacityMask + 1;

		_queue = (Node*)new char[sizeof(Node) * _capacity];
		for (size_t i = 0; i < _capacity; ++i)
		{
			_queue[i].tail.store(i, std::memory_order_relaxed);
			_queue[i].head.store(-1, std::memory_order_relaxed);
		}

		_tail.store(0, std::memory_order_relaxed);
		_head.store(0, std::memory_order_relaxed);
	}

	~LockFreeQueue()
	{
		for (size_t i = _head; i != _tail; ++i)
			(&_queue[i & _capacityMask].data)->~T();

		delete[](char*)_queue;
	}

	size_t capacity() const { return _capacity; }

	size_t size() const
	{
		size_t head = _head.load(std::memory_order_acquire);
		return _tail.load(std::memory_order_relaxed) - head;
	}

	bool push(const T& data)
	{
		Node* node;
		size_t tail = _tail.load(std::memory_order_relaxed);
		for (;;)
		{
			node = &_queue[tail & _capacityMask];
			if (node->tail.load(std::memory_order_relaxed) != tail)
				return false;
			if ((_tail.compare_exchange_weak(tail, tail + 1, std::memory_order_relaxed)))
				break;
		}
		new (&node->data)T(data);
		node->head.store(tail, std::memory_order_release);
		return true;
	}

	bool pop(T& result)
	{
		Node* node;
		size_t head = _head.load(std::memory_order_relaxed);
		for (;;)
		{
			node = &_queue[head & _capacityMask];
			if (node->head.load(std::memory_order_relaxed) != head)
				return false;
			if (_head.compare_exchange_weak(head, head + 1, std::memory_order_relaxed))
				break;
		}
		result = node->data;
		(&node->data)->~T();
		node->tail.store(head + _capacity, std::memory_order_release);
		return true;
	}

private:
	struct Node
	{
		T data;
		std::atomic<size_t> tail;
		std::atomic<size_t> head;
	};

private:
	size_t                _capacityMask;
	Node                  *_queue;
	size_t                _capacity;
	char                  cacheLinePad1[64];
	std::atomic<size_t>   _tail;
	char                  cacheLinePad2[64];
	std::atomic<size_t>   _head;
	char                  cacheLinePad3[64];
};

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
		pLogWriteQueue(maxQueueSize) 
	{
		sWrittenThreads = std::thread(&LockFreeLogWriteImpl::RunWriteThread, this);
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
		pWrittenCondVar.notify_one();

		if (bNeedReport && !inErrorReport) {
			inErrorReport = true;
			std::wstring overflowMsg = L"The log queue overflows and has been discarded "
				+ std::to_wstring(currentDiscard) + L" logs";
			//std::wcerr << L"[WriteLogContent] Report overflow: " << overflowMsg << std::endl;
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
		pWrittenCondVar.notify_all();
		WriteLogContent(L"<================================              Stop log write thread    ", L"================================>");
		if (sWrittenThreads.joinable()) sWrittenThreads.join();
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
			// 检查是否需要切换日志文件（AM/PM切换）
			if (bHasLogLasting) {
				bool isCurrentPM = (GetCurrsTimerTm().tm_hour > 12);
				if (bLastingTmTags != isCurrentPM) {
					CreateLogsFile();
				}
			}

			LightLogWrite_Info sLogMessageInf;
			bool hasLog = pLogWriteQueue.pop(sLogMessageInf);

			// 只有在停止标志为真且队列为空时才退出
			if (bIsStopLogging && pLogWriteQueue.size() == 0 && !hasLog) {
				break;
			}

			// 写入日志内容到文件
			if (hasLog && !sLogMessageInf.sLogContentVal.empty() && pLogFileStream.is_open()) {
				pLogFileStream << sLogMessageInf.sLogTagNameVal
					<< L"-//>>>" << GetCurrentTimer()
					<< L" : " << sLogMessageInf.sLogContentVal << L"\n";
			}
			else if (!hasLog) {
				// 队列为空，避免忙等
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
		}

		// 关闭日志文件流
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
	std::condition_variable pWrittenCondVar; // 条件变量，线程同步

	std::thread sWrittenThreads;
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

#endif //  !LOCK_FREE_LOG_WRITE_IMPL_HPP