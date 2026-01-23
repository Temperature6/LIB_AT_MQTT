# AT MQTT for STM32 HAL

> 将使用AT指令通过MQTT连接华为云IotDA的操作封装成库，用户只需要填写必要的信息即可轻松连接华为云IotDA，支持上报数据和接收华为云下发的指令。
>
> 该库有两个版本，裸机版本(AT_MQTT.h/.c)和FreeRTOS版本(AT_MQTT_OS.h/.c)，该库仅在华为云平台上测试过。



## 使用教程

### 填写连接信息

不管是裸机版本还是RTOS版本需要先填写连接信息

```c
/*MQTT用户配置*/
#define MQTT_WIFI_SSID			""
#define MQTT_WIFI_PWSD			""
#define MQTT_SERVICE_ID			""
/*三元组信息 https://iot-tool.obs-website.cn-north-4.myhuaweicloud.com/*/
#define MQTT_CLIENTID			""
#define MQTT_USERNAME			""
#define MQTT_USERPWSD			""
#define MQTT_HOST_NAME			""
```

`MQTT_WIFI_SSID` ：WIFI名称，建议不要包含中文或者特殊字符，ESP8266仅支持2.4GHzWiFi
`MQTT_WIFI_PWSD` ：WiFi密码
`MQTT_SERVICE_ID` ：服务ID，在华为云控制台->产品->产品列表内点进去对应的产品->左侧服务列表内的名字

