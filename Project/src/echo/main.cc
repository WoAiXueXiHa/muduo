#include "echo.hpp"

int main() {
    EchoServer server(8080);
    server.Loop();
    return 0;
}