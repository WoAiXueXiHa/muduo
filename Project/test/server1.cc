#include <iostream>
#include <sys/wait.h>
#include "../src/server.hpp"

// 服务端逻辑
void runServer() {
    Socket server;
    // 这里是阻塞模式，还没写epoll呢
    int ret = server.createServer("127.0.0.1", 8888, true);
    if(ret < 0) {
        ERR_LOG("create server failed");
        return;
    }
    DBG_LOG("[Server] 服务端启动成功，监听 8080 端口...");

    // 阻塞等待客户端连接
    int connfd = server.Accept();
    if(connfd < 0) {
        ERR_LOG("accept failed");
        return;
    }

    // 使用新连接的fd构造一个专门通信的Socket对象
    Socket client_conn(connfd);
    DBG_LOG("[Server] 新客户端连接成功，fd=%d", connfd);

    Buffer buf;
    char tmp[1024];

    // 阻塞接收数据
    ssize_t n = client_conn.Recv(tmp, sizeof(tmp));
    if(n > 0) {
        // 网络数据写入buffer
        buf.WriteAndMove(tmp, n);
        
        // 从buffer中读取字符串打印
        std::string msg = buf.readStringAndMove(buf.getReadableSize());
        DBG_LOG("[Server] 收到客户端数据: %s", msg.c_str());

        // 回复数据写入buffer
        buf.Clear();
        buf.writeStringAndMove("Hello, client!");

        // 从buffer中提取数据发送出去
        client_conn.Send(buf.getReadIndex(),buf.getReadableSize());
        DBG_LOG("[Server] 回复客户端数据: Hello, client!");
    } else if(0 == n) {
        DBG_LOG("[Server] 客户端断开连接");
    }
}

int main() {
    runServer();
    return 0;
}