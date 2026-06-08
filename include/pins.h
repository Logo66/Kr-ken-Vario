#pragma once

// I2C — shared with onboard chips (verified against LilyGo pin_define)
#define BOARD_I2C_SDA       39
#define BOARD_I2C_SCL       40
#define I2C_FREQ_HZ         400000

// SPI — shared LoRa + SD
#define BOARD_SPI_MISO      21
#define BOARD_SPI_MOSI      13
#define BOARD_SPI_SCLK      14

// GNSS UART2 (verified against LilyGo pin_define: RXD=44, TXD=43)
#define BOARD_GPS_RXD       44
#define BOARD_GPS_TXD       43

// LoRa SX1262
#define BOARD_LORA_CS       46
#define BOARD_LORA_IRQ      10
#define BOARD_LORA_RST      1
#define BOARD_LORA_BUSY     47

// SD Card
#define BOARD_SD_CS         12

// Misc
#define BOARD_BOOT_BTN      0
#define BOARD_BACKLIGHT     11
#define BOARD_PCA9535_INT   38

// I2C addresses — Onboard (appear in scan, NOT our sensors)
#define ADDR_PCA9535        0x20
#define ADDR_GT911          0x5D
#define ADDR_PCF85063       0x51
#define ADDR_TPS65185       0x68
#define ADDR_BQ25896        0x6B
#define ADDR_BQ27220        0x55

// I2C addresses — Aura sensor board
#define ADDR_SHT45             0x44
#define ADDR_BMP581_SECONDARY  0x46
#define ADDR_BMP581_PRIMARY    0x47
#define ADDR_LSM6DSO32         0x6A
