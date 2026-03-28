#include "../include/Logging.hpp"
#include <thread>
#include <vector>

int main() {
    LOG_INFO("程序启动");
    LOG_DEBUG("这是调试信息");
    LOG_WARN("这是警告信息");
    LOG_ERROR("这是错误信息");

    // 多线程测试
    LOG_INFO("开始多线程测试");
    std::vector<std::thread> threads;
    
    for (int i = 0; i < 3; ++i) {
        threads.emplace_back([i]() {
            LOG_INFO("线程 " + std::to_string(i) + " 启动");
            std::this_thread::sleep_for(std::chrono::milliseconds(100 * i));
            LOG_INFO("线程 " + std::to_string(i) + " 结束");
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    LOG_INFO("程序结束");
    return 0;
}
