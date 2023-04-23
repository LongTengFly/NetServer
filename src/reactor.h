#ifndef _REACTOR_H_
#define _REACTOR_H_

enum REACTOR_ERROR_CODE{
    REACTOR_EVENT_DEL_FAILED=-7,
    REACTOR_FCNTL_FAILED=-6,
    REACTOR_ACCEPT_FAILED=-5,
    REACTOR_ADD_LISTEN_FAILED=-4,
    REACTOR_MALLOC_MEMORY_FAILED=-3,
    REACTOR_CREATE_EPOLL_FAILED=-2,
    REACTOR_NULL=-1,
    REACTOR_SUCCESS=0
};

struct socket_item
{
    /* data */
    int     fd;                 // socket的文件描述符
    char    *write_buffer;      // 写缓冲区
    char    *read_buffer;       // 读缓冲区
    int     write_length;       // 已读字节数
    int     read_length;        // 已写字节数

    int     status;             // 状态标识，设置epfd的操作模式

    int     event;              // 事件类型
    void    *arg;               // 回调函数的参数
    int(*callback)(int fd,int events,void* arg);    // 回调函数
};

struct event_block
{
    /* data */
    struct socket_item  *items;     // 事件集合
    struct event_block  *next;      // 指向像一个内存块
};

struct net_reactor
{
    /* data */
    int epollfd;                        // 事件块的数量
    int block_count;                    // 事件块的数量
    int finish_reactor;                 // 判断是否退出服务
    struct event_block  *start_block;   // 事件块的起始地址
};

typedef int NCALLBACK(int ,int, void*);


int init_reactor(struct net_reactor *reactor);
int init_socket(short port);
int callback_accept(int fd, int events, void *arg);
int reactor_destory(struct net_reactor *reactor);
int reactor_add_listener(struct net_reactor *reactor,int sockfd,NCALLBACK *acceptor);
int reactor_run(struct net_reactor *reactor);

static int callback_send(int fd, int events, void *arg);
static int callback_recv(int fd, int events, void *arg);

#endif