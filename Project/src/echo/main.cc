#include "echo.hpp"

int main() {
   // DBG_LOG("[追踪一] [echo初始化]");
    EchoServer server(8080);
   // DBG_LOG("[追踪二] [Loop循环开启]");
    server.Loop();
   // DBG_LOG("[追踪三] [Loop析构]");
    return 0;
}