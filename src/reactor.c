#include <stdlib.h>
#include <sys/epoll.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <fcntl.h>

#include "reactor.h"

#define MAX_SOCKET_ITEMS    1024
#define LISTEN_BLK_SIZE     20
#define BUFFER_LENGTH       1024
#define MAX_EPOLL_EVENTS    1024




// 2.
int init_reactor(struct net_reactor *reactor)
{
    if(reactor==NULL)
        return REACTOR_NULL;
    memset(reactor,0,sizeof(struct net_reactor));

    // 创建epoll，作为IO多路复用器
    reactor->epollfd=epoll_create(1);
    if(reactor->epollfd==-1){
        printf("create epfd in %s error %s\n", __func__, strerror(errno));
        return REACTOR_CREATE_EPOLL_FAILED;
    }

    // 创建事件集
    struct socket_item *items=(struct socket_item *)malloc(MAX_SOCKET_ITEMS*sizeof(struct socket_item));
    if(items==NULL)
    {
        printf("create socket_item in %s error %s\n", __func__, strerror(errno));
        close(reactor->epollfd);
        return REACTOR_MALLOC_MEMORY_FAILED;
    }
    memset(items,0,MAX_SOCKET_ITEMS*sizeof(struct socket_item));

    // 创建事件内存块
    struct event_block *block=(struct event_block *)malloc(sizeof(struct event_block));
    if(block==NULL)
    {
        printf("create block in %s error %s\n", __func__, strerror(errno));
        free(items);
        close(reactor->epollfd);
        return REACTOR_MALLOC_MEMORY_FAILED;
    }
    memset(block,0,sizeof(struct event_block));

    block->items=items;
    block->next=NULL;

    reactor->block_count=1;
    reactor->start_block=block;
    reactor->finish_reactor=0;


    return REACTOR_SUCCESS;
}

// 3.
int init_socket(short port)
{
    int fd=socket(AF_INET,SOCK_STREAM,0);
    if(fd==-1)
    {
        printf("create socket in %s error %s\n", __func__, strerror(errno));
		return -1;
    }

    int ret;
    // nonblock
	int flag = fcntl(fd, F_GETFL, 0);
	flag |= O_NONBLOCK;
	ret=fcntl(fd, F_SETFL, flag);
    if (ret == -1)
	{
		printf("fcntl O_NONBLOCK in %s error %s\n", __func__, strerror(errno));
        close(fd);
		return -1;
	}

    // 绑定
    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_addr.s_addr=htonl(INADDR_ANY);
    server.sin_family=AF_INET;
    server.sin_port=htons(port);
    ret=bind(fd,(struct sockaddr*)&server,sizeof(server));
    if(ret==-1)
    {
        printf("bind() in %s error %s\n", __func__, strerror(errno));
        close(fd);
		return -1;
    }

    // 监听
    ret=listen(fd,LISTEN_BLK_SIZE);
    if(ret==-1)
    {
        printf("listen failed : %s\n", strerror(errno));
        close(fd);
		return -1;
    }
    printf("listen server port : %d\n", port);

    return fd;
}

// 4. 实现Reactor动态扩容功能
static int reactor_resize(struct net_reactor *reactor)
{
    if(reactor==NULL)
        return REACTOR_NULL;
    if(reactor->start_block==NULL)
        return REACTOR_NULL;
    
    // 找到链表末端
    struct event_block *cur_block=reactor->start_block;
    while(cur_block->next!=NULL)
    {
        cur_block=cur_block->next;
    }

    // 创建事件集
    struct socket_item *items=(struct socket_item *)malloc(MAX_SOCKET_ITEMS*sizeof(struct socket_item));
    if(items==NULL)
    {
        printf("create socket_item in %s error %s\n", __func__, strerror(errno));
        return REACTOR_MALLOC_MEMORY_FAILED;
    }
    memset(items,0,MAX_SOCKET_ITEMS*sizeof(struct socket_item));

    // 创建事件内存块
    struct event_block *block=(struct event_block *)malloc(sizeof(struct event_block));
    if(block==NULL)
    {
        printf("create block in %s error %s\n", __func__, strerror(errno));
        free(items);
        return REACTOR_MALLOC_MEMORY_FAILED;
    }
    memset(block,0,sizeof(struct event_block));

    block->next=NULL;
    block->items=items;

    cur_block->next=block;
    
    reactor->block_count++;

    return REACTOR_SUCCESS;
}

