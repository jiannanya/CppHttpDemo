# CppHttpDemo
light weight C++ httpserver and httpclient based on mongoose

g++  -g common/mongoose.c  httpserver/http_server.cpp httpserver/main.cpp -o main.exe -fpermissive -lwsock32
clang  common/mongoose.c  httpserver/http_server.cpp httpserver/main.cpp -o main.exe -fpermissive -lwsock32

