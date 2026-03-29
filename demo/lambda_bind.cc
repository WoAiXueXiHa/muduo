#include <cstdio>
#include <iostream>
#include <functional>


// std::bind 我有一个函数但是现在不想调用它，而是想把它绑定到一个对象上，延迟调用

// void sayHello() { printf("hello\n"); }
// int main() {
//     sayHello(); // 直接调用
//     // 里面只用写函数名，不需要括号
//     auto f = std::bind(sayHello);
//     f();    // 等价于 sayHello()

//     return 0;
// }

// 绑定参数（预填参数）
// void add(int a, int b) { std::cout << a + b << std::endl; }

// // 提前把b固定为10，a留着之后传
// // std::placeholders::_1 是一个占位符，表示这个参数以后再传
// // _1代表第一个参数，_2代表第二个参数，以此类推
// auto f = std::bind(add,std::placeholders::_1, 10);

// int main() {
//     f(30);  // add(30,10)
//     f(11);  // add(11,10)
//     return 0;
// }

// // 绑定成员函数
// // 注意：成员函数有this指针，必须显式传
// class Timer {
// public:
//     void onTimeOut() { std::cout << "onTimeOut" << std::endl; }
//     void onTimeWithVal(int val)  { std::cout << "onTimeWithVal: " << val << std::endl; }
// };
// Timer t;
// // 绑定成员函数：函数指针，this
// auto f1 = std::bind(&Timer::onTimeOut, &t);
// // 绑定带参数的成员函数：函数指针，this，参数
// auto f2 = std::bind(&Timer::onTimeWithVal, &t, 666);
// auto f3 = std::bind(&Timer::onTimeWithVal, &t, std::placeholders::_1);
// int main() {
//     f1();
//     f2();
//     f3(777);
// }

// 总结：
// std::bind(函数, 参数1, 参数2, ...)
// 返回一个以后可以调用的函数对象
// 成员函数必须传this
// 不确定的参数用占位符std::placeholders::_1, _2, ...

// 再来看lambda
// lambda是一个没有名字的函数，可以在用到的地方定义

// auto f = []()->void { printf("hello\n"); };
// int main() {
//     f();
// }

// [捕获列表](参数列表) -> 返回类型 { 函数体 }
// lambda相当于一个独立的作用域，看不到外部变量，捕获列表负责捕获外部变量
// 参数列表和普通函数一样
// 返回类型一般可以省略
// 函数体正常写函数功能

int cnt = 0;
// 全局变量不需要捕获，lambda 内直接访问
auto f1 = []()  { cnt++; };
auto f2 = []()  { std::cout << cnt << std::endl; };
auto f3 = []()  { cnt++; std::cout << cnt << std::endl; };

int main() {
    f1();
    f2();
    f3();

    int num = 100;
    // 报错，Variable 'num' cannot be implicitly captured in a lambda with no capture-default specified (fixes available)
    //  auto f4 = []() { std::cout << num << std::endl; };

    // [num] 按值捕获，默认不可修改，加 mutable 允许修改副本（不影响原变量）
    auto f4 = [num]() mutable { num += 100; std::cout << num << std::endl; };
    // [&num] 按引用捕获，直接操作源对象
    auto f5 = [&num]() { num += 100; std::cout << num << std::endl; };

    f4();
    std::cout << "after f4, num = " << num << std::endl;
    f5();
    std::cout << "after f5, num = " << num << std::endl;
    return 0;
}

// [] 不捕获
// [=] 外部所有变量按值捕获
// [&] 外部所有变量按引用捕获
// [this] 在类内直接捕获this指针
// [=, &x] 按值捕获外部所有变量，按引用捕获x
// 组合很多，按需求来

// 注意：lambda 不能修改外部变量，除非用 mutable 关键字
// 注意：看到[&]或[&x]就立刻要想到：这个引用指向的变量，在lambda被调用时，还活着嘛？
// 危险做法
// SomeType getCallback() {
//     局部变量 x;
//     return [&x]() { 用 x };  // ← 函数返回后 x 死了，引用悬空
// }