// 5. 实现Reactor索引功能
static struct socket_item *reactor_index(struct net_reactor *reactor,int socketfd)
{
    if(reactor==NULL)
        return NULL;
    if(reactor->start_block==NULL)
        return NULL;
    
    // fd所在block序号
    int block_id=socketfd/MAX_SOCKET_ITEMS;

    // block序号不存在时自动扩容
    while(block_id>=reactor->block_count)
    {
        if(reactor_resize(reactor)<0)
        {
            printf("reactor_resize in %s error %s\n", __func__, strerror(errno));
            return NULL;
        }
    }

    // 找到fd对应block的位置
    struct event_block *cur_block=reactor->start_block;
    int i=0;
    while(i++ !=block_id && cur_block!=NULL)
    {
        cur_block=cur_block->next;
    }

    return &cur_block->items[socketfd%MAX_SOCKET_ITEMS];
}

// 6. 实现设置事件信息功能
static void reactor_event_set(int fd,struct socket_item *sockevent,NCALLBACK callback,void *arg)
{
    sockevent->arg=arg;
    sockevent->callback=callback;
    sockevent->event=0;
    sockevent->fd=fd;
}

// 7. 实现设置IO事件监听功能
static int reactor_event_add(int epollfd,int events,struct socket_item *sockevent)
{
    struct epoll_event ep_events={0,{0}};
    ep_events.data.ptr=sockevent;
    ep_events.events=events;
    sockevent->event=events;

    // 判断，设置epfd的操作模式
    int options;
    if(sockevent->status==1)
    {
        options=EPOLL_CTL_MOD;
    }
    else{
        options=EPOLL_CTL_ADD;
        sockevent->status=1;
    }

    if(epoll_ctl(epollfd,options,sockevent->fd,&ep_events)<0)
    {
        printf("event add failed [fd=%d], events[%d]\n", sockevent->fd, events);
		printf("event add failed in %s error %s\n", __func__, strerror(errno));
		return -1;
    }
    return 0;
}

// 8. 实现IO事件移除功能
static int reactor_event_del(int epollfd,struct socket_item *sockevent)
{
    if(sockevent->status!=1)
        return -1;
    struct epoll_event ep_events={0,{0}};
    ep_events.data.ptr=sockevent;
    
    sockevent->status=0;
    // 移除fd的监听
	if(epoll_ctl(epollfd, EPOLL_CTL_DEL,sockevent->fd, &ep_events)<0)
    {
        printf("reactor_event_del failed in %s error %s\n", __func__, strerror(errno));
		return -1;
    }

    return 0;
}

// 9. 实现Reactor事件监听功能
int reactor_add_listener(struct net_reactor *reactor,int sockfd,NCALLBACK *acceptor)
{
    if(reactor==NULL)
        return REACTOR_NULL;
    if(reactor->start_block==NULL)
        return REACTOR_NULL;
    
    // 找到fd对应的event地址
    struct socket_item *item=reactor_index(reactor,sockfd);
    if(item==NULL)
    {
        printf("reactor_index failed in %s error %s\n", __func__, strerror(errno));
        return REACTOR_ADD_LISTEN_FAILED;
    }

    reactor_event_set(sockfd,item,acceptor,reactor);

    if(reactor_event_add(reactor->epollfd,EPOLLIN,item)<0)
    {
        return REACTOR_ADD_LISTEN_FAILED;
    }
    printf("add listen fd = %d\n",sockfd);
    return REACTOR_SUCCESS;
}

