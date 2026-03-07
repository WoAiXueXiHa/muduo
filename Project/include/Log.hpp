#pragma once
#include <iostream>
#include <cstdio>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

#define INFO  0
#define DEBUG 1
#define ERR   2
#define LOG_LEVEL DEBUG

#define LOG(level, format, ...) do { \
    if(level < LOG_LEVEL) break; \
        time_t t = time(nullptr); \
        struct tm tm_info; \
        localtime_r(&t, &tm_info); \
        char time_buffer[32] = { 0 }; \
        strftime(time_buffer, sizeof(time_buffer), "%H:%M:%S", &tm_info); \
        fprintf(stdout, "[%p %s %s:%d] " format "\n", \
                (void*)pthread_self(), time_buffer, __FILE__, __LINE__, ##__VA_ARGS__); \
} while(0)

#define LOG_DEBUG(format, ...) LOG(DEBUG, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...)  LOG(INFO, format, ##__VA_ARGS__)
#define LOG_ERR(format, ...)   LOG(ERR, format, ##__VA_ARGS__)

