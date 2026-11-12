#pragma once
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>
#include "ipc.h"

// Mỗi giây gửi cho mọi client 4 dòng JSON (mỗi dòng kết thúc bằng '\n')
inline void run_tcp_server(uint16_t port, std::atomic<bool>& running, ShmWriter& shm) {
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return; }

    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // chỉ nhận kết nối từ máy này
    addr.sin_port        = htons(port);
    if (bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0 || listen(srv, 8) < 0) {
        perror("bind/listen");
        close(srv);
        return;
    }

    constexpr size_t MAX_CLIENTS = 16;
    std::vector<int> clients;
    auto last = std::chrono::steady_clock::now();

    while (running) {
        pollfd pfd{srv, POLLIN, 0};
        int r = poll(&pfd, 1, 200);                  // chờ kết nối mới tối đa 200 ms
        if (r > 0 && (pfd.revents & POLLIN)) {
            int c = accept(srv, nullptr, nullptr);
            if (c >= 0) {
                if (clients.size() >= MAX_CLIENTS) close(c);
                else clients.push_back(c);
            }
        }

        auto now = std::chrono::steady_clock::now();
        if (now - last < std::chrono::seconds(1)) continue;
        last = now;
        if (clients.empty()) continue;

        SensorSlot snap[(int)SensorType::Count];
        shm.snapshot(snap);

        std::string out;
        for (int i = 0; i < (int)SensorType::Count; ++i) {
            const SensorSlot& s = snap[i];
            if (s.timestamp_ms == 0) continue;       // chưa có mẫu nào
            char line[256];
            snprintf(line, sizeof line,
                     "{\"t\":\"%s\",\"raw\":%.2f,\"avg\":%.2f,\"status\":\"%s\","
                     "\"ts\":%llu,\"valid\":%llu,\"invalid\":%llu}\n",
                     sensor_name((SensorType)i), s.raw, s.filtered,
                     status_name((Status)s.status),
                     (unsigned long long)s.timestamp_ms,
                     (unsigned long long)s.valid_count,
                     (unsigned long long)s.invalid_count);
            out += line;
        }

        for (size_t i = 0; i < clients.size();) {
            // MSG_NOSIGNAL: client ngắt đột ngột không làm hub nhận SIGPIPE
            // MSG_DONTWAIT: client chậm không làm kẹt cả server
            ssize_t n = send(clients[i], out.data(), out.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
            if (n != (ssize_t)out.size()) {          // lỗi hoặc client quá chậm -> loại bỏ
                close(clients[i]);
                clients.erase(clients.begin() + i);
            } else {
                ++i;
            }
        }
    }

    for (int c : clients) close(c);
    close(srv);
}
