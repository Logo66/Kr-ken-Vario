#pragma once

// I2C — gemeinsam mit Onboard-Chips
#define BOARD_I2C_SDA       39
#define BOARD_I2C_SCL       40
#define I2C_FREQ_HZ         400000

// SPI — gemeinsam mit LoRa + SD
#define BOARD_SPI_MISO      21
#define BOARD_SPI_MOSI      13
#define BOARD_SPI_SCLK      14

// GNSS UART
#define BOARD_GPS_RXD       44
#define BOARD_GPS_TXD       43

// LoRa
#define BOARD_LORA_CS       46
#define BOARD_LORA_IRQ      10
#define BOARD_LORA_RST      1
#define BOARD_LORA_BUSY     47

// Button
#define BOARD_BOOT_BTN      0

// I2C-Adressen — Onboard
#define ADDR_PCA9535        0x20
#define ADDR_GT911          0x5D
#define ADDR_PCF85063       0x51
#define ADDR_TPS65185       0x68
#define ADDR_BQ25896        0x6B
#define ADDR_BQ27220        0x55

// I2C-Adressen — Zusatz-Sensoren (AURA-KRUECKE)
#define ADDR_BMP581_PRIMARY    0x47
#define ADDR_BMP581_SECONDARY  0x46
#define ADDR_LSM6DSO32         0x6A
#define ADDR_BNO085            0x4A
#define ADDR_SHT40             0x44
