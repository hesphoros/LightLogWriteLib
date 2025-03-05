#include "convert.h"



std::string Ucs4ConvertToUtf8(const std::wstring& wstr) {
	std::string utf8str;
#ifdef _WIN32
	
	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
#else	
	std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
#endif

	try {
		utf8str = converter.to_bytes(wstr);
	}
	catch (const std::range_error& e) {
		std::cerr << "Conversion error: " << e.what() << std::endl;
	}

	return utf8str;
}


std::string& Ucs4ConvertToUtf82(const std::wstring& wstr) {	
	thread_local std::string utf8str;
	utf8str.clear();

#ifdef _WIN32
	// Windows สนำร UTF-16
	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
#else
	
	std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
#endif

	try {
		
		utf8str = converter.to_bytes(wstr);
	}
	catch (const std::range_error& e) {
		std::cerr << "Conversion error: " << e.what() << std::endl;
		
	return utf8str;
}
