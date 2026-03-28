#ifndef MUDUO_ANY_HPP
#define MUDUO_ANY_HPP

#include <typeinfo>
#include <cassert>
#include <utility>

namespace muduo {

class Any {
private:
    // 虚基类：定义类型擦除的接口
    class holder {
    public:
        virtual ~holder() {}
        virtual const std::type_info& type() const = 0;  // 返回存储的真实类型信息
        virtual holder* clone() = 0;                      // 深拷贝
    };

    // 模板子类：实际存储具体类型的值
    template <class T>
    class placeHolder : public holder {
    public:
        T _val;
        placeHolder(const T& val) : _val(val) {}
        
        virtual const std::type_info& type() const override { 
            return typeid(T); 
        }
        
        virtual holder* clone() override { 
            return new placeHolder(_val); 
        }
    };

    holder* _content;  // 指向实际存储的数据

public:
    // 空构造
    Any() : _content(nullptr) {}

    // 模板构造：接收任意类型，包装成 placeHolder
    template <class T>
    Any(const T& val) : _content(new placeHolder<T>(val)) {}

    // 拷贝构造：深拷贝
    Any(const Any& other) 
        : _content(other._content ? other._content->clone() : nullptr) {}

    // 析构：释放存储的数据
    ~Any() { delete _content; }

    // 交换指针（用于赋值）
    Any& swap(Any& other) {
        std::swap(_content, other._content);
        return *this;
    }

    // 取值：类型检查 + 强转 + 取地址
    template <class T>
    T* getPtr() {
        // 类型检查：确保取出的类型与存储的类型一致
        assert(typeid(T) == _content->type());
        // 强转成 placeHolder<T>，取出 _val，再取地址
        return &((placeHolder<T>*)_content)->_val;
    }

    // 赋值：构造临时 Any，交换指针（自动释放旧数据）
    template <class T>
    Any& operator=(const T& val) {
        Any(val).swap(*this);
        return *this;
    }

    // 拷贝赋值
    Any& operator=(const Any& other) {
        Any(other).swap(*this);
        return *this;
    }
};

} // namespace muduo

#endif
