#pragma once
#include <pthread.h>
#include <cstdio>
#include <cstdarg>
#include <ctime>

class Logger {
public:
    explicit Logger(const char* path) {
        f_ = fopen(path, "a");
        pthread_mutex_init(&m_, nullptr);
    }
    ~Logger() {
        if (f_) fclose(f_);
        pthread_mutex_destroy(&m_);
    }
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    __attribute__((format(printf, 3, 4)))
    void log(const char* level, const char* fmt, ...) {
        char msg[256];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(msg, sizeof msg, fmt, ap);
        va_end(ap);

        time_t t = time(nullptr);
        struct tm tmv;
        localtime_r(&t, &tmv);
        char ts[32];
        strftime(ts, sizeof ts, "%F %T", &tmv);

        pthread_mutex_lock(&m_);
        if (f_) { fprintf(f_, "%s [%s] %s\n", ts, level, msg); fflush(f_); }
        pthread_mutex_unlock(&m_);
    }

private:
    FILE* f_ = nullptr;
    pthread_mutex_t m_;
};
