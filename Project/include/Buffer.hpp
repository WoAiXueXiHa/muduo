#pragma once
#include <vector>
#include <stdint.h>
#include <algorithm>
#include <assert.h>
#include <string>
#include <cstring>
#include "Log.hpp"

#define BUFFER_SIZE 1024        
class Buffer {
private:
    std::vector<char> _buf;
    uint64_t _readPos;     // 读位置偏移量
    uint64_t _writePos;    // 写位置偏移量
public:
    Buffer() :_buf(BUFFER_SIZE), _readPos(0), _writePos(0) {}
    // 获取vector底层数组首地址
    // 解引用访问第一个元素
    // 取地址获取首地址，数组名就是首地址
    char* Begin() { return &(*_buf.begin()); }
    // 获取当前写位置
    char* GetWritePos() { return Begin() + _writePos; }
    // 获取当前读位置
    char* GetReadPos() { return Begin() + _readPos; }    
    // 获取缓冲区末尾空闲空间大小：写偏移之后的空间，总体空间-写偏移
    uint64_t GetTailSize() { return _buf.size() - _writePos; }
    // 获取缓冲区起始空闲空间大小：读偏移之前的空间
    uint64_t GetHeadSize() { return _readPos; }
    // 获取可读数据大小
    uint64_t GetReadableSize() { return _writePos - _readPos; }

    // 读偏移向后移动
    void MoveReadPos(uint64_t len) { 
        if(0 == len) return;
        // 向后移动的大小必须小于可读数据大小
        assert(len <= GetReadableSize());
        _readPos += len; 
    }
    // 写偏移向后移动
    void MoveWritePos(uint64_t len) {
        // 向后移动的大小必须小于缓冲区末尾空闲空间大小
        assert(len <= GetTailSize());
        _writePos += len;
    }   

    // 确保可写空间足够，够了移动数据，不够扩容
    void EnsureWriteable(uint64_t len) {
        // 如果缓冲区末尾空闲空间足够，直接返回
        if(GetTailSize() >= len) return;

        // 末尾空间不够，判断加上起始位置的空闲空间大小是否足够，够了就把数据移动到起始位置
        if(len <= GetHeadSize() + GetTailSize()) {
            // 移动数据到起始位置
            uint64_t rsz = GetReadableSize();   // 保存当前数据大小
            // template <class InputIterator, class OutputIterator>
            //    OutputIterator copy (InputIterator first, InputIterator last, OutputIterator result);
            // 将[first,last)区间内的元素复制到以result为起始位置的区间中，返回一个指向result区间最后一个元素之后位置的迭代器
            std::copy(GetReadPos(), GetReadPos() + rsz, Begin());  
            // 非常重要！数据从起始位置开始了！
            // 读偏移归0，写偏移等于数据大小
            _readPos = 0;
            _writePos = rsz;
        } else {
            // 末尾空间和起始位置的空闲空间加起来都不够了，扩容
            LOG_DEBUG("Buffer扩容 %zu\n", _buf.size() + len);
            _buf.resize(_writePos + len);
        }
    }

    // 写入数据后移动指针
    void Write(const void* data, uint64_t len) {
        if(0 == len) return;
        EnsureWriteable(len);
        const char* d = (const char*)data;
        std::copy(d, d + len, GetWritePos());
        MoveWritePos(len);
    }
    void WriteString(const std::string& data) {
        Write(data.c_str(), data.size());
    }
    void WriteBuffer(Buffer& data) {
        uint64_t len = data.GetReadableSize();
        if(0 == len) return;
        EnsureWriteable(len);
        std::copy(data.GetReadPos(), data.GetReadPos() + len, GetWritePos());
        MoveWritePos(len);
        data.MoveReadPos(len);  // 移动源数据的读偏移，表示数据被消费了
    }

    // 读取数据后移动指针
    void Read(void* data, uint64_t len) {
        if(0 == len) return;
        assert(len <= GetReadableSize());
        char* d = (char*)data;  
        std::copy(GetReadPos(), GetReadPos() + len, d);
        MoveReadPos(len);
    }
    std::string ReadString(uint64_t len) {
        assert(len <= GetReadableSize());
        std::string s(len, '\0');  // 创建一个长度为len的字符串，内容初始化为'\0'
        Read(&s[0], len);  // 读取数据到字符串的缓冲区
        return s;
    }

    // 查找换行符位置，返回指针
    char* FindCRLF() {
        // memchr(const void *str, int c, size_t n) 
        // 在字符串的前n个字节中搜索第一次出现字符c的位置，返回指向该位置的指针
        char* res = (char*)memchr(GetReadPos(), '\n', GetReadableSize());
        return res;
    }

    // 获取一行数据
    std::string GetLine() {
        char* pos = FindCRLF();
        if(nullptr == pos) {
            return "";
        }
        // +1 是为了把换行符也包含在返回的字符串中，表示一行数据完整了
        return ReadString(pos - GetReadPos() + 1); 
    }

    // 清空，不需要释放内存
    void Clear() {
        _readPos = 0;
        _writePos = 0;
    }
};