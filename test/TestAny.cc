#include "../include/Any.hpp"
#include <iostream>
#include <string>

using namespace muduo;

int main() {
    // 1. 存储 int
    Any a = 42;
    std::cout << "int: " << *a.getPtr<int>() << std::endl;

    // 2. 存储 string
    Any b = std::string("hello");
    std::cout << "string: " << *b.getPtr<std::string>() << std::endl;

    // 3. 存储 double
    Any c = 3.14;
    std::cout << "double: " << *c.getPtr<double>() << std::endl;

    // 4. 拷贝
    Any d = a;
    std::cout << "copy int: " << *d.getPtr<int>() << std::endl;

    // 5. 赋值
    a = std::string("world");
    std::cout << "reassign to string: " << *a.getPtr<std::string>() << std::endl;

    // 6. 类型检查（会 assert 失败）
    // int* p = a.getPtr<int>();  // 错误：a 现在存的是 string，不是 int

    std::cout << "All tests passed!" << std::endl;
    return 0;
}
