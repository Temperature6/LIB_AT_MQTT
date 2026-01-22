#include "AT_MQTT_OS.h"
#include "usart.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <cmsis_os2.h>

/*宏定义*/
#define MSG_SUCCESS						"OK\r\n"
#define MSG_FAILED						"ERROR\r\n"
/*AT指令*/
#define CMD_ECHO_OFF                    "ATE0\r\n"
#define CMD_SET_STA						"AT+CWMODE=1\r\n"
#define CMD_GET_CWSTATE                 "AT+CWSTATE?\r\n"
#define CMD_CONNECT_WIFI_F				"AT+CWJAP=\"%s\",\"%s\"\r\n"
#define CMD_SET_MQTTUSERCFG				"AT+MQTTUSERCFG=0,1,\"NULL\",\""MQTT_USERNAME"\",\""MQTT_USERPWSD"\",0,0,\"\"\r\n"
#define CMD_SET_CLIENTID				"AT+MQTTCLIENTID=0,\""MQTT_CLIENTID"\"\r\n"
#define CMD_SET_MQTTCONN				"AT+MQTTCONN=0,\""MQTT_HOST_NAME"\",1883,1\r\n"
#define CMD_SET_TIME_ZONE				"AT+CIPSNTPCFG=1,8\r\n"
#define CMD_GET_TIME					"AT+CIPSNTPTIME?\r\n"

#define TEMP_BUFF_SIZE                  MQTT_QUEUE_SIZE
#define MQTT_REQUEST_ID_LEN             36

QueueHandle_t queueMqttMsg;
char RecvCh;
char RecvBuff[MQTT_QUEUE_SIZE];
size_t RecvLen = 0;
char TempBuff[TEMP_BUFF_SIZE];

