#include <string>

/******************************************************
* Function name :   getCurTime
* Description   :   获取当前系统时间，结果存入 ptime 中
* Parameter     :   char *ptime
* Returns       :   void
********************************************************/
void getCurTime(char *ptime);

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

/*********************************************************
 * Funtion name :   InitSocket
 * Description  :   初始化服务器socket
 * Return       :   初始化成功返回true，否则返回false
*********************************************************/
bool InitSocket();

/*********************************************************************
* Function name :   lossInLossRatio
* Description   :   根据丢失率随机生成一个数字, 判断是否丢失
* Parameter     :   float lossRatio
* Returns       :   丢失返回true, 否则返回false
*********************************************************************/
bool lossInLossRatio(float lossRatio);