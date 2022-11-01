/**
 * @author newyouth (you@domain.com)
 * @brief
 * @version 0.1
 * @date 2022-06-21
 *
 * @copyright Copyright (c) 2022
 *
 */
/* Private includes ----------------------------------------------------------*/
// #include "common.h"
// #include "uart_driver.h"
// #include "mqtt_tools.h"
// #include "esp_crc.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "HLW8110.h"
/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/
#define POWE_UART UART_NUM_1
#define PATTERN_CHR_NUM (3) /*!< Set the number of consecutive and identical characters received by receiver which defines a UART pattern*/
#define BUF_SIZE (1024)
#define RD_BUF_SIZE (BUF_SIZE)
/* Private macro -------------------------------------------------------------*/
static const char *TAG = "uart_events";
static QueueHandle_t uart0_queue;
power Hlw_register;
Electrical_energy electricity;
static uint8_t hlw_addr = 0;
static uint8_t meter_read_state = 0;
/* Private variables ---------------------------------------------------------*/

static void uart_send_buffer(uint8_t *buffer, uint16_t len);

#if 1 //工具函数
static void array_to_u16(uint8_t *data, uint16_t *result)
{
    *result = data[0];
    *result <<= 8;
    *result += data[1];
}
static void array_to_u64(uint8_t *data, uint64_t *result, uint8_t len)
{
    *result = 0;
    for (uint8_t i = 0; i < len; i++)
    {
        ((uint8_t *)result)[len - 1 - i] = data[i];
    }
    printf("array_to_u64: %lld, %llX\r\n", *result, *result);
}
static void array_to_i64(uint8_t *data, int64_t *result, uint8_t len)
{
    *result = 0;
    if (data[0] & 0x80)
    {
        *result |= 0xFFFFFFFFFFFFFFFF;
    }

    for (uint8_t i = 0; i < len; i++)
    {
        ((int8_t *)result)[len - 1 - i] = data[i];
    }
    printf("array_to_i64: %lld, %llX\r\n", *result, *result);
}
#endif
#if 1 //数据帧格式产生
static uint8_t generate_base_frame(uint8_t *buffer, uint8_t cmd, uint8_t data_len, uint8_t *data)
{
    uint8_t index = 0;
    uint8_t sum = 0;
    // head
    buffer[index++] = 0xA5;
    buffer[index++] = cmd;
    if (data_len > 0)
    {
        // data
        for (uint8_t i = 0; i < data_len; i++)
        {
            buffer[index++] = data[i];
        }
        // sum
        for (uint8_t i = 0; i < index; i++)
        {
            sum += buffer[i];
        }
        sum = ~sum;
        buffer[index++] = sum;
    }

    return index;
}
static uint8_t hlw8110_generate_read(uint8_t *buffer, uint8_t addr)
{
    return generate_base_frame(buffer, addr, 0, NULL);
}

static uint8_t hlw8110_generate_write(uint8_t *buffer, uint8_t addr, uint8_t data_len, uint8_t *data)
{
    return generate_base_frame(buffer, 0x80 | addr, data_len, data);
}

static uint8_t hlw8110_generate_unlock(uint8_t *buffer)
{
    uint8_t temp_u8_array[1] = {0xE5};
    return generate_base_frame(buffer, 0xEA, 1, temp_u8_array);
}

static uint8_t hlw8110_generate_lock(uint8_t *buffer)
{
    uint8_t temp_u8_array[1] = {0xDC};
    return generate_base_frame(buffer, 0xEA, 1, temp_u8_array);
}

static uint8_t hlw8110_generate_set_channelA(uint8_t *buffer)
{
    uint8_t temp_u8_array[1] = {0x5A};
    return generate_base_frame(buffer, 0xEA, 1, temp_u8_array);
}
#endif
/**
 * @brief 解析电量模块数据
 *
 * @param data 接收到的数据
 * @param length 接收到的数据长度
 */
