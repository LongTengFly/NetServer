#include <event.h>
#include <event2/listener.h>
#include <event2/buffer.h>

#include <sys/socket.h>

#include <functional>
#include <cstring>
#include <stdlib.h>


#define SOCKET_LISTEN_PORT  9703
#define SOCKET_BACKLOG_NUM  128

class asyn_event
{
private:
    
    /* data */
    struct event_base *base;
    struct evconnlistener *listener;
public:
    asyn_event(/* args */);
    ~asyn_event();
    static void accept_cb(struct evconnlistener *listen,evutil_socket_t fd,struct sockaddr *sock,int socklen,void *arg);
    static void socket_event_callback(struct bufferevent *bev, short events, void *arg);
    static void socket_read_callback(struct bufferevent *bev, void *arg);
    void loop_run();
};


asyn_event::asyn_event(/* args */)
{
    base=event_base_new();
    struct sockaddr_in server={0};
    server.sin_family=AF_INET;
    server.sin_addr.s_addr=htonl(INADDR_ANY);
    server.sin_port=htons(SOCKET_LISTEN_PORT);

    listener=evconnlistener_new_bind(
        base,
        &asyn_event::accept_cb,
        base,
        LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE,
        SOCKET_BACKLOG_NUM,
        (struct sockaddr*)&server,
        sizeof(server)
    );
}

asyn_event::~asyn_event()
{
    // 销毁evconnlistener对象
	evconnlistener_free(listener);

	// 销毁事件对象
	event_base_free(base);
}

void asyn_event::accept_cb(struct evconnlistener *listen,evutil_socket_t fd,struct sockaddr *sock,int socklen,void *arg)
{
	struct event_base *base = (struct event_base *)arg;

	// 连接的建立---接收连接
	char ip[32] = { 0 };
	evutil_inet_ntop(AF_INET, sock, ip, sizeof(ip) - 1);
	printf("accept a client fd:%d, ip:%s\n", fd, ip);

	struct bufferevent *bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
	
	// 注册读事件
	bufferevent_setcb(bev, socket_read_callback, NULL, socket_event_callback, NULL);//写事件回调一般为NULL
	bufferevent_enable(bev, EV_READ | EV_PERSIST);


}
void asyn_event::loop_run()
{
    // 事件循环
	event_base_dispatch(base);
}

// 处理连接断开
void asyn_event::socket_event_callback(struct bufferevent *bev, short events, void *arg)
{
	if (events &BEV_EVENT_EOF)//read=0
	{
		printf("connection closed\n");
	}
	else if (events & BEV_EVENT_ERROR)//strerro(errno)
	{
		printf("some other error\n");
	}
	else if (events &BEV_EVENT_TIMEOUT)
		printf("time out\n");

	bufferevent_free(bev);// close(fd)
}

// 读回调
void asyn_event::socket_read_callback(struct bufferevent *bev, void *arg)
{
    struct evbuffer *input = bufferevent_get_input(bev);
    struct evbuffer *output = bufferevent_get_output(bev);

    // 从输入缓冲区读取数据
    char buf[1024];
    size_t len;
    while ((len = evbuffer_remove(input, buf, sizeof(buf))) > 0) {
        //printf("Received: %.*s", (int)len, buf);
        evbuffer_add(output, buf, len);
    }
    
	//bufferevent_write(bev, reply, strlen(reply));
}

int main()
{
    asyn_event ev;
    ev.loop_run();
    return 0;
}