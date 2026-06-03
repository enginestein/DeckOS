#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "spi_bus.h"
#include "nrf24l01.h"

#define NRF24_CMD_R_REGISTER    0x00
#define NRF24_CMD_W_REGISTER    0x20
#define NRF24_CMD_R_RX_PAYLOAD  0x61
#define NRF24_CMD_W_TX_PAYLOAD  0xA0
#define NRF24_CMD_FLUSH_TX      0xE1
#define NRF24_CMD_FLUSH_RX      0xE2
#define NRF24_CMD_REUSE_TX_PL   0xE3
#define NRF24_CMD_ACTIVATE      0x50
#define NRF24_CMD_R_RX_PL_WID   0x60
#define NRF24_CMD_W_ACK_PAYLOAD 0xA8
#define NRF24_CMD_NOP           0xFF

static spi_inst_t* nrf_spi = spi0;
static uint8_t csn_pin, ce_pin;

static void cs_select(void) { gpio_put(csn_pin, 0); }
static void cs_deselect(void) { gpio_put(csn_pin, 1); }
static void ce_high(void) { gpio_put(ce_pin, 1); }
static void ce_low(void) { gpio_put(ce_pin, 0); }

static uint8_t spi_xfer_byte(uint8_t byte) {
    uint8_t rx;
    spi_bus_transfer(nrf_spi, 0xFF, &byte, &rx, 1);
    return rx;
}

void nrf24_init(nrf24_t* dev, uint8_t csn, uint8_t ce) {
    memset(dev, 0, sizeof(*dev));
    csn_pin = csn;
    ce_pin  = ce;

    gpio_init(csn);
    gpio_set_dir(csn, GPIO_OUT);
    gpio_put(csn, 1);
    gpio_init(ce);
    gpio_set_dir(ce, GPIO_OUT);
    gpio_put(ce, 0);
    sleep_us(100);

    uint8_t def_addr[5] = {0xE7, 0xE7, 0xE7, 0xE7, 0xE7};
    memcpy(dev->pipe0_addr, def_addr, 5);

    nrf24_flush_tx(dev);
    nrf24_flush_rx(dev);
    nrf24_clear_irqs(dev);

    nrf24_write_reg(dev, NRF24_CONFIG, NRF24_CONFIG_EN_CRC | NRF24_CONFIG_CRCO | NRF24_CONFIG_PWR_UP);
    sleep_us(1500);

    nrf24_write_reg(dev, NRF24_EN_AA, 0x3F);
    nrf24_write_reg(dev, NRF24_EN_RXADDR, 0x3F);
    nrf24_write_reg(dev, NRF24_SETUP_AW, 0x03);
    nrf24_write_reg(dev, NRF24_SETUP_RETR, 0x1F);
    nrf24_write_reg(dev, NRF24_RF_CH, 76);
    nrf24_write_reg(dev, NRF24_RF_SETUP, NRF24_RF_SETUP_RF_PWR(NRF24_PWR_MAX));
    for (int i = 0; i < 6; i++)
        nrf24_write_reg(dev, NRF24_RX_PW_P0 + i, NRF24_MAX_PL);

    dev->initialized = true;
}

void nrf24_deinit(nrf24_t* dev) {
    if (!dev->initialized) return;
    ce_low();
    nrf24_write_reg(dev, NRF24_CONFIG, 0);
    dev->initialized = false;
}

bool nrf24_detect(nrf24_t* dev) {
    uint8_t orig = nrf24_read_reg(dev, NRF24_CONFIG);
    if (orig == 0xFF || orig == 0x00) {
        nrf24_write_reg(dev, NRF24_CONFIG, 0x0F);
        uint8_t check = nrf24_read_reg(dev, NRF24_CONFIG);
        nrf24_write_reg(dev, NRF24_CONFIG, orig);
        return (check == 0x0F);
    }
    return true;
}

uint8_t nrf24_read_reg(nrf24_t* dev, uint8_t reg) {
    cs_select();
    spi_xfer_byte(NRF24_CMD_R_REGISTER | (reg & 0x1F));
    uint8_t val = spi_xfer_byte(NRF24_CMD_NOP);
    cs_deselect();
    return val;
}