static void parse_power_data(uint8_t *data, uint16_t length)
{
    uint64_t temp_u64 = 0;
    int64_t temp_i64 = 0;
    uint8_t sum = 0;
    printf("hlw_addr: %02X\r\n", hlw_addr);
    for (int i = 0; i < length; i++)
    {
        printf("%02X ", data[i]);
    }

    // check sum
    sum = 0xA5 + hlw_addr;
    for (uint8_t i = 0; i < length - 1; i++)
    {
        sum += data[i];
    }
    sum = ~(sum & 0xFF);
    printf("sum: %02x\r\n", sum);
    if (sum == data[length - 1])
    {
        switch (hlw_addr)
        {
        case 0x70:
            // read RmsIAC
            array_to_u16(data, &Hlw_register.RmsIAC);
            break;
        case 0x72:
            // read RmsUC
            array_to_u16(data, &Hlw_register.RmsIUC);
            break;
        case 0x73:
            // read PowerPAC
            array_to_u16(data, &Hlw_register.PowerPAC);
            break;
        case 0x27:
            // read PF_reg
            array_to_u64(data, &temp_u64, 3);
            float a = 0;
            printf("A通道功率因素寄存器:%lld\n", temp_u64);
            if (temp_u64 > 0x800000) //为负，容性负载
            {
                a = (float)(0xffffff - temp_u64 + 1) / 0x7fffff;
            }
            else
            {
                a = (float)temp_u64 / 0x7fffff;
            }

            if (Hlw_register.factor < 0.3) // 小于0.3W，空载或小功率，PF不准
                a = 0;
            //功率因素*100，最大为100，最小负100
            Hlw_register.factor = a;
            printf("功率因素：%.2f\n", Hlw_register.factor);
            break;
        case 0x24:
            // read RmsIA
            array_to_i64(data, &temp_i64, 3);
            temp_i64 = temp_i64 * Hlw_register.RmsIAC * 10;
            temp_i64 /= 0x800000;
            electricity.I_rms = (float)temp_i64 * 0.01;
            printf("电流寄存器:%lld   ",temp_i64);
            printf("电流有效值:%.2f\n", electricity.I_rms);
            break;
        case 0x26:
            // read RmsU
            array_to_u64(data, &temp_u64, 3);
            temp_u64 = temp_u64 * Hlw_register.RmsUC * 10;
            temp_u64 /= 0x400000;
            Hlw_register.voltage = (uint32_t)temp_u64;
            printf("电压寄存器:%d\n ", Hlw_register.voltage);
            break;
        case 0x2C:
            // read PowerPA
            array_to_i64(data, &temp_i64, 4);
            temp_i64 = temp_i64;
            temp_i64 = temp_i64 * Hlw_register.PowerPAC * 10;
            temp_i64 /= 0x80000000;
            Hlw_register.ratework = (int32_t)temp_i64;
            break;
        case 0x23:
            // read Ufreq
            array_to_u64(data, &temp_u64, 2);
            Hlw_register.frequency = (uint32_t)(3579545 / 8 * 10 / temp_u64);
            printf("当前频率：%d\n",Hlw_register.frequency);
            break;
        case 0x22:
        {
            float temp = 0.0;
            array_to_u16(data, &Hlw_register.angle_reg);
            if (Hlw_register.frequency < 55)
            {
                temp = (float)Hlw_register.angle_reg;
                temp = temp * 0.0805;
                Hlw_register.angle = temp;
            }
            else
            {
                temp = (float)Hlw_register.angle_reg;
                temp = temp * 0.0965;
                Hlw_register.angle = temp;
            }
            printf("F_Angle = %f\n " ,Hlw_register.angle);	
            break;
        }
        default:
            break;
        }

        // printf("Hlw_register.RmsIAC: %06x\r\n", Hlw_register.RmsIAC);
    }

    // printf("电能数据校验成功!!!\r\n");
    // array_to_u32(data + 3, &Hlw_register.voltage);
    // // printf("Hlw_register.电压 = %0.4f V\r\n",Hlw_register.voltage*0.0001);
    // array_to_u32(data+7, &Hlw_register.current);
    // // printf("Hlw_register.电流 = %0.4f A\r\n",Hlw_register.current*0.0001);
    // array_to_u32(data+11, &Hlw_register.ratework);
    // // printf("Hlw_register.有功功率 = %0.4f W\r\n",Hlw_register.ratework*0.0001);
    // array_to_u32(data+15, &Hlw_register.quantity);
    // // printf("Hlw_register.有功总电量 = %0.4f kWh\r\n",Hlw_register.quantity*0.0001);
    // array_to_u32(data+19, &Hlw_register.factor);
    // // printf("Hlw_register.功率因数 = %0.3f \r\n",Hlw_register.factor*0.001);
    // array_to_u32(data+23, &Hlw_register.CO2);
    // // printf("Hlw_register.二氧化碳 = %0.4f Kg\r\n",Hlw_register.CO2*0.0001);
    // array_to_u32(data+27, &Hlw_register.temperature);
    // // printf("Hlw_register.温度 = %0.2f ℃\r\n",Hlw_register.temperature*0.01);
    // array_to_u32(data+31, &Hlw_register.frequency);
    // // printf("Hlw_register.频率 = %0.4f Hz\r\n",Hlw_register.frequency*0.01);
}