下面三项由三元组生成器生成：[Huaweicloud IoTDA Mqtt ClientId Generator!](https://iot-tool.obs-website.cn-north-4.myhuaweicloud.com/)

`MQTT_CLIENTID` ：设备ID，由三元组生成器生成
`MQTT_USERNAME` ：用户名，由三元组生成器生成
`MQTT_USERPWSD` ：密钥，由三元组生成器生成
`MQTT_HOST_NAME` ：连接域名，在华为云控制台->设备->所有设备->设备列表中点进去对应的设备->MQTT连接参数（查看）->hostname

### 裸机版本

#### 硬件接口移植

在`AT_MQTT.h`中找到

```c
#define MQTT_CONFIG_WIFI		1			//切换网络（WIFI名称，密码）第一次运行需要联网，之后不再需要
#define MQTT_RX_BUF_MAX_LEN	    1000		//接收缓冲区长度
#define MQTT_UART		        huart2		//使用的uart外设句柄
#define MQTT_REPORT_BUF_LEN	    200         //上报数据的缓冲区长度
#define MQTT_DEFAULT_TIMEOUT    10000       //默认超时时间
#define MQTT_REQUEST_ID_LEN     36
#define MQTT_SUBRECV_KEYWORD    "MQTTSUBRECV"
```

将`MQTT_UART`设置为对应的串口句柄，其他一般保持默认即可。

在填写的串口句柄对应的中断处理函数中调用`MQTT_HandleUARTInterrupt()`，如下面代码所示

```c
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &MQTT_UART)
    {
        MQTT_HandleUARTInterrupt();
    }
}
```

#### 初始化

在主函数中先调用初始化函数`MQTT_Init();`，函数返回值表明了初始化是否成功，初始化成功后返回`HAL_OK`，初始化函数中会开启中断，因此不需要手动开启串口中断。

#### 上报数据

使用`MQTT_ReportIntVal`上报整型数据，上报成功返回 `HAL_OK`

```c
MQTT_ReportIntVal("属性名", 值);
```

使用`MQTT_ReportDoubleVal`上报浮点型数据（保留三位小数），上报成功返回 `HAL_OK`

```c
MQTT_ReportDoubleVal("属性名", 值);
```

需要注意的时，由于`MQTT_ReportDoubleVal`中使用了`sprintf`，打印小数时需要编译器支持浮点数打印，CMake的STM32工程可以在CMakeLists.txt里面添加如下代码来支持浮点打印：

```cmake
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -u _printf_float)
```

这两种类型的数据用的最多，如果有其他数据类型的需要可以自己编写对应的函数，比较简单。

#### 下发指令

```c
if (MQTT_IsNewLine)
{
    MQTT_IsNewLine = 0;
    printf("MQTT_RECV>>>%s<<<", MQTT_RxBuf);
    //寻找是否存在命令下发的关键字
    if (strstr(MQTT_RxBuf, MQTT_SUBRECV_KEYWORD))
    {
        char* rx_buf_copy = (char*)malloc(sizeof (char) * strlen(MQTT_RxBuf));
        if (rx_buf_copy)    //内存分配成功
        {
            memset(rx_buf_copy, 0, sizeof (char) * strlen(MQTT_RxBuf));
            memcpy(rx_buf_copy, MQTT_RxBuf, sizeof (char) * strlen(MQTT_RxBuf));
            MQTT_ClearRXBuf();  //复制完成之后清空中断缓冲区
            MQTT_HandleRequestID(rx_buf_copy);
            printf("Context:%s\r\n", rx_buf_copy);
            free(rx_buf_copy);
        }
    }
}
```

此处提供了一个处理下发指令的处理实例，当接收ESP-01S发来的数据之后，`MQTT_IsNewLine`被置1，此时判断缓冲区中是否有下发指令的关键字`MQTTSUBRECV`（此处由宏定义`MQTT_SUBRECV_KEYWORD`代替），如果没有，则认为是无关数据，则忽视，如果有，就说明是云平台下发的命令，此时分配一块新的内存用于存储下发的数据，防止在处理过程中进入有新的数据发来导致就的数据被冲刷。

数据存到新开辟的缓冲区之后，需要调用`MQTT_HandleRequestID`提取命令中的`request_id`并返回给云平台，否则云平台会卡住，超过20S之后没回应云平台会显示超时。

调用之后设备已经完成了从接收命令到返回结果的过程，数据可自行处理。

### FreeRTOS版本

#### 硬件接口移植

```c
/*用户配置*/
#define MQTT_UART                huart6        //使用的uart外设句柄
#define MQTT_DEFAULT_TIMEOUT    10000       //默认超时时间
/*FreeRTOS配置*/
#define MQTT_QUEUE_LEN          (5)     //队列最多有多少条消息
#define MQTT_QUEUE_SIZE         (300)   //队列每条消息的最大长度
#define MQTT_DELAY              osDelay
```

将`MQTT_UART` 设置为接收串口中断的串口句柄

RTOS版本使用消息队列来缓冲串口发来的数据，因此如果默认的`MQTT_QUEUE_LEN`和`MQTT_QUEUE_SIZE`实测可以正常使用，如果资源不够可以考虑调小`MQTT_QUEUE_LEN`，但不建议低于2，`MQTT_QUEUE_SIZE`不建议调整到更低的值。

`MQTT_DELAY`需要填写FreeRTOS的任务延时函数。

```c
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart == &huart6) {
        MQTT_HandleUARTInterrupt();
    }
}
```

在串口中断服务函数中调用`MQTT_HandleUARTInterrupt()`来处理中断内的数据。

**注意**，`MQTT_Init()` 中会完成消息队列的初始化和开启中断，中断服务函数中会使用消息队列，因此在消息队列初始化之前不能开启对应串口的串口中断，否则会导致严重错误。

#### 初始化

```c
void TaskATMqtt(void *argument) {
    /* USER CODE BEGIN TaskATMqtt */
    UNUSED(argument);
    HAL_StatusTypeDef status = HAL_OK;
    //MQTT初始化
    status = MQTT_Init();
    osPrintf("MQTT Init Status:%d\r\n", status);

    /* Infinite loop */
    for (;;) {
        osDelay(2000);
    }
    /* USER CODE END TaskATMqtt */
}
```

在任务的永久循环开始之前完成初始化，返回值用于表明初始化是否成功，成功返回`HAL_OK`

#### 上报数据

**【注意：由于后续所有操作都依赖于消息队列，因此必须先调用**`MQTT_Init()`**完成初始化】**

使用`MQTT_ReportIntVal`上报整型数据，上报成功返回 `HAL_OK`

```c
MQTT_ReportIntVal("属性名", 值);
```

使用`MQTT_ReportDoubleVal`上报浮点型数据（保留三位小数），上报成功返回 `HAL_OK`

```c
MQTT_ReportDoubleVal("属性名", 值);
```

需要注意的时，由于`MQTT_ReportDoubleVal`中使用了`sprintf`，打印小数时需要编译器支持浮点数打印，CMake的STM32工程可以在CMakeLists.txt里面添加如下代码来支持浮点打印：

```cmake
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -u _printf_float)
```

这两种类型的数据用的最多，如果有其他数据类型的需要可以自己编写对应的函数，比较简单。

#### 下发指令

```c
void TaskATMqtt(void *argument) {
    /* USER CODE BEGIN TaskATMqtt */
    UNUSED(argument);
    HAL_StatusTypeDef status = HAL_OK;
    char subrecv_text[MQTT_QUEUE_SIZE] = "";
    
    //MQTT初始化
    status = MQTT_Init();
    osPrintf("MQTT Init Status:%d\r\n", status);

    /* Infinite loop */
    for (;;) {
        xQueueReceive(queueMqttMsg, subrecv_text, portMAX_DELAY);
        if (strstr(subrecv_text, MQTT_SUBRECV_KEYWORD)) {
            MQTT_HandleRequestID(subrecv_text);
            osPrintf("RECV:%s\r\n", subrecv_text);
        }
    }
    /* USER CODE END TaskATMqtt */
}
```

该代码提供的一个简单的实例，展示了MQTT初始化之后处理下发数据的过程，下发的数据接收到之后会进入`queueMqttMsg`，用户此时判断缓冲区中是否有下发指令的关键字`MQTTSUBRECV`（此处由宏定义`MQTT_SUBRECV_KEYWORD`代替），如果没有，则认为是无关数据，则忽视，如果有，就说明是云平台下发的命令，需要调用`MQTT_HandleRequestID`提取命令中的`request_id`并返回给云平台，否则云平台会卡住，超过20S之后没回应云平台会显示超时。调用完成之后数据继续保存在`subrecv_text`中，用户可以自行处理。

下面的实例提供了一个完整处理数据的思路，该实例依赖了[cJSON库](https://github.com/DaveGamble/cJSON)来解析云平台下发的数据：

```c
void TaskATMqtt(void *argument) {
    /* USER CODE BEGIN TaskATMqtt */
    UNUSED(argument);
    HAL_StatusTypeDef status = HAL_OK;
    char subrecv_text[MQTT_QUEUE_SIZE] = "";

    //使用FreeRTOS提供的内存操作API，如果不使用FreeRTOS提供的API，会导致json解析失败，因为不能分配内存
    cJSON_Hooks hooks;
    hooks.malloc_fn = pvPortMalloc;
    hooks.free_fn = vPortFree;
    cJSON_InitHooks(&hooks);
	
    //cJSON库需要的变量
    uint8_t state = 0;
    char *json_text = NULL, *command_name = NULL;
    cJSON *root = NULL, *paras = NULL, *j_cmd = NULL;

    //MQTT初始化
    status = MQTT_Init();
    osPrintf("MQTT Init Status:%d\r\n", status);

    /* Infinite loop */
    for (;;) {
        //等待数据队列出现数据
        xQueueReceive(queueMqttMsg, subrecv_text, portMAX_DELAY);
        //检查数据队列中是否有下发指令的关键字
        if (strstr(subrecv_text, MQTT_SUBRECV_KEYWORD)) {
            MQTT_HandleRequestID(subrecv_text);	//处理request_id
			
            //在数据中寻找到json的部分
            json_text = strstr(subrecv_text, ",{") + 1; //定位到json数据的未知
            *(strstr(json_text, "\r\n")) = 0;	//手动添加json部分的结束符（也许不用）

            /* 
            数据结构大概是这样，根据自己的数据结构调整节点名等
            {
                "paras":{
                    "state":0
                },
                "service_id":"data",
                "command_name":"led_color_ctrl"
            }
         	*/
            root = cJSON_Parse(json_text); //解析json数据
            if (!root) {
                osPrintf("Failed to Parse JSON text:%s\r\n", json_text);
                continue;   //解析失败直接回到循环起始，不再处理
            }
            //paras也是一个json object
            paras = cJSON_GetObjectItem(root, "paras");
            if (!cJSON_IsObject(paras)) continue;
			
            //解析paras节点下state的值
            cJSON *j_param = cJSON_GetObjectItem(paras, "state");
            if (!cJSON_IsNumber(j_param)) continue;
            state = j_param->valueint;
			
            //解析root节点下command_name的值
            j_cmd = cJSON_GetObjectItem(root, "command_name");
            if (!cJSON_IsString(j_cmd)) continue;
            command_name = j_cmd->valuestring;
			
            //打印展示接收到的数据
            osPrintf("command:%s state:%d\r\n", command_name, state);
			
            //将该命令通过队列发送到其他硬件控制线程实现控制硬件，处理数据的部分可自行实现
            if (!strcmp(command_name, "led_color_ctrl"))
            {
                xQueueSend(queueLedCtrl, &state, 100);
            }
            
            cJSON_Delete(root);	//删除资源
        }
    }
    /* USER CODE END TaskATMqtt */
}
```

代码中的`osPrintf`是我自己实现的一个线程安全的printf，具体实现如下，可参考

```c
#define osPrintf(format, ...)  do{                              \
    osMutexAcquire(mutex_printfHandle, osWaitForever);          \
    printf(format __VA_OPT__(,) __VA_ARGS__);                   \
    osMutexRelease(mutex_printfHandle);                         \
}while(0)
```

该宏定义用到了C23的新特性，如果使用旧版C标准，可能会有警告，但大概率不影响使用，在CMakeLists.txt中设置C标准

```cmake
set(CMAKE_C_STANDARD 23)
```



## 参考

[stm32+AT指令+ESP8266接入华为云物联网平台并完成属性上报与命令响应](https://blog.csdn.net/weixin_43351158/article/details/125954842)

[教你如何使用esp8266接入华为云物联网平台（IOTDA）（Arduino IDE开发）](https://blog.csdn.net/weixin_43351158/article/details/122789453)

[Huaweicloud IoTDA Mqtt ClientId Generator!](https://iot-tool.obs-website.cn-north-4.myhuaweicloud.com/)