// 10：实现recv回调函数
static int callback_recv(int fd, int events, void *arg)
{
    struct net_reactor *reactor=(struct net_reactor *)arg;
    if(reactor==NULL)
        return REACTOR_NULL;
    
    // 找到fd对应的event地址
    struct socket_item *item=reactor_index(reactor,fd);
    if(item==NULL)
    {
        printf("callback_recv in %s error %s\n", __func__, strerror(errno));
        return REACTOR_MALLOC_MEMORY_FAILED;
    }

    // 接收数据
    int ret= recv(fd,item->read_buffer,BUFFER_LENGTH,0);

    // 暂时移除监听
	if(reactor_event_del(reactor->epollfd, item)<0)
    {
        printf("reactor_event_del failed in %s error %s\n", __func__, strerror(errno));
		//return REACTOR_EVENT_DEL_FAILED;
    }
    if(ret>0)
    {
        item->read_length+=ret;
        printf("recv [%d]:%s\n", fd, item->read_buffer);

        // demo
        memcpy(item->write_buffer,item->read_buffer,ret);
        item->write_buffer[ret]='\0';
        item->write_length=ret;

        reactor_event_set(fd,item,callback_send,reactor);
        if(reactor_event_add(reactor->epollfd,EPOLLOUT,item)<0)
        {
            printf("reactor_event_add failed in %s error %s\n", __func__, strerror(errno));
            //return REACTOR_ADD_LISTEN_FAILED;
        }

    }
    else if(ret==0)
    {
		printf("recv_cb --> disconnect\n");
        free(item->read_buffer);
        free(item->write_buffer);
		close(item->fd);
    }
    else
    {
        if(errno==EAGAIN || errno==EWOULDBLOCK)
        {
            // 表示没有数据可读。这时可以继续等待数据到来，或者关闭套接字。
        }
        else if (errno == ECONNRESET) {
			// reactor_event_del(reactor->epollfd, item);
            free(item->read_buffer);
            free(item->write_buffer);
			close(item->fd);
		}
		printf("recv[fd=%d] error[%d]:%s\n", fd, errno, strerror(errno));
    }
    return ret;
}

// 11：实现send回调函数
static int callback_send(int fd, int events, void *arg)
{
    struct net_reactor *reactor=(struct net_reactor *)arg;
    if(reactor==NULL)
    {
        return REACTOR_NULL;
    }

    // 找到fd对应的event地址
    struct socket_item *item=reactor_index(reactor,fd);
    if(item==NULL)
    {
        printf("callback_recv in %s error %s\n", __func__, strerror(errno));
        return REACTOR_MALLOC_MEMORY_FAILED;
    }

    int ret=send(fd,item->write_buffer,item->write_length,0);
    // 暂时移除监听
	if(reactor_event_del(reactor->epollfd, item)<0)
    {
        printf("reactor_event_del failed in %s error %s\n", __func__, strerror(errno));
		//return REACTOR_EVENT_DEL_FAILED;
    }
    if (ret > 0)
	{
		
		printf("send[fd=%d], [%d]%s\n", fd, ret, item->write_buffer);
		reactor_event_set(fd, item, callback_recv, reactor);
		reactor_event_add(reactor->epollfd, EPOLLIN, item);
	}
	else
	{
        free(item->read_buffer);
        free(item->write_buffer);
		close(fd);
		printf("send[fd=%d] error %s\n", fd, strerror(errno));
	}
    
    return ret;
}

