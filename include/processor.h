#pragma once
#include "sensor_data.h"

struct SensorLimits {
    double valid_min, valid_max;   // dải vật lý hợp lệ
    double warn_low,  warn_high;   // ngưỡng cảnh báo
    double crit_low,  crit_high;   // ngưỡng nguy hiểm
};

class MovingAverage {
public:
    double add(double v) {
        sum_ -= buf_[idx_];
        buf_[idx_] = v;
        sum_ += v;
        idx_ = (idx_ + 1) % N;
        if (count_ < N) ++count_;
        return sum_ / count_;
    }
private:
    static constexpr int N = 5;
    double buf_[N] = {0};
    double sum_ = 0;
    int idx_ = 0, count_ = 0;
};

class Processor {
public:
    Processor() {
        const double INF = 1e18;
        limits_[(int)SensorType::Temperature] = {-40, 125,  18,  35,   10,   42};
        limits_[(int)SensorType::Humidity]    = {  0, 100,  25,  80,   15,   90};
        limits_[(int)SensorType::Pressure]    = {300,1100, 970,1030,  955, 1045};
        limits_[(int)SensorType::Light] = {0, 1100, -INF, 800, -INF, 950};
    }

    // Trả về dữ liệu đã xử lý; status = Invalid nếu ngoài dải vật lý
    ProcessedSample process(const SensorSample& s) {
        const int i = (int)s.type;
        const SensorLimits& L = limits_[i];
        ProcessedSample out{s.type, s.value, s.value, Status::Ok, s.timestamp_ms};

        if (s.value < L.valid_min || s.value > L.valid_max) {
            out.status = Status::Invalid;      // không đưa vào bộ lọc
            return out;
        }
        out.filtered = filters_[i].add(s.value);
        if (out.filtered < L.crit_low || out.filtered > L.crit_high)
            out.status = Status::Critical;
        else if (out.filtered < L.warn_low || out.filtered > L.warn_high)
            out.status = Status::Warning;
        return out;
    }

private:
    SensorLimits  limits_[(int)SensorType::Count];
    MovingAverage filters_[(int)SensorType::Count];
};
