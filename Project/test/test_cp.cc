#include <iostream>
#include <vector>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

int main() {
    int target_connections = 100000; // 我们先定个小目标：瞬间发起 100000 个连接
    std::vector<int> client_fds;

    // 提前准备好服务端的地址
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = ::htons(8888);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    std::cout << "准备发射 " << target_connections << " 个并发连接..." << std::endl;

    for(int i = 0; i < target_connections; ++i) {
        int sockfd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            std::cerr << "\n创建 Socket 失败! 错误码: " << errno << std::endl;
            break;
        }

        // 发起连接
        if(connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == 0) {
            client_fds.push_back(sockfd);
            if (i % 100 == 0) {
                std::cout << "已成功建立 " << i << " 个连接..." << std::endl;
            }
        } else {
            std::cerr << "\n连接失败! 错误码: " << errno << std::endl;
            break;
        }
    }

    std::cout << "\n压测完成！当前存活连接数: " << client_fds.size() << std::endl;
    std::cout << "观你服务器端的输出，然后在这里按【回车键】瞬间断开所有连接..." << std::endl;
    
    std::cin.get(); // 阻塞住，让连接保持存活
    
    // 瞬间断开所有连接，测试服务器的抗压清理能力
    for(int fd : client_fds) {
        sleep(1);
        close(fd);
    }
    std::cout << "所有客户端已断开，测试结束。" << std::endl;
    return 0;
}