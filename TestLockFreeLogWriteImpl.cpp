#include "LightLogWriteImpl.hpp"
#include "LockFreeLogWriteImpl.hpp"

/*
 * ��ʾ����ʾ���̲߳����� LockFreeLogWriteImpl д����־��
 *
 */

 // ָ���߳�������д��־����
static const int NUM_THREADS = 4;        // �߳�����
static const int LINES_PER_THREAD = 100; // ÿ���̵߳���־����

void LogThreadFunc(int threadId, LockFreeLogWriteImpl* pLogger) {
	std::wstring tag = L"Thread_" + std::to_wstring(threadId);
	for (int i = 0; i < LINES_PER_THREAD; ++i) {
		// ����Ҫд�����־��Ϣ
		std::wstring message = L"Log entry " + std::to_wstring(i) + L" from thread "
			+ std::to_wstring(threadId);
		// д����־
		pLogger->WriteLogContent(tag, message);

		// �����ߣ�ģ��ҵ����
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

int TestLockFreeLogWriteImpl() {
	// ������־д����󣬲�������־�ļ���
	LockFreeLogWriteImpl logger(/*maxQueueSize=*/5000,
		LogQueueFullStrategy::Block,
		/*reportInterval=*/10);

	// ������־�ļ����ƣ��ɻ������·�����ļ�ǰ׺��
	logger.SetLogsFileName(L"lockfree_test_output.log");

	// �����������߳̽��в���д��־
	std::vector<std::thread> threads;
	threads.reserve(NUM_THREADS);
	for (int i = 0; i < NUM_THREADS; ++i) {
		threads.emplace_back(LogThreadFunc, i, &logger);
	}

	// �ȴ������߳����
	for (auto& t : threads) {
		t.join();
	}

	// �������ʱ����� ~LockFreeLogWriteImpl()��ȷ����־�߳������˳�
	std::wcout << L"All threads finished writing. Check 'lockfree_test_output.log' for output.\n";

	return 0;
}

