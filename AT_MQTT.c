#include "AT_MQTT.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*宏定义*/
#define MSG_SUCCESS						"OK\r\n"
#define MSG_FAILED						"ERROR\r\n"
/*AT指令*/
#define CMD_ECHO_OFF                    "ATE0\r\n"
#define CMD_SET_STA						"AT+CWMODE=1\r\n"
#define CMD_CONNECT_WIFI				"AT+CWJAP=\""MQTT_WIFI_SSID"\",\""MQTT_WIFI_PWSD"\"\r\n"
#define CMD_SET_MQTTUSERCFG				"AT+MQTTUSERCFG=0,1,\"NULL\",\""MQTT_USERNAME"\",\""MQTT_USERPWSD"\",0,0,\"\"\r\n"
#define CMD_SET_CLIENTID				"AT+MQTTCLIENTID=0,\""MQTT_CLIENTID"\"\r\n"
#define CMD_SET_MQTTCONN				"AT+MQTTCONN=0,\""MQTT_HOST_NAME"\",1883,1\r\n"
#define CMD_SET_TIME_ZONE				"AT+CIPSNTPCFG=1,8\r\n"
#define CMD_GET_TIME					"AT+CIPSNTPTIME?\r\n"

char MQTT_RxBuf[MQTT_RX_BUF_MAX_LEN];
char ReportBuf[MQTT_REPORT_BUF_LEN];
char MQTT_RxChar;
size_t MQTT_RxBufLen;
uint8_t MQTT_IsNewLine = 0;
extern UART_HandleTypeDef MQTT_UART;

/**
 * @brief MQTT初始化
 * @retval 成功返回HAL_OK
 */
HAL_StatusTypeDef MQTT_Init(void)
{
    HAL_StatusTypeDef status;

    //复位
    MQTT_SendNoRetCmd("AT+RST\r\n");
    HAL_Delay(2000);

    MQTT_EnableReceiveIT(); //使能接收中断

    //关闭命令回显
    status = MQTT_SendRetCmd(CMD_ECHO_OFF, MSG_SUCCESS, 1000);
    if (status != HAL_OK)
    {
        printf("Status:%d, Cmd=%s\r\n", status, CMD_ECHO_OFF);
    }
#if MQTT_CONFIG_WIFI == 1
    //使ESP8266进入STA模式
    status = MQTT_SendRetCmd(CMD_SET_STA, MSG_SUCCESS, 4000);
    if (status != HAL_OK)
    {
        printf("Status:%d, Cmd=%s\r\n", status, CMD_SET_STA);
    }

    //发送wifi的SSID和PASSWORD，这个指令只需要发送一次
    MQTT_SendRetCmd(CMD_CONNECT_WIFI, MSG_SUCCESS, 10000);
    if (status != HAL_OK)
    {
        printf("Status:%d, Cmd=%s\r\n", status, CMD_CONNECT_WIFI);
    }

#else
    status = MQTT_SendRetCmd("AT\r\n", "WIFI GOT IP", MQTT_DEFAULT_TIMEOUT);
    if (status == HAL_OK)
    {
        printf("Wifi Connected.\r\n");
    }
    if (status == HAL_ERROR) return status;
#endif //CONFIG_WIFI

    //发送MQTT用户配置，包含用户名和用户密钥
    status = MQTT_SendRetCmd(CMD_SET_MQTTUSERCFG, MSG_SUCCESS, MQTT_DEFAULT_TIMEOUT);
    if (status != HAL_OK)
    {
        printf("Status:%d, Cmd=%s\r\n", status, CMD_SET_MQTTUSERCFG);
    }
    if (status == HAL_ERROR) return status;

    //设置MQTT的ClientID
    status = MQTT_SendRetCmd(CMD_SET_CLIENTID, MSG_SUCCESS, MQTT_DEFAULT_TIMEOUT);
    if (status != HAL_OK)
    {
        printf("Status:%d, Cmd=%s\r\n", status, CMD_SET_CLIENTID);
    }
    if (status == HAL_ERROR) return status;

    //设置MQTT连接地址
    status = MQTT_SendRetCmd(CMD_SET_MQTTCONN, MSG_SUCCESS, MQTT_DEFAULT_TIMEOUT);
    if (status != HAL_OK)
    {
        printf("Status:%d, Cmd=%s\r\n", status, CMD_SET_MQTTCONN);
    }
    if (status == HAL_ERROR) return status;

    //设置MQTT订阅的Topic：REPORT：上报数据
    status = MQTT_SendRetCmd(MQTT_SUB_TOPIC_REPORT, MSG_SUCCESS, MQTT_DEFAULT_TIMEOUT);
    if (status != HAL_OK)
    {
        printf("Status:%d, Cmd=%s\r\n", status, MQTT_SUB_TOPIC_REPORT);
    }
    if (status == HAL_ERROR) return status;

    //设置MQTT订阅的Topic：REPORT：下发数据
    status = MQTT_SendRetCmd(MQTT_SUB_TOPIC_COMMAND, MSG_SUCCESS, MQTT_DEFAULT_TIMEOUT);
    if (status != HAL_OK)
    {
        printf("Status:%d, Cmd=%s\r\n", status, MQTT_SUB_TOPIC_COMMAND);
    }
    if (status == HAL_ERROR) return status;

    //设置时区+8（获取正确的时间）
    status = MQTT_SendRetCmd(CMD_SET_TIME_ZONE, MSG_SUCCESS, MQTT_DEFAULT_TIMEOUT);
    if (status != HAL_OK)
    {
        printf("Status:%d, Cmd=%s\r\n", status, CMD_SET_TIME_ZONE);
    }
    if (status == HAL_ERROR) return status;

    return status;
}

