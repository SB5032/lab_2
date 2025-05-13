#include "usbcontroller.h" // Contains VENDOR_ID, PRODUCT_ID, GAMEPAD_CONTROL_PROTOCOL etc.
#include <stdio.h>
#include <stdlib.h> 

// find and return a usb controller via the argument, or NULL if not found
struct libusb_device_handle *opencontroller(uint8_t *endpoint_address) {
    //initialize libusb
	int initReturn = libusb_init(NULL); // Initialize libusb context

	if(initReturn < 0) {
		fprintf(stderr, "libusb_init failed: %s\n", libusb_error_name(initReturn));
		// Cannot proceed if libusb cannot be initialized.
		exit(1); 
	}
    printf("DEBUG: libusb initialized successfully.\n");

    struct libusb_device_descriptor desc;
    struct libusb_device_handle *controller = NULL; // Handle for the opened device
    libusb_device **devs; // Pointer to a list of devices
    ssize_t num_devs, d_idx; // Number of devices and loop index for devices
    uint8_t i_idx, k_idx;    // Loop indices for interfaces and altsettings

    // Get a list of all USB devices
    if ( (num_devs = libusb_get_device_list(NULL, &devs)) < 0 ) {
        fprintf(stderr, "Error: libusb_get_device_list failed: %s\n", libusb_error_name((int)num_devs));
        libusb_exit(NULL); // Clean up libusb if device list fails
        exit(1);
    }
    printf("DEBUG: Found %zd USB devices.\n", num_devs);

    // Iterate over all devices to find the gamepad
    for (d_idx = 0; d_idx < num_devs; d_idx++) {
        libusb_device *current_dev = devs[d_idx]; // Get a pointer to the current device
        if ( libusb_get_device_descriptor(current_dev, &desc) < 0 ) {
            fprintf(stderr, "Warning: libusb_get_device_descriptor failed for a device, skipping.\n");
            continue; // Skip this device
        }
        
        // Optional: Print VID/PID for debugging all devices
        // printf("DEBUG: Checking Device %zd: VID=0x%04x, PID=0x%04x, Class=0x%02x\n", 
        //        d_idx, desc.idVendor, desc.idProduct, desc.bDeviceClass);

        // Gamepads often have bDeviceClass = 0 (defined at interface level) or LIBUSB_CLASS_PER_INTERFACE
        if (desc.bDeviceClass == 0 || desc.bDeviceClass == LIBUSB_CLASS_PER_INTERFACE) {
            struct libusb_config_descriptor *config = NULL; // Initialize to NULL
            int r = libusb_get_config_descriptor(current_dev, 0, &config);
            if (r < 0) {
                // fprintf(stderr, "Warning: Could not get config descriptor for device %zd: %s\n", d_idx, libusb_error_name(r));
                if (config) libusb_free_config_descriptor(config); // Should be NULL if failed, but good practice
                continue; // Skip this device
            }

            for (i_idx = 0 ; i_idx < config->bNumInterfaces ; i_idx++) {
                for ( k_idx = 0 ; k_idx < config->interface[i_idx].num_altsetting ; k_idx++ ) {
                    const struct libusb_interface_descriptor *inter = &config->interface[i_idx].altsetting[k_idx];
                    
                    // printf("DEBUG: Dev %zd, Iface %d, Alt %d: Class=0x%02x, SubClass=0x%02x, Protocol=0x%02x, NumEndpoints=%d\n",
                    //        d_idx, inter->bInterfaceNumber, inter->bAlternateSetting,
                    //        inter->bInterfaceClass, inter->bInterfaceSubClass, inter->bInterfaceProtocol, inter->bNumEndpoints);

                    // Check if this interface matches our criteria (HID class and specific protocol)
                    if (inter->bInterfaceClass == LIBUSB_CLASS_HID &&
                        inter->bInterfaceProtocol == GAMEPAD_CONTROL_PROTOCOL) { // THIS IS THE SELECTED LINE
                        
                        printf("DEBUG: Found potential HID Interface %d with Protocol %d on Device %zd (VID:0x%04x, PID:0x%04x)\n",
                               inter->bInterfaceNumber, inter->bInterfaceProtocol, d_idx, desc.idVendor, desc.idProduct);

                        if (inter->bNumEndpoints == 0) {
                            // printf("DEBUG: Interface has 0 endpoints, cannot be our gamepad data interface. Skipping.\n");
                            continue;
                        }

                        // Attempt to open the device
                        if ((r = libusb_open(current_dev, &controller)) != 0) {
                            fprintf(stderr, "Warning: libusb_open failed for matching device (VID:0x%04x, PID:0x%04x): %s. Trying next...\n", 
                                    desc.idVendor, desc.idProduct, libusb_error_name(r));
                            controller = NULL; 
                            continue; 
                        }
                        printf("DEBUG: Device VID:0x%04x, PID:0x%04x opened successfully.\n", desc.idVendor, desc.idProduct);

                        // Enable auto-detaching of kernel driver for the whole device
                        if (libusb_set_auto_detach_kernel_driver(controller, 1) != LIBUSB_SUCCESS) {
                             fprintf(stderr, "Warning: Could not enable auto-detach kernel driver for controller. Claiming might fail if driver is active.\n");
                        }
                        
                        // Check if kernel driver is active on this specific interface and detach it
                        if (libusb_kernel_driver_active(controller, inter->bInterfaceNumber)) {
                            printf("DEBUG: Kernel driver active on interface %d. Attempting to detach...\n", inter->bInterfaceNumber);
                            if (libusb_detach_kernel_driver(controller, inter->bInterfaceNumber) == 0) {
                                printf("DEBUG: Kernel driver detached successfully from interface %d.\n", inter->bInterfaceNumber);
                            } else {
                                fprintf(stderr, "Warning: Error detaching kernel driver from interface %d. Claim might fail.\n", inter->bInterfaceNumber);
                                fprintf(stderr, "         This could be a permissions issue. Check udev rules or try with sudo (for testing only).\n");
                            }
                        }

                        // Claim the interface
                        if ((r = libusb_claim_interface(controller, inter->bInterfaceNumber)) != 0) {
                            fprintf(stderr, "Error: Claim interface %d failed: %s.\n", inter->bInterfaceNumber, libusb_error_name(r));
                            fprintf(stderr, "       This is often due to permissions or another process/kernel driver using the device.\n");
                            fprintf(stderr, "       Ensure udev rules are set up correctly or try running with sudo (for testing only).\n");
                            libusb_close(controller); 
                            controller = NULL;        
                            // MODIFICATION: If claim fails, continue to the next altsetting/interface rather than next device immediately.
                            // The outer loops will handle trying other interfaces/devices.
                            // However, if we opened the device, we should continue within this device's interfaces first.
                            // If this was the *only* matching interface on this device, the config will be freed below.
                            continue; 
                        }
                        
                        *endpoint_address = inter->endpoint[0].bEndpointAddress; 
                        printf("SUCCESS: USB Controller interface %d claimed. Endpoint Address: 0x%02X\n", inter->bInterfaceNumber, *endpoint_address);
                        
                        libusb_free_config_descriptor(config); 
                        goto found_controller; 
                    }
                }
            }
            libusb_free_config_descriptor(config); // Free config descriptor if no match found within it
        }
    }

found_controller:
    libusb_free_device_list(devs, 1); // Free the list of devices

