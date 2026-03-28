#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <algorithm>
#include <errno.h>
#include <fcntl.h>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <poll.h>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

struct HttpResponseView {
    int status = 0;
    size_t content_length = 0;
    std::string body;
};

static int ConnectServer(const std::string& ip, uint16_t port, int timeout_ms = 2000) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int ret = connect(fd, (sockaddr*)&addr, sizeof(addr));
    if (ret < 0) {
        if (errno != EINPROGRESS) {
            close(fd);
            return -1;
        }
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLOUT;
        int pr = poll(&pfd, 1, timeout_ms);
        if (pr <= 0) {
            close(fd);
            return -1;
        }

        int so_error = 0;
        socklen_t len = sizeof(so_error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) != 0 || so_error != 0) {
            close(fd);
            return -1;
        }
    }

    // back to blocking mode for the subsequent send/recv helpers
    if (flags >= 0) fcntl(fd, F_SETFL, flags);
    return fd;
}

static bool SendAll(int fd, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = send(fd, data.data() + sent, data.size() - sent, 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

static bool ReadHttpResponse(int fd, HttpResponseView* rsp) {
    std::string buf;
    char tmp[4096];
    size_t header_end = std::string::npos;
    size_t need_body = 0;

    while (true) {
        if (header_end == std::string::npos) {
            header_end = buf.find("\r\n\r\n");
            if (header_end != std::string::npos) {
                std::string head = buf.substr(0, header_end + 2);
                std::istringstream iss(head);
                std::string version;
                iss >> version >> rsp->status;

                std::string line;
                while (std::getline(iss, line)) {
                    if (line.find("Content-Length:") == 0) {
                        size_t pos = line.find(':');
                        if (pos != std::string::npos) {
                            rsp->content_length = std::stoul(line.substr(pos + 1));
                            need_body = rsp->content_length;
                        }
                    }
                }
            }
        }

        if (header_end != std::string::npos) {
            size_t body_start = header_end + 4;
            if (buf.size() >= body_start + need_body) {
                rsp->body = buf.substr(body_start, need_body);
                return true;
            }
        }

        ssize_t n = recv(fd, tmp, sizeof(tmp), 0);
        if (n <= 0) return false;
        buf.append(tmp, n);
    }
}

static bool TestKeepAlive(const std::string& ip, uint16_t port, int request_cnt = 8) {
    int fd = ConnectServer(ip, port);
    if (fd < 0) return false;

    for (int i = 0; i < request_cnt; ++i) {
        std::ostringstream oss;
        oss << "GET / HTTP/1.1\r\n"
            << "Host: " << ip << ":" << port << "\r\n"
            << "Connection: keep-alive\r\n"
            << "\r\n";
        if (!SendAll(fd, oss.str())) {
            close(fd);
            return false;
        }

        HttpResponseView rsp;
        if (!ReadHttpResponse(fd, &rsp) || rsp.status != 200 || rsp.content_length == 0) {
            close(fd);
            return false;
        }
    }

    close(fd);
    return true;
}

static bool TestReconnect(const std::string& ip, uint16_t port, int rounds = 30) {
    for (int i = 0; i < rounds; ++i) {
        int fd = ConnectServer(ip, port);
        if (fd < 0) return false;

        std::ostringstream oss;
        oss << "GET / HTTP/1.1\r\n"
            << "Host: " << ip << ":" << port << "\r\n"
            << "Connection: close\r\n"
            << "\r\n";

        if (!SendAll(fd, oss.str())) {
            close(fd);
            return false;
        }

        HttpResponseView rsp;
        bool ok = ReadHttpResponse(fd, &rsp);
        close(fd);
        if (!ok || rsp.status != 200) return false;
    }
    return true;
}

static bool TestConcurrency(const std::string& ip, uint16_t port, int threads = 24, int req_per_thread = 30) {
    std::atomic<int> success{0};
    std::atomic<int> fail{0};

    auto worker = [&]() {
        for (int i = 0; i < req_per_thread; ++i) {
            int fd = ConnectServer(ip, port);
            if (fd < 0) {
                fail++;
                continue;
            }

            const std::string body = "username=admin&password=123456";
            std::string req =
                "POST /login HTTP/1.1\r\n"
                "Host: " + ip + ":" + std::to_string(port) + "\r\n"
                "Content-Type: application/x-www-form-urlencoded\r\n"
                "Connection: close\r\n"
                "Content-Length: " + std::to_string(body.size()) + "\r\n"
                "\r\n"
                + body;

            if (!SendAll(fd, req)) {
                close(fd);
                fail++;
                continue;
            }

            HttpResponseView rsp;
            bool ok = ReadHttpResponse(fd, &rsp);
            close(fd);
            if (ok && rsp.status == 200) success++;
            else fail++;
        }
    };

    std::vector<std::thread> ts;
    ts.reserve(threads);
    for (int i = 0; i < threads; ++i) ts.emplace_back(worker);
    for (auto& t : ts) t.join();

    std::cout << "[并发测试] success=" << success << " fail=" << fail << std::endl;
    return fail == 0;
}

struct ConcurrencyRunStat {
    int threads = 0;
    int success = 0;
    int fail = 0;
    long long cost_ms = 0;
    long long avg_ms = 0;
    long long p95_ms = 0;
    long long max_ms = 0;
};

static ConcurrencyRunStat RunConcurrencyOnce(const std::string& ip, uint16_t port, int threads, int timeout_ms) {
    ConcurrencyRunStat st;
    st.threads = threads;

    std::atomic<int> success{0};
    std::atomic<int> fail{0};
    std::vector<long long> lat_ms(threads, 0);

    std::mutex gate_mu;
    std::condition_variable gate_cv;
    int ready = 0;
    bool start = false;

    auto worker = [&](int idx) {
        // Barrier (C++11 compatible): make all threads start "as simultaneously as possible".
        {
            std::unique_lock<std::mutex> lk(gate_mu);
            if (++ready == threads) gate_cv.notify_one();
            gate_cv.wait(lk, [&]() { return start; });
        }

        auto begin = std::chrono::steady_clock::now();

        int fd = ConnectServer(ip, port, timeout_ms);
        if (fd < 0) {
            fail++;
            return;
        }

        const std::string body = "username=admin&password=123456";
        std::ostringstream oss;
        oss << "POST /login HTTP/1.1\r\n"
            << "Host: " << ip << ":" << port << "\r\n"
            << "Content-Type: application/x-www-form-urlencoded\r\n"
            << "Connection: close\r\n"
            << "Content-Length: " << body.size() << "\r\n"
            << "\r\n"
            << body;

        if (!SendAll(fd, oss.str())) {
            close(fd);
            fail++;
            return;
        }

        HttpResponseView rsp;
        bool ok = ReadHttpResponse(fd, &rsp);
        close(fd);

        auto end = std::chrono::steady_clock::now();
        long long cost = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();
        lat_ms[idx] = cost;

        if (ok && rsp.status == 200) success++;
        else fail++;
    };

    std::vector<std::thread> ts;
    ts.reserve(threads);

    auto begin_all = std::chrono::steady_clock::now();
    for (int i = 0; i < threads; ++i) ts.emplace_back(worker, i);

    {
        std::unique_lock<std::mutex> lk(gate_mu);
        gate_cv.wait(lk, [&]() { return ready == threads; });
        start = true;
    }
    gate_cv.notify_all();

    for (auto& t : ts) t.join();
    auto end_all = std::chrono::steady_clock::now();
    st.cost_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_all - begin_all).count();

    st.success = success.load();
    st.fail = fail.load();

    if (st.success > 0) {
        std::vector<long long> ok_lat;
        ok_lat.reserve(st.success);
        for (int i = 0; i < threads; ++i) {
            // We don't know which idx succeeded without extra signaling; using non-zero latencies
            // keeps the analysis simple and avoids extra locks. Fail paths leave lat_ms[idx]==0.
            if (lat_ms[i] > 0) ok_lat.push_back(lat_ms[i]);
        }
        if (!ok_lat.empty()) {
            std::sort(ok_lat.begin(), ok_lat.end());
            long long sum = 0;
            for (auto v : ok_lat) sum += v;
            st.avg_ms = sum / static_cast<long long>(ok_lat.size());
            st.max_ms = ok_lat.back();
            size_t p95_idx = static_cast<size_t>(ok_lat.size() * 95 / 100);
            if (p95_idx >= ok_lat.size()) p95_idx = ok_lat.size() - 1;
            st.p95_ms = ok_lat[p95_idx];
        }
    }
    return st;
}

static bool TestMaxConcurrency(const std::string& ip, uint16_t port,
                                int start_level = 4, int max_level = 128,
                                int rounds_per_level = 2, int timeout_ms = 2000) {
    int best = 0;
    int level = start_level;

    while (level <= max_level) {
        bool level_ok = true;

        for (int r = 0; r < rounds_per_level; ++r) {
            ConcurrencyRunStat st = RunConcurrencyOnce(ip, port, level, timeout_ms);
            std::cout << "[极限并发] level=" << level << " round=" << (r + 1)
                      << " success=" << st.success << " fail=" << st.fail
                      << " cost=" << st.cost_ms << "ms"
                      << " avg=" << st.avg_ms << "ms"
                      << " p95=" << st.p95_ms << "ms"
                      << " max=" << st.max_ms << "ms" << std::endl;
            if (st.fail != 0) {
                level_ok = false;
                break;
            }
        }

        if (level_ok) {
            best = level;
            level *= 2;
        } else {
            break;
        }
    }

    std::cout << "[极限并发量结果] max_success_concurrency=" << best << std::endl;
    return best >= start_level;
}

static bool TestHalfPacket(const std::string& ip, uint16_t port) {
    int fd = ConnectServer(ip, port);
    if (fd < 0) return false;

    std::string part1 = "GET / HTTP/1.1\r\nHost: 127.0.0.1:3389\r\n";
    std::string part2 = "Connection: close\r\n\r\n";

    if (!SendAll(fd, part1)) {
        close(fd);
        return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    if (!SendAll(fd, part2)) {
        close(fd);
        return false;
    }

    HttpResponseView rsp;
    bool ok = ReadHttpResponse(fd, &rsp);
    close(fd);
    return ok && rsp.status == 200;
}

static bool TestBadRequest(const std::string& ip, uint16_t port) {
    int fd = ConnectServer(ip, port);
    if (fd < 0) return false;

    std::string req =
        "GEX / HTTP/1.1\r\n"
        "Host: 127.0.0.1:3389\r\n"
        "Connection: close\r\n"
        "\r\n";

    if (!SendAll(fd, req)) {
        close(fd);
        return false;
    }

    HttpResponseView rsp;
    bool ok = ReadHttpResponse(fd, &rsp);
    close(fd);
    return ok && rsp.status >= 400;
}

static bool RunCase(const std::string& name, const std::function<bool()>& fn) {
    auto begin = std::chrono::steady_clock::now();
    bool ok = fn();
    auto cost = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - begin).count();

    std::cout << (ok ? "[PASS] " : "[FAIL] ") << name
              << "  cost=" << cost << "ms" << std::endl;
    return ok;
}

int main(int argc, char* argv[]) {
    std::string ip = "127.0.0.1";
    uint16_t port = 3389;
    if (argc >= 2) ip = argv[1];
    if (argc >= 3) port = static_cast<uint16_t>(std::stoi(argv[2]));

    std::cout << "HTTP 边缘测试启动: " << ip << ":" << port << std::endl;
    std::cout << "提示: 请先启动 http_server，再运行本程序。" << std::endl;

    int passed = 0;
    int total = 0;

    total++; if (RunCase("长连接测试(keep-alive)", [&](){ return TestKeepAlive(ip, port); })) passed++;
    total++; if (RunCase("断开重连测试", [&](){ return TestReconnect(ip, port); })) passed++;
    total++; if (RunCase("并发压测", [&](){ return TestConcurrency(ip, port); })) passed++;
    total++; if (RunCase("极限并发量压测", [&](){ return TestMaxConcurrency(ip, port); })) passed++;
    total++; if (RunCase("半包测试", [&](){ return TestHalfPacket(ip, port); })) passed++;
    total++; if (RunCase("非法请求测试", [&](){ return TestBadRequest(ip, port); })) passed++;

    std::cout << "\n测试汇总: " << passed << "/" << total << " 通过" << std::endl;
    return passed == total ? 0 : 1;
}
