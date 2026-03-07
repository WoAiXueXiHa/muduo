#include "../include/Socket.hpp"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

void serverTest() {
    Socket serverSock;
    if (!serverSock.Create()) {
        std::cout << "Server: Failed to create socket" << std::endl;
        return;
    }
    std::cout << "Server: Socket created, fd: " << serverSock.GetFd() << std::endl;

    if (!serverSock.Bind("127.0.0.1", 8080)) {
        std::cout << "Server: Failed to bind" << std::endl;
        return;
    }
    std::cout << "Server: Bound to 127.0.0.1:8080" << std::endl;

    if (!serverSock.Listen()) {
        std::cout << "Server: Failed to listen" << std::endl;
        return;
    }
    std::cout << "Server: Listening" << std::endl;

    // 接受连接
    int clientFd = serverSock.Accept();
    if (clientFd < 0) {
        std::cout << "Server: Failed to accept" << std::endl;
        return;
    }
    std::cout << "Server: Accepted connection, fd: " << clientFd << std::endl;

    Socket clientSock(clientFd);

    // 接收数据
    char buffer[1024];
    ssize_t n = clientSock.Recv(buffer, sizeof(buffer));
    if (n > 0) {
        buffer[n] = '\0';
        std::cout << "Server: Received: " << buffer << std::endl;
    }

    // 发送数据
    std::string response = "Hello from server!";
    clientSock.Send(response.c_str(), response.size());
    std::cout << "Server: Sent: " << response << std::endl;

    clientSock.Close();
    serverSock.Close();
}

void clientTest() {
    // 等待服务器启动
    std::this_thread::sleep_for(std::chrono::seconds(1));

    Socket clientSock;
    if (!clientSock.Create()) {
        std::cout << "Client: Failed to create socket" << std::endl;
        return;
    }
    std::cout << "Client: Socket created, fd: " << clientSock.GetFd() << std::endl;

    if (!clientSock.Connect("127.0.0.1", 8080)) {
        std::cout << "Client: Failed to connect" << std::endl;
        return;
    }
    std::cout << "Client: Connected to server" << std::endl;

    // 发送数据
    std::string message = "Hello from client!";
    clientSock.Send(message.c_str(), message.size());
    std::cout << "Client: Sent: " << message << std::endl;

    // 接收数据
    char buffer[1024];
    ssize_t n = clientSock.Recv(buffer, sizeof(buffer));
    if (n > 0) {
        buffer[n] = '\0';
        std::cout << "Client: Received: " << buffer << std::endl;
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