    if (!controller) {
        fprintf(stderr, "Error: No USB game controller found or claimed after checking all devices/interfaces.\n");
        fprintf(stderr, "       Please ensure the controller is connected and that you have permissions (udev rules on Linux).\n");
        libusb_exit(NULL); // Deinitialize libusb if no controller was successfully set up
    }
    return controller;
}


struct controller_output_packet *usb_to_output(struct controller_output_packet *packet, 
                                                unsigned char* output_array) {
    // Check up and down arrow
    switch(output_array[IND_UPDOWN]) {
        case 0x00: packet->updown = 1; break;    // Up
        case 0xff: packet->updown = -1; break;   // Down
        default:   packet->updown = 0; break;    // Neutral or invalid
    }
    // Check left and right arrow
    switch(output_array[IND_LEFTRIGHT]) {
        case 0x00: packet->leftright = 1; break;  // Left
        case 0xff: packet->leftright = -1; break; // Right
        default:   packet->leftright = 0; break;  // Neutral or invalid
    }

    // Check select and start (typically in the upper nibble of IND_SELSTARIB)
    unsigned char sel_start_byte = output_array[IND_SELSTARIB] >> 4;
    packet->select = (sel_start_byte & 0x01) ? 1 : 0; // Assuming bit 0 for select
    packet->start  = (sel_start_byte & 0x02) ? 1 : 0; // Assuming bit 1 for start
                                                     // Adjust if your controller maps these differently

    // Check left and right rib (typically in the lower nibble of IND_SELSTARIB)
    unsigned char rib_byte = output_array[IND_SELSTARIB] & 0x0F;
    packet->left_rib  = (rib_byte & 0x02) ? 1 : 0; // Assuming bit 1 for Left Rib
    packet->right_rib = (rib_byte & 0x01) ? 1 : 0; // Assuming bit 0 for Right Rib
                                                   // Adjust if your controller maps these differently

    // Check face buttons (X, Y, A, B - typically in IND_XYAB)
    unsigned char face_buttons_upper_nibble = output_array[IND_XYAB] >> 4;

    packet->x = (face_buttons_upper_nibble & 0x01) ? 1 : 0; // Original: X
    packet->a = (face_buttons_upper_nibble & 0x02) ? 1 : 0; // Original: A
    packet->b = (face_buttons_upper_nibble & 0x04) ? 1 : 0; // Original: B
    packet->y = (face_buttons_upper_nibble & 0x08) ? 1 : 0; // Original: Y

    return packet;
}
