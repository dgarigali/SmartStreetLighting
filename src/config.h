#ifndef __CONFIG_H_
#define __CONFIG_H_
 
/** Library configuration options */
#define ENABLE_LOGGING
#define ENABLE_ASSERTIONS
#define FRAME_BUFFER_SIZE           4
#define MAX_FRAME_PAYLOAD_LEN       128
 
#define SYNC_OPS_TIMEOUT_MS         2000

#define RADIO_TX p9
#define RADIO_RX p10 
#define DEBUG_TX USBTX 
#define DEBUG_RX USBRX
#define RADIO_SLEEP_REQ p14
#define RADIO_ON_SLEEP p20
 
//#define RADIO_TX                NC /* TODO: specify your setup's Serial TX pin connected to the XBee module DIN pin */
//#define RADIO_RX                NC /* TODO: specify your setup's Serial RX pin connected to the XBee module DOUT pin */
//#define RADIO_RTS               NC /* TODO: specify your setup's Serial RTS# pin connected to the XBee module RTS# pin */
//#define RADIO_CTS               NC /* TODO: specify your setup's Serial CTS# pin connected to the XBee module CTS# pin */
//#define RADIO_RESET             NC /* TODO: specify your setup's GPIO (output) connected to the XBee module's reset pin */
//#define RADIO_SLEEP_REQ         NC /* TODO: specify your setup's GPIO (output) connected to the XBee module's SLEEP_RQ pin */
//#define RADIO_ON_SLEEP          NC /* TODO: specify your setup's GPIO (input) connected to the XBee module's ON_SLEEP pin */
//#define DEBUG_TX                NC /* TODO: specify your setup's Serial TX for debugging */
//#define DEBUG_RX                NC /* TODO: specify your setup's Serial RX for debugging (optional) */
 
#if !defined(RADIO_TX)
    #error "Please define RADIO_TX pin"
#endif
 
#if !defined(RADIO_RX)
    #error "Please define RADIO_RX pin"
#endif
 
#if !defined(RADIO_RESET)
    #define RADIO_RESET             NC
    #warning "RADIO_RESET not defined, defaulted to 'NC'"
#endif
 
#if defined(ENABLE_LOGGING)
    #if !defined(DEBUG_TX)
        #error "Please define DEBUG_TX"
    #endif
    #if !defined(DEBUG_RX)
        #define DEBUG_RX                NC
        #warning "DEBUG_RX not defined, defaulted to 'NC'"
    #endif
#endif
 
#endif /* __CONFIG_H_ */