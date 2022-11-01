/**
 * @author newyouth (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2022-06-21
 * 
 * @copyright Copyright (c) 2022
 * 
 */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __UART_DRIVER_H
#define __UART_DRIVER_H
/* Includes ------------------------------------------------------------------*/
/* Exported types ------------------------------------------------------------*/
typedef struct
{
    uint32_t voltage;
    int32_t current;
    int32_t ratework;
    uint32_t energy;
    // uint32_t quantity;
    float factor;
    uint16_t angle_reg;
    // uint32_t CO2;
    uint32_t temperature;
    uint32_t frequency;


    uint16_t RmsIAC;
    uint16_t RmsUC;
    uint16_t PowerPAC;
    float angle;
} power;

typedef struct 
{
    /* data */
    float U_rms;//电压有效值
    float I_rms;//电流有效值
    float P;//有功功率
    float S;//视在功率
    float Q;//功率因素
    float kW_h;//电量，度
    float freq;
}Electrical_energy;

/* Exported constants --------------------------------------------------------*/
/* Exported macro ------------------------------------------------------------*/
extern power Hlw_register;
extern Electrical_energy electricity;
/* Exported functions --------------------------------------------------------*/
#endif /* __UART_DRIVER_H */
