#include <iostream>
#include <sys/wait.h>
#include "../src/copy.hpp"

void runClient() {
    Socket client;
    // 阻塞模式
    int ret = client.createClient("127.0.0.1", 8888, true);
    if (!ret) {
        ERR_LOG("create client failed");
        return;
    }
    INF_LOG("[Client] 成功连接到服务端!");

    Buffer buf;
    buf.writeStringAndMove("hello server!");

    // 从buffer发数据
    client.Send(buf.getReadIndex(), buf.getReadableSize());
    DBG_LOG("[Client] 发送数据完成，等待回复...");

    // 接收服务端回复
    char tmp[1024];
    ssize_t n = client.Recv(tmp, sizeof(tmp));
    if(n > 0) {
        buf.Clear();
        buf.WriteAndMove(tmp, n);
        std::string reply = buf.readStringAndMove(buf.getReadableSize());
        DBG_LOG("[Client] 收到回复: %s", reply.c_str());
    }
}
int main() {    
    runClient();    
    return 0;    
}