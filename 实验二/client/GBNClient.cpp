#include "GBNClient.h"
#include <cstdio>
#include <stdlib.h>
#include <time.h>
#include <winsock2.h>
#include <fstream>

#pragma comment(lib, "ws2_32.lib")

#define SERVER_PORT 12340           //接收数据的端口号
#define SERVER_IP "127.0.0.1"       //服务器的ip地址

const int SEND_WIND_SIZE = 10;  //发送窗口大小为10, GBN中应满足W + 1 <= N(W为发送窗口大小, N为序列号个数)
const int BUFFER_LENGTH = 1026;
const int SEQ_SIZE = 20;            //接收端序列号个数, 为1到20
SOCKET socketClient;                //客户端套接字
SOCKADDR_IN addrServer;             //服务器地址

bool ack[SEQ_SIZE];             //收到ack情况, 对应0到19的ack
int curSeq;                     //当前数据包的seq
int curAck;                     //当前等待
int totalSeq;                   //收到的包的总数
int totalPacket;                //需要发送的包总数

char *gets_s(char *str, int num);

int main()
{
    if(!InitSocket())
    {
        printf("Failed to initialize socket\n");
        return 1;
    }
    
    //接收缓冲区
    char buffer[BUFFER_LENGTH];
    ZeroMemory(buffer, sizeof(buffer));
    int len = sizeof(SOCKADDR);

    //存储数据
    char recvData[1024 * 113];
    int point = 0;

    printTips();
    int ret;
    int interval = 1;               //收到数据报之后返回ck的间隔, 默认1表示每个都返回ack, 0或者负数均表示所有的都不返回ack
    char cmd[128];                  //命令缓冲区
    float packetLossRatio = 0.2;    //默认丢包率为0.2
    float ackLossRatio = 0.2;       //默认ACK丢失率0.2

    //将测试数据读入内存
    std::ifstream icin;
    icin.open("test2.txt");
    char data[1024 * 113];
    ZeroMemory(data, sizeof(data));
    icin.read(data, 1024 * 113);
    icin.close();

    //设置时间为随机数种子
    srand((unsigned)time(NULL));

    totalPacket = strlen(data) / 1024 + (strlen(data) % 1024 != 0);
    for(int i = 0; i < SEQ_SIZE; i++)
        ack[i] = true;

    while(true)
    {
        fflush(stdin);
        ZeroMemory(buffer, sizeof(buffer));
        gets_s(buffer, BUFFER_LENGTH);
        ret = sscanf_s(buffer, "%s", cmd);

        //开始GBN测试, 使用GBN协议实现UDP可靠文件传输
        if(!strcmp(cmd, "-testgbn"))
        {
            int sender;
            fflush(stdin);
            scanf("%d", &sender);
            printf("Begin to test GBN protocol, please don't abort the process.\n");
            printf("The loss ratio of the packet is %.2f, the loss ratio of ack is %.2f.\n", packetLossRatio, ackLossRatio);

            int waitCount = 0;
            int stage = 0;
            unsigned char u_code;   //状态码
            unsigned short seq;     //包的序列号
            unsigned short recvSeq; //接收窗口大小为1, 已确定的序列号
            unsigned short waitSeq; //等待的序列号

            sendto(socketClient, "-testgbn", strlen("-testgbn") + 1, 0, (SOCKADDR *) &addrServer, sizeof(SOCKADDR));

            bool flag = true;

            while(flag)
            {
                //等待server回复设置UDP为阻塞模式
                switch(stage)
                {
                    case 0: //等待握手阶段
                        recvfrom(socketClient, buffer, BUFFER_LENGTH, 0, (SOCKADDR *) &addrServer, &len);
                        u_code = (unsigned char)buffer[0];
                        if(u_code == 205)
                        {
                            printf("Ready for file transmission.\n");
                            buffer[0] = (char)200;
                            if(!sender)
                            {
                                stage = 1;
                                buffer[1] = 0;
                                int iMode = 0;                                          //1:非阻塞, 0:阻塞
                                ioctlsocket(socketClient, FIONBIO, (u_long FAR*)&iMode);
                            }
                            else
                            {
                                stage = 2;
                                buffer[1] = 1;
                                int iMode = 1;                                          //1:非阻塞, 0:阻塞
                                ioctlsocket(socketClient, FIONBIO, (u_long FAR*)&iMode);
                                Sleep(500);
                            }
                            buffer[2] = '\0';
                            sendto(socketClient, buffer, 3, 0, (SOCKADDR *) &addrServer, sizeof(SOCKADDR));
                            recvSeq = 0;
                            waitSeq = 1;
                            curSeq = 0;
                            curAck = 0;
                            totalSeq = 0;
                            waitCount = 0;
                        }
                        break;
                    case 1: //等待接收数据阶段
                        recvfrom(socketClient, buffer, BUFFER_LENGTH, 0, (SOCKADDR *) &addrServer, &len);
                        seq = (unsigned short)buffer[0];

                        if(seq == (unsigned short)(SEQ_SIZE + 1))
                        {
                            flag = false;
                            printf("%s\n", recvData);
                            break;
                        }

                        //随机法模拟包是否丢失
                        if(lossInLossRatio(packetLossRatio))
                        {
                            printf("The packet with a seq of %d loss.\n", seq);
                            continue;
                        }
                        printf("Recv a packet with a seq of %d.\n", seq);

                        if(!(waitSeq - seq))
                        {
                            //如果是期待的包, 正确接收, 正常确认
                            waitSeq++;
                            if(waitSeq == 21)
                                waitSeq = 1;
                            

                            memcpy(recvData + point * 1024, buffer + 1, 1024);
                            point++;

                            buffer[0] = seq;
                            recvSeq = seq;
                            buffer[1] = '\0';
                        }
                        else
                        {
                            //如果当前一个包都没收到, 则等待Seq为1的数据包, 不是则不返回ACK
                            if(!recvSeq)
                                continue;
                            buffer[0] = recvSeq;
                            buffer[1] = '\0';
                        }

                        if(lossInLossRatio(ackLossRatio))
                        {
                            printf("The ack of %d loss.\n", (unsigned char)buffer[0]);
                            continue;
                        }

                        sendto(socketClient, buffer, 2, 0, (SOCKADDR *) &addrServer, sizeof(SOCKADDR));
                        printf("Send a ack of %d.\n", (unsigned char)buffer[0]);
                        break;
                    case 2:
                        if(seqIsAvaliable())
                        {
                            if(totalSeq >= totalPacket)
                            {
                                flag = false;
                                buffer[0] = (char)(SEQ_SIZE + 1);
                                buffer[1] = '\0';
                                sendto(socketClient, buffer, BUFFER_LENGTH, 0, (SOCKADDR *) &addrServer, sizeof(SOCKADDR));
                                break;
                            }
                            //发送给客户端的序列号从1开始
                            buffer[0] = curSeq + 1;
                            ack[curSeq] = false;
                            memcpy(&buffer[1], data + 1024 * totalSeq, 1024);
                            printf("Send a packet with a seq of %d\n.", curSeq);
                            sendto(socketClient, buffer, BUFFER_LENGTH, 0, (SOCKADDR *) &addrServer, sizeof(SOCKADDR));
                            curSeq++;
                            curSeq %= SEQ_SIZE;
                            totalSeq++;
                            Sleep(500);
                        }
                        //等待ack, 若没有收到, 则返回-1, 计数器+1
                        ret = recvfrom(socketClient, buffer, BUFFER_LENGTH, 0, (SOCKADDR *) &addrServer, &len);
                        if(ret < 0)
                        {
                            waitCount++;
                            //20次等待ack则超时重传
                            if(waitCount > 20)
                            {
                                timeoutHandler();
                                waitCount = 0;
                            }
                        }
                        else
                        {
                            //收到ack
                            ackHandler(buffer[0]);
                            waitCount = 0;
                        }
                        Sleep(500);
                        break;
                }
                Sleep(500);
            }
        }
        else
        {
            sendto(socketClient, buffer, strlen(buffer) + 1, 0, (SOCKADDR *) &addrServer, sizeof(SOCKADDR));
            ret = recvfrom(socketClient, buffer, BUFFER_LENGTH, 0, (SOCKADDR *) &addrServer, &len);
            if(!strcmp(buffer, "Good bye!"))
                break;
        }
        printTips();
    }

    //关闭套接字, 卸载库
    closesocket(socketClient);
    WSACleanup();
    return 0;
}

