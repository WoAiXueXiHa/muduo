#include "../server.hpp"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <regex>
#include <sys/stat.h>

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
    // 分割字符串
    static size_t Split(const std::string& src, const std::string& sep, std::vector<std::string>* arr) {
        size_t offset = 0;
        while(offset < src.size()) {
            size_t pos = src.find(sep, offset);
            // 走到末尾找不到的情况，整个字符串作为子串返回
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
     
    //读取文件的所有内容，将读取的内容放到一个Buffer中
    static bool readFile(const std::string& filename, std::string* buf) {
        std::ifstream ifs(filename, std::ios::binary);
        if(ifs.is_open() == false) {
            printf("OPEN %s FILE FAILED!!", filename.c_str());
            return false;
        }

        size_t fsize = 0;
        ifs.seekg(0, std::ios::end);    // 跳转读写位置到末尾
        fsize = ifs.tellg();            //获取当前读写位置相对于起始位置的偏移量，从末尾偏移刚好就是文件大小
        ifs.seekg(0, ifs.beg);          //跳转到起始位置
        buf->resize(fsize);             //开辟文件大小的空间
        ifs.read(&(*buf)[0], fsize);
        if (ifs.good() == false) {
            printf("READ %s FILE FAILED!!", filename.c_str());
            ifs.close();
            return false;
        }
        ifs.close();
        return true;
    }

    //向文件写入数据
    static bool writeFile(const std::string &filename, const std::string &buf) {
        std::ofstream ofs(filename, std::ios::binary | std::ios::trunc);
        if (ofs.is_open() == false) {
            printf("OPEN %s FILE FAILED!!", filename.c_str());
            return false;
        }
        ofs.write(buf.c_str(), buf.size());
        if (ofs.good() == false) {
            ERR_LOG("WRITE %s FILE FAILED!", filename.c_str());
            ofs.close();    
            return false;
        }
        ofs.close();
        return true;
    }

    // URL编码格式：将特殊字符的ASCII值转换成两个16进制字符，前缀%      C++ -> C%2B%2B
    // 不编码的字符：. - _ ~ 字母，数字属于绝对不编码字符
    // 查询字符串中的空格，需要编码为+，解码就是+转成空格
    static char HEX2I(char c) {
        if(c >= '0' && c <= '9') return c - '0';
        // 这里很容易错！编码格式是%HH，有两位！
        if(c >= 'A' && c <= 'Z') return c - 'A' + 10;
        if(c >= 'a' && c <= 'z') return c - 'a' + 10;
        return -1;
    }

    static std::string urlDecode(const std::string url, bool convertPlus2Space) {
        std::string res;
        for(int i = 0; i < url.size(); i++) {
            if(url[i] == '+' && convertPlus2Space == true) {
                res += ' ';
                continue;
            }
            // 解析百分号编码：遇到%，就吃掉它和它后面的两个字符
            if(url[i] == '%' && (i + 2) < url.size()) {
                char v1 = HEX2I(url[i+1]);
                char v2 = HEX2I(url[i+2]);
                // 位运算还原：16进制高位 * 16 + 十六进制低位
                char v = v1 * 16 + v2;
                res += v;
                i += 2;
                continue;
            }
            res += url[i];
        }
        return res;
    }

    // 路径合法性校验
    // 请求 GET /../../etc/passwd HTTP/1.1
    static bool validPath(const std::string& path) {
        std::vector<std::string> subdir;
        Split(path, "/", &subdir);
        int level = 0;
        for(auto& dir : subdir) {
            if(dir == "..") {
                level--;
                // 哪怕进去了10层，只要退到-1层，就是越界
                if(level < 0) return false;
                continue;   // 继续检查是否回退
            }
            level++;
        }
        return true;
    }

    // 获取响应状态码描述信息
    static std::string statuDesc(int statu) {
        auto it = _statu_msg.find(statu);
        if(it != _statu_msg.end()) return it->second;
        return "Unknown";
    }

    // 根据文件后缀名获取文件mime
    static std::string extMime(const std::string& filename) {
        // a.x.txt 先获取文件扩展名
        size_t pos = filename.find_last_of('.');
        if(pos == std::string::npos) return "application/octet-stream";

        // 根据文件扩展名，获取mime类型
        std::string ext = filename.substr(pos);
        auto it = _mime_msg.find(ext);
        if(it ==  _mime_msg.end()) return "application/octet-stream";
        return it->second;
    }

    // 判断一个文件是否为目录
    static bool isDirectory(const std::string& filename) {
        struct stat st;
        int ret = stat(filename.c_str(), &st);
        if(ret < 0) return false;
        return S_ISDIR(st.st_mode);
    }

    // 判断一个文件是否为普通文件
    static bool isRegularFile(const std::string& filename) {
        struct stat st;
        int ret = stat(filename.c_str(), &st);
        if(ret < 0) return false;
        return S_ISREG(st.st_mode);
    }
};

class HttpRequest {
public:
    std::string _method;        // 请求方法
    std::string _path;          // 请求路径
    std::string _version;       // HTTP版本
    std::string _body;          // 请求正文
    std::smatch _matches;       // 正则匹配结果
    std::unordered_map<std::string, std::string> _headers;  // 头部字段
    std::unordered_map<std::string, std::string> _params;   // 查询字符串

public:
    HttpRequest() :_version("HTTP/1.1") {}

    void reSet() {
        _method.clear();
        _path.clear();
        _version = "HTTP/1.1";
        _body.clear();
        std::smatch match;
        _matches.swap(match);
        _headers.clear();
        _params.clear();
    }

    // 插入头部字段
    void setHeader(const std::string& key, const std::string& value) {
        _headers.insert(std::make_pair(key, value));
    }

    // 判断是否存在指定头部字段
    bool hasHeader(const std::string& key) const {
        auto it = _headers.find(key);
        if(it == _headers.end()) return false;
        return true;
    }

    // 获取指定头部字段的值
    std::string getHeader(const std::string& key) const {
        auto it = _headers.find(key);
        if(it == _headers.end()) return "";
        return it->second;
    }

    // 插入查询字符串
    void setParam(const std::string& key, const std::string& value) {
        _params.insert(std::make_pair(key, value));
    }

    // 判断是否存在指定查询字符串
    bool hasParam(const std::string& key) const {
        auto it = _params.find(key);
        if(it == _params.end()) return false;
        return true;
    }

    // 获取指定的查询字符串
    std::string getParam(const std::string& key) const {
        auto it = _params.find(key);
        if(it == _params.end()) return "";  
        return it->second;
    }

    // 获取正文长度
    size_t contentLength() const {
        bool ret = hasHeader("Content-Length");
        if(ret == false) return 0;
        return std::stoul(getHeader("Content-Length"));
    }
    
    // 判断是否短连接
    bool Close() const {
        // 没有Connection字段，或者有Connection但是值是close，则都是短链接，否则就是长连接
        if(hasHeader("Connection") == true && getHeader("Connection") == "keep-alive")
            return false;
        return true;
    }
};

class HttpResponse {
public:
    int _statu;                // 响应状态码
    std::string _body;         // 响应正文
    bool _redirect_flag;       // 是否重定向
    std::string _redirect_url; // 重定向URL
    std::unordered_map<std::string, std::string> _headers;  // 头部字段

public:
    HttpResponse() :_statu(200), _redirect_flag(false) {}
    HttpResponse(int statu) :_statu(statu), _redirect_flag(false) {}

    void reSet() {
        _statu = 200;
        _redirect_flag = false;
        _body.clear();
        _redirect_url.clear();
        _headers.clear();
    }

    // 插入头部字段
    void setHeader(const std::string& key, const std::string& value) {
        _headers.insert(std::make_pair(key, value));
    }

    // 判断是否存在指定头部字段
    bool hasHeader(const std::string& key) const {
        auto it = _headers.find(key);
        if(it == _headers.end()) return false;
        return true;
    }

    // 获取指定头部字段的值
    std::string getHeader(const std::string& key) const {
        auto it = _headers.find(key);
        if(it == _headers.end()) return "";
        return it->second;
    }
 
    // 设置响应正文
    void setContentType(const std::string& body, const std::string& type = "text/html") {
        _body = body;
        setHeader("Content-Type", type);
    }

    // 设置重定向
    void setRedirect(const std::string& url, int statu = 302) {
        _statu = statu;
        _redirect_flag = true;
        _redirect_url = url;
    }

    // 判断是否短连接
    bool Close() const {
        // 没有Connection字段，或者有Connection但是值是close，则都是短链接，否则就是长连接
        if(hasHeader("Connection") == true && getHeader("Connection") == "keep-alive")
            return false;   
        return true;
    }

};


typedef enum {
    RECV_HTTP_ERROR,
    RECV_HTTP_LINE,             // 1. 解析HTTP请求行
    RECV_HTTP_HEAD,             // 2. 解析HTTP头部
    RECV_HTTP_BODY,             // 3. 解析HTTP正文
    RECV_HTTP_DONE
}HttpRecvStatu;
#define MAX_HTTP_LINE_SIZE 8192
class HttpContext {
private:
    int _resp_statu;             // 响应状态码
    HttpRecvStatu _recv_statu;   // 接收状态
    HttpRequest _request;        // 已经解析得到的请求信息   

private:
    bool parseHttpLine(const std::string& line) {
        std::smatch matches;
        std::regex e("(GET|HEAD|POST|PUT|DELETE) ([^?]*)(?:\\?(.*))? (HTTP/1\\.[01])(?:\n|\r\n)?", std::regex::icase);
        bool ret = std::regex_match(line, matches, e);
        if(ret == false) {
            _recv_statu = RECV_HTTP_ERROR;
            _resp_statu = 400;
            return false;
        }
        
        // 获取请求方法
        _request._method = matches[1];
        std::transform(_request._method.begin(), _request._method.end(), _request._method.begin(), ::toupper);
        // 获取请求路径，对URL解码，但是不用+转空格
        _request._path = Util::urlDecode(matches[2], false);
        // 获取协议版本
        _request._version = matches[4];

        // 获取并处理查询字符串
        std::vector<std::string> query_string_arr;
        std::string query_string = matches[3];
        // key=value&key=value..... 先以&进行分割，得到各个子串
        Util::Split(query_string, "&", &query_string_arr);
        // 针对各个字串，以=进行分割，得到key和value，得到之后进行URL解码
        for(auto& str : query_string_arr) {
            size_t pos = str.find("=");
            if(pos == std::string::npos) {
                _recv_statu = RECV_HTTP_ERROR;
                _resp_statu = 400;
                return false;
            }
            std::string key = Util::urlDecode(str.substr(0, pos), false);
            std::string value = Util::urlDecode(str.substr(pos+1), false);
            _request.setParam(key, value);
        }
        return true;
    }

    // 接收并解析http请求行
    // "GET /login HTTP/1.1\r\n"
    bool recvHttpLine(Buffer* buf) {
        if(_recv_statu != RECV_HTTP_LINE) return false;
        // 从Buffer里找\r\n
        std::string line = buf->readLineAndMove();

        // 如果没读到换行符，说明没收到完整的包，这行命令没收全！
        if(0 == line.size()) {
            // 如果发了8KB数据还没有换行符，肯定是恶意攻击，直接拉黑
            if(buf->getReadableSize() > MAX_HTTP_LINE_SIZE) {
                _recv_statu = RECV_HTTP_ERROR;
                _resp_statu = 400;
                return false;
            }
            // 半包处理，返回true
            // 意思是解析先告一段落，等下次epoll提示可读了，我再进来找\r\n
            return true;
        }

        if(line.size() > MAX_HTTP_LINE_SIZE) return false;

        // 提取请求方法、路径、版本
        if(parseHttpLine(line) == false) return false;

        // 解析完请求行，进入头部解析
        _recv_statu = RECV_HTTP_HEAD;
        return true;
    }

    // 解析http请求头
    // "Content-Length: 1234"
    bool parseHttpHead(std::string& line) {
        if(line.back() == '\n') line.pop_back();
        if(line.back() == '\r') line.pop_back();
        
        // key: value
        size_t pos = line.find(": ");
        if(pos == std::string::npos) {
            _recv_statu = RECV_HTTP_ERROR;
            _resp_statu = 400;
            return false;
        }
        // 拿冒号前的key
        std::string key = line.substr(0, pos);
        // 跳过冒号和空格，拿value
        std::string value = line.substr(pos+2);
        _request.setHeader(key, value);
        return true;
    }

    // 接收所有请求头部字段
    bool recvHttpHead(Buffer* buf) {
        if(_recv_statu != RECV_HTTP_HEAD) return false;

        while(1) {
            std::string line = buf->readLineAndMove();
            // 处理半包
            if(0 == line.size()) {
                if(buf->getReadableSize() > MAX_HTTP_LINE_SIZE) return false;
                return true;
            }

            // 空行代表头部字段结束
            if(line == "\n" || line == "\r\n") break;

            if(parseHttpHead(line) == false) return false;
        }
        _recv_statu = RECV_HTTP_BODY;
        return true;
    }
    
    // 接收请求正文
    bool recvHttpBody(Buffer* buf) {
        if(_recv_statu != RECV_HTTP_BODY) return false;
        
        size_t content_length = _request.contentLength();
        if(0 == content_length) {
            // 没有正文
            _recv_statu = RECV_HTTP_DONE;
            return true;
        }

        // 计算正文长度
        size_t real_len = content_length - _request._body.size();
        
        // 粘包了怎么办
        // 我需要100字节，buffer里有150字节
        if(buf->getReadableSize() >= real_len) {
            // 只拿100，剩下的50留着
            _request._body.append(buf->getReadIndex(), real_len);
            buf->moveReadOffset(real_len);
            _recv_statu = RECV_HTTP_DONE;
            return true;
        }

        // 半包了怎么办
        // 要100字节，buffer里只有50字节
        // 拿完50字节，状态保持不变，返回true等待下次epoll唤醒！
        _request._body.append(buf->getReadIndex(), buf->getReadableSize());
        buf->moveReadOffset(buf->getReadableSize());
        return true;
    }

public:
    HttpContext() :_resp_statu(200), _recv_statu(RECV_HTTP_LINE) {}
    void reSet() {
        _resp_statu = 200;
        _recv_statu = RECV_HTTP_LINE;
        _request.reSet();
    }

    int getRespStatu() const { return _resp_statu; }
    HttpRecvStatu getRecvStatu() const { return _recv_statu; }
    const HttpRequest& getRequest() const { return _request; }

    // 接收http请求并解析
    void recvHttpRequest(Buffer* buf) {
        // 不同的状态做不同的事情，一定不能break
        // 处理完请求行之后，接着处理请求头，然后处理请求正文
        switch(_recv_statu) {
            case RECV_HTTP_LINE: recvHttpLine(buf); 
            case RECV_HTTP_HEAD: recvHttpHead(buf);
            case RECV_HTTP_BODY: recvHttpBody(buf);
        }
    }
};