#ifndef _USBKEYBOARD_H
#define _USBKEYBOARD_H

#include "libusb.h"

#define VENDOR_ID 0x079
#define PRODUCT_ID 0x011
#define GAMEPAD_ENDPOINT_ADDRESS 0x81
#define GAMEPAD_CONTROL_PROTOCOL 0
#define GAMEPAD_READ_LENGTH 8

#define IND_UPDOWN 4
#define IND_LEFTRIGHT 3
#define IND_SELSTARIB 7
#define IND_XYAB 5

#define UP 1
#define DOWN -1
#define LEFT 1
#define RIGHT -1



struct controller_output_packet {
    short updown; // 0 for no change, 1 for up, -1 for down
    short leftright; // 0 for no change, 1 for left, -1 for right
    uint8_t select; // for the rest, 1 is true/active, 0 is false/not active
    uint8_t start;
    uint8_t left_rib;
    uint8_t right_rib;
    uint8_t x;
    uint8_t y;
    uint8_t a;
    uint8_t b;
};


/* Find and open a USB controller device, argument should 
point to space to store an endpoint address. Returns NULL
if no controller was found */
extern struct libusb_device_handle *opencontroller(uint8_t *);


/* convert the usb controller output into a packet for access */
extern struct controller_output_packet *usb_to_output(struct controller_output_packet *, unsigned char*);

#endif