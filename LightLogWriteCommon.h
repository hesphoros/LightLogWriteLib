#pragma once

// 日志写入接口
struct LightLogWrite_Info {
	std::wstring                   sLogTagNameVal;//日志标签
	std::wstring                   sLogContentVal;//日志的内容
};

enum class LogQueueFullStrategy {
	Block,      // 阻塞等待
	DropOldest  // 丢弃最旧日志
};
