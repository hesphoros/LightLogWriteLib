#include "LightLogWriteImpl.h"
#include "UniConv.h"
#include "pch.h"


LightLogWrite_Impl::LightLogWrite_Impl(size_t maxQueueSize, LogQueueOverflowStrategy strategy, size_t reportInterval)
  :	kMaxQueueSize(maxQueueSize),
	discardCount(0),
	lastReportedDiscardCount(0),
	bIsStopLogging{ false },
	queueFullStrategy(strategy),
	reportInterval(reportInterval),
	bHasLogLasting{ false } 
{
	sWrittenThreads = std::thread(&LightLogWrite_Impl::RunWriteThread, this);
}

LightLogWrite_Impl::~LightLogWrite_Impl()
{
	CloseLogStream();
}

void LightLogWrite_Impl::SetLogsFileName(const std::wstring& sFilename)
{
	std::lock_guard<std::mutex> sWriteLock(pLogWriteMutex);
	if (pLogFileStream.is_open())
		pLogFileStream.close();
	ChecksDirectory(sFilename); 
	pLogFileStream.open(sFilename, std::ios::app);
}

void LightLogWrite_Impl::SetLogsFileName(const std::string& sFilename)
{
	std::wstring wFilename = UniConv::GetInstance()->LocaleToWideString(sFilename);
	//Utf8ConvertsToUcs4(sFilename)
	SetLogsFileName(wFilename);
}

void LightLogWrite_Impl::SetLogsFileName(const std::u16string& sFilename)
{
	std::wstring wFilename = UniConv::GetInstance()->U16StringToWString(sFilename);
	SetLogsFileName(wFilename);
}

void LightLogWrite_Impl::SetLastingsLogs(const std::wstring& sFilePath, const std::wstring& sBaseName)
{
	sLogLastingDir = sFilePath;
	sLogsBasedName = sBaseName;
	bHasLogLasting = true;
	CreateLogsFile();
}

void LightLogWrite_Impl::SetLastingsLogs(const std::u16string& sFilePath, const std::u16string& sBaseName)
{
	SetLastingsLogs(UniConv::GetInstance()->U16StringToWString(sFilePath), UniConv::GetInstance()->U16StringToWString(sBaseName));
}

void LightLogWrite_Impl::SetLastingsLogs(const std::string& sFilePath, const std::string& sBaseName)
{
	SetLastingsLogs(UniConv::GetInstance()->LocaleToWideString(sFilePath), UniConv::GetInstance()->LocaleToWideString(sBaseName));
}

void LightLogWrite_Impl::WriteLogContent(const std::wstring& sTypeVal, const std::wstring& sMessage)
{
	bool bNeedReport = false;
	size_t currentDiscard = 0;
	static thread_local bool inErrorReport = false;

	if (queueFullStrategy == LogQueueOverflowStrategy::Block) {
		std::unique_lock<std::mutex> sWriteLock(pLogWriteMutex);
		// std::wcerr << L"[WriteLogContent] Try push (Block), queue size: " <<
		// pLogWriteQueue.size() << std::endl;
		pWrittenCondVar.wait(sWriteLock, [this] { return pLogWriteQueue.size() < kMaxQueueSize; });
		pLogWriteQueue.push({ sTypeVal, sMessage });
		// std::wcerr << L"[WriteLogContent] Pushed (Block), queue size: " <<
		// pLogWriteQueue.size() << std::endl;
	}
	else if ( queueFullStrategy == LogQueueOverflowStrategy::DropOldest ) {
		std::lock_guard<std::mutex> sWriteLock(pLogWriteMutex);
		// std::wcerr << L"[WriteLogContent] Try push (DropOldest), queue size: " <<
		// pLogWriteQueue.size() << std::endl;
		if (pLogWriteQueue.size() >= kMaxQueueSize) {
			// std::wcerr << L"[WriteLogContent] Drop oldest, queue full: " <<
			// pLogWriteQueue.size() << std::endl;
			pLogWriteQueue.pop();
			++discardCount;
			if (discardCount - lastReportedDiscardCount >= reportInterval) {
				bNeedReport = true;
				currentDiscard = discardCount;
				lastReportedDiscardCount.store(discardCount.load());
			}
		}
		pLogWriteQueue.push({ sTypeVal, sMessage });
		// std::wcout << L"[WriteLogContent] Pushed (DropOldest), queue size: " <<
		// pLogWriteQueue.size() << std::endl;
	}
	pWrittenCondVar.notify_one();
	if (bNeedReport && !inErrorReport) {
		inErrorReport = true;
		std::wstring overflowMsg = L"The log queue overflows and has been discarded " + std::to_wstring(currentDiscard) + L" logs";
		// std::wcerr << L"[WriteLogContent] Report overflow: " << overflowMsg << std::endl;
		WriteLogContent(L"LOG_OVERFLOW", overflowMsg);
		inErrorReport = false;
	}
}

