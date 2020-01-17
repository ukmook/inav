/*
 * This file is part of Cleanflight.
 *
 * Cleanflight is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Cleanflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Cleanflight.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once
#define TARGET_BOARD_IDENTIFIER "QRKV"

#define USBD_PRODUCT_STRING "qUarkVF405_16"
#ifdef OPBL
#define USBD_SERIALNUMBER_STRING "0x8020000"
#endif

#define PLL_M                   16
#define PLL_N                   336

#define LED0                    PC13
#define LED1                    PC14

#define BEEPER                  PC15
#define BEEPER_INVERTED

//------------SPI----------------------
#define USE_SPI

//MAX7456 OSD
#define USE_SPI_DEVICE_1 //OSD MAX 7456
#define SPI1_SCK_PIN            PA5
#define SPI1_MISO_PIN           PA6
#define SPI1_MOSI_PIN           PA7

#define USE_OSD
#define USE_MAX7456
#define MAX7456_SPI_BUS         BUS_SPI1
#define MAX7456_CS_PIN          PB12

//MPU600 & MS5611
#define USE_EXTI
#define USE_MPU_DATA_READY_SIGNAL
#define ENSURE_MPU_DATA_READY_IS_LOW

#define USE_SPI_DEVICE_2 //MPU600 & MS5611
#define SPI2_SCK_PIN            PB13
#define SPI2_MISO_PIN           PC2
#define SPI2_MOSI_PIN           PC3

#define USE_GYRO
#define USE_GYRO_MPU6000

#define USE_ACC
#define USE_ACC_MPU6000

#define MPU6000_SPI_BUS         BUS_SPI2
#define MPU6000_CS_PIN          PC1
#define GYRO_INT_EXTI           PC0

#define USE_BARO
#define USE_BARO_MS5611
#define MS5611_CS_PIN           PA14
#define MS5611_SPI_BUS          BUS_SPI2

//dataflash
#define USE_SPI_DEVICE_3
#define SPI3_SCK_PIN            PC10
#define SPI3_MISO_PIN           PC11
#define SPI3_MOSI_PIN           PC12

#define USE_FLASHFS
#define USE_FLASH_M25P16
#define M25P16_CS_PIN           PA13
#define M25P16_SPI_BUS          BUS_SPI3

//------------I2C3----------------------
#define USE_I2C
#define USE_I2C_DEVICE_3
#define I2C3_SCL                PA8
#define I2C3_SDA                PC9

#define MAG_I2C_BUS             BUS_I2C3
#define USE_MAG_HMC5883
#define USE_MAG_QMC5883
#define USE_MAG_IST8310
#define USE_MAG_IST8308
#define USE_MAG_MAG3110
#define USE_MAG_LIS3MDL

#define TEMPERATURE_I2C_BUS     BUS_I2C3
#define PCA9685_I2C_BUS         BUS_I2C3

#define USE_RANGEFINDER
#define RANGEFINDER_I2C_BUS     BUS_I2C3

//------------USARTS----------------------
#define USE_VCP
#define VBUS_SENSING_PIN        PA8

#define USE_UART_INVERTER

#define USE_UART1
#define UART1_RX_PIN            PB7
#define UART1_TX_PIN            PB6

#define USE_UART2
#define UART2_RX_PIN            PA3
#define UART2_TX_PIN            PA2
#define INVERTER_PIN_UART2_RX   PB2

#define USE_UART4
#define UART4_RX_PIN            PA1
#define UART4_TX_PIN            PA0

#define USE_UART6
#define UART6_RX_PIN            PC7
#define UART6_TX_PIN            PC6

#define SERIAL_PORT_COUNT 5

//------------ADC----------------------
#define USE_ADC
#define ADC_CHANNEL_1_PIN               PA4
#define ADC_CHANNEL_2_PIN               PC5
#define CURRENT_METER_ADC_CHANNEL       ADC_CHN_1
#define VBAT_ADC_CHANNEL                ADC_CHN_2

//LED
#define USE_LED_STRIP
#define WS2811_PIN                      PA15

//FEATURES
#define DEFAULT_FEATURES        (FEATURE_TX_PROF_SEL | FEATURE_VBAT | FEATURE_CURRENT_METER | FEATURE_TELEMETRY | FEATURE_OSD )
#define ENABLE_BLACKBOX_LOGGING_ON_SPIFLASH_BY_DEFAULT
#define DEFAULT_RX_TYPE         RX_TYPE_SERIAL
#define SERIALRX_PROVIDER       SERIALRX_SBUS
#define SERIALRX_UART           SERIAL_PORT_USART1

#define USE_SERIAL_4WAY_BLHELI_INTERFACE

#define MAX_PWM_OUTPUT_PORTS    8

#define TARGET_IO_PORTA         0xffff
#define TARGET_IO_PORTB         0xffff
#define TARGET_IO_PORTC         0xffff
#define TARGET_IO_PORTD         0xffff
