#pragma once

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
#include "convert_tools.h"

// ��־д��ӿ�
struct LightLogWriteInfo {
    std::wstring                   sLogTagNameVal;//��־��ǩ
    std::wstring                   sLogContentVal;//��־������
};

enum class eLogQueueFullStrategy {
    Block,      // �����ȴ�
    DropOldest  // ���������־
};


/**
 * @brief Structure to track construction/destruction counts for debugging.
 */
template<typename T>
class LockFreeQueue
{
private:
    /**
     * @brief Node counter (Hungarian notation: stNodeCounter).
     *        Uses a bit field to store reference counts:
     *        - iInternalCount: number of internal references
     *        - iExternalCounters: number of external references
     */
    struct stNodeCounter
    {
        unsigned iInternalCount : 30;      ///< Internal reference count
        unsigned iExternalCounters : 2;    ///< External reference count
    };

    /**
     * @brief Forward declaration of stNode.
     */
    struct stNode;

    /**
     * @brief Holds a pointer to a stNode and an external reference count (Hungarian notation: stCountedNodePtr).
     */
    struct stCountedNodePtr
    {
        stCountedNodePtr() : iExternalCount(0), pPtr(nullptr) {}
        int      iExternalCount;  ///< External reference count
        stNode* pPtr;            ///< Pointer to a node
    };

    /**
     * @brief The actual node structure (Hungarian notation: stNode).
     *        Contains: data pointer, go-to-next pointer, and the node counter.
     */
    struct stNode
    {
        std::atomic<T*>          atomic_pData;    ///< Pointer to the stored data (atomic)
        std::atomic<stNodeCounter> atomic_stCount; ///< Internal and external reference counters
        std::atomic<stCountedNodePtr> atomic_stNext;///< Pointer to next node with external count

        /**
         * @brief Constructor initializes the node's counters and sets next to a null pointer.
         * @param iExternalCount Initial external reference count (default 2 for safety).
         */
        stNode(int iExternalCount = 2)
        {
            stNodeCounter stNewCounter;
            stNewCounter.iInternalCount = 0;
            stNewCounter.iExternalCounters = iExternalCount;
            atomic_stCount.store(stNewCounter);

            stCountedNodePtr stNodePtr;
            stNodePtr.pPtr = nullptr;
            stNodePtr.iExternalCount = 0;
            atomic_stNext.store(stNodePtr);

            atomic_pData.store(nullptr);
        }

        /**
         * @brief Release an internal reference to this node. If zero references remain,
         *        deletes this node.
         */
        void ReleaseRef()
        {
            std::cout << "call ReleaseRef" << std::endl;
            stNodeCounter stOldCounter =
                atomic_stCount.load(std::memory_order_relaxed);
            stNodeCounter stNewCounter;
            do
            {
                stNewCounter = stOldCounter;
                --stNewCounter.iInternalCount;
            } while (!atomic_stCount.compare_exchange_strong(
                stOldCounter, stNewCounter,
                std::memory_order_acquire, std::memory_order_relaxed));

            if (!stNewCounter.iInternalCount && !stNewCounter.iExternalCounters)
            {
                delete this;
                std::cout << "ReleaseRef delete success" << std::endl;
                LockFreeQueue<T>::atomic_iDestructCount.fetch_add(1);
            }
        }
    };

    std::atomic<stCountedNodePtr> atomic_stHead; ///< Head pointer of the queue
    std::atomic<stCountedNodePtr> atomic_stTail; ///< Tail pointer of the queue

    /**
     * @brief Helper function to set a new tail node.
     * @param stOldTail The old tail node pointer.
     * @param stNewTail The new tail node pointer.
     */
    void SetNewTail(stCountedNodePtr& stOldTail,
        stCountedNodePtr const& stNewTail)
    {
        stNode* const pCurrentTailPtr = stOldTail.pPtr;
        while (!atomic_stTail.compare_exchange_weak(stOldTail, stNewTail) &&
            stOldTail.pPtr == pCurrentTailPtr);

        if (stOldTail.pPtr == pCurrentTailPtr)
            FreeExternalCounter(stOldTail);
        else
            pCurrentTailPtr->ReleaseRef();
    }