/**
 * @brief 发送不带返回值的MQTT命令
 * @param at_cmd AT命令，应当以“\r\n”结尾
 */
void MQTT_SendNoRetCmd(char* at_cmd)
{
    HAL_UART_Transmit(&MQTT_UART, (uint8_t*)at_cmd, strlen(at_cmd), HAL_MAX_DELAY);
    printf("AT>>>%s\n", at_cmd);
}

/**
 * @brief 发送带返回值的MQTT命令
 * @param at_cmd AT命令，应当以“\\r\\n”结尾
 * @param ret_keyword 期待的返回值，当检测到该关键词之后返回 HAL_OK
 * @param timeout 超时时间（ms），在超过时间之后返回 HAL_ERROR
 * @retval 成功返回HAL_OK
 * @note 如果返回的数据中包含ERROR，则立即返回 HAL_ERROR
 */
HAL_StatusTypeDef MQTT_SendRetCmd(char* at_cmd, char* ret_keyword, uint32_t timeout)
{
    HAL_StatusTypeDef status = HAL_TIMEOUT;
    uint32_t beg_tick = HAL_GetTick();

    MQTT_ClearRXBuf();

    //发送命令
    HAL_UART_Transmit(&MQTT_UART, (uint8_t*)at_cmd, strlen(at_cmd), timeout);

    //解析返回值
    while (HAL_GetTick() - beg_tick <= timeout)
    {
        //没有新行且没有超时就继续等待
        while (!MQTT_IsNewLine)
        {
            if (HAL_GetTick() - beg_tick > timeout)
            {
                status = HAL_TIMEOUT;
                break;
            }
            HAL_Delay(50);
        }

        //此时跳出等待循环，可能是超时跳出，也可能是有新行，检查接收缓冲区
        MQTT_IsNewLine = 0;
        if (strstr(MQTT_RxBuf, ret_keyword))
        {
            status = HAL_OK;
            break;
        }
        if (strstr(MQTT_RxBuf, MSG_FAILED))   //检测到错误，立刻退出
        {
            status = HAL_ERROR;
            break;
        }
    }
    //MQTT_ClearRXBuf();
    return status;

}

/**
 * @brief 清空接收缓冲区，重置接收数据长度，重置新行标志位
 */
void MQTT_ClearRXBuf(void)
{
    MQTT_IsNewLine = 0;
    memset(MQTT_RxBuf, 0, MQTT_RX_BUF_MAX_LEN);
    MQTT_RxBufLen = 0;
}

/**
 * @brief 上报整型数据
 * @param property_name 属性名
 * @param val 值
 * @retval 成功返回HAL_OK
 * @note 需要订阅Report的Topic
 */
HAL_StatusTypeDef MQTT_ReportIntVal(char* property_name, int val)
{
    memset(ReportBuf, 0, MQTT_REPORT_BUF_LEN);
    sprintf(ReportBuf, MQTT_CMD_F_PUS_INT, property_name, val);
    return MQTT_SendRetCmd(ReportBuf, MSG_SUCCESS, MQTT_DEFAULT_TIMEOUT);
}

/**
 * @brief 上报浮点数据
 * @param property_name 属性名
 * @param val 值
 * @note 需要订阅Report的Topic
 * @note 需要编译器支持浮点打印
 * @note 浮点数格式化默认保留三位小数
 * @retval 成功返回HAL_OK
 */
