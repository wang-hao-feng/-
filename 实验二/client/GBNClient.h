/********************************************************************
* Function name :   printTips
* Description   :   输出提示信息
                    -time               从服务器获取当前时间
                    -quit               退出客户端
                    -testgbn [X] [Y]    测试GBN协议实现可靠数据传输
                            [X] [0, 1]  模拟数据包丢失的概率
                            [Y] [0, 1]  模拟ACK丢失的概率
* Returns       :   void
*********************************************************************/
void printTips();

/*********************************************************************
* Function name :   lossInLossRatio
* Description   :   根据丢失率随机生成一个数字, 判断是否丢失
* Parameter     :   float lossRatio
* Returns       :   丢失返回true, 否则返回false
*********************************************************************/
bool lossInLossRatio(float lossRatio);

/*********************************************************
 * Funtion name :   InitSocket
 * Description  :   初始化客户端socket
 * Return       :   初始化成功返回true，否则返回false
*********************************************************/
bool InitSocket();

/******************************************************
* Function name :   seqIsAvaliable
* Description   :   当前序列号curSeq是否可用
* Returns       :   可用返回true, 否则返回false
********************************************************/
bool seqIsAvaliable();

/**************************************************************
* Function name :   timeoutHandler
* Description   :   超时重传处理函数, 滑动窗口内的数据帧都要重传
* Returns       :   void
***************************************************************/
void timeoutHandler();

/******************************************************
* Function name :   ackHandler
* Description   :   收到ack, 累计确认, 取数据帧 第一个字节
* Parameter     :   char c
* Returns       :   void
********************************************************/
void ackHandler(char c);