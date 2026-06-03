#pragma once
#include <stdint.h>
#include <stdbool.h>

#define NRF24_CONFIG      0x00
#define NRF24_EN_AA       0x01
#define NRF24_EN_RXADDR   0x02
#define NRF24_SETUP_AW    0x03
#define NRF24_SETUP_RETR  0x04
#define NRF24_RF_CH       0x05
#define NRF24_RF_SETUP    0x06
#define NRF24_STATUS      0x07
#define NRF24_OBSERVE_TX  0x08
#define NRF24_RPD         0x09
#define NRF24_RX_ADDR_P0  0x0A
#define NRF24_RX_ADDR_P1  0x0B
#define NRF24_RX_ADDR_P2  0x0C
#define NRF24_RX_ADDR_P3  0x0D
#define NRF24_RX_ADDR_P4  0x0E
#define NRF24_RX_ADDR_P5  0x0F
#define NRF24_TX_ADDR     0x10
#define NRF24_RX_PW_P0    0x11
#define NRF24_RX_PW_P1    0x12
#define NRF24_RX_PW_P2    0x13
#define NRF24_RX_PW_P3    0x14
#define NRF24_RX_PW_P4    0x15
#define NRF24_RX_PW_P5    0x16
#define NRF24_FIFO_STATUS 0x17
#define NRF24_DYNPD       0x1C
#define NRF24_FEATURE     0x1D

#define NRF24_CONFIG_PRIM_RX  1
#define NRF24_CONFIG_PWR_UP  2
#define NRF24_CONFIG_CRCO    4
#define NRF24_CONFIG_EN_CRC  8
#define NRF24_CONFIG_MASK_RX_DR  64
#define NRF24_CONFIG_MASK_TX_DS  32
#define NRF24_CONFIG_MASK_MAX_RT 16

#define NRF24_RF_SETUP_RF_PWR(v)  ((v & 3) << 1)
#define NRF24_RF_SETUP_RF_DR_HIGH (1 << 3)
#define NRF24_RF_SETUP_RF_DR_LOW  (1 << 5)

#define NRF24_STATUS_RX_DR    (1 << 6)
#define NRF24_STATUS_TX_DS    (1 << 5)
#define NRF24_STATUS_MAX_RT   (1 << 4)

#define NRF24_FIFO_TX_FULL  (1 << 5)
#define NRF24_FIFO_TX_EMPTY (1 << 4)
#define NRF24_FIFO_RX_FULL  (1 << 1)
#define NRF24_FIFO_RX_EMPTY (1 << 0)

#define NRF24_MAX_PL 32

typedef enum {
    NRF24_PWR_MIN  = 0,
    NRF24_PWR_LOW  = 1,
    NRF24_PWR_HIGH = 2,
    NRF24_PWR_MAX  = 3,
} nrf24_power_t;

typedef enum {
    NRF24_DR_1MBPS   = 0,
    NRF24_DR_2MBPS   = 1,
    NRF24_DR_250KBPS = 2,
} nrf24_datarate_t;

typedef struct {
    uint8_t     pipe0_addr[5];
    bool        initialized;
} nrf24_t;

void     nrf24_init(nrf24_t* dev, uint8_t csn, uint8_t ce);
void     nrf24_deinit(nrf24_t* dev);
bool     nrf24_detect(nrf24_t* dev);
uint8_t  nrf24_read_reg(nrf24_t* dev, uint8_t reg);
void     nrf24_write_reg(nrf24_t* dev, uint8_t reg, uint8_t val);
void     nrf24_read_buf(nrf24_t* dev, uint8_t reg, uint8_t* buf, uint8_t len);
void     nrf24_write_buf(nrf24_t* dev, uint8_t reg, const uint8_t* buf, uint8_t len);
void     nrf24_set_channel(nrf24_t* dev, uint8_t ch);
void     nrf24_set_power(nrf24_t* dev, nrf24_power_t pwr);
void     nrf24_set_datarate(nrf24_t* dev, nrf24_datarate_t dr);
void     nrf24_set_tx_addr(nrf24_t* dev, const uint8_t* addr, uint8_t len);
void     nrf24_set_rx_addr(nrf24_t* dev, uint8_t pipe, const uint8_t* addr, uint8_t len);
void     nrf24_tx_mode(nrf24_t* dev);
void     nrf24_rx_mode(nrf24_t* dev);
bool     nrf24_send(nrf24_t* dev, const uint8_t* data, uint8_t len);
bool     nrf24_available(nrf24_t* dev);
bool     nrf24_read(nrf24_t* dev, uint8_t* data, uint8_t* len);
void     nrf24_flush_tx(nrf24_t* dev);
void     nrf24_flush_rx(nrf24_t* dev);
uint8_t  nrf24_status(nrf24_t* dev);
void     nrf24_clear_irqs(nrf24_t* dev);
void     nrf24_print_regs(nrf24_t* dev);
int      nrf24_scan_channels(nrf24_t* dev, uint8_t* results, int max_results);