/**
 * @brief 用于向串口1发送指定长度的数据
 *
 * @param buffer
 * @param len
 */
static void uart_send_buffer(uint8_t *buffer, uint16_t len)
{
    uart_write_bytes(POWE_UART, buffer, len);
}

static void uart_event_task(void *pvParameters)
{
    uart_event_t event;
    size_t buffered_size;
    uint8_t *dtmp = (uint8_t *)malloc(RD_BUF_SIZE);
    for (;;)
    {
        // Waiting for UART event.
        if (xQueueReceive(uart0_queue, (void *)&event, (portTickType)portMAX_DELAY))
        {
            bzero(dtmp, RD_BUF_SIZE);
            // ESP_LOGI(TAG, "uart[%d] event:", POWE_UART);
            switch (event.type)
            {
            // Event of UART receving data 最好快速处理事件，防止队列满
            case UART_DATA:
                ESP_LOGI(TAG, "UART[%d] [UART DATA]: %d", POWE_UART, event.size);
                uart_read_bytes(POWE_UART, dtmp, event.size, portMAX_DELAY);
                for (int i = 0; i < event.size; i++)
                {
                    printf("%02X ", dtmp[i]);
                }
                printf("\r\n");
                parse_power_data(dtmp, event.size);
                break;
            // Event of HW FIFO overflow detected
            case UART_FIFO_OVF:
                ESP_LOGI(TAG, "hw fifo overflow");
                // If fifo overflow happened, you should consider adding flow control for your application.
                // The ISR has already reset the rx FIFO,
                // As an example, we directly flush the rx buffer here in order to read more data.
                uart_flush_input(POWE_UART);
                xQueueReset(uart0_queue);
                break;
            // Event of UART ring buffer full
            case UART_BUFFER_FULL:
                ESP_LOGI(TAG, "ring buffer full");
                // If buffer full happened, you should consider encreasing your buffer size
                // As an example, we directly flush the rx buffer here in order to read more data.
                uart_flush_input(POWE_UART);
                xQueueReset(uart0_queue);
                break;
            // Event of UART RX break detected
            case UART_BREAK:
                ESP_LOGI(TAG, "uart rx break");
                break;
            // Event of UART parity check error
            case UART_PARITY_ERR:
                ESP_LOGI(TAG, "uart parity error");
                break;
            // Event of UART frame error
            case UART_FRAME_ERR:
                ESP_LOGI(TAG, "uart frame error");
                break;
            // UART_PATTERN_DET
            case UART_PATTERN_DET:
                uart_get_buffered_data_len(POWE_UART, &buffered_size);
                int pos = uart_pattern_pop_pos(POWE_UART);
                ESP_LOGI(TAG, "[UART PATTERN DETECTED] pos: %d, buffered size: %d", pos, buffered_size);
                if (pos == -1)
                {
                    // There used to be a UART_PATTERN_DET event, but the pattern position queue is full so that it can not
                    // record the position. We should set a larger queue size.
                    // As an example, we directly flush the rx buffer here.
                    uart_flush_input(POWE_UART);
                }
                else
                {
                    uart_read_bytes(POWE_UART, dtmp, pos, 100 / portTICK_PERIOD_MS);
                    uint8_t pat[PATTERN_CHR_NUM + 1];
                    memset(pat, 0, sizeof(pat));
                    uart_read_bytes(POWE_UART, pat, PATTERN_CHR_NUM, 100 / portTICK_PERIOD_MS);
                    ESP_LOGI(TAG, "read data: %s", dtmp);
                    ESP_LOGI(TAG, "read pat : %s", pat);
                }
                break;
            // Others
            default:
                ESP_LOGI(TAG, "uart event type: %d", event.type);
                break;
            }
        }
    }
    free(dtmp);
    dtmp = NULL;
    vTaskDelete(NULL);
}

