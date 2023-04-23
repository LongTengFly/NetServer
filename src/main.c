#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "reactor.h"

#define SERVER_PORT     9703
#define PORT_COUNT      2

int main(int argc,char **argv)
{
    struct net_reactor *reactor=(struct net_reactor *)malloc(sizeof(struct net_reactor));
    if(reactor==NULL)
    {
        perror("malloc struct net_reactor failed!\n");
        return REACTOR_MALLOC_MEMORY_FAILED;
    }

    if(init_reactor(reactor)<0)
    {
        free(reactor);
        return REACTOR_NULL;
    }

    unsigned short port = SERVER_PORT;
    int sockfds[PORT_COUNT]={0};
    int i=0;
    for(i=0;i<PORT_COUNT;i++)
    {
        sockfds[i]=init_socket(port+i);
        if(sockfds[i]>0)
        {
            if(reactor_add_listener(reactor,sockfds[i],callback_accept)<0)
                printf("reactor_add_listener failed in %s : %d\n",__func__,__LINE__);
        }
        else
        {
            printf("init_socket failed in %s : %d\n",__func__,__LINE__);
        }
        
    }

    reactor_run(reactor);

    // 销毁 reactor
	//reactor_destory(reactor);
    reactor->finish_reactor=1;

    // 关闭socket集
    for(i=0;i<PORT_COUNT;i++)
    {
        close(sockfds[i]);
    }

    // 释放reactor
	free(reactor);

    return 0;

}