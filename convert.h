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

std::string Ucs4ConvertToUtf8(const std::wstring& wstr);