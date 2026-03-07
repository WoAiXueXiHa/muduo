#include "../include/Buffer.hpp"
#include "../include/Socket.hpp"
#include "../include/Poller.hpp"
#include "../include/Channel.hpp"
#include <iostream>
#include <string>

void TestBuffer() {
    Buffer buf;
    std::string s1 = "Hello, World!\n";
    std::string s2 = "Welcome to C++ programming.\n";
    buf.WriteString(s1);
    buf.WriteString(s2);

    std::cout << "Readable size: " << buf.GetReadableSize() << std::endl;

    std::string line1 = buf.GetLine();
    std::string line2 = buf.GetLine();

    std::cout << "Line 1: " << line1;
    std::cout << "Line 2: " << line2;

    std::cout << "Readable size after reading lines: " << buf.GetReadableSize() << std::endl;

    buf.Clear();
    std::cout << "Readable size after clearing buffer: " << buf.GetReadableSize() << std::endl;
}

int main() {
    // TestBuffer();
    return 0;
}