const char MONTH_LIST[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

#if MQTT_QUEUE_SIZE < 200
#error "MQTT_QUEUE_SIZE 应当大于200"
#endif

/**
 * @brief MQTT初始化
 * @retval 成功返回HAL_OK
 */
HAL_StatusTypeDef MQTT_Init(void)
{
    HAL_StatusTypeDef status;
    TickType_t beg_tick = 0;
    //创建数据缓冲队列
    queueMqttMsg = xQueueCreate(MQTT_QUEUE_LEN, MQTT_QUEUE_SIZE);
    if (queueMqttMsg == NULL)
    {
        status = HAL_ERROR;
        return status;
    }
    //开启中断
    HAL_UART_Receive_IT(&MQTT_UART, (uint8_t*)&RecvCh, 1);

    //开始初始化ESP-01S
    MQTT_SendNoRetCmd("AT+RST\r\n");
    MQTT_DELAY(2000);

    //关闭命令回显
    status = MQTT_SendRetCmd(CMD_ECHO_OFF, MSG_SUCCESS, 1000);
    if (status != HAL_OK) return status;

    //一般上电之后能迅速获取到IP，如果超过10s还没有获取到IP，则重新配网
    beg_tick = xTaskGetTickCount();
    while (xTaskGetTickCount() - beg_tick < 10000)
    {
        status = MQTT_GetWiFiState(200);
        if (status == HAL_OK) break;
        MQTT_DELAY(200);
    }

    status = MQTT_GetWiFiState(200); //再次查询是否正确连接WiFi
    if (status != HAL_OK)
    {
        status = MQTT_ConnectWiFi(MQTT_WIFI_SSID, MQTT_WIFI_PSWD, 10000);
        if (status != HAL_OK) return status;
    }

    //发送MQTT用户配置，包含用户名和用户密钥
    status = MQTT_SendRetCmd(CMD_SET_MQTTUSERCFG, MSG_SUCCESS, MQTT_DEFAULT_TIMEOUT);
    if (status == HAL_ERROR) return status;

    //设置MQTT的ClientID
    status = MQTT_SendRetCmd(CMD_SET_CLIENTID, MSG_SUCCESS, MQTT_DEFAULT_TIMEOUT);
    if (status == HAL_ERROR) return status;

    //设置MQTT连接地址
    status = MQTT_SendRetCmd(CMD_SET_MQTTCONN, MSG_SUCCESS, MQTT_DEFAULT_TIMEOUT);
    if (status == HAL_ERROR) return status;

    //设置MQTT订阅的Topic：REPORT：上报数据
    status = MQTT_SendRetCmd(MQTT_SUB_TOPIC_REPORT, MSG_SUCCESS, MQTT_DEFAULT_TIMEOUT);
    if (status == HAL_ERROR) return status;

    //设置MQTT订阅的Topic：REPORT：下发数据
    status = MQTT_SendRetCmd(MQTT_SUB_TOPIC_COMMAND, MSG_SUCCESS, MQTT_DEFAULT_TIMEOUT);
    if (status == HAL_ERROR) return status;

    //设置时区+8（获取正确的时间）
    status = MQTT_SendRetCmd(CMD_SET_TIME_ZONE, MSG_SUCCESS, MQTT_DEFAULT_TIMEOUT);
    if (status == HAL_ERROR) return status;

    return status;
}

/**
 * @brief 获取Wifi状态（需要在调用初始化之后才可用）
 * @param timeout 超时时间，超出返回 HAL_TIMEOUT
 * @retval WiFi连接正常返回 HAL_OK
 * */
HAL_StatusTypeDef MQTT_GetWiFiState(uint32_t timeout)
{
    HAL_StatusTypeDef status;
    char* keyword_pos;

    status = MQTT_SendRetCmd(CMD_GET_CWSTATE, "+CWSTATE", timeout);
    if (status != HAL_OK) return status;

    keyword_pos = strstr(TempBuff, "+CWSTATE");
    if (*(keyword_pos + 9) == '2')
    {
        status = HAL_OK;
        return status;
    }

    status = HAL_ERROR;
    return status;
}

/**
 * @brief 连接Wifi
 * @param ssid WIFI的名称
 * @param pswd WIFI的密码
 * @param timeout 超时时间, 超过返回 HAL_TIMEOUT, 建议10s左右
 * @retval 成功连接到指定WiFi后返回 HAL_OK
 * */
HAL_StatusTypeDef MQTT_ConnectWiFi(char* ssid, char* pswd, uint32_t timeout)
{
    HAL_StatusTypeDef status;

    status = MQTT_SendRetCmd(CMD_SET_STA, MSG_SUCCESS, 4000);
    if (status != HAL_OK) return status;

    memset(TempBuff, 0, TEMP_BUFF_SIZE);
    sprintf(TempBuff, CMD_CONNECT_WIFI_F, ssid, pswd);

    status = MQTT_SendRetCmd(TempBuff, "WIFI GOT IP", timeout);
    if (status != HAL_OK)
    {
        return status;
    }

    status = MQTT_GetWiFiState(500);
    return status;
}

/**
 * @brief 发送不带返回值的MQTT命令
 * @param at_cmd AT命令，应当以“\r\n”结尾
 */
void MQTT_SendNoRetCmd(char* at_cmd)
{
    HAL_UART_Transmit(&MQTT_UART, (uint8_t*)at_cmd, strlen(at_cmd), MQTT_DEFAULT_TIMEOUT);
}

/**
 * @brief 发送带返回值的MQTT命令
 * @param at_cmd AT命令，应当以“\\r\\n”结尾
 * @param ret_keyword 期待的返回值，当检测到该关键词之后返回 HAL_OK
 * @param timeout 超时时间（ms），在超过时间之后返回 HAL_ERROR
 * @retval 成功返回HAL_OK
 * @note 如果返回的数据中包含ERROR，则立即返回 HAL_ERROR
 * @note 发送命令之后返回的数据会留在TempBuff中
 */
HAL_StatusTypeDef MQTT_SendRetCmd(char* at_cmd, char* ret_keyword, uint32_t timeout)
{
    TickType_t beg_tick;
    HAL_StatusTypeDef status = HAL_ERROR;
    if (queueMqttMsg == NULL) return HAL_ERROR;

    beg_tick = xTaskGetTickCount();
    //清空队列之后再接收新的数据
    if (xQueueReset(queueMqttMsg) != pdPASS)
    {
        status = HAL_ERROR;
        return status;
    }
    MQTT_SendNoRetCmd(at_cmd);

    //存在多条数据的情况，因此需要多条接收直到找到需要的
    while (xTaskGetTickCount() - beg_tick <= timeout)
    {
        if (xQueueReceive(queueMqttMsg, TempBuff, timeout) != pdTRUE)
        {
            status = HAL_TIMEOUT;
            return status;
        }
        if (strstr(TempBuff, ret_keyword))
        {
            status = HAL_OK;
            return status;
        }
        if (strstr(TempBuff, MSG_FAILED))
        {
            status = HAL_ERROR;
            return status;
        }
    }
    return status;
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
    memset(TempBuff, 0, TEMP_BUFF_SIZE);
    sprintf(TempBuff, MQTT_CMD_F_PUS_INT, property_name, val);
    return MQTT_SendRetCmd(TempBuff, MSG_SUCCESS, MQTT_DEFAULT_TIMEOUT);
}

/**
 * @brief 上报浮点数据
 * @param property_name 属性名
 * @param val 值
 * @note 需要订阅Report的Topic
 * @note 需要编译器支持浮点打印 target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -u _printf_float)
 * @note 浮点数格式化默认保留三位小数
 * @retval 成功返回HAL_OK
 */
HAL_StatusTypeDef MQTT_ReportDoubleVal(char* property_name, double val)
{
    /*
     * target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -u _printf_float)
     * */
    memset(TempBuff, 0, TEMP_BUFF_SIZE);
    sprintf(TempBuff, MQTT_CMD_F_PUS_DOUBLE, property_name, val);
    return MQTT_SendRetCmd(TempBuff, MSG_SUCCESS, MQTT_DEFAULT_TIMEOUT);
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

    char *request_id = (char*)pvPortMalloc(request_id_len);
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

        memset(TempBuff, 0, TEMP_BUFF_SIZE);
        sprintf(TempBuff, MQTT_SUB_REQUEST_F, request_id);
        MQTT_SendRetCmd(TempBuff, MSG_SUCCESS, MQTT_DEFAULT_TIMEOUT);

        memset(TempBuff, 0, TEMP_BUFF_SIZE);
        sprintf(TempBuff, MQTT_PUB_REQUEST_F, request_id);
        MQTT_SendRetCmd(TempBuff, MSG_SUCCESS, MQTT_DEFAULT_TIMEOUT);

        status = HAL_OK;
    }
    else
    {
        status = HAL_ERROR;
    }

    vPortFree(request_id);
    return status;
}