void nrf24_write_reg(nrf24_t* dev, uint8_t reg, uint8_t val) {
    cs_select();
    spi_xfer_byte(NRF24_CMD_W_REGISTER | (reg & 0x1F));
    spi_xfer_byte(val);
    cs_deselect();
}

void nrf24_read_buf(nrf24_t* dev, uint8_t reg, uint8_t* buf, uint8_t len) {
    cs_select();
    spi_xfer_byte(NRF24_CMD_R_REGISTER | (reg & 0x1F));
    for (uint8_t i = 0; i < len; i++)
        buf[i] = spi_xfer_byte(NRF24_CMD_NOP);
    cs_deselect();
}

void nrf24_write_buf(nrf24_t* dev, uint8_t reg, const uint8_t* buf, uint8_t len) {
    cs_select();
    spi_xfer_byte(NRF24_CMD_W_REGISTER | (reg & 0x1F));
    for (uint8_t i = 0; i < len; i++)
        spi_xfer_byte(buf[i]);
    cs_deselect();
}

uint8_t nrf24_status(nrf24_t* dev) {
    cs_select();
    uint8_t s = spi_xfer_byte(NRF24_CMD_NOP);
    cs_deselect();
    return s;
}

void nrf24_clear_irqs(nrf24_t* dev) {
    nrf24_write_reg(dev, NRF24_STATUS, 0x70);
}

void nrf24_flush_tx(nrf24_t* dev) {
    cs_select();
    spi_xfer_byte(NRF24_CMD_FLUSH_TX);
    cs_deselect();
}

void nrf24_flush_rx(nrf24_t* dev) {
    cs_select();
    spi_xfer_byte(NRF24_CMD_FLUSH_RX);
    cs_deselect();
}

void nrf24_set_channel(nrf24_t* dev, uint8_t ch) {
    nrf24_write_reg(dev, NRF24_RF_CH, ch < 125 ? ch : 0);
}

void nrf24_set_power(nrf24_t* dev, nrf24_power_t pwr) {
    uint8_t rf = nrf24_read_reg(dev, NRF24_RF_SETUP);
    rf &= ~0x06;
    rf |= NRF24_RF_SETUP_RF_PWR(pwr);
    nrf24_write_reg(dev, NRF24_RF_SETUP, rf);
}

void nrf24_set_datarate(nrf24_t* dev, nrf24_datarate_t dr) {
    uint8_t rf = nrf24_read_reg(dev, NRF24_RF_SETUP);
    rf &= ~0x28;
    if (dr == NRF24_DR_2MBPS)
        rf |= NRF24_RF_SETUP_RF_DR_HIGH;
    else if (dr == NRF24_DR_250KBPS)
        rf |= NRF24_RF_SETUP_RF_DR_LOW;
    nrf24_write_reg(dev, NRF24_RF_SETUP, rf);
}

void nrf24_set_tx_addr(nrf24_t* dev, const uint8_t* addr, uint8_t len) {
    if (len < 3) len = 3;
    if (len > 5) len = 5;
    nrf24_write_buf(dev, NRF24_TX_ADDR, addr, len);
    nrf24_write_buf(dev, NRF24_RX_ADDR_P0, addr, len);
}

void nrf24_set_rx_addr(nrf24_t* dev, uint8_t pipe, const uint8_t* addr, uint8_t len) {
    if (pipe > 5 || len < 1) return;
    if (len > 5) len = 5;
    if (pipe <= 1) {
        nrf24_write_buf(dev, NRF24_RX_ADDR_P0 + pipe, addr, len);
        if (pipe == 0)
            memcpy(dev->pipe0_addr, addr, len);
    } else {
        nrf24_write_reg(dev, NRF24_RX_ADDR_P0 + pipe, addr[0]);
    }
}

void nrf24_tx_mode(nrf24_t* dev) {
    ce_low();
    uint8_t cfg = nrf24_read_reg(dev, NRF24_CONFIG);
    cfg &= ~NRF24_CONFIG_PRIM_RX;
    cfg |= NRF24_CONFIG_PWR_UP;
    nrf24_write_reg(dev, NRF24_CONFIG, cfg);
    sleep_us(130);
}

