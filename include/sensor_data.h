#pragma once
#include <cstdint>

enum class SensorType : uint8_t { Temperature = 0, Humidity, Pressure, Light, Count };
enum class Status : uint8_t { Ok, Warning, Critical, Invalid };

struct SensorSample {            // dữ liệu thô từ cảm biến
    SensorType type;
    double     value;
    uint64_t   timestamp_ms;
};

struct ProcessedSample {         // dữ liệu sau xử lý
    SensorType type;
    double     raw;
    double     filtered;
    Status     status;
    uint64_t   timestamp_ms;
};

inline const char* sensor_name(SensorType t) {
    switch (t) {
        case SensorType::Temperature: return "temperature";
        case SensorType::Humidity:    return "humidity";
        case SensorType::Pressure:    return "pressure";
        case SensorType::Light:       return "light";
        default:                      return "unknown";
    }
}

inline const char* status_name(Status s) {
    switch (s) {
        case Status::Ok:       return "OK";
        case Status::Warning:  return "WARNING";
        case Status::Critical: return "CRITICAL";
        case Status::Invalid:  return "INVALID";
    }
    return "?";
}