    /**
     * @brief Free an external counter from a node pointer and adjust internal counters if needed.
     * @param stOldNodePtr The node pointer whose external count should be freed.
     */
    static void FreeExternalCounter(stCountedNodePtr& stOldNodePtr)
    {
        std::cout << "call FreeExternalCounter" << std::endl;
        stNode* const pPtr = stOldNodePtr.pPtr;
        int const iCountIncrease = stOldNodePtr.iExternalCount - 2;
        stNodeCounter stOldCounter =
            pPtr->atomic_stCount.load(std::memory_order_relaxed);
        stNodeCounter stNewCounter;

        do
        {
            stNewCounter = stOldCounter;
            --stNewCounter.iExternalCounters;
            stNewCounter.iInternalCount += iCountIncrease;
        } while (!pPtr->atomic_stCount.compare_exchange_strong(
            stOldCounter, stNewCounter,
            std::memory_order_acquire, std::memory_order_relaxed));

        if (!stNewCounter.iInternalCount && !stNewCounter.iExternalCounters)
        {
            LockFreeQueue<T>::atomic_iDestructCount.fetch_add(1);
            std::cout << "FreeExternalCounter delete success" << std::endl;
            delete pPtr;
        }
    }

    /**
     * @brief Increase the external counter for a node.
     * @param atomicCounter The atomic node pointer to be incremented.
     * @param stOldCounter The old counter snapshot to be updated.
     */
    static void IncreaseExternalCount(
        std::atomic<stCountedNodePtr>& atomicCounter,
        stCountedNodePtr& stOldCounter)
    {
        stCountedNodePtr stNewCounter;
        do
        {
            stNewCounter = stOldCounter;
            ++stNewCounter.iExternalCount;
        } while (!atomicCounter.compare_exchange_strong(
            stOldCounter, stNewCounter,
            std::memory_order_acquire, std::memory_order_relaxed));

        stOldCounter.iExternalCount = stNewCounter.iExternalCount;
    }

public:
    /**
     * @brief Constructor for LockFreeQueue.
     *        Initializes the head and tail with a dummy node.
     */
    LockFreeQueue()
    {
        stCountedNodePtr stNewNext;
        stNewNext.pPtr = new stNode();
        stNewNext.iExternalCount = 1;

        atomic_stTail.store(stNewNext);
        atomic_stHead.store(stNewNext);

        std::cout << "stNewNext.pPtr is " << stNewNext.pPtr << std::endl;
    }

    /**
     * @brief Destructor that pops elements until empty and deletes dummy node.
     */
    ~LockFreeQueue()
    {
        while (Pop());
        auto stHeadCountedNode = atomic_stHead.load();
        delete stHeadCountedNode.pPtr;
    }

    /**
     * @brief Push a new value into the lock-free queue.
     * @param tNewValue The value to be pushed.
     */
    void Push(T tNewValue)
    {
        std::unique_ptr<T> uptrNewData(new T(tNewValue));

        stCountedNodePtr stNewNext;
        stNewNext.pPtr = new stNode;
        stNewNext.iExternalCount = 1;

        stCountedNodePtr stOldTail = atomic_stTail.load();
        for (;;)
        {
            IncreaseExternalCount(atomic_stTail, stOldTail);

            T* pOldData = nullptr;
            if (stOldTail.pPtr->atomic_pData.compare_exchange_strong(
                pOldData, uptrNewData.get()))
            {
                stCountedNodePtr stOldNext;
                stCountedNodePtr stNowNext = stOldTail.pPtr->atomic_stNext.load();

                if (!stOldTail.pPtr->atomic_stNext.compare_exchange_strong(
                    stOldNext, stNewNext))
                {
                    delete stNewNext.pPtr;
                    stNewNext = stOldNext;
                }
                SetNewTail(stOldTail, stNewNext);
                uptrNewData.release();
                break;
            }
            else
            {
                stCountedNodePtr stOldNext;
                if (stOldTail.pPtr->atomic_stNext.compare_exchange_strong(
                    stOldNext, stNewNext))
                {
                    stOldNext = stNewNext;
                    stNewNext.pPtr = new stNode;
                }
                SetNewTail(stOldTail, stOldNext);
            }
        }

        LockFreeQueue<T>::atomic_iConstructCount++;
    }

    /**
     * @brief Pop a value from the lock-free queue.
     * @return A std::unique_ptr to the value. If queue empty, returns nullptr.
     */
    std::unique_ptr<T> Pop()
    {
        stCountedNodePtr stOldHead =
            atomic_stHead.load(std::memory_order_relaxed);

        for (;;)
        {
            IncreaseExternalCount(atomic_stHead, stOldHead);

            stNode* const pPtr = stOldHead.pPtr;
            if (pPtr == atomic_stTail.load().pPtr)
            {
                pPtr->ReleaseRef();
                return std::unique_ptr<T>();
            }

            stCountedNodePtr stNext = pPtr->atomic_stNext.load();
            if (atomic_stHead.compare_exchange_strong(stOldHead, stNext))
            {
                T* const pRes = pPtr->atomic_pData.exchange(nullptr);
                FreeExternalCounter(stOldHead);
                return std::unique_ptr<T>(pRes);
            }
            pPtr->ReleaseRef();
        }
    }

