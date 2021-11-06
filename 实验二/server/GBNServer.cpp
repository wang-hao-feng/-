#include "GBNServer.h"
#include <stdlib.h>
#include <time.h>
#include <winsock2.h>
#include <fstream>

#pragma comment(lib, "ws2_32.lib")

#define SERVER_PORT 12340       //端口号
#define SERVER_IP "0.0.0.0"     //IP地址

const int BUFFER_LENGTH = 1026; //缓冲区大小(以太网中UDP的数据帧中包长度应小于1480字节)
const int SEND_WIND_SIZE = 10;  //发送窗口大小为10, GBN中应满足W + 1 <= N(W为发送窗口大小, N为序列号个数)
const int SEQ_SIZE = 20;        //序列号个数, 从0到19共20个.由于发送数据第一个字节如果值是0, 则会发送失败, 因此从1开始

bool ack[SEQ_SIZE];             //收到ack情况, 对应0到19的ack
int curSeq;                     //当前数据包的seq
int curAck;                     //当前等待
int totalSeq;                   //收到的包的总数
int totalPacket;                //需要发送的包总数
SOCKET socketServer;            //服务器套接字

//主函数
int main(int argc, char *argv[])
{
    if(!InitSocket())
    {
        printf("Failed to initialize socket\n");
        return -1;
    }

    SOCKADDR_IN addrClient;     //客户端地址
    int length = sizeof(SOCKADDR);
    char buffer[BUFFER_LENGTH]; //数据发送接受缓冲区
    ZeroMemory(buffer, sizeof(buffer));

    //将测试数据读入内存
    std::ifstream icin;
    icin.open("test1.txt");
    char data[1024 * 113];
    ZeroMemory(data, sizeof(data));
    icin.read(data, 1024 * 113);
    icin.close();

    //存储数据
    char recvData[1024 * 113];
    int point = 0;

    float packetLossRatio = 0.2;    //默认丢包率为0.2
    float ackLossRatio = 0.2;       //默认ACK丢失率0.2

    totalPacket = strlen(data) / 1024 + (strlen(data) % 1024 != 0);
    int recvSize;
    for(int i = 0; i < SEQ_SIZE; i++)
        ack[i] = true;
    
    while(1)
    {
        //非阻塞接受, 若没有收到数据, 返回值为-1
        recvSize = recvfrom(socketServer, buffer, BUFFER_LENGTH, 0, ((SOCKADDR *) &addrClient), &length);
        if(recvSize < 0)
        {
            Sleep(200);
            continue;
        }
        
        printf("recv from client:%s\n", buffer);
        if(strcmp(buffer, "-time\n") == 0)
        {
            getCurTime(buffer);
            sendto(socketServer, buffer, strlen(buffer) + 1, 0, (SOCKADDR *) &addrClient, sizeof(SOCKADDR));
        }
        else if(strcmp(buffer, "-quit\n") == 0)
        {
            strcpy_s(buffer, strlen("Good bye!") + 1, "Good bye!");
            sendto(socketServer, buffer, strlen(buffer) + 1, 0, (SOCKADDR *) &addrClient, sizeof(SOCKADDR));
        }
        //进入gbn测试阶段
        else if(strcmp(buffer, "-testgbn") == 0)
        {
            unsigned short seq;     //包的序列号
            unsigned short recvSeq; //接收窗口大小为1, 已确定的序列号
            unsigned short waitSeq; //等待的序列号
            //首先server(server处于0状态)想client发送205状态码(server进入1状态)
            //server等待client回复200状态码, 如果收到(server进入2状态), 则开始传输文件, 否则延时等待直至超时
            //在文件传输阶段, server发送窗口大小设为SEND_WIND_SIZE
            ZeroMemory(buffer, sizeof(buffer));
            int waitCount = 0;
            printf("Begin to test GBN protocol, please don't abort the process.\n");
            //加入了一个握手协议
            //首先服务器向客户端发送一个205状态码表示服务器准备好了, 可以发送数据
            //客户端收到205之后回复一个200状态码, 表示客户端准备好了，可以接受数据了
            //服务器收到200状态码, 开始使用GBN发送数据
            printf("Shake hands stage.\n");
            int stage = 0;
            bool runFlag = true;
            while(runFlag)
            {
                switch(stage)
                {
                    case 0: //发送205阶段
                        buffer[0] = (char)205;
                        sendto(socketServer, buffer, strlen(buffer) + 1, 0, (SOCKADDR *) &addrClient, sizeof(SOCKADDR));
                        Sleep(100);
                        stage = 1;
                        recvSeq = 0;
                        waitSeq = 1;
                        break;
                    case 1: //等待接收200阶段, 没有收到则计数器+1，超时则放弃此次“连接”, 等待从第一步开始
                        recvSize = recvfrom(socketServer, buffer, BUFFER_LENGTH, 0, (SOCKADDR *) &addrClient, &length);
                        if(recvSize < 0)
                        {
                            waitCount++;
                            if(waitCount > 20)
                            {
                                runFlag = false;
                                printf("Timeout error.\n");
                                break;
                            }
                            Sleep(500);
                            continue;
                        }
                        else
                        {
                            if((unsigned char)buffer[0] == 200)
                            {
                                printf("Begin a file transfer.\n");
                                printf("File size is %lldB, each packet is 1024B and packet total num is %d.\n", strlen(data), totalPacket);
                                curSeq = 0;
                                curAck = 0;
                                totalSeq = 0;
                                waitCount = 0;
                                stage = buffer[1] == 0 ? 2 : 3; //0表示对方是文件接受方
                                if(buffer[1] == 0)
                                {
                                    int iMode = 1;                                          //1:非阻塞, 0:阻塞
                                    ioctlsocket(socketServer, FIONBIO, (u_long FAR*)&iMode);
                                }
                                else
                                {
                                    int iMode = 0;                                          //1:非阻塞, 0:阻塞
                                    ioctlsocket(socketServer, FIONBIO, (u_long FAR*)&iMode);
                                }
                            }
                        }
                        break;
                    case 2: //数据传输阶段
                        if(seqIsAvaliable())
                        {
                            if(totalSeq >= totalPacket)
                            {
                                runFlag = false;
                                buffer[0] = (char)(SEQ_SIZE + 1);
                                buffer[1] = '\0';
                                sendto(socketServer, buffer, strlen(buffer) + 1, 0, (SOCKADDR *) &addrClient, sizeof(SOCKADDR));
                                break;
                            }
                            //发送给客户端的序列号从1开始
                            buffer[0] = curSeq + 1;
                            ack[curSeq] = false;
                            memcpy(&buffer[1], data + 1024 * totalSeq, 1024);
                            printf("Send a packet with a seq of %d\n.", curSeq);
                            sendto(socketServer, buffer, BUFFER_LENGTH, 0, (SOCKADDR *) &addrClient, sizeof(SOCKADDR));
                            curSeq++;
                            curSeq %= SEQ_SIZE;
                            totalSeq++;
                            Sleep(500);
                        }
                        //等待ack, 若没有收到, 则返回-1, 计数器+1
                        recvSize = recvfrom(socketServer, buffer, BUFFER_LENGTH, 0, (SOCKADDR *) &addrClient, &length);
                        if(recvSize < 0)
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
                    case 3:
                        recvfrom(socketServer, buffer, BUFFER_LENGTH, 0, (SOCKADDR *) &addrClient, &length);

                        seq = (unsigned short)buffer[0];

                        if(seq == (unsigned short)(SEQ_SIZE + 1))
                        {
                            printf("%s\n", recvData);
                            runFlag = false;
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

                        sendto(socketServer, buffer, 2, 0, (SOCKADDR *) &addrClient, sizeof(SOCKADDR));
                        printf("Send a ack of %d.\n", (unsigned char)buffer[0]);
                        break;
                }
            }
        }
        Sleep(500);
    }
    
    //关闭套接字, 卸载库
    closesocket(socketServer);
    WSACleanup();
    return 0;
}

void getCurTime(char *ptime)
{
    char buffer[128];
    memset(buffer, 0, sizeof(buffer));
    time_t c_time;
    struct tm p;

    time(&c_time);
    localtime_s(&p, &c_time);
    sprintf_s(buffer, "%d/%d/%d %d:%d:%d", p.tm_year + 1900, p.tm_mon + 1, p.tm_mday, p.tm_hour, p.tm_min, p.tm_sec);
    strcpy_s(ptime, sizeof(buffer), buffer);
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
    
    socketServer = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    //设置套接字为非阻塞模式
    int iMode = 1;                                          //1:非阻塞, 0:阻塞
    ioctlsocket(socketServer, FIONBIO, (u_long FAR*)&iMode);  //非阻塞设置

    //绑定socket与本地端口
    SOCKADDR_IN addrServer;                                 //服务器地址
    addrServer.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
    addrServer.sin_family = AF_INET;
    addrServer.sin_port = htons(SERVER_PORT);
    err = bind(socketServer, (SOCKADDR *)&addrServer, sizeof(SOCKADDR));
    if(err)
    {
        err = GetLastError();
        printf("Could not bind the port %d for socket.Error code is %d\n", SERVER_PORT, err);
        WSACleanup();
        return false;
    }

    return true;
}

bool lossInLossRatio(float lossRatio)
{
    int lossBound = (int)(lossRatio * 100);
    int r = rand() % 100;
    if(r < lossBound)
        return true;
    return false;
}