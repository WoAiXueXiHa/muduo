#include <iostream>
#include <map>
#include "../src/server.hpp"
int main() {
    // 创建服务器Socket
    Socket listen_sock;
    int ret = listen_sock.createServer("127.0.0.1", 8888, false);
    if(ret < 0) {
        ERR_LOG("create server failed");
        return -1;
    }
    DBG_LOG("create server success");

    // 创建Poller
    Poller poller;

    // 为监听Socket创建专属Channel
    Channel listen_channel(listen_sock.getFd(), &poller);

    // 存放连进来的客户端
   std::map<int, std::pair<Socket*, Channel*>> client_map;
   std::vector<int> trash_can;

    // 4. 配置监听 Channel 的“读事件”回调（有新客来了）
    listen_channel.setReadCallback([&]() {
        int connfd = listen_sock.Accept();
        if (connfd >= 0) {
            DBG_LOG("[新连接] 客户端 FD: %d 接入！", connfd);
            
            // 为新客户分配 Socket 和 Channel
            Socket* client_sock = new Socket(connfd);
            client_sock->SetNonblock(); // 客户端也要非阻塞
            Channel* client_channel = new Channel(connfd, &poller);

            // 配置客户端 Channel 的“读事件”回调（客户发消息了）
            client_channel->setReadCallback([client_sock, client_channel, &client_map, &trash_can]() {
                char buf[1024] = {0};
                ssize_t n = client_sock->Recv(buf, sizeof(buf));
                
                if (n > 0) {
                    DBG_LOG("[收到消息] FD %d: %s", client_sock->getFd(), buf);
                    // 简单粗暴的 Echo：原样发回去
                    client_sock->Send(buf, n); 
                } else if (n == 0) {
                    DBG_LOG("[断开连接] FD %d 离开", client_sock->getFd());
                    // 客户走了，必须清理战场，否则就内存泄漏了！
                    client_channel->disableAll(); // 自动调用 _poller->updateChannel 删除内核监听
                    client_channel->remove();
                    
                    // 从 map 中移除并释放内存
                    int fd = client_sock->getFd();
                    trash_can.push_back(fd);
                    client_map.erase(fd);
                } else {
                    // n < 0 且不是 EAGAIN 时，出错了
                    ERR_LOG("FD %d 读取错误", client_sock->getFd());
                }
            });

            // 把新客户记在账上，防止指针丢失
            client_map[connfd] = std::make_pair(client_sock, client_channel);
            
            // 见证奇迹的时刻：只要喊一句 enableRead，它就会自动注册到底层 Epoll！
            client_channel->enableRead(); 
        }
    });

    // 让监听Channel开始读事件监控
    listen_channel.enableRead();

    // 启动事件循环
    std::vector<Channel*> active_channels;
    while (true) {
        active_channels.clear();
        poller.Poll(&active_channels);
        
        // 1. 安全期：让所有的 Channel 先干完活
        for (Channel* channel : active_channels) {
            channel->handleEvent();
        }
        
        // 2. 【修改点 4】：活儿全干完了，集中清理垃圾！
        for (int fd : trash_can) {
            if (client_map.count(fd)) {
                delete client_map[fd].second; // delete client_channel
                delete client_map[fd].first;  // delete client_sock
                client_map.erase(fd);
            }
        }
        trash_can.clear(); // 记得把垃圾桶倒空，迎接下一轮！
    }
    return 0;
}