/**
 * @brief 从MQTT服务器获取时间日期
 * @param time_str 获取到的时间日期字符串，大小应 >= 25 Byte，格式为 Sat Jan 10 15:58:27 2026
 * @param timeout 超时时间，超出返回 HAL_TIMEOUT
 * @retval 成功返回 HAL_OK
 * @note 获取时间需要配置MQTT服务器，且连接正常，如果正常连接到服务器之后仍然获取失败（例如返回1970年），请等待一段时间之后再获取
 * */
HAL_StatusTypeDef MQTT_GetNTPTimeStr(char* time_str, uint32_t timeout)
{
    /*
     * +CIPSNTPTIME:Sat Jan 10 15:58:27 2026
     * Length:24
     * */
    const size_t time_str_len = 24;
    HAL_StatusTypeDef status;

    status = MQTT_SendRetCmd(CMD_GET_TIME, "CIPSNTPTIME", timeout);
    if (status != HAL_OK)
        return status;

    char* keywork_pos = strstr(TempBuff, "CIPSNTPTIME");
    if (keywork_pos)
    {
//        memcpy(time_str, keywork_pos + strlen("CIPSNTPTIME") + 1, (time_str_len) * sizeof (char));
        strncpy(time_str, keywork_pos + strlen("CIPSNTPTIME") + 1, time_str_len);
        time_str[time_str_len] = 0; //手动添加结束符
        status = HAL_OK;
    }
    else
    {
        status = HAL_ERROR;
    }
    return status;
}

/**
 * @brief 获取struct tm格式的时间
 * @param timeout 超时时间，超出返回 HAL_TIMEOUT
 * @retval 成功返回 HAL_OK
 * @note 获取时间需要配置MQTT服务器，且连接正常，如果正常连接到服务器之后仍然获取失败（例如返回1970年），请等待一段时间之后再获取
 */
HAL_StatusTypeDef MQTT_GetNTPTimeTm(struct tm *p_tm, uint32_t timeout)
{
    HAL_StatusTypeDef status;
    char time_str[25] = "";
    char month_str[4] = "";
    char *number_begin = NULL;
    char *convert_end = NULL;
    uint16_t temp_year = 0;
    uint8_t i = 0;

    status = MQTT_GetNTPTimeStr(time_str, timeout);
    if (status != HAL_OK) return status;

    //time_str: Sat Jan 10 15:58:27 2026
    strncpy(month_str, time_str + 4, 3);
    month_str[3] = '\0';

    for (i = 0; i < 12; i++)
    {
        if (!strcmp(month_str, MONTH_LIST[i]))
        {
            p_tm->tm_mon = i;
            break;
        }
    }

    number_begin = time_str + 8;
    //printf("<%s>", number_begin); < 1 08:00:01 1970>

    //number_begin:< 1 08:00:01 1970>
    p_tm->tm_mday = strtol(number_begin, &convert_end, 10);
    //convert_end:< 15:58:27 2026>
    p_tm->tm_hour = strtol(convert_end, &convert_end, 10);
    convert_end += 1;   //跳过 “:”
    p_tm->tm_min = strtol(convert_end, &convert_end, 10);
    convert_end += 1;   //跳过 “:”
    p_tm->tm_sec = strtol(convert_end, &convert_end, 10);
    temp_year = strtol(convert_end, &convert_end, 10);
    p_tm->tm_year = temp_year - 1900;

    status = HAL_OK;
    return status;
}

void MQTT_HandleUARTInterrupt()
{
    if (RecvLen >= MQTT_QUEUE_SIZE)
    {
        memset(RecvBuff, 0, MQTT_QUEUE_SIZE);
        RecvLen = 0;
    }
    else
    {
        RecvBuff[RecvLen++] = RecvCh;
        if (RecvCh == '\n')
        {
            //将数据发送到接收队列
            BaseType_t pxHigherPriorityTaskWoken = pdTRUE;
            if (xQueueSendFromISR(queueMqttMsg, RecvBuff, &pxHigherPriorityTaskWoken) == pdTRUE)
            {
                //发送成功则清除缓冲区
                RecvLen = 0;
                memset(RecvBuff, 0, MQTT_QUEUE_SIZE);
            }

        }
    }
    HAL_UART_Receive_IT(&huart6, (uint8_t*)&RecvCh, 1);
}