// 12. 实现accept回调函数
int callback_accept(int fd, int events, void *arg)
{
    struct net_reactor *reactor=(struct net_reactor *)arg;
    if(reactor==NULL)
    {
        return REACTOR_NULL;
    }

    struct sockaddr_in client;
    socklen_t len=sizeof(client);
    int connectfd=accept(fd,(struct sockaddr*)&client,&len);
    if(connectfd<0)
    {
        printf("accept failed in %s error %s\n", __func__, strerror(errno));
        return REACTOR_ACCEPT_FAILED;
    }

    // 设置非阻塞
    int flag = fcntl(connectfd, F_GETFL, 0);
	flag |= O_NONBLOCK;
	int ret=fcntl(connectfd, F_SETFL, flag);
    if (ret == -1)
	{
		printf("fcntl O_NONBLOCK in %s error %s\n", __func__, strerror(errno));
        close(connectfd);
		return REACTOR_FCNTL_FAILED;
	}

    // 找到fd对应的event地址
    struct socket_item *item=reactor_index(reactor,connectfd);
    if(item==NULL)
    {
        printf("reactor_index in %s error %s\n", __func__, strerror(errno));
        close(connectfd);
        return REACTOR_MALLOC_MEMORY_FAILED;
    }
    
    // 设置fd的事件信息
    reactor_event_set(connectfd,item,callback_recv,reactor);

    // 添加事件到epoll监听
    if(reactor_event_add(reactor->epollfd,EPOLLIN,item)<0)
    {
        close(connectfd);
        return REACTOR_ADD_LISTEN_FAILED;
    }

    // 为fd分配好读写缓冲区
    item->read_buffer=(char *)malloc(BUFFER_LENGTH * sizeof(char));
    if(item->read_buffer==NULL)
    {
        printf("mallc in %s error %s\n", __func__, strerror(errno));
        close(connectfd);
        return REACTOR_MALLOC_MEMORY_FAILED;
    }
    memset(item->read_buffer,0,BUFFER_LENGTH * sizeof(char));
    item->read_length=0;
    item->write_buffer=(char *)malloc(BUFFER_LENGTH * sizeof(char));
    if(item->write_buffer==NULL)
    {
        printf("mallc in %s error %s\n", __func__, strerror(errno));
        close(connectfd);
        free(item->read_buffer);
        return REACTOR_MALLOC_MEMORY_FAILED;
    }
    memset(item->write_buffer,0,BUFFER_LENGTH * sizeof(char));
    item->write_length=0;

    printf("new connect [%s:%d], pos[%d]\n",inet_ntoa(client.sin_addr), ntohs(client.sin_port), connectfd);

	return REACTOR_SUCCESS;
}

// 13：实现reactor运行函数
int reactor_run(struct net_reactor *reactor)
{
    if(reactor==NULL)
        return REACTOR_NULL;
    if(reactor->start_block==NULL)
        return REACTOR_NULL;
    if(reactor->epollfd<0)
        return REACTOR_CREATE_EPOLL_FAILED;

    struct epoll_event events[MAX_EPOLL_EVENTS + 1];
    
    while(!reactor->finish_reactor)
    {
        int nready=epoll_wait(reactor->epollfd,events,MAX_EPOLL_EVENTS,1000);
        if(nready<0)
        {
            printf("epoll wait error\n");
			continue;
        }
        int i=0;
        for(i=0;i<nready;i++)
        {
            struct socket_item *item=(struct socket_item *)events[i].data.ptr;
            if((events[i].events & EPOLLIN) && (item->event&EPOLLIN))
            {
                // 处理可读事件
				item->callback(item->fd, events[i].events, item->arg);
            }
            if((events[i].events & EPOLLOUT) && (item->event&EPOLLOUT))
            {
                // 处理可读事件
				item->callback(item->fd, events[i].events, item->arg);
            }
        }

    }
    printf("Clearing memory......\n");
    reactor_destory(reactor);

    printf("finish reactor\n");
    return REACTOR_SUCCESS;
}

// 14：实现reactor销毁功能
int reactor_destory(struct net_reactor *reactor)
{
	// 关闭epoll
	close(reactor->epollfd);

    
	struct event_block *blk= reactor->start_block;
	struct event_block *next;

	while (blk != NULL)
	{
		next = blk->next;
		// 释放内存块
        for(int i=0;i<MAX_SOCKET_ITEMS;i++)
        {
            if(blk->items[i].read_buffer!=NULL)
            {
                free(blk->items[i].read_buffer);
                blk->items[i].read_buffer=NULL;
            }
            if(blk->items[i].write_buffer!=NULL)
            {
                free(blk->items[i].write_buffer);
                blk->items[i].write_buffer=NULL;
            }
        }
		free(blk->items);
		free(blk);
		blk = next;
	}
	return REACTOR_SUCCESS;
}

