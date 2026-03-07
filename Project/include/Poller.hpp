#pragma once
#include <sys/epoll.h>
#include <unordered_map>
#include <vector>
#include <cstring>
#include <cassert>
#include "Channel.hpp"
#include "Log.hpp"

#define MAX_EPOLL_EVENTS 1024
class Channel;
class Poller {
private:
    int _epfd;  // epoll实例的文件描述符
    // struct epoll_event { 
    //     uint32_t events;  // 关注的事件类型
    //     epoll_data_t data; // 用户数据
    // };
    struct epoll_event _events[MAX_EPOLL_EVENTS];  // 存储就绪事件的数组
    std::unordered_map<int, Channel*> _channels;   // fd:Channel的映射表，方便通过fd找到对应的Channel对象

private:
    // 对epoll的直接操作，把操作命令给我
    void Update(Channel* channel, int op) {
        // int epoll_ctl(int epfd, int op, int fd,
        //               struct epoll_event *_Nullable event);
        int fd = channel->GetFd();          // 要监控的事件的文件描述符
        struct epoll_event ev;
        ev.data.fd = fd;                    // 将Channel的fd作为用户数据存储在epoll_event中
        ev.events = channel->GetEvents();   // 获取Channel关注的事件类型

        int ret = ::epoll_ctl(_epfd, op, fd, &ev);
        if(ret < 0) {
            LOG_ERR("epoll_ctl error: %s\n", strerror(errno));
        }
    }

    // 判断一个Channel是否添加了事件监控
    bool HasChannel(Channel* channel) {
        auto it = _channels.find(channel->GetFd());
        return it != _channels.end();
    }

public:
    Poller() {
        _epfd = ::epoll_create(MAX_EPOLL_EVENTS);
        if(_epfd < 0) {
            LOG_ERR("epoll_create error: %s\n", strerror(errno)); 
        }
    }

    // 添加或修改事件监控，没有就添加，有就修改
    void UpdateEvent(Channel* channel) {
        bool ret = HasChannel(channel);
        if(!ret) {
            // 不存在则添加
            _channels.insert(std::make_pair(channel->GetFd(), channel));
            Update(channel, EPOLL_CTL_ADD);
        }
        Update(channel, EPOLL_CTL_MOD);
    }

    // 移除监控
    void RemoveEvent(Channel* channel) {
        auto it = _channels.find(channel->GetFd());
        if(it != _channels.end()) {
            _channels.erase(it);
        }
        Update(channel, EPOLL_CTL_DEL);
    }

    // 开始监控，返回活跃连接的列表
    void Poll(std::vector<Channel*>* active_arr) {
        // int epoll_wait(int epfd, struct epoll_event *events,
        //              int maxevents, int timeout);
        // epfd是要监控的描述符，events是保存就绪事件的数组
        // maxevents是events数组的大小，timeout是超时时间，-1表示阻塞等待，0表示立即返回
        // 返回值是就绪事件的数量，0表示超时，-1表示出错
        int nfds = ::epoll_wait(_epfd, _events, MAX_EPOLL_EVENTS, -1);
        if(nfds < 0) {
            if(errno == EINTR) {
                // EINTR表示系统调用被信号中断了，这种情况不是错误，可以继续等待
                return;
            }
            LOG_ERR("epoll_wait error: %s\n", strerror(errno));
            abort();    // 退出程序
        }

        // nfds表示有多少个事件就绪了，遍历这些就绪事件，找到对应的Channel对象，并将它添加到活跃连接的列表中
        for(int i = 0; i < nfds; ++i) {
            auto it = _channels.find(_events[i].data.fd);
            assert(it != _channels.end());  // 确保这个fd对应的Channel对象存在
            // it->second是Channel对象的指针，_events[i].events是这个fd就绪的事件类型
            it->second->SetEvents(_events[i].events); 
            active_arr->push_back(it->second);  // 将这个Channel对象添加到活跃连接的列表中
        }
    }
};

#include "Channel.hpp"
    // 移除监控，把红黑树上这个节点删除掉
    void Channel::Remove() { _poller->RemoveEvent(this); }
    // 更新监控
    void Channel::Update() { _poller->UpdateEvent(this); }
