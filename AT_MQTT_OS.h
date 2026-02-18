#ifndef AT_MQTT_OS_H
#define AT_MQTT_OS_H

#include "main.h"
#include "FreeRTOS.h"
#include "queue.h"
#include <time.h>

/*MQTT用户配置*/
#define MQTT_WIFI_SSID              ""
#define MQTT_WIFI_PSWD              ""

#define MQTT_SERVICE_ID             ""
//三元组信息 https://iot-tool.obs-website.cn-north-4.myhuaweicloud.com/
#define MQTT_CLIENTID               ""
#define MQTT_USERNAME               ""
#define MQTT_USERPWSD               ""
#define MQTT_HOST_NAME              ""

#define MQTT_SUBRECV_KEYWORD    "MQTTSUBRECV"

/*用户配置*/
#define MQTT_UART                   huart6        //使用的uart外设句柄
#define MQTT_DEFAULT_TIMEOUT        10000       //默认超时时间
/*FreeRTOS配置*/
#define MQTT_ENABLE_DECIMAL_FORMAT  0
#define MQTT_QUEUE_LEN              (5)     //队列最多有多少条消息
#define MQTT_QUEUE_SIZE             (300)   //队列每条消息的最大长度
#define MQTT_DELAY                  osDelay

extern QueueHandle_t queueMqttMsg;


HAL_StatusTypeDef MQTT_Init(void);

HAL_StatusTypeDef MQTT_GetWiFiState(char *ssid, uint32_t timeout);

HAL_StatusTypeDef MQTT_ConnectWiFi(char *ssid, char *pswd, uint32_t timeout);

void MQTT_SendNoRetCmd(char *ATCmd);

HAL_StatusTypeDef MQTT_SendRetCmd(char *at_cmd, char *ret_keyword, uint32_t timeout);

HAL_StatusTypeDef MQTT_ReportIntVal(char *property_name, int val);

HAL_StatusTypeDef MQTT_ReportDoubleVal(char *property_name, double val);

HAL_StatusTypeDef MQTT_ReportCustomJSONPayload(const char * payload);

HAL_StatusTypeDef MQTT_HandleRequestID(char *sub_recv_text, uint16_t result_code, char *response_name, char *execute_result);

HAL_StatusTypeDef MQTT_GetNTPTimeStr(char *time_str, uint32_t timeout);

HAL_StatusTypeDef MQTT_GetNTPTimeTm(struct tm *p_tm, uint32_t timeout);

void MQTT_HandleUARTInterrupt();

#endif //AT_MQTT_OS_H
