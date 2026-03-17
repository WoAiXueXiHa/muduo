#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <regex>
#include <sys/stat.h>
#include "../server.hpp"

#define DEFALT_TIMEOUT 10

std::unordered_map<int, std::string> _statu_msg = {
    {100,  "Continue"},
    {101,  "Switching Protocol"},
    {102,  "Processing"},
    {103,  "Early Hints"},
    {200,  "OK"},
    {201,  "Created"},
    {202,  "Accepted"},
    {203,  "Non-Authoritative Information"},
    {204,  "No Content"},
    {205,  "Reset Content"},
    {206,  "Partial Content"},
    {207,  "Multi-Status"},
    {208,  "Already Reported"},
    {226,  "IM Used"},
    {300,  "Multiple Choice"},
    {301,  "Moved Permanently"},
    {302,  "Found"},
    {303,  "See Other"},
    {304,  "Not Modified"},
    {305,  "Use Proxy"},
    {306,  "unused"},
    {307,  "Temporary Redirect"},
    {308,  "Permanent Redirect"},
    {400,  "Bad Request"},
    {401,  "Unauthorized"},
    {402,  "Payment Required"},
    {403,  "Forbidden"},
    {404,  "Not Found"},
    {405,  "Method Not Allowed"},
    {406,  "Not Acceptable"},
    {407,  "Proxy Authentication Required"},
    {408,  "Request Timeout"},
    {409,  "Conflict"},
    {410,  "Gone"},
    {411,  "Length Required"},
    {412,  "Precondition Failed"},
    {413,  "Payload Too Large"},
    {414,  "URI Too Long"},
    {415,  "Unsupported Media Type"},
    {416,  "Range Not Satisfiable"},
    {417,  "Expectation Failed"},
    {418,  "I'm a teapot"},
    {421,  "Misdirected Request"},
    {422,  "Unprocessable Entity"},
    {423,  "Locked"},
    {424,  "Failed Dependency"},
    {425,  "Too Early"},
    {426,  "Upgrade Required"},
    {428,  "Precondition Required"},
    {429,  "Too Many Requests"},
    {431,  "Request Header Fields Too Large"},
    {451,  "Unavailable For Legal Reasons"},
    {501,  "Not Implemented"},
    {502,  "Bad Gateway"},
    {503,  "Service Unavailable"},
    {504,  "Gateway Timeout"},
    {505,  "HTTP Version Not Supported"},
    {506,  "Variant Also Negotiates"},
    {507,  "Insufficient Storage"},
    {508,  "Loop Detected"},
    {510,  "Not Extended"},
    {511,  "Network Authentication Required"}
};

