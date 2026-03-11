# 1. 声明编译器和编译选项
CXX = g++
# 💡 进阶提示：既然你有 src 目录存放头文件，建议加上 -I./src 告诉编译器去哪里找头文件
CXXFLAGS = -g -Wall -std=c++11 -I./src
LIBS = -lpthread -lrt

# 2. 默认目标
all: server client

# 3. 目标 server 依赖 test 目录下的 server1.cc
server: test/server1.cc
	$(CXX) -o server test/server1.cc $(CXXFLAGS) $(LIBS)

# 4. 目标 client 依赖 test 目录下的 client1.cc
client: test/client1.cc
	$(CXX) -o client test/client1.cc $(CXXFLAGS) $(LIBS)

# 5. 清理规则
.PHONY: clean
clean:
	rm -f server client