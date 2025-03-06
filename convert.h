#pragma once
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
#include "iconv.h"

static std::string       Ucs4ConvertToUtf8(const std::wstring& wstr);

static std::wstring      Utf8ConvertToUcs4(const std::string& str);
static std::wstring      Utf16ConvertToUcs4(const std::u16string& str);
static std::wstring      Utf16ConvertToUcs4(const std::wstring& wstr);
static std::wstring      Ucs4ConvertToUtf16(const std::wstring& wstr);
