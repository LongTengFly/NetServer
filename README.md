# NetServer
High-performance network server that supports TCP.
# reactor使用步骤
1. 创建structnet_reactor对象。
2. 调用init_reactor初始化。
3. 调用init_socket监听端口。
4. 调用reactor_add_listener将端口添加到reactor中管理。
5. 调用reactor_run运行reactor服务。

# 编译
gcc -o main.c reactor.c