    /**
     * @brief Static counters for debugging the number of constructed and destructed nodes.
     */
    static std::atomic<int> atomic_iDestructCount;   ///< Tracks node destruction count
    static std::atomic<int> atomic_iConstructCount;  ///< Tracks node construction count
};


template<typename T>
std::atomic<int> LockFreeQueue<T>::atomic_iDestructCount = 0;

template<typename T>
std::atomic<int> LockFreeQueue<T>::atomic_iConstructCount = 0;



/**
 * @brief ʹ��LockFreeQueue�滻ԭ�е���+�����������С�
 *        ͨ�������ԭ�Ӽ���������������������֧�ֲ�ͬ�������в��ԡ�
 */
class LockFreeLogWriteImpl {
public:
    LockFreeLogWriteImpl(size_t maxQueueSize = 5000,
        eLogQueueFullStrategy strategy = eLogQueueFullStrategy::Block,
        size_t reportInterval = 100)
        : kMaxQueueSize(maxQueueSize),
        discardCount(0),
        lastReportedDiscardCount(0),
        bIsStopLogging(false),
        queueFullStrategy(strategy),
        reportInterval(reportInterval),
        bHasLogLasting(false),
        stQueueApproxSize(0)
    {
        sWritedThreads = std::thread(&LockFreeLogWriteImpl::RunWriteThread, this);
    }

    ~LockFreeLogWriteImpl() {
        CloseLogStream();
    }

