#ifndef LIGHT_LOG_WRITE_COMMON_H
#define LIGHT_LOG_WRITE_COMMON_H

// Removed #pragma execution_character_set("utf-8") as it is not supported by all compilers.
// Ensure the file is saved with UTF-8 encoding or use compiler-specific options.
#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif
#include <string>

/**
 * @file LightLogWriteCommon.h
 * @brief Common definitions and structures for LightLogWrite.
 * This file contains the definitions of structures and enums used in the LightLogWrite library.
 * It includes the LightLogWrite_Info structure for log messages and the LogQueueFullStrategy enum for handling full log queues.
 */

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

#endif // LIGHT_LOG_WRITE_COMMON_H