#include "../include/Buffer.hpp"

#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

int main() {
    // ---------- 1. 刚开始：没有数据，读指针 == 写指针 ----------
    Buffer b;
    assert(b.getReadableSize() == 0);
    assert(b.getReadIndex() == b.getWriteIndex());

    // ---------- 2. 写入：WriteAndMove 会推进写指针 ----------
    b.writeStringAndMove("hello");
    assert(b.getReadableSize() == 5);
    assert(std::memcmp(b.getReadIndex(), "hello", 5) == 0);

    // ---------- 3. 读走数据：readAndMove 会推进读指针 ----------
    char out[8] = {};
    b.readAndMove(out, 5);
    assert(std::strcmp(out, "hello") == 0);
    assert(b.getReadableSize() == 0);

    // ---------- 4. Read 不移动读指针（只“看一眼”）----------
    b.writeStringAndMove("abc");
    char peek[4] = {};
    b.Read(peek, 2);
    assert(std::strncmp(peek, "ab", 2) == 0);
    assert(b.getReadableSize() == 3);  // 还在，没被消费

    // ---------- 5. 先读后写：前面读过的地方叫“头废弃”，不算可读数据 ----------
    b.Clear();
    b.writeStringAndMove("0123456789");
    b.moveReadOffset(4);  // 丢掉 "0123"
    assert(b.getHeadSize() == 4);
    assert(b.getReadableSize() == 6);
    assert(b.getReadIndex()[0] == '4');

    // ---------- 6. 按行读（找 '\n'）----------
    b.Clear();
    b.writeStringAndMove("hi\n");
    assert(b.readLineAndMove() == "hi\n");
    assert(b.getReadableSize() == 0);

    b.writeStringAndMove("no newline");
    assert(b.findCRLF() == nullptr);
    assert(b.readLine() == "");  // 没有换行时返回空串

    // ---------- 7. Write 不自动推进写指针；要自己 moveWriteOffset ----------
    Buffer w;
    w.Write("xyz", 3);
    assert(w.getReadableSize() == 0);  // 还没算“有效数据”，因为写指针没动
    w.moveWriteOffset(3);
    assert(w.getReadableSize() == 3);

    // ---------- 8. Clear：清空逻辑上的读写位置，vector 容量不变 ----------
    w.Clear();
    assert(w.getReadableSize() == 0);
    assert(w.getTailSize() == static_cast<uint64_t>(BUFFER_SIZE));

    std::cout << "Buffer 测试全部通过。\n";
    return 0;
}
