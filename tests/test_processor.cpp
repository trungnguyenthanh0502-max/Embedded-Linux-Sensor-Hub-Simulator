#include <cmath>
#include <cstdio>
#include "processor.h"

static int g_fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fails; } } while (0)

static ProcessedSample feed(Processor& p, SensorType t, double v) {
    return p.process(SensorSample{t, v, 0});
}

int main() {
    { // 9999 phải INVALID với MỌI cảm biến (lỗi Light trước đây)
        Processor p;
        CHECK(feed(p, SensorType::Temperature, 9999).status == Status::Invalid);
        CHECK(feed(p, SensorType::Humidity,    9999).status == Status::Invalid);
        CHECK(feed(p, SensorType::Pressure,    9999).status == Status::Invalid);
        CHECK(feed(p, SensorType::Light,       9999).status == Status::Invalid);
    }
    { // giá trị bình thường
        Processor p;
        CHECK(feed(p, SensorType::Temperature, 25).status == Status::Ok);
        CHECK(feed(p, SensorType::Light,      500).status == Status::Ok);
    }
    { // ngưỡng
        Processor p1, p2;
        CHECK(feed(p1, SensorType::Temperature, 40).status == Status::Warning);
        CHECK(feed(p2, SensorType::Temperature, 50).status == Status::Critical);
    }
    { // moving average
        Processor p;
        feed(p, SensorType::Temperature, 20);
        feed(p, SensorType::Temperature, 25);
        ProcessedSample r = feed(p, SensorType::Temperature, 30);
        CHECK(std::fabs(r.filtered - 25.0) < 1e-9);
    }
    { // mẫu INVALID không làm nhiễm bộ lọc
        Processor p;
        feed(p, SensorType::Temperature, 20);
        feed(p, SensorType::Temperature, 9999);
        ProcessedSample r = feed(p, SensorType::Temperature, 30);
        CHECK(std::fabs(r.filtered - 25.0) < 1e-9);
    }

    puts(g_fails ? "test_processor: FAILED" : "test_processor: all passed");
    return g_fails ? 1 : 0;
}
