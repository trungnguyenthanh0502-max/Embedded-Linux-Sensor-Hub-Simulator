#include <pthread.h>
#include <cstdio>

static int counter = 0;

void* worker(void* arg) {
    int id = *static_cast<int*>(arg);
    for (int i = 0; i < 100000; ++i) {
        ++counter;               // chưa có mutex: cố tình để lỗi
    }
    printf("Thread %d xong\n", id);
    return nullptr;
}

int main() {
    pthread_t t[4];
    int ids[4] = {0, 1, 2, 3};

    for (int i = 0; i < 4; ++i)
        pthread_create(&t[i], nullptr, worker, &ids[i]);
    for (int i = 0; i < 4; ++i)
        pthread_join(t[i], nullptr);

    printf("counter = %d (mong đợi 400000)\n", counter);
    return 0;
}
