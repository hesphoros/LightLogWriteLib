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
#include <vector>
#include <stdexcept>
#include "iconv.h"


#pragma comment ( lib,"libiconv.lib" ) 



// 函数：将 UTF-8 转换为 std::wstring（假设为 UCS-4）
static std::wstring Utf8ConvertsToUcs4(const std::string& utf8str) {

	try {
		// 创建 std::wstring_convert 对象，使用 std::codecvt_utf8<wchar_t> 进行转换
		std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
		// 将 UTF-8 字符串转换为 std::wstring
		return converter.from_bytes(utf8str);
	}
	catch (const std::range_error& e) {
		// 如果转换失败（例如输入不是有效的 UTF-8），抛出异常
		throw std::runtime_error("Failed to convert UTF-8 to UCS-4: " + std::string(e.what()));
	}
}




static std::string Ucs4ConvertToUtf8(const std::wstring& wstr) {
	try {
		// 创建 std::wstring_convert 对象，使用 std::codecvt_utf8<wchar_t> 进行转换
		std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
		// 将 std::wstring 转换为 UTF-8 编码的 std::string
		return converter.to_bytes(wstr);
	}
	catch (const std::range_error& e) {
		// 如果转换失败（例如输入包含无效的宽字符），抛出异常
		throw std::runtime_error("Failed to convert UCS-4 to UTF-8: " + std::string(e.what()));
	}
}