static void power_detected_task(void *pvParameters)
{
    uint8_t requst_power_data[8] = {0};

    uint8_t buffer_len = hlw8110_generate_unlock(requst_power_data);
    uart_send_buffer(requst_power_data, buffer_len);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    buffer_len = hlw8110_generate_set_channelA(requst_power_data);
    uart_send_buffer(requst_power_data, buffer_len);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    uint8_t temp_u8_array1[2] = {0x0A, 0x04};
    buffer_len = hlw8110_generate_write(requst_power_data, 0x00, 2, temp_u8_array1);
    uart_send_buffer(requst_power_data, buffer_len);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    uint8_t temp_u8_array2[2] = {0x00, 0x01};
    buffer_len = hlw8110_generate_write(requst_power_data, 0x01, 2, temp_u8_array2);
    uart_send_buffer(requst_power_data, buffer_len);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    uint8_t temp_u8_array3[2] = {0x04, 0x65};
    buffer_len = hlw8110_generate_write(requst_power_data, 0x13, 2, temp_u8_array3);
    uart_send_buffer(requst_power_data, buffer_len);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    buffer_len = hlw8110_generate_lock(requst_power_data);
    uart_send_buffer(requst_power_data, buffer_len);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    buffer_len = hlw8110_generate_read(requst_power_data, 0x00);
    uart_send_buffer(requst_power_data, buffer_len);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    buffer_len = hlw8110_generate_read(requst_power_data, 0x01);
    uart_send_buffer(requst_power_data, buffer_len);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    buffer_len = hlw8110_generate_read(requst_power_data, 0x13);
    uart_send_buffer(requst_power_data, buffer_len);
    vTaskDelay(100 / portTICK_PERIOD_MS);

    while (true)
    {
        switch (meter_read_state)
        {
        case 1:
            // read RmsIAC
            hlw_addr = 0x70;
            break;
        case 2:
            // read RmsUC
            hlw_addr = 0x72;
            break;
        case 3:
            // read PowerPAC
            hlw_addr = 0x73;
            break;
        case 4:
            // read PowerSC
            hlw_addr = 0x75;
            break;
        case 5:
            // read EnergyAC
            hlw_addr = 0x76;
            break;
        case 6:
            // read HFConst
            hlw_addr = 0x02;
            break;
        case 7:
            // read Angle
            hlw_addr = 0x22;
            break;
        case 8:
            // read Ufreq
            hlw_addr = 0x23;
            break;
        case 9:
            // read RmsIA
            hlw_addr = 0x24;
            break;
        case 10:
            // read RmsU
            hlw_addr = 0x26;
            break;
        case 11:
            // read PowerFactor
            hlw_addr = 0x27;
            break;
        case 12:
            // read Energy_PA
            hlw_addr = 0x28;
            break;
        case 13:
            // read PowerPA
            hlw_addr = 0x2C;
            break;
        case 14:
            // read PowerS
            hlw_addr = 0x2E;
            break;
        default:
            meter_read_state = 0;
            break;
        }
        meter_read_state++;
        // printf("hlw_addr: %02X\r\n", hlw_addr);

        uint8_t buffer_len = hlw8110_generate_read(requst_power_data, hlw_addr);
        uart_send_buffer(requst_power_data, buffer_len);
        vTaskDelay(200 / portTICK_PERIOD_MS);
        uart_send_buffer(requst_power_data, buffer_len);
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
}

void app_main(void)
{
    esp_log_level_set(TAG, ESP_LOG_INFO);

    /* Configure parameters of an UART driver,
     * communication pins and install the driver */
    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_EVEN,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    // Install UART driver, and get the queue.
    uart_driver_install(POWE_UART, BUF_SIZE * 2, BUF_SIZE * 2, 20, &uart0_queue, 0);
    uart_param_config(POWE_UART, &uart_config);

    // Set UART log level
    esp_log_level_set(TAG, ESP_LOG_INFO);
    // Set UART pins
    uart_set_pin(POWE_UART, GPIO_NUM_27, GPIO_NUM_14, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    // Set uart pattern detect function.
    uart_enable_pattern_det_baud_intr(POWE_UART, '+', PATTERN_CHR_NUM, 9, 0, 0);
    // Reset the pattern queue length to record at most 20 pattern positions.
    uart_pattern_queue_reset(POWE_UART, 20);

    gpio_set_direction(26,GPIO_MODE_OUTPUT);
    gpio_set_level(26,1);
    // Create a task to handler UART event from ISR
    xTaskCreate(uart_event_task, "uart_event_task", 2048, NULL, 12, NULL);
    xTaskCreate(power_detected_task, "power_detected_task", 2048, NULL, 6, NULL);
}