HAL_StatusTypeDef MQTT_ReportDoubleVal(char* property_name, double val)
{
    /*
     * target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -u _printf_float)
     * */
    memset(ReportBuf, 0, MQTT_REPORT_BUF_LEN);
    sprintf(ReportBuf, MQTT_CMD_F_PUS_DOUBLE, property_name, val);
    return MQTT_SendRetCmd(ReportBuf, MSG_SUCCESS, MQTT_DEFAULT_TIMEOUT);
}

/**
 * @brief 处理下发命令中的request_id，该函数将下发内容中断 request_id提取出来并完成topic的订阅和数据推送，函数应当在收到下发指令后20S内调用
 * @param sub_recv_text 接收到的下发命令的完整字段
 * @retval 成功返回HAL_OK
 * */
HAL_StatusTypeDef MQTT_HandleRequestID(char* sub_recv_text)
{
    HAL_StatusTypeDef status = HAL_OK;
    const char* request_id_keyword =  "request_id=";
    const size_t request_id_len = sizeof (char) * MQTT_REQUEST_ID_LEN + 1;  //包含字符串结束符\0的字符串长度

    char *request_id = (char*)malloc(request_id_len);
    if (!request_id)
    {
        status = HAL_ERROR;
        return status;
    }
    memset(request_id, 0, request_id_len);

    char *request_id_pos = strstr(sub_recv_text, request_id_keyword);
    if (request_id_pos)
    {
        memcpy(request_id, request_id_pos + strlen(request_id_keyword), request_id_len - 1);
        request_id[request_id_len] = 0;

        memset(ReportBuf, 0, MQTT_REPORT_BUF_LEN);
        sprintf(ReportBuf, MQTT_SUB_REQUEST_F, request_id);
        MQTT_SendRetCmd(ReportBuf, MSG_SUCCESS, MQTT_DEFAULT_TIMEOUT);

        memset(ReportBuf, 0, MQTT_REPORT_BUF_LEN);
        sprintf(ReportBuf, MQTT_PUB_REQUEST_F, request_id);
        MQTT_SendRetCmd(ReportBuf, MSG_SUCCESS, MQTT_DEFAULT_TIMEOUT);

        status = HAL_OK;
    }
    else
    {
        status = HAL_ERROR;
    }

    free(request_id);
    return status;
}

/**
 * @brief 从MQTT服务器获取时间日期
 * @param time_str 获取到的时间日期字符串，格式为 Sat Jan 10 15:58:27 2026
 * @param timeout 超时时间，超出返回 HAL_TIMEOUT
 * @retval 成功返回 HAL_OK
 * @note 获取时间需要配置MQTT服务器，且连接正常，如果正常连接到服务器之后仍然获取失败（例如返回1970年），请等待一段时间之后再获取
 * */
HAL_StatusTypeDef MQTT_GetNTPTime(char* time_str, uint32_t timeout)
{
    /*
     * +CIPSNTPTIME:Sat Jan 10 15:58:27 2026
     * Length:24
     * */
    const size_t time_str_len = 24 + 1;

    HAL_StatusTypeDef status;
    MQTT_ClearRXBuf();
    status = MQTT_SendRetCmd(CMD_GET_TIME, MSG_SUCCESS, timeout);
    if (status != HAL_OK)
        return status;


    char* keywork_pos = strstr(MQTT_RxBuf, "CIPSNTPTIME");
    if (keywork_pos)
    {
        memcpy(time_str, keywork_pos + strlen("CIPSNTPTIME") + 1, time_str_len * sizeof (char));
        time_str[time_str_len] = 0; //手动添加结束符
        status = HAL_OK;
    }

    MQTT_ClearRXBuf();
    return status;
}

/**
 * @brief 使能串口中断
 */
void MQTT_EnableReceiveIT(void)
{
    HAL_UART_Receive_IT(&MQTT_UART, (uint8_t*)&MQTT_RxChar, 1);
}

/**
 * @brief 串口接收回调函数
 */
void MQTT_HandleUARTInterrupt(void)
{
    if (MQTT_RxBufLen >= MQTT_RX_BUF_MAX_LEN)
    {
        memset(MQTT_RxBuf, 0, MQTT_RX_BUF_MAX_LEN);
        MQTT_RxBufLen = 0;
    }
    else
    {
        MQTT_RxBuf[MQTT_RxBufLen++] = MQTT_RxChar;
        if (MQTT_RxChar == '\n')
        {
            MQTT_IsNewLine = 1;
        }
    }

    MQTT_EnableReceiveIT();
}
