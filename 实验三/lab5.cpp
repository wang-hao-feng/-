/*
* THIS FILE IS FOR IP FORWARD TEST
*/
#include "sysInclude.h"

// system support
extern void fwd_LocalRcv(char *pBuffer, int length);

extern void fwd_SendtoLower(char *pBuffer, int length, unsigned int nexthop);

extern void fwd_DiscardPkt(char *pBuffer, int type);

extern unsigned int getIpv4Address( );

// implemented by students

typedef struct stud_route_msg
{
    unsigned int dest;
    unsigned int masklen;
    unsigned int nexthop;
} stud_route_msg;

struct route_item
{
    stud_route_msg *item;
    route_item * next;
};

route_item *route = NULL;

unsigned short calculate_checksum(unsigned short *p, unsigned short head_length)
{
    unsigned int checksum = 0;
    for(int i = 0; i < head_length; i++)
    {
        checksum += htons(p[i]);
        checksum = (checksum & 0xFFFF) + ((checksum & 0xFFFF0000) >> 16);
    }
    return ~(unsigned short)(checksum);
}

unsigned int s2l(unsigned int n)
{
    return (n >> 24) + ((n & 0x00FF0000) >> 8) + ((n & 0x0000FF00) << 8) + (n << 24);
}

void stud_Route_Init()
{
    route_item *item = (route_item *)malloc(sizeof(route_item));
    item->item = NULL;
    item->next = NULL;
	return;
}

void stud_route_add(stud_route_msg *proute)
{
    //建立路由表项
    route_item *item = (route_item *)malloc(sizeof(route_item));
    stud_route_msg *msg = (stud_route_msg *)malloc(sizeof(stud_route_msg));
    memcpy(msg, proute, sizeof(stud_route_msg));

    item->item = msg;
    item->next = NULL;

    //插入表项
    item->next = route;
    route = item;
}


int stud_fwd_deal(char *pBuffer, int length)
{

    //根据目的判断是否接受该报文
    unsigned int target = ntohl(*(unsigned int *)(pBuffer + 16));
    if(target == getIpv4Address() || target == (unsigned int)0xFFFFFFFF)
    {
        fwd_LocalRcv(pBuffer, length);
        return 0;
    }

    //判断TTL, 如果合法则TTL减一
    if(*((unsigned char*)(pBuffer + 8)) <= 1)
    {
        fwd_DiscardPkt(pBuffer, STUD_FORWARD_TEST_TTLERROR);
        return 1;
    }
    *((unsigned char*)(pBuffer + 8)) -= 1;

    //查找路由表
    stud_route_msg *nexthop = NULL;
    route_item *now = route;
    while(now->next != NULL)
    {
        now = now->next;
        unsigned int mask = (unsigned int)(0xFFFFFFFF << (32 - (now->item->masklen >> 24)));
        if((target & mask) == (s2l(now->item->dest) & mask))
            if(nexthop == NULL || (target ^ s2l(now->item->dest)) > s2l((target ^ nexthop->dest)))
                nexthop = now->item;
    }
    if(nexthop == NULL)
    {
        fwd_DiscardPkt(pBuffer, STUD_FORWARD_TEST_NOROUTE);
        return 1;
    }

    //重新计算校验和
    unsigned short head_length = (unsigned short)(*pBuffer & 0x0F) << 2;
    *((unsigned short *)(pBuffer + 10)) = 0;
    *((unsigned short *)(pBuffer + 10)) = htons(calculate_checksum((unsigned short *)pBuffer, head_length / 2));

    //转发
    fwd_SendtoLower(pBuffer, length, s2l(nexthop->nexthop));

	return 0;
}

