#include "usbcontroller.h"

#include <stdio.h>
#include <stdlib.h> 

// find and return a usb controller via the argument, or NULL if not found
struct libusb_device_handle *opencontroller(uint8_t *endpoint_address) {
    //initialize libusb
	int initReturn = libusb_init(NULL);

	if(initReturn < 0) {
		fprintf(stderr, "libusb_init failed: %s\n", libusb_error_name(initReturn));
		// No handle to return, libusb_exit should be called if we are the one failing init.
		// However, if another part of the program might have already inited, this is complex.
		// For a self-contained controller function, exiting here if init fails is reasonable.
		exit(1); 
	}

    struct libusb_device_descriptor desc;
    struct libusb_device_handle *controller = NULL;
    libusb_device **devs;
    ssize_t num_devs, d;
    uint8_t i_interface, k_altsetting; // Renamed to avoid confusion with loop variables

    if ( (num_devs = libusb_get_device_list(NULL, &devs)) < 0 ) {
        fprintf(stderr, "Error: libusb_get_device_list failed\n");
        libusb_exit(NULL); // Clean up libusb if list fails
        exit(1);
    }

    // Iterate over all devices list to find the one with the right protocol
    for (d = 0; d < num_devs; d++) {
        libusb_device *dev_ptr = devs[d]; // Use a different name for the device pointer
        if ( libusb_get_device_descriptor(dev_ptr, &desc) < 0 ) {
            fprintf(stderr, "Error: libusb_get_device_descriptor failed for a device\n");
            continue; // Skip this device
        }

        // Check if this device might be the one based on Vendor/Product ID if known,
        // or proceed to check interfaces.
        // For this example, we rely on interface descriptors as in the original code.

        if (desc.bDeviceClass == LIBUSB_CLASS_PER_INTERFACE || desc.bDeviceClass == 0) { // Also check for class 0
            struct libusb_config_descriptor *config;
            int getConfigDescResult = libusb_get_config_descriptor(dev_ptr, 0, &config);
            if (getConfigDescResult < 0) {
                // fprintf(stderr, "Warning: libusb_get_config_descriptor failed for device %zd: %s\n", d, libusb_error_name(getConfigDescResult));
                continue; // Skip this device if config descriptor fails
            }

            for (i_interface = 0 ; i_interface < config->bNumInterfaces ; i_interface++) {
                for ( k_altsetting = 0 ; k_altsetting < config->interface[i_interface].num_altsetting ; k_altsetting++ ) {
                    const struct libusb_interface_descriptor *inter = 
                    config->interface[i_interface].altsetting + k_altsetting;

                    if (inter->bInterfaceClass == LIBUSB_CLASS_HID &&
                        inter->bInterfaceProtocol == GAMEPAD_CONTROL_PROTOCOL) {
                        
                        int r;
                        if ((r = libusb_open(dev_ptr, &controller)) != 0) {
                            fprintf(stderr, "libusb_open failed for matching device: %s\n", libusb_error_name(r));
                            controller = NULL; // Ensure controller is NULL if open fails
                            continue; // Try next compatible interface/device
                        }

                        // MODIFICATION: Enable auto-detaching of kernel driver for this device handle
                        // This should be done BEFORE claiming the interface.
                        if (libusb_set_auto_detach_kernel_driver(controller, 1) != LIBUSB_SUCCESS) {
                             fprintf(stderr, "Warning: Could not enable auto-detach kernel driver for controller.\n");
                             // This is not necessarily fatal, claim might still work or fail if driver is active.
                        }
                        
                        // Attempt to detach kernel driver if active (auto-detach might not always work or be immediate)
                        // This is somewhat redundant if auto-detach is enabled but can be a fallback.
                        if (libusb_kernel_driver_active(controller, i_interface)) {
                            // printf("Kernel driver active on interface %d. Attempting to detach...\n", i_interface);
                            if (libusb_detach_kernel_driver(controller, i_interface) != 0) {
                                fprintf(stderr, "Error detaching kernel driver from interface %d. Claim might fail.\n", i_interface);
                            } else {
                                // printf("Kernel driver detached successfully from interface %d.\n", i_interface);
                            }
                        }

                        // Now claim the interface
                        if ((r = libusb_claim_interface(controller, i_interface)) != 0) {
                            fprintf(stderr, "Claim interface %d failed: %s\n", i_interface, libusb_error_name(r));
                            libusb_close(controller); // Close handle if claim fails
                            controller = NULL;        // Mark as failed for this attempt
                            continue;                 // Try next compatible interface/device
                        }
                        
                        *endpoint_address = inter->endpoint[0].bEndpointAddress;
                        printf("DEBUG: USB Controller found, opened, and interface %d claimed. Endpoint: 0x%02X\n", i_interface, *endpoint_address);
                        libusb_free_config_descriptor(config); // Free config descriptor
                        goto found; // Successfully found and claimed
                    }
                }
            }
            libusb_free_config_descriptor(config); // Free config descriptor if no match found in it
        }
    }

found:
    libusb_free_device_list(devs, 1); // Free the list of devices

    if (!controller) {
        fprintf(stderr, "Error: No suitable USB game controller found or claimed.\n");
        libusb_exit(NULL); // Deinitialize libusb if no controller was successfully set up
                           // This assumes this function is the sole manager of libusb_init for this path.
    }
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
        case 0x03: packet->select = 1; packet->start = 1; // Fixed: was packet->select = packet->start = 1;
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
        default: // Should not happen with 4 bits
            packet->start = 0;
            packet->select = 0;
            break;
    }

    /* check left and right rib with bitmasking */
    // Original code for IND_SELSTARIB & 0x0f seems correct for L/R ribs
    // Assuming LSB is right_rib, next bit is left_rib
    packet->right_rib = (output_array[IND_SELSTARIB] & 0x01) ? 1 : 0;
    packet->left_rib  = (output_array[IND_SELSTARIB] & 0x02) ? 1 : 0;


    packet->x = 0; packet->y = 0; packet->a = 0; packet->b = 0; // Initialize

    /* check if x, y, a, b is pressed */
    // Assuming byte IND_XYAB:
    // Bit 0: A
    // Bit 1: B
    // Bit 2: X
    // Bit 3: Y
    // (This might vary by controller, original code used >> 4 which implies upper nibble)
    // Reverting to original interpretation based on IND_XYAB >> 4
    unsigned char xyab_byte = output_array[IND_XYAB] >> 4; // Look at upper nibble

    if (xyab_byte & 0x01) { // Original code implies this bit is X
        packet->x = 1;
    }
    if (xyab_byte & 0x02) { // Original code implies this bit is A
        packet->a = 1;
    }
    if (xyab_byte & 0x04) { // Original code implies this bit is B
        packet->b = 1;
    }
    if (xyab_byte & 0x08) { // Original code implies this bit is Y
        packet->y = 1;
    }

    return packet;
}
