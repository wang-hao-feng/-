#include <stdio.h>
#include <fstream>
#include <stdlib.h>
#include <Windows.h>
#include <regex>
#include <process.h>
#include <string.h>
#include <vector>
#include <tchar.h>

#pragma comment(lib, "Ws2_32.lib")

#define MAXSIZE 65507   //发送数据报最大长度
#define HTTP_PORT 80    //服务器端口

using namespace std;

//http头部数据
struct HttpHeader
{
    char method[4]; //POST或GET
    char url[1024]; //请求的url
    char host[1024];//目标主机
    char cookie[1024 * 10]; //cookie
    HttpHeader()
    {
        ZeroMemory(this, sizeof(HttpHeader));
    }
};

bool InitSocket();
void ParseHttpHead(char *buffer, HttpHeader *httpHeader);
bool ConnectToServer(SOCKET *serverSocket, char *host);
unsigned int __stdcall ProxyThread(LPVOID lpParameter);
void ReadList(const string filename, vector<char *> *list);
bool InList(char *url, vector<char *> *list);
void ChangeHttpHead(char *buffer, char *url, char *host);
void MakeCache(char *Buffer, char *url, char *Date);
bool GetCache(char *Buffer, char *url, char *Date);
void MakeHttp(char *Buffer, char *Date);
bool IsUpdate(char *Buffer, char *Date);

SOCKET ProxyServer;
sockaddr_in ProxyServerAddr;
const int ProxyPort = 2080;

vector<char *> black;
vector<char *> fish;
vector<char *> blackip;

struct ProxyParam
{
    SOCKET clientSocket;
    SOCKET serverSocket;
};

int _tmain(int argc, char** argv)
{
    printf("Proxy Server starting...\n");
    printf("Initializing...\n");

    if(!InitSocket())
    {
        printf("Failed to initialize socket\n");
        return -1;
    }

    printf("Listening on port %d\n", ProxyPort);
    SOCKET acceptSocket = INVALID_SOCKET;
    ProxyParam *lpParameter;
    HANDLE hThread;
    DWORD dwThreadID;
    sockaddr_in peerAddr;
    int peerLen;

    //读取名单
    ReadList("black.txt", &black);
    ReadList("fishing.txt", &fish);
    ReadList("blackip.txt", &blackip);

    //代理服务器不断监听
    while(true)
    {
        acceptSocket = accept(ProxyServer, NULL, NULL);
        getpeername(acceptSocket, (struct sockaddr *)&peerAddr, &peerLen);
        if(InList(inet_ntoa(peerAddr.sin_addr), &blackip))
        {
            printf("This %s is blocked\n", inet_ntoa(peerAddr.sin_addr));
            continue;
        }
        lpParameter = (ProxyParam *)malloc(sizeof(ProxyParam));
        if(lpParameter == NULL)
            continue;
        lpParameter->clientSocket = acceptSocket;
        hThread = (HANDLE) _beginthreadex(NULL, 0, &ProxyThread, (LPVOID)lpParameter, 0, 0);
        CloseHandle(hThread);
        Sleep(200);
    }

    closesocket(ProxyServer);
    WSACleanup();

    return 0;
}

