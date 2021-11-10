/*
* THIS FILE IS FOR IP TEST
*/
// system support
#include "sysInclude.h"

extern void ip_DiscardPkt(char* pBuffer,int type);

extern void ip_SendtoLower(char*pBuffer,int length);

extern void ip_SendtoUp(char *pBuffer,int length);

extern unsigned int getIpv4Address();

// implemented by students

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

int stud_ip_recv(char *pBuffer,unsigned short length)
{
    //判断是否为IPv4
	char version = (*pBuffer & 0xF0) >> 4;
    if(version != 4)
    {
        ip_DiscardPkt(pBuffer, STUD_IP_TEST_VERSION_ERROR);
        return 1;
    }

    //检查头部长度
    unsigned short head_length = (unsigned short)(*pBuffer & 0x0F) << 2;
    if(head_length < 20)
    {
        ip_DiscardPkt(pBuffer, STUD_IP_TEST_HEADLEN_ERROR);
        return 1;
    }

    //判断TTL
    if(*((unsigned char*)(pBuffer + 8)) <= 0)
    {
        ip_DiscardPkt(pBuffer, STUD_IP_TEST_TTL_ERROR);
        return 1;
    }

    //提取上层协议号
    byte protocol = *(pBuffer + 9);

    //检验首部校验和
    unsigned short checksum = calculate_checksum((unsigned short *)pBuffer, head_length / 2);
    if(checksum)
    {
        ip_DiscardPkt(pBuffer, STUD_IP_TEST_CHECKSUM_ERROR);
        return 1;
    }

    //根据目的判断是否接受该报文
    unsigned int target = ntohl(*(unsigned int *)(pBuffer + 16));
    if(target != getIpv4Address() && target != (unsigned int)0xFFFFFFFF)
    {
        ip_DiscardPkt(pBuffer, STUD_IP_TEST_DESTINATION_ERROR);
        return 1;
    }

    ip_SendtoUp(pBuffer + head_length, (int)(length - head_length));

	return 0;
}

int stud_ip_Upsend(char *pBuffer,unsigned short len,unsigned int srcAddr,
				   unsigned int dstAddr,byte protocol,byte ttl)
{
    //数据报总长度
    unsigned short total_length = 20 + len;

    //申请缓冲区
    char *buffer = (char *)malloc(total_length);
    memset(buffer, 0, 20);

    //拷贝数据段
    memcpy(buffer + 20, pBuffer, len);

    //存入版本号与首部长度
    *buffer = 0x45;

    //存入总长度
    *(unsigned short *)(buffer + 2) = htons(total_length);

    //存入TTL
    *(byte *)(buffer + 8) = ttl;

    //存入协议
    *(byte *)(buffer + 9) = protocol;

    //存入源ip与目的ip
    *(unsigned int *)(buffer + 12) = htonl(srcAddr);
    *(unsigned int *)(buffer + 16) = htonl(dstAddr);

    //计算校验和
    *((unsigned short *)(buffer + 10)) = htons(calculate_checksum((unsigned short *)buffer, 10));

    ip_SendtoLower(buffer, (int)total_length);

	return 0;
}
