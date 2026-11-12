#include <csignal>
#include <atomic>
#include <deque>
#include <string>
#include <vector>
#include <cstdio>
#include "ipc.h"

static std::atomic<bool> g_running{true};
static void on_sigint(int) { g_running = false; }

static const char* color(uint8_t s) {
    switch ((Status)s) {
        case Status::Ok:       return "\033[32m";
        case Status::Warning:  return "\033[33m";
        case Status::Critical: return "\033[31m";
        default:               return "\033[35m";
    }
}

int main() {
    signal(SIGINT, on_sigint);

    // 1. Chờ hub tạo shared memory
    SharedState* st = nullptr;
    while (g_running && !st) {
        int fd = shm_open(SHM_NAME, O_RDWR, 0);
        if (fd >= 0) {
            struct stat sb;
            if (fstat(fd, &sb) == 0 && sb.st_size >= (off_t)sizeof(SharedState)) {
                void* p = mmap(nullptr, sizeof(SharedState), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
                if (p != MAP_FAILED) st = static_cast<SharedState*>(p);
            }
            close(fd);
        }
        if (!st) { puts("Waiting for hub..."); sleep(1); }
    }
    if (!st) return 0;
    while (g_running && __atomic_load_n(&st->magic, __ATOMIC_ACQUIRE) != SHM_MAGIC)
        usleep(100000);

    // 2. Mở message queue (đọc, non-blocking)
    mqd_t mq = open_alarm_queue(O_RDONLY | O_NONBLOCK);
    if (mq == (mqd_t)-1) { perror("mq_open"); return 1; }
    struct mq_attr qa;
    mq_getattr(mq, &qa);
    std::vector<char> buf(qa.mq_msgsize);

    std::deque<std::string> alarms;

    while (g_running) {
        // Chụp nhanh shared memory, giữ khóa càng ngắn càng tốt
        SensorSlot snap[(int)SensorType::Count];
        shm_lock(&st->mtx);
        memcpy(snap, st->slot, sizeof snap);
        pthread_mutex_unlock(&st->mtx);

        // Rút hết alarm đang chờ (priority cao đến trước)
        unsigned prio;
        ssize_t n;
        while ((n = mq_receive(mq, buf.data(), buf.size(), &prio)) >= (ssize_t)sizeof(AlarmMsg)) {
            AlarmMsg m;
            memcpy(&m, buf.data(), sizeof m);
            char line[128];
            snprintf(line, sizeof line, "%s%-8s %-12s value=%.2f  t=%.1fs\033[0m",
                     color(m.status), status_name((Status)m.status),
                     sensor_name((SensorType)m.type), m.value, m.timestamp_ms / 1000.0);
            alarms.push_back(line);
            if (alarms.size() > 8) alarms.pop_front();
        }

        printf("\033[H\033[J");                    // xóa màn hình
        printf("=== SENSOR HUB MONITOR (Ctrl+C to quit) ===\n\n");
        printf("%-12s %9s %9s  %-9s %7s %7s\n", "sensor", "raw", "avg", "status", "valid", "invalid");
        for (int i = 0; i < (int)SensorType::Count; ++i) {
            const SensorSlot& s = snap[i];
            printf("%-12s %9.2f %9.2f  %s%-9s\033[0m %7llu %7llu\n",
                   sensor_name((SensorType)i), s.raw, s.filtered,
                   color(s.status), status_name((Status)s.status),
                   (unsigned long long)s.valid_count, (unsigned long long)s.invalid_count);
        }
        printf("\nRecent alarms:\n");
        for (auto& a : alarms) printf("  %s\n", a.c_str());
        fflush(stdout);
        usleep(500000);
    }

    mq_close(mq);
    munmap(st, sizeof(SharedState));
    return 0;
}
