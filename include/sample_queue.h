#pragma once
#include <pthread.h>
#include <queue>
#include "sensor_data.h"

class SampleQueue {
public:
    SampleQueue()  { pthread_mutex_init(&m_, nullptr); pthread_cond_init(&cv_, nullptr); }
    ~SampleQueue() { pthread_mutex_destroy(&m_); pthread_cond_destroy(&cv_); }
    SampleQueue(const SampleQueue&) = delete;
    SampleQueue& operator=(const SampleQueue&) = delete;

    void push(const SensorSample& s) {
        pthread_mutex_lock(&m_);
        q_.push(s);
        pthread_mutex_unlock(&m_);
        pthread_cond_signal(&cv_);
    }

    // Chờ có dữ liệu. Trả false khi đã stop và queue rỗng.
    bool pop(SensorSample& out) {
        pthread_mutex_lock(&m_);
        while (q_.empty() && !stop_) pthread_cond_wait(&cv_, &m_);
        if (q_.empty()) { pthread_mutex_unlock(&m_); return false; }
        out = q_.front();
        q_.pop();
        pthread_mutex_unlock(&m_);
        return true;
    }

    void stop() {
        pthread_mutex_lock(&m_);
        stop_ = true;
        pthread_mutex_unlock(&m_);
        pthread_cond_broadcast(&cv_);
    }

private:
    std::queue<SensorSample> q_;
    pthread_mutex_t m_;
    pthread_cond_t  cv_;
    bool stop_ = false;
};
