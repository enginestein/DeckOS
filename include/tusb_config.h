#ifndef _DECKOS_TUSB_CONFIG_H
#define _DECKOS_TUSB_CONFIG_H

#include "pico/stdio_usb.h"

#ifdef __cplusplus
extern "C" {
  #endif

  #ifndef CFG_TUSB_RHPORT0_MODE
  #define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE)
  #endif

  #define CFG_TUD_ENABLED 1

  #ifndef CFG_TUD_ENDPOINT0_SIZE
  #define CFG_TUD_ENDPOINT0_SIZE 64
  #endif

  #define CFG_TUD_CDC 1
  #define CFG_TUD_MSC 1
  #define CFG_TUD_HID 1
  #define CFG_TUD_MIDI 0
  #define CFG_TUD_VENDOR 0

  #ifndef CFG_TUD_CDC_RX_BUFSIZE
  #define CFG_TUD_CDC_RX_BUFSIZE 64
  #endif
  #ifndef CFG_TUD_CDC_TX_BUFSIZE
  #define CFG_TUD_CDC_TX_BUFSIZE 64
  #endif
  #ifndef CFG_TUD_CDC_EP_BUFSIZE
  #define CFG_TUD_CDC_EP_BUFSIZE 64
  #endif

  #ifndef CFG_TUD_MSC_EP_BUFSIZE
  #define CFG_TUD_MSC_EP_BUFSIZE 512
  #endif

  #ifndef CFG_TUD_HID_EP_BUFSIZE
  #define CFG_TUD_HID_EP_BUFSIZE 16
  #endif

  #ifdef __cplusplus
}
#endif

#endif