    void SetLogsFileName(const std::wstring& sFilename) {
        std::lock_guard<std::mutex> sWriteLock(pLogWriteMutex);
        if (pLogFileStream.is_open()) {
            pLogFileStream.close();
        }
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

    /**
     * @brief д��־���ݣ����������Խ��д���
     */
    void WriteLogContent(const std::wstring& sTypeVal, const std::wstring& sMessage) {
        if (bIsStopLogging) {
            return;
        }

        bool localNeedReport = false;
        size_t currentDiscard = 0;

        // �򵥵ظ��ݶ��е�ǰ��С�ж�����
        if (queueFullStrategy == eLogQueueFullStrategy::Block) {
            // �����ȴ�ֱ���������пռ��ֹͣ��־����
            while (stQueueApproxSize.load(std::memory_order_relaxed) >= kMaxQueueSize && !bIsStopLogging) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if (bIsStopLogging) {
                return;
            }
            stLogWriteQueue.Push({ sTypeVal, sMessage });
            stQueueApproxSize.fetch_add(1, std::memory_order_relaxed);
        }
        else if (queueFullStrategy == eLogQueueFullStrategy::DropOldest) {
            // �������������������ɵ�һ��������
            if (stQueueApproxSize.load(std::memory_order_relaxed) >= kMaxQueueSize) {
                auto oldItem = stLogWriteQueue.Pop();
                if (oldItem) {
                    stQueueApproxSize.fetch_sub(1, std::memory_order_relaxed);
                }
                discardCount++;
                if (discardCount.load() - lastReportedDiscardCount.load() >= reportInterval) {
                    localNeedReport = true;
                    currentDiscard = discardCount.load();
                    lastReportedDiscardCount.store(discardCount.load());
                }
            }
            stLogWriteQueue.Push({ sTypeVal, sMessage });
            stQueueApproxSize.fetch_add(1, std::memory_order_relaxed);
        }

        // �����Ҫ���涪����־���������ֹ�ݹ飩
        static thread_local bool inErrorReport = false;
        if (localNeedReport && !inErrorReport) {
            inErrorReport = true;
            // �ݹ���ñ�����
            WriteLogContent(L"LOG_OVERFLOW",
                L"The log queue overflows and has been discarded " + std::to_wstring(currentDiscard) + L" logs");
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
        return discardCount.load();
    }

    void ResetDiscardCount() {
        discardCount.store(0);
        lastReportedDiscardCount.store(0);
    }

private:
    void CloseLogStream() {
        bIsStopLogging = true;
        // ����ȴ��߳��˳�
        if (sWritedThreads.joinable()) {
            sWritedThreads.join();
        }
    }

    void CreateLogsFile() {
        std::wstring  sOutFileName = BuildLogFileOut();
        std::lock_guard<std::mutex> sLock(pLogWriteMutex);
        ChecksDirectory(sOutFileName);
        pLogFileStream.close();
        pLogFileStream.open(sOutFileName, std::ios::app);
    }

    /**
     * @brief �̺߳����������������в��ϵ�����־��д���ļ���
     */
    void RunWriteThread() {
        while (true) {
            // ���ֹͣ��־Ϊ true �Ҷ���Ϊ�գ����˳��߳�
            if (bIsStopLogging && stQueueApproxSize.load(std::memory_order_relaxed) == 0) {
                break;
            }

            if (bHasLogLasting) {
                // �ļ���AM/PM�л��ж�
                if (bLastingTmTags != (GetCurrsTimerTm().tm_hour > 12)) {
                    CreateLogsFile();
                }
            }

            // �������е���
            auto itemPtr = stLogWriteQueue.Pop();
            if (!itemPtr) {
                // ���п�������sleep������æ��
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            stQueueApproxSize.fetch_sub(1, std::memory_order_relaxed);

            std::cerr << "pop: " << Ucs4ConvertToUtf8(itemPtr->sLogContentVal) << std::endl;

            if (!itemPtr->sLogContentVal.empty() && pLogFileStream.is_open()) {
                pLogFileStream << itemPtr->sLogTagNameVal << L"-//>>>"
                    << GetCurrentTimer() << " : "
                    << itemPtr->sLogContentVal << std::endl;
            }
        }

        pLogFileStream.close();
        std::cerr << "Log write thread Exit\n";
    }

    std::wstring BuildLogFileOut() {
        std::tm sTmPartsInfo = GetCurrsTimerTm();
        std::wostringstream sWosStrStream;
        sWosStrStream << std::put_time(&sTmPartsInfo, L"%Y_%m_%d")
            << (sTmPartsInfo.tm_hour >= 12 ? L"_AM" : L"_PM") << L".log";

        bLastingTmTags = (sTmPartsInfo.tm_hour > 12);

        std::filesystem::path sLotOutPaths = sLogLastingDir;
        std::filesystem::path sLogOutFiles = sLotOutPaths / (sLogsBasedName + sWosStrStream.str());
        return sLogOutFiles.wstring();
    }

    void ChecksDirectory(const std::wstring& sFilename) {
        std::filesystem::path sFullFileName(sFilename);
        std::filesystem::path sOutFilesPath = sFullFileName.parent_path();
        if (!sOutFilesPath.empty() && !std::filesystem::exists(sOutFilesPath)) {
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
        localtime_r(&sCurrTmDatas, &sCurrTimerTm);
#endif
        return sCurrTmDatas;
    }

    // ���ߺ���ʾ����UTF8 <-> wide-string ת������������������ʵ�֣�
    static std::wstring Utf8ConvertsToUcs4(const std::string& input) {
        // TODO: implement actual conversion
        std::wstring tmp(input.begin(), input.end());
        return tmp;
    }

    static std::wstring U16StringToWString(const std::u16string& input) {
        // TODO: implement actual conversion
        std::wstring tmp(input.begin(), input.end());
        return tmp;
    }

    static std::string Ucs4ConvertToUtf8(const std::wstring& input) {
        // TODO: implement actual conversion
        std::string tmp(input.begin(), input.end());
        return tmp;
    }

private:
    // ---- ��־�ļ������������ ----
    std::wofstream  pLogFileStream;    // ��־�ļ���
    std::mutex      pLogWriteMutex;    // ͬ���ļ�������

    // ---- �滻����Ϊ�������� ----
    LockFreeQueue<LightLogWriteInfo> stLogWriteQueue;  // ʹ��LockFreeQueue
    std::atomic<size_t>              stQueueApproxSize; // ���ƶ��д�С��������������

    // ---- ��־�����߳������ ----
    std::thread      sWritedThreads;   // ��־�����߳�
    std::atomic<bool> bIsStopLogging;  // �رձ��

    // ---- �־û����� ----
    std::wstring sLogLastingDir;  // �־û���־·��
    std::wstring sLogsBasedName;  // �־û���־�ļ�ǰ׺
    std::atomic<bool> bHasLogLasting;
    std::atomic<bool> bLastingTmTags; // ��ӦAM/PM

    // ---- ������������� ----
    const size_t              kMaxQueueSize;   // �����г���
    eLogQueueFullStrategy     queueFullStrategy;
    std::atomic<size_t>       discardCount;           // ��������
    std::atomic<size_t>       lastReportedDiscardCount; // �ϴα��涪����
    std::atomic<size_t>       reportInterval;          // ������ֵ
};