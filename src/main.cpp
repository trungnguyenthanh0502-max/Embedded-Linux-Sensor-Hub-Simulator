#include <pthread.h>
#include <unistd.h>
#include <csignal>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <random>
#include "sample_queue.h"
#include "logger.h"
#include "processor.h"
#include "ipc.h"
#include "tcp_server.h"

static SampleQueue       g_queue;
static Logger            g_log("sensor_hub.log");
static std::atomic<bool> g_running{true};
static ShmWriter         g_shm;
static AlarmSender       g_alarm;

static uint64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static void on_signal(int) { g_running = false; }

struct SensorCfg {
    SensorType type;
    double min, max;      // dải sinh dữ liệu bình thường
    unsigned period_ms;
};

// Producer: mỗi cảm biến một thread
static void* sensor_thread(void* arg) {
    auto* cfg = static_cast<SensorCfg*>(arg);
    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<double> dist(cfg->min, cfg->max);
    std::uniform_real_distribution<double> chance(0.0, 1.0);

    while (g_running) {
        double v = dist(rng);
        if (chance(rng) < 0.03) v = 9999.0;   // 3% giả lập cảm biến lỗi
        g_queue.push({cfg->type, v, now_ms()});
        usleep(cfg->period_ms * 1000);
    }
    return nullptr;
}

// Consumer: validate -> lọc -> ngưỡng -> shared memory -> alarm -> log
static void* processing_thread(void*) {
    Processor proc;
    Status last[(int)SensorType::Count] = {};      // tất cả = Ok
    SensorSample s;

    while (g_queue.pop(s)) {
        ProcessedSample p = proc.process(s);
        const char* name = sensor_name(p.type);

        g_shm.update(p);                           // cập nhật trạng thái mới nhất

        Status& prev = last[(int)p.type];
        if (p.status != prev) {                    // chỉ báo khi trạng thái đổi
            prev = p.status;
            if (p.status != Status::Ok) g_alarm.send(p);
        }

        if (p.status == Status::Invalid)
            g_log.log("ERROR", "%s invalid reading %.2f", name, p.raw);
        else if (p.status == Status::Critical)
            g_log.log("CRIT", "%s avg=%.2f", name, p.filtered);
        else if (p.status == Status::Warning)
            g_log.log("WARN", "%s avg=%.2f", name, p.filtered);
    }
    return nullptr;
}

// TCP server: gửi trạng thái mới nhất cho các client mỗi giây
static void* tcp_thread(void*) {
    run_tcp_server(5555, g_running, g_shm);
    return nullptr;
}

int main() {
    struct sigaction sa{};
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, nullptr);              // Ctrl+C
    sigaction(SIGTERM, &sa, nullptr);              // kill <pid>

    g_log.log("INFO", "sensor hub started");

    if (!g_shm.open() || !g_alarm.open()) {
        g_log.log("ERROR", "failed to initialize IPC");
        return 1;
    }

    SensorCfg cfgs[4] = {
        {SensorType::Temperature,  15,   45,  500},
        {SensorType::Humidity,     20,   90,  700},
        {SensorType::Pressure,    950, 1050, 1000},
        {SensorType::Light,         0, 1000,  300},
    };

    pthread_t sensors[4], proc, tcp;
    for (int i = 0; i < 4; ++i)
        pthread_create(&sensors[i], nullptr, sensor_thread, &cfgs[i]);
    pthread_create(&proc, nullptr, processing_thread, nullptr);
    pthread_create(&tcp,  nullptr, tcp_thread, nullptr);

    while (g_running) sleep(1);                    // chờ Ctrl+C / SIGTERM

    for (auto& t : sensors) pthread_join(t, nullptr);
    pthread_join(tcp, nullptr);                    // thread TCP thoát khi g_running = false
    g_queue.stop();                                // báo consumer: hết dữ liệu
    pthread_join(proc, nullptr);

    g_log.log("INFO", "sensor hub stopped");
    return 0;                                      // destructor dọn shm + mq
}