static std::wstring U16StringToWString(const std::u16string& u16str)
{
	std::wstring wstr;

#ifdef _WIN32
	// Windows 平台：wchar_t 是 2 字节（UTF-16），直接拷贝
	wstr.assign(u16str.begin(), u16str.end());
#else
	// Linux 平台：wchar_t 是 4 字节（UTF-32），需要转换
	std::wstring_convert<std::codecvt_utf16<wchar_t, 0x10ffff, std::little_endian>> converter;
	wstr = converter.from_bytes(
		reinterpret_cast<const char*>(u16str.data()),
		reinterpret_cast<const char*>(u16str.data() + u16str.size())
	);
#endif

	return wstr;
}


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
		sWritedThreads = std::thread(&LightLogWrite_Impl::RunWriteThread, this);
	}

	~LightLogWrite_Impl() {
		CloseLogStream();
	}

	void SetLogsFileName(const std::wstring& sFilename) {
		std::lock_guard<std::mutex> sWriteLock(pLogWriteMutex);
		if (pLogFileStream.is_open()) pLogFileStream.close();
		ChecksDirectory(sFilename);//确保目录存在
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
	void SetLogsLasting(const std::wstring& sFilePath, const std::wstring& sBaseName) {
		sLogLastingDir = sFilePath;
		sLogsBasedName = sBaseName;
		bHasLogLasting = true;
		CreateLogsFile();

	}

	void SetLogsLasting(const std::u16string& sFilePath, const std::u16string& sBaseName) {
		SetLogsLasting(U16StringToWString(sFilePath), U16StringToWString(sBaseName));
	}
	void SetLogsLasting(const std::string & sFilePath, const std::string & sBaseName) {
		SetLogsLasting(Utf8ConvertsToUcs4(sFilePath), Utf8ConvertsToUcs4(sBaseName));
	}

	void CreateLogsFile() 
	{

		std::wstring  sOutFileName = BuildLogFileOut();
		std::lock_guard<std::mutex> sLock(pLogWriteMutex);		
		ChecksDirectory(sOutFileName);
		pLogFileStream.close();	//关闭之前提交的文件流
		pLogFileStream.open(sOutFileName, std::ios::app);

	}


	std::wstring BuildLogFileOut() const {
		std::tm                 sTmPartsInfo = GetCurrsTimerTm();
		std::wostringstream     sWosStrStream;
		sWosStrStream << std::put_time(&sTmPartsInfo, L"%Y_%m_%d") << (sTmPartsInfo.tm_hour > 12 ? L"PM" : L"AM") << L".log";

		std::filesystem::path   sLotOutPaths = sLogLastingDir;
		std::filesystem::path   sLogOutFiles = sLotOutPaths / (sLogsBasedName + sWosStrStream.str());

		return sLogOutFiles.wstring();
	}

	void WriteLogContent(const std::wstring & sTypeVal, const std::wstring & sMessage) {
		{
			std::lock_guard<std::mutex> sWriteLock(pLogWriteMutex);
			pLogWriteQueue.push({ sTypeVal, sMessage });

		}
		pWritedCondVar.notify_one();//通知线程
	}

	void WriteLogContent(const std::string & sTypeVal, const std::string & sMessage)
	{
		WriteLogContent(Utf8ConvertsToUcs4(sTypeVal), Utf8ConvertsToUcs4(sMessage));
	}

	void WriteLogContent(const std::u16string & sTypeVal, const std::u16string & sMessage) {
		WriteLogContent(U16StringToWString(sTypeVal), U16StringToWString(sMessage));
	}


	void CloseLogStream() 
	{
		bIsStopLogging = true;
		pWritedCondVar.notify_all();
		WriteLogContent(L"Stop log write thread", L"================================>");
		if (sWritedThreads.joinable()) sWritedThreads.join();//等待线程结束
	}
	

private:
	void RunWriteThread() {
		while (true) {
			if (bHasLogLasting) 
				if (bLastingTmTags != GetCurrsTimerTm().tm_hour > 12) 
					CreateLogsFile();
			LightLogWrite_Info sLogMessageInf;
			{				
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

	void ChecksDirectory(const std::wstring& sFilename) {
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

	std::wofstream                      pLogFileStream;	// 日志文件流
	std::mutex                          pLogWriteMutex;	// 日志写入锁
	std::queue<LightLogWrite_Info>      pLogWriteQueue;	// 日志消息队列
	std::condition_variable	            pWritedCondVar;	// 条件变量

	std::thread                         sWritedThreads;	// 日志处理线程
	std::atomic<bool>                   bIsStopLogging;	// 停止标志
	std::wstring                        sLogLastingDir;	// 持久化日志路径
	std::wstring                        sLogsBasedName; // 持久化日志选项
	std::atomic<bool>                   bHasLogLasting;	// 是否日志持久化输出
	std::atomic<bool>                   bLastingTmTags;	// 判断时间是上午还是下午

};



// 测试日志文件创建和写入
void TestLogFileCreation() {
	LightLogWrite_Impl logger;
	logger.SetLogsFileName(L"test_log.txt");
	logger.WriteLogContent(L"TestLogFileCreation", L"This is a test log message.");
	std::this_thread::sleep_for(std::chrono::seconds(1)); // 等待日志写入完成
	std::cout << "TestLogFileCreation: Log file created and message written.\n";
}

// 测试多线程日志写入
void TestMultiThreadLogging() {
	LightLogWrite_Impl logger;
	logger.SetLogsFileName(L"multi_thread_log.txt");

	auto logTask = [&logger](int threadId) {
		for (int i = 0; i < 5; ++i) {
			std::wstring message = L"Thread " + std::to_wstring(threadId) + L" - Log " + std::to_wstring(i);
			logger.WriteLogContent(L"TestMultiThreadLogging", message);
			std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 模拟延迟
		}
		};

	std::vector<std::thread> threads;
	for (int i = 0; i < 5; ++i) {
		threads.emplace_back(logTask, i + 1);
	}

	for (auto& t : threads) {
		t.join();
	}

	std::cout << "TestMultiThreadLogging: Log messages written from multiple threads.\n";
}

// 测试日志持久化功能
void TestLogLasting() {
	LightLogWrite_Impl logger;
	logger.SetLogsLasting(L"./logs", L"test_log_");
	logger.WriteLogContent(L"TestLogLasting", L"This is a persistent log message.");
	std::this_thread::sleep_for(std::chrono::seconds(1)); // 等待日志写入完成
	std::cout << "TestLogLasting: Persistent log file created and message written.\n";
}

// 测试日志流关闭功能
void TestCloseLogStream() {
	LightLogWrite_Impl logger;
	logger.SetLogsFileName(L"close_log.txt");
	logger.WriteLogContent(L"TestCloseLogStream", L"This log should be written.");
	logger.CloseLogStream();
	logger.WriteLogContent(L"TestCloseLogStream", L"This log should NOT be written.");
	std::cout << "TestCloseLogStream: Log stream closed.\n";
}

// 主函数，运行所有测试
int main() {
	try {
		TestLogFileCreation();
		TestMultiThreadLogging();
		TestLogLasting();
		TestCloseLogStream();
	}
	catch (const std::exception& e) {
		std::cerr << "Test failed: " << e.what() << std::endl;
	}

	return 0;
}