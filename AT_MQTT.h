#ifndef AT_MQTT_H_
#define AT_MQTT_H_
#include "main.h"

/*MQTT用户配置*/
#define MQTT_WIFI_SSID			""
#define MQTT_WIFI_PWSD			""
#define MQTT_SERVICE_ID			""
/*三元组信息 https://iot-tool.obs-website.cn-north-4.myhuaweicloud.com/*/
#define MQTT_CLIENTID			""
#define MQTT_USERNAME			""
#define MQTT_USERPWSD			""
#define MQTT_HOST_NAME			""

/*MQTT固定格式*/
//上报数据
#define MQTT_TOPIC_REPORT		"$oc/devices/"MQTT_USERNAME"/sys/properties/report"
#define MQTT_SUB_TOPIC_REPORT	"AT+MQTTSUB=0,\""MQTT_TOPIC_REPORT"\",1\r\n"
//下发数据
#define MQTT_TOPIC_COMMAND		"$oc/devices/"MQTT_USERNAME"/sys/commands/#"
#define MQTT_SUB_TOPIC_COMMAND	"AT+MQTTSUB=0,\""MQTT_TOPIC_COMMAND"\",1\r\n"
//下发数据反馈
#define MQTT_SUB_REQUEST_F "AT+MQTTSUB=0,\"$oc/devices/"MQTT_USERNAME"/sys/commands/response/request_id=%s\",1\r\n"
#define MQTT_PUB_REQUEST_F "AT+MQTTPUB=0,\"$oc/devices/"MQTT_USERNAME"/sys/commands/response/request_id=%s\",\"\",1,1\r\n"

#define MQTT_JSON_REPORT_INT	"{\\\"services\\\":[{\\\"service_id\\\":\\\""MQTT_SERVICE_ID"\\\"\\,\\\"properties\\\":{\\\"%s\\\":%d}}]}"
#define MQTT_CMD_F_PUS_INT	    "AT+MQTTPUB=0,\""MQTT_TOPIC_REPORT"\",\""MQTT_JSON_REPORT_INT"\",0,0\r\n"
#define MQTT_JSON_REPORT_DOUBLE "{\\\"services\\\":[{\\\"service_id\\\":\\\""MQTT_SERVICE_ID"\\\"\\,\\\"properties\\\":{\\\"%s\\\":%.3lf}}]}"
#define MQTT_CMD_F_PUS_DOUBLE   "AT+MQTTPUB=0,\""MQTT_TOPIC_REPORT"\",\""MQTT_JSON_REPORT_DOUBLE"\",0,0\r\n"

/*用户配置*/
#define MQTT_CONFIG_WIFI		1			//切换网络（WIFI名称，密码）第一次运行需要联网，之后不再需要
#define MQTT_RX_BUF_MAX_LEN	    1000		//接收缓冲区长度
#define MQTT_UART		        huart2		//使用的uart外设句柄
#define MQTT_REPORT_BUF_LEN	    200         //上报数据的缓冲区长度
#define MQTT_DEFAULT_TIMEOUT    10000       //默认超时时间
#define MQTT_REQUEST_ID_LEN     36
#define MQTT_SUBRECV_KEYWORD    "MQTTSUBRECV"

extern char MQTT_RxBuf[MQTT_RX_BUF_MAX_LEN];
extern char MQTT_RxChar;
extern size_t MQTT_RxBufLen;
extern uint8_t MQTT_IsNewLine;

HAL_StatusTypeDef MQTT_Init(void);
void MQTT_SendNoRetCmd(char* ATCmd);
HAL_StatusTypeDef MQTT_SendRetCmd(char* at_cmd,char* ret_keyword, uint32_t timeout);
HAL_StatusTypeDef MQTT_ReportIntVal(char* property_name, int val);
HAL_StatusTypeDef MQTT_ReportDoubleVal(char* property_name, double val);
HAL_StatusTypeDef MQTT_HandleRequestID(char* sub_recv_text);
HAL_StatusTypeDef MQTT_GetNTPTime(char* time_str, uint32_t timeout);
void MQTT_ClearRXBuf(void);
void MQTT_HandleUARTInterrupt(void);


#endif //AT_MQTT_H_
