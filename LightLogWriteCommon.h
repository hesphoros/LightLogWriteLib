#pragma once

// ��־д��ӿ�
struct LightLogWrite_Info {
	std::wstring                   sLogTagNameVal;//��־��ǩ
	std::wstring                   sLogContentVal;//��־������
};

enum class LogQueueFullStrategy {
	Block,      // �����ȴ�
	DropOldest  // ���������־
};
