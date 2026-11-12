#pragma once
#include <fcntl.h>
#include <mqueue.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include "sensor_data.h"

constexpr const char* SHM_NAME  = "/sensor_hub_shm";
constexpr const char* MQ_NAME   = "/sensor_hub_alarm";
constexpr uint32_t    SHM_MAGIC = 0x53485542;

struct SensorSlot {
    double   raw;
    double   filtered;
    uint8_t  status;
    uint64_t timestamp_ms;
    uint64_t valid_count;
    uint64_t invalid_count;
};

struct SharedState {
    uint32_t        magic;       // != 0 khi hub đã khởi tạo xong
    pthread_mutex_t mtx;
    SensorSlot      slot[(int)SensorType::Count];
};

struct AlarmMsg {
    uint8_t  type;
    uint8_t  status;
    double   value;
    uint64_t timestamp_ms;
};

// Khóa mutex liên-process; xử lý trường hợp chủ khóa đã chết
inline void shm_lock(pthread_mutex_t* m) {
    if (pthread_mutex_lock(m) == EOWNERDEAD) pthread_mutex_consistent(m);
}

inline mqd_t open_alarm_queue(int flags) {
    struct mq_attr attr;
    memset(&attr, 0, sizeof attr);
    attr.mq_maxmsg  = 10;
    attr.mq_msgsize = sizeof(AlarmMsg);
    return mq_open(MQ_NAME, flags | O_CREAT, 0600, &attr);
}

// ---------- phía hub ----------
class ShmWriter {
public:
    ~ShmWriter() {
        if (st_) { munmap(st_, sizeof(SharedState)); shm_unlink(SHM_NAME); }
    }

    bool open() {
        shm_unlink(SHM_NAME);                          // xóa rác của lần chạy trước
        int fd = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0600);
        if (fd < 0) { perror("shm_open"); return false; }
        if (ftruncate(fd, sizeof(SharedState)) < 0) { perror("ftruncate"); close(fd); return false; }
        void* p = mmap(nullptr, sizeof(SharedState), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if (p == MAP_FAILED) { perror("mmap"); return false; }
        st_ = static_cast<SharedState*>(p);
        memset(st_, 0, sizeof(SharedState));

        pthread_mutexattr_t a;
        pthread_mutexattr_init(&a);
        pthread_mutexattr_setpshared(&a, PTHREAD_PROCESS_SHARED);
        pthread_mutexattr_setrobust(&a, PTHREAD_MUTEX_ROBUST);
        pthread_mutex_init(&st_->mtx, &a);
        pthread_mutexattr_destroy(&a);

        __atomic_store_n(&st_->magic, SHM_MAGIC, __ATOMIC_RELEASE);  // báo "đã sẵn sàng"
        return true;
    }

    void update(const ProcessedSample& p) {
        SensorSlot& s = st_->slot[(int)p.type];
        shm_lock(&st_->mtx);
        s.raw = p.raw;
        s.timestamp_ms = p.timestamp_ms;
        s.status = (uint8_t)p.status;
        if (p.status == Status::Invalid) {
            ++s.invalid_count;
        } else {
            s.filtered = p.filtered;
            ++s.valid_count;
        }
        pthread_mutex_unlock(&st_->mtx);
    }

    // Chụp nhanh 4 slot (dùng cho TCP server), giữ khóa càng ngắn càng tốt
    void snapshot(SensorSlot* out) {
        shm_lock(&st_->mtx);
        memcpy(out, st_->slot, sizeof(SensorSlot) * (int)SensorType::Count);
        pthread_mutex_unlock(&st_->mtx);
    }

private:
    SharedState* st_ = nullptr;
};

class AlarmSender {
public:
    ~AlarmSender() {
        if (mq_ != (mqd_t)-1) { mq_close(mq_); mq_unlink(MQ_NAME); }
    }

    bool open() {
        mq_unlink(MQ_NAME);                            // bỏ tin nhắn cũ
        mq_ = open_alarm_queue(O_WRONLY | O_NONBLOCK);
        if (mq_ == (mqd_t)-1) { perror("mq_open"); return false; }
        return true;
    }

    void send(const ProcessedSample& p) {
        AlarmMsg m{(uint8_t)p.type, (uint8_t)p.status,
                   p.status == Status::Invalid ? p.raw : p.filtered, p.timestamp_ms};
        unsigned prio = (p.status == Status::Critical) ? 2 : 1;
        // Queue đầy (monitor chưa chạy/chậm) -> bỏ qua, không làm kẹt hub
        mq_send(mq_, reinterpret_cast<const char*>(&m), sizeof m, prio);
    }

private:
    mqd_t mq_ = (mqd_t)-1;
};
