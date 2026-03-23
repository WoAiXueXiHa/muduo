#include "http.hpp"
#include <iostream>

#define WWWROOT "./wwwroot/"

// 辅助函数：解析前端表单 application/x-www-form-urlencoded 格式的 Body 数据
void ParseUrlEncodedBody(const std::string& body, std::unordered_map<std::string, std::string>& form_data) {
    std::vector<std::string> kvs;
    // 1. 按 '&' 切割出键值对
    Util::Split(body, "&", &kvs); //
    for (auto& kv : kvs) {
        size_t pos = kv.find('=');
        if (pos != std::string::npos) {
            // 2. 切割 key 和 value，并进行 URL 解码 (把 + 转换为空格，把 %XX 转为字符)
            std::string key = Util::urlDecode(kv.substr(0, pos), true); //
            std::string val = Util::urlDecode(kv.substr(pos + 1), true); //
            form_data[key] = val;
        }
    }
}

// 核心业务：处理登录请求
void Login(const HttpRequest &req, HttpResponse *rsp) 
{
    // 打印接收到的完整 body 方便调试观察
    std::cout << "[DEBUG] 收到 POST /login 请求，Body内容为: " << req._body << std::endl; //

    // 解析前端传来的正文数据
    std::unordered_map<std::string, std::string> form_data;
    ParseUrlEncodedBody(req._body, form_data); //

    std::string username = form_data["username"];
    std::string password = form_data["password"];

    std::string rsp_body;
    // 模拟数据库校验（这里硬编码 admin 和 123456）
    if (username == "admin" && password == "123456") {
        // 登录成功，拼装一个绿色的成功 HTML 页面
        rsp_body = "<html><head><meta charset='utf-8'></head><body style='background:#e6f4ea; text-align:center; padding-top:100px;'>";
        rsp_body += "<h2>🎉 登录成功！欢迎回来，" + username + "</h2>";
        rsp_body += "<p>Muduo HTTP Server 状态：高并发引擎运行良好 🚀</p>";
        rsp_body += "<a href='/'>返回首页</a></body></html>";
    } else {
        // 登录失败，拼装一个红色的失败 HTML 页面
        rsp_body = "<html><head><meta charset='utf-8'></head><body style='background:#fce8e6; text-align:center; padding-top:100px;'>";
        rsp_body += "<h2 style='color:red;'>❌ 登录失败！</h2>";
        rsp_body += "<p>用户名或密码错误。（提示：账号 admin，密码 123456）</p>";
        rsp_body += "<a href='/'>重新登录</a></body></html>";
    }

    // 将动态生成的 HTML 页面作为响应正文返回，并设置类型为 text/html
    rsp->setBody(rsp_body, "text/html"); //
}

int main()
{
    // 监听 8080 端口
    HttpServer server(3389); //
    server.setThreadCnt(3); // 开启三个子 EventLoop 线程处理业务
    
    // 设置静态资源根目录，当浏览器请求网址时，
    // HttpServer 的 IsFileHandler 会自动去 wwwroot 下找 index.html 响应给前端
    server.setBaseDir(WWWROOT); //
    
    // 注册 /login 路由的回调函数
    server.Post("/login", Login); //
    
    std::cout << "🌐 服务器启动成功！请在浏览器访问" << std::endl;
    server.Listen(); // 启动 Reactor 事件循环
    return 0;
}