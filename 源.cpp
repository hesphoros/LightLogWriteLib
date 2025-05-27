#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <string>
#include "LightLogWriteImpl.hpp"        // ��������ԭʼʵ��ͷ�ļ�
#include "LockFreeLogWriteImpl.hpp"     // ����������������ʵ��ͷ�ļ�

/*
 * ����Ҫ��:
 * ԭ������ʾ����Ϊ LockFreeLogWriteImpl �а��� const ���ݳ�Ա (kMaxQueueSize)��
 * ���±�������ʽɾ���˿�����ֵ�����������ģ�庯����ʹ�������� "logger = tmpLogger" �ķ�ʽ��
 * ��ᴥ������������ɾ���ĺ�����
 *
 * �����ʾ����������ģ����� logger ���ж�θ�ֵ������ֱ��ͨ�����캯���������ն���
 * �Ӷ����� ��= std::move(...)�� �Ĳ�����
 */

 // ���ò��Բ���
    static const int NUM_THREADS = 4;       // �����߳���
static const int LINES_PER_THREAD = 100000;   // ÿ���߳�д����־����

// ������Ҫ�л�ʹ�����ֲ���
static const bool USE_BLOCK_STRATEGY = false; // �� false

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

    // �������߳̽�����־д��
    std::vector<std::thread> threads;
    threads.reserve(NUM_THREADS);
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(writeTask, i);
    }

    // �ȴ������߳̽���
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
    // ���� LightLogWriteImpl ����
    double timeLightLog = MeasureLogPerformance<LightLogWrite_Impl>(
        L"LightLogWriteImpl",
        L"LightLogWriteImpl_result.log"
    );

    // ���� LockFreeLogWriteImpl ����
    double timeLockFree = MeasureLogPerformance<LockFreeLogWriteImpl>(
        L"LockFreeLogWriteImpl",
        L"LockFreeLogWriteImpl_result.log"
    );

    // ��ӡ��Ҫ�ԱȽ��
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