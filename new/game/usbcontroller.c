#include "usbcontroller.h"

#include <stdio.h>
#include <stdlib.h> 


// find and return a usb controller via the argument, or NULL if not found
struct libusb_device_handle *opencontroller(uint8_t *endpoint_address) {
    //initialize libusb
	int initReturn = libusb_init(NULL);

	if(initReturn < 0) {
		printf("libusb initialization error!\n");
		exit(1);
	}

    // for searching for descriptor info
    struct libusb_device_descriptor desc;
    struct libusb_device_handle *controller = NULL;
    libusb_device **devs;
    ssize_t num_devs, d;
    uint8_t i, k;

    if ( (num_devs = libusb_get_device_list(NULL, &devs)) < 0 ) {
        fprintf(stderr, "Error: libusb_get_device_list failed\n");
        exit(1);
    }

    // iterate over all devices list to find the one with the right protocol
    for (d = 0; d < num_devs; d++) {
        libusb_device *dev = devs[d];
        if ( libusb_get_device_descriptor(dev, &desc) < 0 ) {
            fprintf(stderr, "Error: libusb_get_device_descriptor failed\n");
            libusb_free_device_list(devs, 1);
            exit(1);
        }
        if (desc.bDeviceClass == LIBUSB_CLASS_PER_INTERFACE) {
            struct libusb_config_descriptor *config;
            libusb_get_config_descriptor(dev, 0, &config);
            for (i = 0 ; i < config->bNumInterfaces ; i++) {
                for ( k = 0 ; k < config->interface[i].num_altsetting ; k++ ) {
                    const struct libusb_interface_descriptor *inter = 
                    config->interface[i].altsetting + k;
                    if (inter->bInterfaceClass == LIBUSB_CLASS_HID &&
                        inter->bInterfaceProtocol == GAMEPAD_CONTROL_PROTOCOL) {
                        int r;
                        if ((r = libusb_open(dev, &controller)) != 0) {
                            fprintf(stderr, "libusb_open failed: %s\n", libusb_error_name(r));
                            exit(1);
                        }
                        if (libusb_kernel_driver_active(controller,i)) {
                            libusb_detach_kernel_driver(controller, i);
                        }
                        libusb_set_auto_detach_kernel_driver(controller, i);
						libusb_detach_kernel_driver(controller, i);
                        if ((r = libusb_claim_interface(controller, i)) != 0) {
                            fprintf(stderr, "claim interface failed: %s\n", libusb_error_name(r));
                            exit(1);
                        }
                        // endpoint address
                        *endpoint_address = inter->endpoint[0].bEndpointAddress;
                        goto found;
                    }
                    // printf("d:%zd  i:%d  k:%d  interface class:%x  interface protocol: %x  endpoint address: %x\n", d, i, k, inter->bInterfaceClass, inter->bInterfaceProtocol, inter->endpoint[0].bEndpointAddress);
                }
            }
        }
    }

found:
    libusb_free_device_list(devs, 1);

    return controller;

}


struct controller_output_packet *usb_to_output(struct controller_output_packet *packet, 
                                                unsigned char* output_array) {
    /* check up and down arrow */
    switch(output_array[IND_UPDOWN]) {
        case 0x0: packet->updown = 1;
            break;
        case 0xff: packet->updown = -1;
            break;
        default: packet->updown = 0;
            break;
    }
    /* check left and right arrow */
    switch(output_array[IND_LEFTRIGHT]) {
        case 0x0: packet->leftright = 1;
            break;
        case 0xff: packet->leftright = -1;
            break;
        default: packet->leftright = 0;
            break;
    }

    /* check select and start with bitshifting */
    switch(output_array[IND_SELSTARIB] >> 4) {
        case 0x03: packet->select = packet->start = 1;
            break;
        case 0x02: packet->start = 1;
            packet->select = 0;
            break;
        case 0x01: packet->start = 0;
            packet->select = 1;
            break;
        case 0x00: packet->start = 0;
            packet->select = 0;
            break;
    }

    /* check left and right rib with bitmasking */
    switch(output_array[IND_SELSTARIB] & 0x0f) {
        case 0x03: packet->left_rib = packet->right_rib = 1;
            break;
        case 0x02: packet->right_rib = 1;
            packet->left_rib = 0;
            break;
        case 0x01: packet->right_rib = 0;
            packet->left_rib = 1;
            break;
        case 0x00: packet->right_rib = 0;
            packet->left_rib= 0;
            break;
    }

    packet->x = packet->y = packet->a = packet->b = 0;

    /* check if x, y, a, b is pressed */
    if ((output_array[IND_XYAB] >> 4) & 0x01) { // x
        packet->x = 1;
    }
    if ((output_array[IND_XYAB] >> 4) & 0x02) { // a
        packet->a = 1;
    }
    if ((output_array[IND_XYAB] >> 4) & 0x04) { // b
        packet->b = 1;
    }
    if ((output_array[IND_XYAB] >> 4) & 0x08) { // y
        packet->y = 1;
    }

    return packet;
}