void nrf24_rx_mode(nrf24_t* dev) {
    ce_low();
    uint8_t cfg = nrf24_read_reg(dev, NRF24_CONFIG);
    cfg |= NRF24_CONFIG_PRIM_RX | NRF24_CONFIG_PWR_UP;
    nrf24_write_reg(dev, NRF24_CONFIG, cfg);
    ce_high();
    sleep_us(130);
}

bool nrf24_send(nrf24_t* dev, const uint8_t* data, uint8_t len) {
    if (!data || len == 0 || len > NRF24_MAX_PL) return false;

    nrf24_tx_mode(dev);
    nrf24_flush_tx(dev);
    nrf24_clear_irqs(dev);

    cs_select();
    spi_xfer_byte(NRF24_CMD_W_TX_PAYLOAD);
    for (uint8_t i = 0; i < len; i++)
        spi_xfer_byte(data[i]);
    cs_deselect();

    ce_high();
    sleep_us(15);
    ce_low();

    uint64_t start = time_us_64();
    while (time_us_64() - start < 100000) {
        uint8_t s = nrf24_status(dev);
        if (s & NRF24_STATUS_TX_DS) {
            nrf24_clear_irqs(dev);
            return true;
        }
        if (s & NRF24_STATUS_MAX_RT) {
            nrf24_clear_irqs(dev);
            nrf24_flush_tx(dev);
            return false;
        }
        sleep_us(10);
    }
    return false;
}

bool nrf24_available(nrf24_t* dev) {
    uint8_t fifo = nrf24_read_reg(dev, NRF24_FIFO_STATUS);
    return !(fifo & NRF24_FIFO_RX_EMPTY);
}

bool nrf24_read(nrf24_t* dev, uint8_t* data, uint8_t* len) {
    if (!data || !len) return false;

    uint8_t fifo = nrf24_read_reg(dev, NRF24_FIFO_STATUS);
    if (fifo & NRF24_FIFO_RX_EMPTY) return false;

    cs_select();
    spi_xfer_byte(NRF24_CMD_R_RX_PL_WID);
    uint8_t width = spi_xfer_byte(NRF24_CMD_NOP);
    cs_deselect();

    if (width > NRF24_MAX_PL) {
        nrf24_flush_rx(dev);
        *len = 0;
        return false;
    }

    cs_select();
    spi_xfer_byte(NRF24_CMD_R_RX_PAYLOAD);
    for (uint8_t i = 0; i < width; i++)
        data[i] = spi_xfer_byte(NRF24_CMD_NOP);
    cs_deselect();

    nrf24_write_reg(dev, NRF24_STATUS, NRF24_STATUS_RX_DR);
    *len = width;
    return true;
}

