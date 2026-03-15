#include "../server.hpp"
#include <iostream>
#include "./http.hpp"

static size_t Split(const std::string& src, const std::string& sep, std::vector<std::string>* arr) {
    size_t offset = 0;
    while(offset < src.size()) {
        size_t pos = src.find(sep, offset);
        // 走到末尾找不到的情况，整个字符串作为子串返回
        // abcbbbb.
        if(pos == std::string::npos) {
            if(offset < src.size()) {   // 只要offset没越界，把最后的部分也要塞进去
                arr->push_back(src.substr(offset));
            }
            break;
        }

        // 遇到连续的分割符sep，要跳过sep
        if(pos == offset) {
            offset = pos + sep.size();
            continue;
        }

        // 正常的分割符sep，把子串塞进去
        arr->push_back(src.substr(offset, pos - offset));
        offset = pos + sep.size();
    }
    return arr->size();
}

int main() {
    /* std::string sep = ".";
    std::string src = "...";
    std::vector<std::string> arr;
    Split(src, sep, &arr);
    for(auto& s : arr) {
        std::cout << s << std::endl;    
    }
    */

    return 0;
}