#pragma once
#include <cstdint>
#include <cassert>
#include <cstring>

#include <string>
#include <vector>

#define BUFFER_SIZE (1024 * 1024)

class Buffer {
private:
    std::vector<char> _buffer;

    // 相对于 _buffer 首地址的偏移量
    // [_readIndex, _writeIndex) 是当前有效数据区
    uint64_t _readIndex = 0;
    uint64_t _writeIndex = 0;

public:
    // ===================== 构造与基础指针 =====================
    Buffer() :_buffer(BUFFER_SIZE), _readIndex(0), _writeIndex(0) {}

    // 返回底层连续内存首地址
    char* Begin() { return &*_buffer.begin(); }

    // 返回当前“写位置”的地址（尾插点）
    char* getWriteIndex() { return Begin() + _writeIndex; }

    // 返回当前“读位置”的地址（头读点）
    char* getReadIndex() { return Begin() + _readIndex; }

    // ===================== 空间查询 =====================

    // 头部废弃空间大小：[0, _readIndex)
    uint64_t getHeadSize() { return _readIndex; }

    // 尾部可写空间大小：[_writeIndex, _buffer.size())
    uint64_t getTailSize() { return _buffer.size() - _writeIndex; }

    // 当前有效可读数据大小：[_readIndex, _writeIndex)
    uint64_t getReadableSize() { return _writeIndex - _readIndex; }

    // ===================== 读写偏移推进 =====================

    // 读偏移后移 len
    void moveReadOffset(uint64_t len) {
        if(len == 0) return;
        assert(len <= getReadableSize());
        _readIndex += len;
    }

    // 写偏移后移 len
    void moveWriteOffset(uint64_t len) {
        if(len == 0) return;
        assert(len <= getTailSize());
        _writeIndex += len;
    }

    // ===================== 可写空间保障 =====================

    // 确保至少还有 len 字节可写空间
    void ensureWriteable(uint64_t len) {
        // 尾部空间足够，不用管
        if(len <= getTailSize()) return;
        // 先移动可读区域覆盖头部废弃
        uint64_t readAbleSize = getReadableSize();
        std::copy(getReadIndex(), getReadIndex() + readAbleSize, Begin());
        _readIndex = 0;
        _writeIndex = readAbleSize;
        // 如果现在的尾部空间不够，再扩容
        if(getTailSize() < len) {   
            _buffer.resize(_writeIndex + len);
        }
    }

    // ===================== 写入接口 =====================

    // 写入 len 字节到可写区，不移动 _writeIndex
    void Write(const char* data, uint64_t len) {
        if(len == 0) return;
        ensureWriteable(len);
        std::copy(data, data + len, getWriteIndex());
    }

    // 写入并移动写偏移
    void WriteAndMove(const char* data, uint64_t len) {
        Write(data, len);
        moveWriteOffset(len);
    }

    // 写入 string（不移动写偏移）
    void writeString(const std::string& data) {
        Write(data.c_str(), data.size());
    }

    // 写入 string 并移动写偏移
    void writeStringAndMove(const std::string& data) {
        writeString(data);
        moveWriteOffset(data.size());
    }

    // 写入另一个 Buffer 的可读区（不移动写偏移）
    void writeBuffer(Buffer& data) {
        Write(data.getReadIndex(), data.getReadableSize());
    }

    // 写入另一个 Buffer 的可读区并移动写偏移
    void writeBufferAndMove(Buffer& data) {
        uint64_t n = data.getReadableSize();
        writeBuffer(data);
        moveWriteOffset(n);
    }

    // ===================== 读取接口 =====================

    // 从可读区读取 len 字节到外部 data，不移动 _readIndex
    // void* 输出型参数
    void Read(void* data, uint64_t len) {
        if(len == 0) return;
        assert(len <= getReadableSize());
        std::copy(getReadIndex(), getReadIndex() + len, (char*)data);
    }

    // 读取并移动读偏移
    void readAndMove(void* data, uint64_t len) {
        Read(data, len);
        moveReadOffset(len);
    }

    // 读取 len 字节为 string（不移动读偏移）
    std::string readString(uint64_t len) {
        if(len == 0) return "";
        assert(len <= getReadableSize());
        return std::string(getReadIndex(), len);
    }

    // 读取 len 字节为 string 并移动读偏移
    std::string readStringAndMove(uint64_t len) {
        assert(len <= getReadableSize());
        std::string str = readString(len);
        moveReadOffset(len);
        return str;
    }

    // ===================== 行读取（HTTP 解析常用） =====================

    // 在可读区查找 '\n'，返回其地址，找不到返回 nullptr
    // memchr(getReadIndex(), '\n', getReadableSize())
    char* findCRLF() {
        char* p = (char*)memchr(getReadIndex(), '\n', getReadableSize());
        return p;
    }

    // 读取一行（包含结尾 '\n'），不移动读偏移
    std::string readLine() {
        char* p = findCRLF();
        if(p == nullptr) return "";
        // +1：把换行符也取出来
        return readString(p - getReadIndex() + 1);
    }

    // 读取一行并移动读偏移
    std::string readLineAndMove() {
        std::string str = readLine();
        moveReadOffset(str.size());
        return str;
    }

    // ===================== 状态重置 =====================

    // 清空逻辑状态（不释放容量）
    void Clear() {
        _readIndex = 0;
        _writeIndex = 0;
    }
};
