#include "../server.hpp"

class EchoServer {
private:
    TcpServer _server;

    void onConnection(const ptrConnection& conn) {
        DBG_LOG("New Connection:%p", conn.get());
    }

    void onClosed(const ptrConnection& conn) {
        DBG_LOG("Close Connection:%p", conn.get());
    }

    void onMessage(const ptrConnection& conn, Buffer* buf) {
        conn->Send(buf->getReadIndex(), buf->getReadableSize());
        buf->moveReadOffset(buf->getReadableSize());
        conn->Shutdown();
    }
public:
    EchoServer(int port) : _server(port) {
        _server.setThreadCnt(10);
        _server.setEnableInactiveRelease(10);
        _server.setConnectedCallBack(std::bind(&EchoServer::onConnection, this, std::placeholders::_1));
        _server.setClosedCallBack(std::bind(&EchoServer::onClosed, this, std::placeholders::_1));
        _server.setMessageCallBack(std::bind(&EchoServer::onMessage, this, std::placeholders::_1, std::placeholders::_2));
    }

    void Loop() { _server.Loop(); }
};