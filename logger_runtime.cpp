#ifndef LOGGER_RUNTIME_CPP
#define LOGGER_RUNTIME_CPP


#include <cstdint>
#include <cstdio>
#include <mutex>
#include <fstream>
#include <iostream>


extern "C" void __log_instr(const char* instr, uint64_t value, const char* func, const char* bb);


static std::ofstream g_log;
static std::mutex g_mutex;


struct _InitLogger {
_InitLogger() {
g_log.open("log.txt", std::ios::out | std::ios::trunc);
if (!g_log.is_open()) {
std::cerr << "Failed to open log.txt" << std::endl;
}
}
~_InitLogger() {
if (g_log.is_open()) g_log.close();
}
} _initLogger;


extern "C" void __log_instr(const char* instr, uint64_t value, const char* func, const char* bb) {
std::lock_guard<std::mutex> lk(g_mutex);
if (g_log.is_open()) {
g_log << "[" << func << ":" << bb << "] " << instr << " val=" << value << "\n";
g_log.flush();
}
}


#endif // LOGGER_RUNTIME_CPP