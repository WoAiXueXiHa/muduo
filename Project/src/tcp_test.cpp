#include "../include/Socket.hpp"
#include "../include/Poller.hpp"
#include "../include/Channel.hpp"
#include "../include/Buffer.hpp"
#include <iostream>
#include <thread>
#include <vector>
#include <unistd.h>

void serverTest() {
    // 创建服务器套接字
    Socket serverSock;
    if (!serverSock.Create()) {
        std::cout << "Server: Failed to create socket" << std::endl;
        return;
    }
    serverSock.ReuseAddr();
    if (!serverSock.Bind("127.0.0.1", 8080)) {
        std::cout << "Server: Failed to bind" << std::endl;
        return;
    }
    if (!serverSock.Listen()) {
        std::cout << "Server: Failed to listen" << std::endl;
        return;
    }
    std::cout << "Server: Listening on 127.0.0.1:8080" << std::endl;

    // 创建Poller
    Poller poller;

    // 创建监听Channel
    Channel listenChannel(serverSock.GetFd(), &poller);
    listenChannel.EnableReading();
    listenChannel.SetReadCallback([&]() {
        int connfd = serverSock.Accept();
        if (connfd < 0) {
            std::cout << "Server: Accept failed" << std::endl;
            return;
        }
        std::cout << "Server: Accepted connection, fd: " << connfd << std::endl;

        // 创建连接Channel
        Socket* connSock = new Socket(connfd);
        // connSock->NonBlock();  // 移除非阻塞
        Channel* connChannel = new Channel(connfd, &poller);
        Buffer* buffer = new Buffer();

        connChannel->EnableReading();
        connChannel->SetReadCallback([connSock, connChannel]() {
            char buf[1024];
            ssize_t n = connSock->Recv(buf, sizeof(buf));
            if (n > 0) {
                std::cout << "Server: Received: " << std::string(buf, n);
                // 回显
                connSock->Send(buf, n);
            } else if (n == 0) {
                std::cout << "Server: Client closed" << std::endl;
                connChannel->Remove();
                delete connSock;
                delete connChannel;
            } else {
                // 忽略EAGAIN
                if (errno != EAGAIN && errno != EINTR) {
                    std::cout << "Server: Recv error: " << strerror(errno) << std::endl;
                }
            }
        });
        poller.UpdateEvent(connChannel);
    });
    poller.UpdateEvent(&listenChannel);

    // 事件循环
    std::vector<Channel*> activeChannels;
    for (int i = 0; i < 5; ++i) {  // 运行几轮
        activeChannels.clear();
        poller.Poll(&activeChannels);
        for (auto ch : activeChannels) {
            ch->HandleEvent();
        }
        sleep(1);
    }

    serverSock.Close();
}

void clientTest() {
    sleep(1);  // 等待服务器启动

    Socket clientSock;
    if (!clientSock.Create()) {
        std::cout << "Client: Failed to create socket" << std::endl;
        return;
    }
    // clientSock.NonBlock();  // 移除非阻塞
    if (!clientSock.Connect("127.0.0.1", 8080)) {
        std::cout << "Client: Failed to connect" << std::endl;
        return;
    }
    std::cout << "Client: Connected" << std::endl;

    // 发送数据
    std::string msg = "Hello from client\n";
    clientSock.Send(msg.c_str(), msg.size());
    std::cout << "Client: Sent: " << msg;

    sleep(1);  // 等待服务器处理

    // 接收响应
    char buf[1024];
    ssize_t n = clientSock.Recv(buf, sizeof(buf));
    if (n > 0) {
        buf[n] = '\0';
        std::cout << "Client: Received: " << buf;
    }

    clientSock.Close();
}

int main() {
    std::thread serverThread(serverTest);
    std::thread clientThread(clientTest);

    serverThread.join();
    clientThread.join();

    std::cout << "Test completed" << std::endl;
    return 0;
}