std::unordered_map<std::string, std::string> _mime_msg = {
    {".aac",        "audio/aac"},
    {".abw",        "application/x-abiword"},
    {".arc",        "application/x-freearc"},
    {".avi",        "video/x-msvideo"},
    {".azw",        "application/vnd.amazon.ebook"},
    {".bin",        "application/octet-stream"},
    {".bmp",        "image/bmp"},
    {".bz",         "application/x-bzip"},
    {".bz2",        "application/x-bzip2"},
    {".csh",        "application/x-csh"},
    {".css",        "text/css"},
    {".csv",        "text/csv"},
    {".doc",        "application/msword"},
    {".docx",       "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
    {".eot",        "application/vnd.ms-fontobject"},
    {".epub",       "application/epub+zip"},
    {".gif",        "image/gif"},
    {".htm",        "text/html"},
    {".html",       "text/html"},
    {".ico",        "image/vnd.microsoft.icon"},
    {".ics",        "text/calendar"},
    {".jar",        "application/java-archive"},
    {".jpeg",       "image/jpeg"},
    {".jpg",        "image/jpeg"},
    {".js",         "text/javascript"},
    {".json",       "application/json"},
    {".jsonld",     "application/ld+json"},
    {".mid",        "audio/midi"},
    {".midi",       "audio/x-midi"},
    {".mjs",        "text/javascript"},
    {".mp3",        "audio/mpeg"},
    {".mpeg",       "video/mpeg"},
    {".mpkg",       "application/vnd.apple.installer+xml"},
    {".odp",        "application/vnd.oasis.opendocument.presentation"},
    {".ods",        "application/vnd.oasis.opendocument.spreadsheet"},
    {".odt",        "application/vnd.oasis.opendocument.text"},
    {".oga",        "audio/ogg"},
    {".ogv",        "video/ogg"},
    {".ogx",        "application/ogg"},
    {".otf",        "font/otf"},
    {".png",        "image/png"},
    {".pdf",        "application/pdf"},
    {".ppt",        "application/vnd.ms-powerpoint"},
    {".pptx",       "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
    {".rar",        "application/x-rar-compressed"},
    {".rtf",        "application/rtf"},
    {".sh",         "application/x-sh"},
    {".svg",        "image/svg+xml"},
    {".swf",        "application/x-shockwave-flash"},
    {".tar",        "application/x-tar"},
    {".tif",        "image/tiff"},
    {".tiff",       "image/tiff"},
    {".ttf",        "font/ttf"},
    {".txt",        "text/plain"},
    {".vsd",        "application/vnd.visio"},
    {".wav",        "audio/wav"},
    {".weba",       "audio/webm"},
    {".webm",       "video/webm"},
    {".webp",       "image/webp"},
    {".woff",       "font/woff"},
    {".woff2",      "font/woff2"},
    {".xhtml",      "application/xhtml+xml"},
    {".xls",        "application/vnd.ms-excel"},
    {".xlsx",       "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
    {".xml",        "application/xml"},
    {".xul",        "application/vnd.mozilla.xul+xml"},
    {".zip",        "application/zip"},
    {".3gp",        "video/3gpp"},
    {".3g2",        "video/3gpp2"},
    {".7z",         "application/x-7z-compressed"}
};

class Util {
public:
    // 字符串分割
    // 解析头部时，按照"\r\n"切割每一行，或按照"&"切割请求参数
    static size_t Split(const std::string& src, const std::string& sep, std::vector<std::string>* arr) {
        size_t offset = 0;
        while(offset < src.size()) {
            size_t pos = src.find(sep, offset);
            if(pos == std::string::npos) {
                if(pos == src.size()) break;
                arr->push_back(src.substr(offset));
                return arr->size();
            }
            if(pos == offset) {
                offset = pos + sep.size();
                continue;
            }
            
            arr->push_back(src.substr(offset, pos - offset));
            offset = pos + sep.size();
        }
        return arr->size();
    }

    // 读取文件到内存
    static bool readFile(const std::string &filename, std::string *buf) {
        std::ifstream ifs(filename, std::ios::binary);
        if (ifs.is_open() == false) return false;
        
        ifs.seekg(0, ifs.end);
        size_t fsize = ifs.tellg();  
        ifs.seekg(0, ifs.beg);
        
        buf->resize(fsize); 
        ifs.read(&(*buf)[0], fsize);
        bool good = ifs.good();
        ifs.close();
        return good;
    }

    // 写入文件
    static bool writeFile(const std::string &filename, const std::string &buf) {
        std::ofstream ofs(filename, std::ios::binary | std::ios::trunc);
        if (ofs.is_open() == false) return false;
        ofs.write(buf.c_str(), buf.size());
        bool good = ofs.good();
        ofs.close();
        return good;
    }

    // URL编码
    // 保留字母数字和 . - _ ~，把空格转成 '+' (查询参数中)，其他的转成 "%HH" 格式
    static std::string urlEncode(const std::string url, bool convert_space_to_plus) {
        std::string res;
        for (auto &c : url) {
            if (c == '.' || c == '-' || c == '_' || c == '~' || isalnum(c)) {
                res += c;
                continue;
            }
            if (c == ' ' && convert_space_to_plus == true) {
                res += '+';
                continue;
            }
            char tmp[4] = {0};
            snprintf(tmp, 4, "%%%02X", c); // 格式化为 % 加上两位的十六进制大写字母
            res += tmp;
        }
        return res;
    }

    // URL解码
    static char HEXTOI(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        else if (c >= 'a' && c <= 'z') return c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
        return -1; 
    }
    // 把浏览器发来的 "%2B" 还原成真正的 "+"
    static std::string urlDecode(const std::string url, bool convert_plus_to_space) {
        std::string res;
        for (int i = 0; i < url.size(); i++) {
            // 历史遗留问题：查询字符串里的 + 号往往代表空格，需还原
            if (url[i] == '+' && convert_plus_to_space == true) {
                res += ' ';
                continue;
            }
            // 遇到 % 就把后面两个字符按照十六进制计算还原
            if (url[i] == '%' && (i + 2) < url.size()) {
                char v1 = HEXTOI(url[i + 1]);
                char v2 = HEXTOI(url[i + 2]);
                char v = v1 * 16 + v2;
                res += v;
                i += 2;
                continue;
            }
            res += url[i];
        }
        return res;
    }

    // 查状态码字典
    static std::string statuDesc(int statu) {
        auto it = _statu_msg.find(statu);
        if (it != _statu_msg.end()) return it->second;
        return "Unknow";
    }

    // 根据文件后缀名获取 MIME 类型
    static std::string extMime(const std::string &filename) {
        size_t pos = filename.find_last_of('.');
        if (pos == std::string::npos) return "application/octet-stream";
        
        std::string ext = filename.substr(pos);
        auto it = _mime_msg.find(ext);
        if (it == _mime_msg.end()) return "application/octet-stream";
        
        return it->second;
    }

    // 文件系统判断
    static bool isDirectory(const std::string &filename) {
        struct stat st;
        int ret = stat(filename.c_str(), &st);
        if (ret < 0) return false;
        return S_ISDIR(st.st_mode);
    }
    static bool isRegular(const std::string &filename) {
        struct stat st;
        int ret = stat(filename.c_str(), &st);
        if (ret < 0) return false;
        return S_ISREG(st.st_mode);
    }

    // 防御目录穿越攻击
    // 防止用户发送类似 GET /../../../etc/passwd HTTP/1.1 这样的请求
    // 计算目录深度，进一层+1，只要深度小于0，说明企图跳出根目录，直接拦死
    static bool validPath(const std::string& path) {
        std::vector<std::string> subdir;
        Split(path, "/", &subdir);
        int level = 0;
        for(auto& dir : subdir) {
            if(dir == "..") {
                level--;
                if(level < 0) return false;
                continue;
            }
            level++;
        }
        return true;
    }
};

class HttpRequest{
public:
    std::string _method;        // 请求方法
    std::string _path;          // 请求资源路径
    std::string _version;       // HTTP 版本
    std::string _body;          // 请求正文
    std::smatch _match;         // 资源路径正则匹配结果
    // 头部字段用哈希表存放，如果用字符串那么就糅杂在一起，还需要进一步分割
    std::unordered_map<std::string, std::string> _headers; // 请求头
    std::unordered_map<std::string, std::string> _params;  // 请求参数
public:
    HttpRequest() :_version("HTTP/1.1") {}
    void reSet() {
        _method.clear();
        _path.clear();
        _version = "HTTP/1.1";
        _body.clear();
        std::smatch().swap(_match);
        _headers.clear();
        _params.clear();
    }

    // 获头部字段
    std::string getHeader(const std::string& key) const {
        auto it = _headers.find(key);
        if(it == _headers.end()) return "";
        return it->second;
    }

    // 获请求参数
    std::string getParam(const std::string& key) const {
        auto it = _params.find(key);
        if(it == _params.end()) return "";
        return it->second;
    }

    // 获取正文长度
    size_t getContentLength() const {
        bool ret = hasHeader("Content-Length");
        if(ret == false) return 0;
        std::string clen = getHeader("Content-Length");
        return std::stol(clen);
    }

    // 判断是否长连接
    bool isKeepAlive() const {
        // 没有Connection字段，或者有这个字段但是值是close是短连接
        if(hasHeader("Connection") == true && getHeader("Connection") == "keep-alive")
            return true;
        return false;
    }

    // 判断头部字段是否有指定值
    bool hasHeader(const std::string& key) const {
        auto it = _headers.find(key);
        if(it == _headers.end()) return false;
        return true;
    }

    void setHeader(const std::string& key, const std::string& value) {
        _headers[key] = value;
    }
    void setParam(const std::string& key, const std::string& value) {
        _params[key] = value;
    }
};

class HttpResponse{
public:
    int _statu;                // 状态码
    bool _redirect_flag;       // 是否重定向
    std::string _redirect_url; // 重定向地址
    std::string _body;         // 响应正文
    std::unordered_map<std::string, std::string> _headers; // 响应头
public:
    HttpResponse() :_statu(200), _redirect_flag(false) {}
    HttpResponse(int statu) :_statu(statu), _redirect_flag(false) {}

    void reSet() {
        _statu = 200;
        _redirect_flag = false;
        _redirect_url.clear();
        _body.clear();
        _headers.clear();
    }

    // 获取头部字段指定值
    std::string getHeader(const std::string& key) const {
        auto it = _headers.find(key);
        if(it == _headers.end()) return "";
        return it->second;
    }

    // 插入头部字段
    void setHeader(const std::string& key, const std::string& value) {
        _headers[key] = value;
    }

    // 判断头部字段是否有指定值
    bool hasHeader(const std::string& key) const {
        auto it = _headers.find(key);
        if(it == _headers.end()) return false;
        return true;
    }

    // 设置正文
    void setBody(const std::string& body, const std::string& type = "text/html") {
        _body = body;
        setHeader("Content-Type", type);
    }

    // 设置重定向
    void setRedirect(const std::string& url, int statu = 302) {
        _redirect_flag = true;
        _redirect_url = url;
        _statu = statu;
    }

    // 判断是否长连接
    bool isKeepAlive() const {
        // 没有Connection字段，或者有这个字段但是值是close是短连接
        if(hasHeader("Connection") == true && getHeader("Connection") == "keep-alive")
            return true;
        return false;
    }
};


typedef enum {
    RECV_HTTP_ERROR,      // 接收 HTTP 错误
    RECV_HTTP_LINE,       // 在请求行
    RECV_HTTP_HEAD,       // 在请求头
    RECV_HTTP_BODY,       // 在请求正文
    RECV_HTTP_DONE        // 解析出HttpRequest对象
} HttpRecvStatu;

#define MAX_LINE 8192

class HttpContext {
private:
    int _resp_statu;             // 响应状态码
    HttpRecvStatu _recv_statu;   // 接收状态
    HttpRequest _request;        // 解析得到的HttpRequest对象

private:
    // 解析请求行
     bool parseHttpLine(const std::string &line) {
        std::smatch matches;
        std::regex e("(GET|HEAD|POST|PUT|DELETE) ([^?]*)(?:\\?(.*))? (HTTP/1\\.[01])(?:\n|\r\n)?", std::regex::icase);
        bool ret = std::regex_match(line, matches, e);
        if (ret == false) {
            _recv_statu = RECV_HTTP_ERROR;
            _resp_statu = 400;//BAD REQUEST
            return false;
        }
        //请求方法的获取
        _request._method = matches[1];
        std::transform(_request._method.begin(), _request._method.end(), _request._method.begin(), ::toupper);
        //资源路径的获取，需要进行URL解码操作，但是不需要+转空格
        _request._path = Util::urlDecode(matches[2], false);
        //协议版本的获取
        _request._version = matches[4];
        //查询字符串的获取与处理
        std::vector<std::string> query_string_arry;
        std::string query_string = matches[3];
        //查询字符串的格式 key=val&key=val....., 先以 & 符号进行分割，得到各个字串
        Util::Split(query_string, "&", &query_string_arry);
        //针对各个字串，以 = 符号进行分割，得到key 和val， 得到之后也需要进行URL解码
        for (auto &str : query_string_arry) {
            size_t pos = str.find("=");
            if (pos == std::string::npos) {
                _recv_statu = RECV_HTTP_ERROR;
                _resp_statu = 400;//BAD REQUEST
                return false;
            }
            std::string key = Util::urlDecode(str.substr(0, pos), true);  
            std::string val = Util::urlDecode(str.substr(pos + 1), true);
            _request.setParam(key, val);
        }
        return true;
    }

    // 获取请求行
    bool recvHttpLine(Buffer* buf) {
        if(_recv_statu != RECV_HTTP_LINE) return false;

        // 1. 尝试从缓冲区拿出一行数据
        std::string line = buf->readLineAndMove();

        if(0 == line.size()) {
            // 情况一：没拿到换行符，且当前缓冲区里的数据已经超过规定的最大长度
            // 说明请求行太长了，直接返回错误
            if(buf->getReadableSize() > MAX_LINE) {
                _recv_statu = RECV_HTTP_ERROR;
                _resp_statu = 414; // URI Too Long
                return false;
            }
            // 情况二：数据不够一行，但是长度在安全范围内，半包
            // 说明还没收到完整的请求行，继续等待
            return true;
        }
        // 情况三：拿到了一行，但是这一行超过了安全阈值，粘包
        if(line.size() > MAX_LINE) {
            _recv_statu = RECV_HTTP_ERROR;
            _resp_statu = 400; // Bad Request
            return false;
        }
        
        // 完美数据，交给正则函数去提取方法、路径、版本
        bool ret = parseHttpLine(line);
        if(ret == false) return false;

        // 成功解析出请求行，进入头部解析
        _recv_statu = RECV_HTTP_HEAD;
        return true;
    }

    // 解析请求头
     bool parseHttpHead(std::string &line) {
        //key: val\r\nkey: val\r\n....
        if (line.back() == '\n') line.pop_back();//末尾是换行则去掉换行字符
        if (line.back() == '\r') line.pop_back();//末尾是回车则去掉回车字符
        size_t pos = line.find(": ");
        if (pos == std::string::npos) {
            _recv_statu = RECV_HTTP_ERROR;
            _resp_statu = 400;//
            return false;
        }
        std::string key = line.substr(0, pos);  
        std::string val = line.substr(pos + 2);
        _request.setHeader(key, val);
        return true;
    }

    // 获取请求头 key: value\r\n
    bool recvHttpHead(Buffer* buf) {
        if(_recv_statu != RECV_HTTP_HEAD) return false;

        while(1) {
            std::string line = buf->readLineAndMove();

            if(0 == line.size()) {
                // 情况一：没拿到换行符，且当前缓冲区里的数据已经超过规定的最大长度
                if(buf->getReadableSize() > MAX_LINE) {
                    _recv_statu = RECV_HTTP_ERROR;
                    _resp_statu = 414; // URI Too Long
                    return false;
                }
                // 情况二：数据不够一行，但是长度在安全范围内，半包
                // 说明还没收到完整的请求头，继续等待
                return true;
            }
            
            // 情况三：拿到了一行，但是这一行超过了安全阈值，粘包
            if(line.size() > MAX_LINE) {
                _recv_statu = RECV_HTTP_ERROR;
                _resp_statu = 400; // Bad Request
                return false;
            }

            // 遇到了换行符，说明头部结束了，进入正文解析
            if(line == "\n" || line == "\r\n") break;

            bool ret = parseHttpHead(line);
            if(ret == false) return false;
        }
        // 进入正文获取阶段
        _recv_statu = RECV_HTTP_BODY;
        return true;
    }

    // 获取请求正文
    bool recvHttpBody(Buffer* buf) {
        if(_recv_statu != RECV_HTTP_BODY) return false;
        // 获取正文长度
        size_t content_length = _request.getContentLength();
        if(0 == content_length) {
            // 没有正文，直接进入完成状态
            _recv_statu = RECV_HTTP_DONE;
            return true;
        }

        // 获取实际要接收的长度
        // 总长100，当前_request._body里已经装了20，还需要80
        size_t real_len = content_length - _request._body.size(); // 实际要接收的

        // 接收正文放到body里，需要考虑当前缓冲区的数据，是否全部的正文
        if(buf->getReadableSize() >= real_len) {
            _request._body.append(buf->getReadIndex(), real_len);
            buf->moveReadOffset(real_len);
            _recv_statu = RECV_HTTP_DONE;
            return true;
        }

        // 缓冲区里没有足够的数据，取出当前数据，等待新数据
        _request._body.append(buf->getReadIndex(), buf->getReadableSize());
        buf->moveReadOffset(buf->getReadableSize());
        return true;
    }

    

public:
    HttpContext() :_resp_statu(200), _recv_statu(RECV_HTTP_LINE) {}

    // 每次处理完一个请求，必须重置，要复用这个连接处理其它请求
    void reSet() {
        _resp_statu = 200;
        _recv_statu = RECV_HTTP_LINE;
        _request.reSet();
    }
    
    // 获取状态码
    int getRespStatu() const {
        return _resp_statu;
    }
    // 获取接收状态信息
    HttpRecvStatu getRecvStatu() const {
        return _recv_statu;
    }
    // 获取请求对象
    HttpRequest& getRequest() {
        return _request;
    }

    // 接收并解析HTTP请求
    void recvHttpRequest(Buffer* buf) {
        // 不同的状态，做不同的事情
        // 但是一定不能break！处理完请求行，接着处理请求头！
        switch(_recv_statu) {
            case RECV_HTTP_LINE: recvHttpLine(buf);
            case RECV_HTTP_HEAD: recvHttpHead(buf);
            case RECV_HTTP_BODY: recvHttpBody(buf);
        }
    }
};


class HttpServer {
private:
    using Handler = std::function<void(const HttpRequest&, HttpResponse*)>;
    using Handlers = std::vector<std::pair<std::regex, Handler>>;

    Handlers _get_route;
    Handlers _post_route;
    Handlers _put_route;
    Handlers _delete_route;
    std::string _basedir;       // 静态资源根目录
    TcpServer _server;          // TCP服务器

private:
    // 组装一个简单的 4xx/5xx HTML 报错页面
    void errorHandler(const HttpRequest &req, HttpResponse *rsp) {
        std::string body = "<html><body><h1>" + std::to_string(rsp->_statu) + 
                           " " + Util::statuDesc(rsp->_statu) + "</h1></body></html>";
        rsp->setBody("body", "text/html");
    }

    // 把 HttpResponse 对象，重新变成 HTTP 字符串发给网卡
    void writeReponse(const ptrConnection &conn, const HttpRequest &req, HttpResponse &rsp) {
        // 完善头部：长短连接、正文长度、数据类型
        if (req.isKeepAlive() == false) rsp.setHeader("Connection", "close");
        else rsp.setHeader("Connection", "keep-alive");
        
        if (!rsp._body.empty() && !rsp.hasHeader("Content-Length"))
            rsp.setHeader("Content-Length", std::to_string(rsp._body.size()));
        if (!rsp._body.empty() && !rsp.hasHeader("Content-Type"))
            rsp.setHeader("Content-Type", "application/octet-stream");
        if (rsp._redirect_flag)
            rsp.setHeader("Location", rsp._redirect_url);

        // 严格按照 HTTP 报文格式拼接：状态行 -> 头部 -> 空行 -> 正文
        std::stringstream rsp_str;
        rsp_str << req._version << " " << rsp._statu << " " << Util::statuDesc(rsp._statu) << "\r\n";
        for (auto &head : rsp._headers) {
            rsp_str << head.first << ": " << head.second << "\r\n";
        }
        rsp_str << "\r\n" << rsp._body;

        // 调用底层 TCP 接口发送数据
        conn->Send(rsp_str.str().c_str(), rsp_str.str().size());
    }

    // 静态资源处理,去硬盘里读文件
    void fileHandler(const HttpRequest &req, HttpResponse *rsp) {
        std::string req_path = _basedir + req._path;
        if (req._path.back() == '/') req_path += "index.html"; // 默认主页
        
        bool ret = Util::readFile(req_path, &rsp->_body);
        if (ret == false) {
            rsp->_statu = 404;
            return;
        }
        rsp->setHeader("Content-Type", Util::extMime(req_path));
    }

    // 正则分发器，遍历路由表，匹配就执行
    void Dispatcher(HttpRequest &req, HttpResponse *rsp, Handlers &handlers) {
        for (auto &handler : handlers) {
            const std::regex &re = handler.first;
            const Handler &functor = handler.second;
            if (std::regex_match(req._path, req._match, re)) {
                return functor(req, rsp); // 匹配成功，执行业务代码！
            }
        }
        rsp->_statu = 404; // 遍历完没找到，报 404
    }

    // 总控路由，决定是找静态文件，还是找动态接口
    bool isFileHandler(const HttpRequest &req) {
        // 1. 必须设置了静态资源根目录
        if (_basedir.empty()) {
            return false;
        }
        // 2. 请求方法，必须是GET / HEAD请求方法
        if (req._method != "GET" && req._method != "HEAD") {
            return false;
        }
        // 3. 请求的资源路径必须是一个合法路径
        if (Util::validPath(req._path) == false) {
            return false;
        }
        // 4. 请求的资源必须存在,且是一个普通文件
        //    有一种请求比较特殊 -- 目录：/, /image/， 这种情况给后边默认追加一个 index.html
        // index.html    /image/a.png
        // 不要忘了前缀的相对根目录,也就是将请求路径转换为实际存在的路径  /image/a.png  ->   ./wwwroot/image/a.png
        std::string req_path = _basedir + req._path;//为了避免直接修改请求的资源路径，因此定义一个临时对象
        if (req._path.back() == '/')  {
            req_path += "index.html";
        }
        if (Util::isRegular(req_path) == false) {
            return false;
        }
        return true;
    }
    void Route(HttpRequest &req, HttpResponse *rsp) {
        // IsFileHandler 是一个判断路径是不是请求文件的简单函数（比如看后缀，看基于 basedir 是否存在）
        if (isFileHandler(req) == true) {
            return fileHandler(req, rsp);
        }
        // 不是文件，那就去查对应的动态接口路由表
        if (req._method == "GET" || req._method == "HEAD") return Dispatcher(req, rsp, _get_route);
        else if (req._method == "POST") return Dispatcher(req, rsp, _post_route);
        else if (req._method == "PUT") return Dispatcher(req, rsp, _put_route);
        else if (req._method == "DELETE") return Dispatcher(req, rsp, _delete_route);
        
        rsp->_statu = 405; // Method Not Allowed
    }

    // 设置上下文，底层TCP刚建立连接时触发
    void onConnected(const ptrConnection& conn) {
        // 利用Any，给新连接绑定一个全新的HTTP状态
        conn->setContext(HttpContext());
    }

    // 当TCP接收到数据时触发
    void onMessage(const ptrConnection& conn, Buffer* buf) {
        // 只要缓冲区里有4数据，就一直循环处理
        while(buf->getReadableSize() > 0) {
            HttpContext* context = conn->getContext()->getPtr<HttpContext>();
            context->recvHttpRequest(buf);

            HttpRequest& req = context->getRequest();
            HttpResponse rsp(context->getRespStatu());

            if(context->getRespStatu() >= 400) {
                errorHandler(req, &rsp);                // 出错界面
                writeReponse(conn, req, rsp);           // 发送报错页面
                context->reSet();                       // 重置状态
                buf->moveReadOffset(buf->getReadableSize()); // 跳过当前请求
                conn->Shutdown();                       // 关闭连接
            }

            // 半包
            if(context->getRecvStatu() != RECV_HTTP_DONE) {
                return;
            }

            // 业务分发
            Route(req, &rsp);

            // 长连接重置
            context->reSet();

            if(rsp.isKeepAlive() == false) conn->Shutdown(); // 短连接关闭连接
        }
    }

public:
    HttpServer(int port, int timeout = DEFALT_TIMEOUT)
        :_server(port) {
        _server.setEnableInactiveRelease(timeout);
        _server.setConnectedCallBack(std::bind(&HttpServer::onConnected, this, std::placeholders::_1));
        _server.setMessageCallBack(std::bind(&HttpServer::onMessage, this, std::placeholders::_1, std::placeholders::_2));
    }

    void setBaseDir(const std::string& path) {
        assert(Util::isDirectory(path) == true);
        _basedir = path;
    }

    void Get(const std::string& pattern, const Handler& handler) {
        _get_route.push_back(std::make_pair(std::regex(pattern), handler));
    }

    void Post(const std::string& pattern, const Handler& handler) {
        _post_route.push_back(std::make_pair(std::regex(pattern), handler));
    }

    void Put(const std::string& pattern, const Handler& handler) {
        _put_route.push_back(std::make_pair(std::regex(pattern), handler));
    }

    void Delete(const std::string& pattern, const Handler& handler) {
        _delete_route.push_back(std::make_pair(std::regex(pattern), handler));
    }

    void setThreadCnt(int cnt) {
        _server.setThreadCnt(cnt);
    }

    void Listen() {
        _server.Loop();
    }
};