/*********************************************************
 * Funtion name :   InitSocket
 * Description  :   初始化服务器socket
 * Return       :   初始化成功返回true，否则返回false
*********************************************************/
bool InitSocket()
{
    //winsocket版本
    WORD wsaVersion = MAKEWORD(2, 2);
    //初始化版本数据
    WSADATA wsaData;
    int error;

    error = WSAStartup(wsaVersion, &wsaData);

    //判断是否初始化成功
    if(error != 0)
    {
        printf("Failed to initialize socket, error:%d\n", error);
        return false;
    }
    //判断初始化版本是否为制定版本
    if(LOBYTE(wsaData.wVersion) != LOBYTE(wsaVersion) || HIBYTE(wsaData.wVersion) != HIBYTE(wsaVersion))
    {
        printf("Can't find the winsock version\n");
        WSACleanup();
        return false;
    }

    //初始化socket
    ProxyServer = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if(ProxyServer == INVALID_SOCKET)
    {
        printf("Socket creation failed, error:%d\n", WSAGetLastError());
        WSACleanup();
        return false;
    }

    //绑定socket与本地端点地址
    ProxyServerAddr.sin_family = AF_INET;
    ProxyServerAddr.sin_port = htons(ProxyPort);
    ProxyServerAddr.sin_addr.S_un.S_addr = INADDR_ANY;
    if(bind(ProxyServer, (SOCKADDR*)&ProxyServerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR)
    {
        printf("Failed to bind socket, error:%d\n", WSAGetLastError());
        WSACleanup();
        return false;
    }

    if(listen(ProxyServer, SOMAXCONN) == SOCKET_ERROR)
    {
        printf("Failed to listen on port%d, error:%d\n", ProxyPort, WSAGetLastError());
        WSACleanup();
        return false;
    }

    return true;
}

/*********************************************************
 * Funtion name :   ProxyThread
 * Description  :   执行线程
 * Parameters   :   LPVOID lpParameter
 * Return       :   unsigned int
*********************************************************/
unsigned int __stdcall ProxyThread(LPVOID lpParameter)
{
    char Buffer[MAXSIZE];
    char *CacheBuffer;
    ZeroMemory(Buffer, MAXSIZE);
    SOCKADDR_IN clientAddr;
    int length = sizeof(SOCKADDR_IN);
    int recvSize;
    int ret;
    
    recvSize = recv(((ProxyParam *)lpParameter)->clientSocket, Buffer, MAXSIZE, 0);
    if(recvSize > 0)
    {
        char Date[MAXSIZE];
        HttpHeader *httpHeader = new HttpHeader();
        CacheBuffer = new char[recvSize + 1];
        ZeroMemory(CacheBuffer, recvSize + 1);
        memcpy(CacheBuffer, Buffer, recvSize);
        ParseHttpHead(CacheBuffer, httpHeader);
        delete CacheBuffer;

        //钓鱼
        if(InList(httpHeader->url, &fish))
        {
            ChangeHttpHead(Buffer, httpHeader->url, httpHeader->host);
            memcpy(httpHeader->url, "http://jwts.hit.edu.cn/", 24);
            memcpy(httpHeader->host, "jwts.hit.edu.cn", 16);
        }

        //过滤网站
        if(InList(httpHeader->url, &black))
            printf("%s is blocked\n", httpHeader->url);
        //连接服务器
        else if(ConnectToServer(&((ProxyParam *)lpParameter)->serverSocket, httpHeader->host))
        {
            printf("The agent connects to host %s successfully\n", httpHeader->host);

            CacheBuffer = new char[MAXSIZE];
            bool haveCache = GetCache(CacheBuffer, httpHeader->url, Date);
            MakeHttp(Buffer, Date);

            //将客户端发送的HTTP数据报直接转发给目标服务器
            ret = send(((ProxyParam *)lpParameter)->serverSocket, Buffer, strlen(Buffer) + 1, 0);
            ZeroMemory(Buffer, sizeof(Buffer));

            //等待目标服务器返回数据
            recvSize = recv(((ProxyParam *)lpParameter)->serverSocket, Buffer, MAXSIZE, 0);
            if(recvSize > 0)
            {
                char *tempBuffer;
                tempBuffer = new char[recvSize + 1];
                ZeroMemory(tempBuffer, recvSize + 1);
                memcpy(tempBuffer, Buffer, recvSize);
                if(haveCache && IsUpdate(tempBuffer, Date))
                {
                    ret =send(((ProxyParam *)lpParameter)->clientSocket, CacheBuffer, MAXSIZE, 0);
                    printf("Use cache\n");
                }
                else
                {
                    IsUpdate(tempBuffer, Date);
                    //MakeCache(Buffer, httpHeader->url, Date);
                    ret = send(((ProxyParam *)lpParameter)->clientSocket, Buffer, MAXSIZE, 0);
                }
                delete tempBuffer;
            }
            else
                printf("Receive nothing from server, error:%d\n", WSAGetLastError());
            
            delete CacheBuffer;
        }
        else
            printf("Can't connect to server, error:%d\n", WSAGetLastError());
        
        delete httpHeader;
    }
    else
        printf("Receive nothing from client, error:%d\n", WSAGetLastError());

    printf("Closing socket...\n");
    Sleep(200);
    closesocket(((ProxyParam *)lpParameter)->clientSocket);
    closesocket(((ProxyParam *)lpParameter)->serverSocket);
    free(lpParameter);
    _endthreadex(0);

    return 0;
}

/**********************************************************************
 * Funtion name :   ParseHttpHead
 * Description  :   解析TCP报文中的HTTP头部并存入httpHeader指向的空间中
 * Parameters   :   char *buffer, HttpHeader *httpHeader
 * Return       :   unsigned int
***********************************************************************/
void ParseHttpHead(char *buffer, HttpHeader *httpHeader)
{
    char *p;
    char *ptr;
    const char *delim = "\r\n";
    p = strtok_s(buffer, delim, &ptr);//提取第一行

    if(p[0] == 'G') //Get方法
    {
        memcpy(httpHeader->method, "GET", 3);
        memcpy(httpHeader->url, &p[4], strlen(p) - 13);
    }
    else if(p[0] == 'P')
    {
        memcpy(httpHeader->method, "POST", 4);
        memcpy(httpHeader->url, &p[5], strlen(p) - 14);
    }

    p = strtok_s(NULL, delim, &ptr);
    while(p)
    {
        switch (p[0])
        {
            case 'H':   //Host
            {
                regex reg(":443$");
                memcpy(httpHeader->host, &p[6], strlen(p) - 6);
                if(regex_search(httpHeader->host, reg))
                    httpHeader->host[strlen(httpHeader->host) - 4] = httpHeader->host[strlen(httpHeader->host)];
                break;
            }
            case 'C':   //Cookie
                if(strlen(p) > 8)
                {
                    char header[8];
                    ZeroMemory(header, sizeof(header));
                    memcpy(header, p, 6);
                    if(!strcmp(header, "Cookie"))
                        memcpy(httpHeader->cookie, &p[8], strlen(p) - 8);
                }
                break;
            default:
                break;
        }
        p = strtok_s(NULL, delim, &ptr);
    }
}

/*********************************************************
 * Funtion name :   ConnectToServer
 * Description  :   根据主机创建目标服务器套接字，并连接
 * Parameter    :   SOCKET *serverSocket, char *host
 * Return       :   成功返回true，否则返回false
*********************************************************/
bool ConnectToServer(SOCKET *serverSocket, char *host)
{
    //获取主机信息
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(HTTP_PORT);
    HOSTENT *hostent = gethostbyname(host);
    if(!hostent)
        return false;
    
    //构造与主机连接的套接字
    in_addr Inaddr = *((in_addr *)hostent->h_addr);
    serverAddr.sin_addr.s_addr = inet_addr(inet_ntoa(Inaddr));
    *serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    
    if(*serverSocket == INVALID_SOCKET)
        return false;
    
    //建立与主机的连接
    if(connect(*serverSocket, (SOCKADDR *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        printf("%d\n", WSAGetLastError());
        printf("%s\n", host);
        printf("%s\n", inet_ntoa(Inaddr));
        closesocket(*serverSocket);
        return false;
    }

    return true;
}

/*********************************************************
 * Funtion name :   ReadList
 * Description  :   读取名单
 * Parameter    :   const string filename, vector<char *> list
*********************************************************/
void ReadList(const string filename, vector<char *> *list)
{
    ifstream file(filename);

    string url = new char[1024];
    url.clear();

    while(getline(file, url))
    {
        char *c = new char[1024];
        memcpy(c, url.c_str(), url.length());
        list->push_back(c);
        url.clear();
    }

    file.close();
}

/*********************************************************
 * Funtion name :   InList
 * Description  :   判断网站是否在名单中
 * Parameter    :   char *url, vector<char *> list
 * Return       :   在则返回true，否则返回false
*********************************************************/
bool InList(char *url, vector<char *> *list)
{
    string str = url;
    for(char *line : *list)
    {
        if(str.find(line) != string::npos)
            return true;
    }
    return false;
}

/*********************************************************
 * Funtion name :   ChangeHttpHead
 * Description  :   修改http报文头部信息
 * Parameter    :   char *buffer, char *url, char *host
*********************************************************/
void ChangeHttpHead(char *buffer, char *url, char *host)
{
    string line = buffer;
    //替换url
    int pos = line.find("GET http://") + 11;
    int len = strlen(host);
    line.replace(pos, len, "jwts.hit.edu.cn");

    //替换host
    pos = line.find("Host: ") + 6;
    line.replace(pos, len, "jwts.hit.edu.cn");

    //替换Refer
    pos = line.find("Referer: http://");
    if(pos != string::npos)
    {
        pos += 16;
        line.replace(pos, len, "jwts.hit.edu.cn");
    }

    //写回缓冲区
    memcpy(buffer, line.c_str(), line.length());
}

/*********************************************************
 * Funtion name :   MackCache
 * Description  :   缓存文件对应报文
 * Parameter    :   char *Buffer, char *url, char *Date
*********************************************************/
void MakeCache(char *Buffer, char *url, char *Date)
{
    char *temp;
    temp = new char[strlen(url)];
    ZeroMemory(temp, strlen(url));
    char *p = temp;
    while(*url != '\0')
    {
        if(*url != '\\' && *url != '/' && *url != '.' && *url != ':')
            *p++ = *url;
        url++;
    }
    string filename = temp;
    delete temp;
    filename = "cache\\" + filename + ".txt";
    ofstream file;
    file.open(filename, ios::out);
    if(file)
    {
        file.write(Date, strlen(Date));
        file << endl;
        file.write(Buffer, strlen(Buffer));
    }
    file.close();
}

/*************************************************************
 * Funtion name :   GetCache
 * Description  :   读取缓存文件
 * Parameter    :   char *Buffer, char *url, char *Date
 * Return       :   若缓存文件存在则返回true，否则返回false
*************************************************************/
bool GetCache(char *Buffer, char *url, char *Date)
{
    char *temp;
    temp = new char[strlen(url)];
    ZeroMemory(temp, strlen(url));
    char *p = temp;
    while(*url != '\0')
    {
        if(*url != '\\' && *url != '/' && *url != '.' && *url != ':')
            *p++ = *url;
        url++;
    }
    string filename = temp;
    delete temp;
    filename = "cache/" + filename + ".txt";
    ifstream file(filename);
    if(!file.is_open())
    {
        file.close();
        const char *c = "Thu, 01 Jan 1970 00:00:00 GMT";
        memcpy(Date, c, strlen(c));
        return false;
    }
    file.getline(Date, sizeof(Date));
    file.read(Buffer, sizeof(Buffer));

    return true;
}

/*********************************************************
 * Funtion name :   MackHttp
 * Description  :   插入If-Modified-Since字段
 * Parameter    :   char *Buffer, char *Date
*********************************************************/
void MakeHttp(char *Buffer, char *Date)
{
    string buffer = Buffer;
    string date = Date;
    int pos = buffer.find("Host");
    while(buffer[pos] != '\n')
        pos++;
    buffer.insert(++pos, "If-Modified-Since: " + date + '\n');
    const char *temp = buffer.c_str();
    memcpy(Buffer, temp, strlen(temp));
}

/*********************************************************
 * Funtion name :   IsUpdate
 * Description  :   判断缓存是否更新，是则更新Date
 * Parameter    :   char *Buffer, char *Date
 * Return       :   更新则返回true，否则返回false
*********************************************************/
bool IsUpdate(char *Buffer, char *Date)
{
    if(Buffer[10] == '3')
        return false;
    char *p;
    char *ptr;
    const char *delim = "\r\n";
    p = strtok_s(Buffer, delim, &ptr);
    regex reg("Date:");
    while(p)
    {
        if(regex_search(p, reg))
        {
            memcpy(Date, p + 6, strlen(p) - 6);
            return true;
        }
        p = strtok_s(NULL, delim, &ptr);
    }
    return false;
}