void nrf24_print_regs(nrf24_t* dev) {
    if (!dev->initialized) { printf("  module not initialized\n"); return; }

    uint8_t cfg    = nrf24_read_reg(dev, NRF24_CONFIG);
    uint8_t en_aa  = nrf24_read_reg(dev, NRF24_EN_AA);
    uint8_t en_rx  = nrf24_read_reg(dev, NRF24_EN_RXADDR);
    uint8_t aw     = nrf24_read_reg(dev, NRF24_SETUP_AW);
    uint8_t retr   = nrf24_read_reg(dev, NRF24_SETUP_RETR);
    uint8_t ch     = nrf24_read_reg(dev, NRF24_RF_CH);
    uint8_t rf     = nrf24_read_reg(dev, NRF24_RF_SETUP);
    uint8_t st     = nrf24_read_reg(dev, NRF24_STATUS);
    uint8_t obs    = nrf24_read_reg(dev, NRF24_OBSERVE_TX);
    uint8_t rpd    = nrf24_read_reg(dev, NRF24_RPD);
    uint8_t fifo   = nrf24_read_reg(dev, NRF24_FIFO_STATUS);
    uint8_t feat   = nrf24_read_reg(dev, NRF24_FEATURE);

    uint8_t tx_addr[5], rx_addr[5];
    nrf24_read_buf(dev, NRF24_TX_ADDR, tx_addr, 5);
    nrf24_read_buf(dev, NRF24_RX_ADDR_P0, rx_addr, 5);

    printf("nRF24L01+ registers:\n");
    printf("  CONFIG      0x%02X  (%s %s CRC=%s IRQ:%s%s%s)\n", cfg,
           cfg & 2 ? "PWR_UP" : "PWR_DN",
           cfg & 1 ? "PRX" : "PTX",
           cfg & 8 ? (cfg & 4 ? "2byte" : "1byte") : "off",
           cfg & 64 ? "!RX" : "-", cfg & 32 ? "!TX" : "-", cfg & 16 ? "!RT" : "-");
    printf("  EN_AA       0x%02X  (auto-ack: ", en_aa);
    for (int i = 0; i < 6; i++) printf("%c", (en_aa & (1 << i)) ? 'Y' : '-');
    printf(")\n  EN_RXADDR   0x%02X  (rx pipes open: ", en_rx);
    for (int i = 0; i < 6; i++) printf("%c", (en_rx & (1 << i)) ? 'Y' : '-');
    printf(")\n");
    printf("  SETUP_AW    0x%02X  (addr width: %d bytes)\n", aw, aw + 2);
    printf("  SETUP_RETR  0x%02X  (retry delay: %d us, count: %d)\n",
           retr, ((retr >> 4) + 1) * 250, retr & 0x0F);
    printf("  RF_CH       0x%02X  (channel: %d = %.1f MHz)\n", ch, ch, 2400.0 + ch);
    printf("  RF_SETUP    0x%02X  (power: ", rf);
    uint8_t pwr = (rf >> 1) & 3;
    printf("%s", pwr == 3 ? "0 dBm" : pwr == 2 ? "-6 dBm" : pwr == 1 ? "-12 dBm" : "-18 dBm");
    printf(", datarate: ");
    if (rf & 0x20) printf("250 kbps");
    else if (rf & 8) printf("2 Mbps");
    else printf("1 Mbps");
    printf(")\n");
    printf("  STATUS      0x%02X  (%s%s%s TX_FIFO=%s)\n", st,
           st & 0x40 ? "RX_DR " : "", st & 0x20 ? "TX_DS " : "", st & 0x10 ? "MAX_RT " : "",
           st & 1 ? "FULL" : "ok");
    printf("  OBSERVE_TX  0x%02X  (lost: %d, retrans: %d)\n", obs, (obs >> 4) & 0x0F, obs & 0x0F);
    printf("  RPD/CD      0x%02X  (carrier: %s)\n", rpd, rpd ? "detected" : "none");
    printf("  FIFO_STATUS 0x%02X  (TX:%s%s RX:%s%s)\n", fifo,
           fifo & 0x20 ? "FULL" : "ok", fifo & 0x10 ? " EMPTY" : "",
           fifo & 2 ? "FULL" : "ok", fifo & 1 ? " EMPTY" : "");
    printf("  FEATURE     0x%02X\n", feat);
    printf("  TX_ADDR     %02X:%02X:%02X:%02X:%02X\n",
           tx_addr[0], tx_addr[1], tx_addr[2], tx_addr[3], tx_addr[4]);
    printf("  RX_ADDR_P0  %02X:%02X:%02X:%02X:%02X\n",
           rx_addr[0], rx_addr[1], rx_addr[2], rx_addr[3], rx_addr[4]);
}

int nrf24_scan_channels(nrf24_t* dev, uint8_t* results, int max_results) {
    if (!dev->initialized || !results) return 0;

    uint8_t save_cfg = nrf24_read_reg(dev, NRF24_CONFIG);
    uint8_t save_ch  = nrf24_read_reg(dev, NRF24_RF_CH);

    nrf24_rx_mode(dev);
    int found = 0;

    for (int ch = 0; ch < 126 && found < max_results; ch++) {
        nrf24_write_reg(dev, NRF24_RF_CH, (uint8_t)ch);
        sleep_us(130);
        uint8_t rpd = nrf24_read_reg(dev, NRF24_RPD);
        if (rpd) results[found++] = (uint8_t)ch;
    }

    nrf24_write_reg(dev, NRF24_RF_CH, save_ch);
    nrf24_write_reg(dev, NRF24_CONFIG, save_cfg);
    if (save_cfg & NRF24_CONFIG_PRIM_RX)
        ce_high();

    return found;
}
