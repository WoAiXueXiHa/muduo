#pragma once
#include <vector>
#include <cerrno>
#include <unordered_map>
#include <sys/epoll.h>
#include <unistd.h>
#include "./Channel.hpp"
#include "./Logging.hpp"
#include <cstring>



#define MAX_EPOLLEVENTS 1024
class Poller {
private:
    int _epfd;
    std::unordered_map<int, Channel*> _channels;
    struct epoll_event _events[MAX_EPOLLEVENTS];

    // 直接操作epoll
    void updateChannel(Channel* channel, int op) {
        int fd = channel->getFd();
        struct epoll_event event;        
        event.data.fd = fd;
        event.events = channel->getEvents();

        int ret = ::epoll_ctl(_epfd, op, fd, &event);
        if(ret < 0) {
            LOG_ERROR("epoll_ctl error: %s", strerror(errno));
            abort();
        }
    }

    // 判断channel是否添加了事件监控
    bool hasChannel(Channel* channel) {
        auto it = _channels.find(channel->getFd());
        return it != _channels.end();
    }

public:
    Poller() {
        _epfd = epoll_create(1);
        if (_epfd < 0) {
            LOG_ERROR("epoll_create failed: %s", strerror(errno));
            abort();
        }
    }

    // 增加或修改事件监控（ADD/MOD 由内部判断）
    void updateChannel(Channel* channel) {
        bool ret = hasChannel(channel);
        if(!ret) {
            // 之前没有添加事件监控，先添加
            _channels[channel->getFd()] = channel;
            updateChannel(channel, EPOLL_CTL_ADD);
        } else {
            updateChannel(channel, EPOLL_CTL_MOD);
        }
    }
    // 移除事件监控
    void removeChannel(Channel* channel) {
        // 只有真正注册过的 fd 才能 DEL，否则 epoll_ctl 会报错
        if(!hasChannel(channel)) return;
        _channels.erase(channel->getFd());
        updateChannel(channel, EPOLL_CTL_DEL);
    }
    // 等待就绪事件，返回活跃 Channel 列表
    void Poll(std::vector<Channel*>* active) {
        int nfds = ::epoll_wait(_epfd, _events, MAX_EPOLLEVENTS, -1);
        if(nfds < 0) {
            if(errno == EINTR) return;
            LOG_ERROR("epoll_wait error: %s", strerror(errno));
            abort();
        }

        // 处理活跃事件
        for(int i = 0; i < nfds; ++i) {
           auto it = _channels.find(_events[i].data.fd);
            if(it == _channels.end()) continue;
            it->second->setRevents(_events[i].events);
            active->push_back(it->second);
        }
        
    }

    ~Poller() { close(_epfd); }
};