#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>

static volatile sig_atomic_t g_stop = 0;
static void on_signal(int) { g_stop = 1; }

int main(int argc, char** argv) {
    const char* host = argc > 1 ? argv[1] : "127.0.0.1";
    int port         = argc > 2 ? atoi(argv[2]) : 5555;

    struct sigaction sa{};
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);

    while (!g_stop) {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port   = htons(port);
        if (inet_pton(AF_INET, host, &a.sin_addr) != 1) {
            fprintf(stderr, "bad address: %s\n", host);
            return 1;
        }

        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) < 0) {
            perror("connect");
            close(fd);
            sleep(1);
            continue;
        }
        puts("connected");

        timeval tv{3, 0};                            // 3 giây không có dữ liệu -> coi như lỗi
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

        std::string buf;
        char tmp[512];
        while (!g_stop) {
            ssize_t n = recv(fd, tmp, sizeof tmp, 0);
            if (n > 0) {
                buf.append(tmp, n);
                size_t p;
                while ((p = buf.find('\n')) != std::string::npos) {   // tách theo dòng
                    puts(buf.substr(0, p).c_str());
                    buf.erase(0, p + 1);
                }
            } else if (n == 0) {
                puts("server closed connection");
                break;
            } else if (errno == EINTR) {
                continue;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                puts("timeout: no data for 3s");
                break;
            } else {
                perror("recv");
                break;
            }
        }
        close(fd);
        if (!g_stop) sleep(1);                       // rồi thử kết nối lại
    }
    return 0;
}