void LightLogWrite_Impl::WriteLogContent(const std::string& sTypeVal, const std::string& sMessage)
{
	WriteLogContent(UniConv::GetInstance()->LocaleToWideString(sTypeVal), UniConv::GetInstance()->LocaleToWideString(sMessage));
}

void LightLogWrite_Impl::WriteLogContent(const std::u16string& sTypeVal, const std::u16string& sMessage)
{
	WriteLogContent(UniConv::GetInstance()->U16StringToWString(sTypeVal), UniConv::GetInstance()->U16StringToWString(sMessage));
}

size_t LightLogWrite_Impl::GetDiscardCount() const
{
	return discardCount;
}

void LightLogWrite_Impl::ResetDiscardCount()
{
	discardCount = 0;
}

std::wstring LightLogWrite_Impl::BuildLogFileOut()
{
	std::tm sTmPartsInfo = GetCurrsTimerTm();
	std::wostringstream sWosStrStream;

	sWosStrStream << std::put_time(&sTmPartsInfo, L"%Y_%m_%d") << (sTmPartsInfo.tm_hour >= 12 ? L"_AM" : L"_PM") << L".log";

	bLastingTmTags = (sTmPartsInfo.tm_hour > 12);

	std::filesystem::path sLotOutPaths = sLogLastingDir;
	std::filesystem::path sLogOutFiles = sLotOutPaths / (sLogsBasedName + sWosStrStream.str());

	return sLogOutFiles.wstring();
}

void LightLogWrite_Impl::CloseLogStream()
{
	WriteLogContent(L"<================================              Stop log write thread    ", L"================================>");
	bIsStopLogging = true;
	pWrittenCondVar.notify_all();
	
	if (sWrittenThreads.joinable())
		sWrittenThreads.join();
}

void LightLogWrite_Impl::CreateLogsFile()
{
	std::wstring sOutFileName = BuildLogFileOut();
	std::lock_guard<std::mutex> sLock(pLogWriteMutex);
	ChecksDirectory(sOutFileName);
	pLogFileStream.close(); 
	pLogFileStream.open(sOutFileName, std::ios::app);
	pLogFileStream.imbue(std::locale(std::locale(), new std::codecvt_utf8_utf16<wchar_t>));
}

void LightLogWrite_Impl::RunWriteThread()
{
	while (true) {
		if (bHasLogLasting)
			if (bLastingTmTags != (GetCurrsTimerTm().tm_hour > 12))
				CreateLogsFile();
		LightLogWriteInfo sLogMessageInf;

		{
			auto sLock = std::unique_lock<std::mutex>(pLogWriteMutex);
			pWrittenCondVar.wait(sLock, [this]
				{ return !pLogWriteQueue.empty() || bIsStopLogging; });

			if (bIsStopLogging && pLogWriteQueue.empty())
				break; 
			if (!pLogWriteQueue.empty()) {
				sLogMessageInf = pLogWriteQueue.front();
				pLogWriteQueue.pop();
				pWrittenCondVar.notify_one();
			}
		}
		if (!sLogMessageInf.sLogContentVal.empty() && pLogFileStream.is_open()) {
			pLogFileStream << sLogMessageInf.sLogTagNameVal << L"-//>>>" << GetCurrentTimer() << " : " << sLogMessageInf.sLogContentVal << "\n";
		}
	}
	pLogFileStream.close();
	std::cerr << "Log write thread Exit\n";
}

void LightLogWrite_Impl::ChecksDirectory(const std::wstring& sFilename)
{
	std::filesystem::path sFullFileName(sFilename);
	std::filesystem::path sOutFilesPath = sFullFileName.parent_path();
	if (!sOutFilesPath.empty() && !std::filesystem::exists(sOutFilesPath))
	{
		std::filesystem::create_directories(sOutFilesPath);
	}
}

std::wstring LightLogWrite_Impl::GetCurrentTimer() const
{
	std::tm sTmPartsInfo = GetCurrsTimerTm();
	std::wostringstream sWosStrStream;
	sWosStrStream << std::put_time(&sTmPartsInfo, L"%Y-%m-%d %H:%M:%S");
	return sWosStrStream.str();
}


std::tm LightLogWrite_Impl::GetCurrsTimerTm() const
{
	auto sCurrentTime = std::chrono::system_clock::now();
	std::time_t sCurrTimerTm = std::chrono::system_clock::to_time_t(sCurrentTime);
	std::tm sCurrTmDatas;
#ifdef _WIN32
	localtime_s(&sCurrTmDatas, &sCurrTimerTm);
#else
	localtime_r(&sCurrTmDatas, &sCurrTimerTm);
#endif
	return sCurrTmDatas;

}