void printTips()
{
    printf("*****************************************\n");
    printf("| -time to get current time |\n");
    printf("| -quit to exit client |\n");
    printf("| -testgbn [0/1] to test the gbn |\n");
    printf("*****************************************\n");
}

bool lossInLossRatio(float lossRatio)
{
    int lossBound = (int)(lossRatio * 100);
    int r = rand() % 100;
    if(r < lossBound)
        return true;
    return false;
}

bool InitSocket()
{
    //加载套接字库
    WORD wVersionRequested;
    WSADATA wsaData;
    //套接字加载时数错误提示
    int err;
    //版本号2.2
    wVersionRequested = MAKEWORD(2, 2);
    //加载dll文件Socket库
    err = WSAStartup(wVersionRequested, &wsaData);
    if(err != 0)
    {
        //找不到winsock.dll
        printf("WSAStartup failed with error:%d\n", err);
        return false;
    }
    if(LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
    {
        printf("Could not find a usable version of Winsock.dll\n");
        WSACleanup();
        return false;
    }
    else
        printf("The Winsock 2.2 dll was found okay.\n");
    
    socketClient = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    
    //绑定socket与本地端口
    addrServer.sin_addr.S_un.S_addr = inet_addr(SERVER_IP);
    addrServer.sin_family = AF_INET;
    addrServer.sin_port = htons(SERVER_PORT);

    return true;
}

char *gets_s(char *str, int num)
{
    if (fgets(str,num, stdin) != 0)
    {
        size_t len = strlen(str);

        if (len > 0 && str[len-1] == 'n')

            str[len-1] = ' ';
        return str;
    }
    return 0;
}

bool seqIsAvaliable()
{
    int step;
    step = curSeq - curAck;
    step = step >= 0 ? step : step + SEQ_SIZE;

    //序列号是否在当前发送窗口之内
    if(step >= SEND_WIND_SIZE)
        return false;
    if(ack[curSeq])
        return true;
    return false;
}

void timeoutHandler()
{
    printf("Time out error.\n");
    int index;
    for(int i = 0; i < SEND_WIND_SIZE; i++)
    {
        index = (i + curAck) % SEQ_SIZE;
        ack[index] = true;
    }
    totalSeq -= SEND_WIND_SIZE;
    curSeq = curAck;
}

void ackHandler(char c)
{
    unsigned char index = (unsigned char)c - 1; //序列号减一, 因为序号0发送会失败, 所以需要还原
    printf("Recv a ack of %d\n", index);
    if(curAck <= index)
    {
        for(int i = 0; i <= index; i++)
            ack[i] = true;
        curAck = (index + 1) % SEQ_SIZE;
    }
    else
    {
        //ack超过了最大值, 回到了curAck的左边
        for(int i = curAck; i < SEQ_SIZE; i++)
            ack[i] = true;
        for(int i = 0; i < index; i++)
            ack[i] = true;
        curAck = index + 1;
    }
}