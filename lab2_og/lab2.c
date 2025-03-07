/*
 *
 * CSEE 4840 Lab 2 for 2019
 *
 * Name/UNI: , , 
 */
#include "fbputchar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "usbkeyboard.h"
#include <pthread.h>
#include <stdbool.h>


/* Update SERVER_HOST to be the IP address of
 * the chat server you are connecting to
 */
/* arthur.cs.columbia.edu */
#define SERVER_HOST "128.59.19.114"
#define SERVER_PORT 42000

#define BUFFER_SIZE 128

/*
 * References:
 *
 * https://web.archive.org/web/20130307100215/http://beej.us/guide/bgnet/output/html/singlepage/bgnet.html
 *
 * http://www.thegeekstuff.com/2011/12/c-socket-programming/
 * 
 */

int sockfd; /* Socket file descriptor */

struct libusb_device_handle *keyboard;
uint8_t endpoint_address;

pthread_t network_thread;
void *network_thread_f(void *);

char HID_to_ASCII(char keycode, char modifier); 

int main()
{
  int err, col;

  struct sockaddr_in serv_addr;

  struct usb_keyboard_packet packet;
  int transferred;
  char keystate[12];
  char message[128];
  int i = -1;
  int j = 0;
  char curr_char;
  int rows;
  int cols;

  for (int k = 0; k < 128; k++) message[k] = ' ';

  void update_screen_message() {
    rows = 20;
    cols = 0;
    for (j = 0; j < 128; j++) {
      cols = j % 64;
      if (cols == 0) rows += 1;
      fbputchar(' ', rows, cols);
    }
    rows = 20;
    cols = 0;
    for (j = 0; j < 128; j++) {
      cols = j % 64;
      if (cols == 0) rows += 1;
      fbputchar(message[j], rows, cols);
    }
   }

   void clear_chat_box() {
    for (int k = 0; k < 128; k++) message[k] = ' ';
    update_screen_message();
    i = -1;
  }


  if ((err = fbopen()) != 0) {
    fprintf(stderr, "Error: Could not open framebuffer: %d\n", err);
    exit(1);
  }

  fbclear();

  /* Draw rows of asterisks across the top and bottom of the screen */
  for (col = 0 ; col < 64 ; col++) {
    fbputchar('*', 0, col);
    fbputchar('*', 23, col);
    fbputchar('_', 20, col);
  }

  /* Open the keyboard */
  if ( (keyboard = openkeyboard(&endpoint_address)) == NULL ) {
    fprintf(stderr, "Did not find a keyboard\n");
    exit(1);
  }
    
  /* Create a TCP communications socket */
  if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
    fprintf(stderr, "Error: Could not create socket\n");
    exit(1);
  }

  /* Get the server address */
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(SERVER_PORT);
  if ( inet_pton(AF_INET, SERVER_HOST, &serv_addr.sin_addr) <= 0) {
    fprintf(stderr, "Error: Could not convert host IP \"%s\"\n", SERVER_HOST);
    exit(1);
  }

  /* Connect the socket to the server */
  if ( connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
    fprintf(stderr, "Error: connect() failed.  Is the server running?\n");
    exit(1);
  }

  /* Start the network thread */
  pthread_create(&network_thread, NULL, network_thread_f, NULL);
 
  /* Look for and handle keypresses */
  for (;;) { 
    libusb_interrupt_transfer(keyboard, endpoint_address,
			      (unsigned char *) &packet, sizeof(packet),
			      &transferred, 0);
    if (transferred == sizeof(packet)) {
      sprintf(keystate, "%02x %02x %02x", packet.modifiers, packet.keycode[0],
	      packet.keycode[1]);

	if (packet.keycode[1] == 0x00)
	{
		curr_char = HID_to_ASCII(packet.keycode[0], packet.modifiers);
	}
	else
	{
		curr_char = HID_to_ASCII(packet.keycode[1], packet.modifiers);
	}
    //   curr_char = HID_to_ASCII(packet.keycode[0], packet.modifiers);
    
      if (i >= 127) {
        clear_chat_box();
      }
      switch (packet.keycode[0]) {
        case 0x28:  // Enter key
          // Sends the message over the socket
          if (write(sockfd, message, 128) < 0) {
            fprintf(stderr, "Error: write() failed. Is the server running?\n");
            exit(1);
          }
          // Resets the message buffer with spaces
          memset(message, ' ', 128);
          // Updates the display to clear the message
          update_screen_message();
          // Resets cursor position
          i = -1;
          break;
      
        case 0x2a:  // Backspace
        case 0x4c:  // Delete
          if (i >= 0) {
            // Removes the last character and updates the display
            message[i--] = ' ';
            update_screen_message();
            fbputchar('|', (i < 63) ? 21 : 22, (i + 1) % 64);  // Blinking cursor
          }
          break;
      
        case 0x50:  // Left Arrow
          if (i >= 0) {
            // Moves cursor left if possible and updates the display
            update_screen_message();
            i--;
            fbputchar('|', (i < 63) ? 21 : 22, (i + 1) % 64);  // Blinking cursor
          }
          break;
      
        case 0x4f:  // Right Arrow
          if (i < 127) {
            // Moves cursor right if possible and updates the display
            i++;
            update_screen_message();
            fbputchar('|', (i < 63) ? 21 : 22, (i + 1) % 64);  // Blinking cursor
          }
          break;
      
        case 0x52:  // Up Arrow
          if (i > 63) {
            // Moves cursor up one line if possible and updates the display
            i -= 64;
            update_screen_message();
            fbputchar('|', (i < 63) ? 21 : 22, (i + 1) % 64);  // Blinking cursor
          }
          break;
      
        case 0x51:  // Down Arrow
          if (i < 64) {
            // Moves cursor down one line if possible and updates the display
            i += 64;
            update_screen_message();
            fbputchar('|', (i < 63) ? 21 : 22, (i + 1) % 64);  // Blinking cursor
          }
          break;
      
        default:
          if (curr_char != 0 && i < 127) {
            // Handles printable characters by adding them to the message buffer
            message[++i] = curr_char;
            update_screen_message();
            fbputchar('|', (i < 63) ? 21 : 22, (i + 1) % 64);  // Blinking cursor
          }
          break;
      }
      
      // Terminates input loop if ESC key is pressed
      if (packet.keycode[0] == 0x29) {  // ESC key
        break;
      }
      
    }
  }

  /* Terminate the network thread */
  pthread_cancel(network_thread);

  /* Wait for the network thread to finish */
  pthread_join(network_thread, NULL);

  return 0;
}

void fbclear_rx() {
  for (int k = 0; k < 64 * 19; k++) {
      fbputchar(' ', k / 64, k % 64);
  }
}

void *network_thread_f(void *ignored) {
  char recvBuf[BUFFER_SIZE];
  int n;
  int r_row = 1;

  /* Receive data */
  while ((n = read(sockfd, recvBuf, BUFFER_SIZE - 1)) > 0) {
      recvBuf[n] = '\0';
      printf("%s\n", recvBuf);

      for (int k = 0; k < n; k++) {
          int r_col = k % 64;
          if (r_row >= 19) {
              fbclear_rx();
              r_row = 1;
          }
          fbputchar(recvBuf[k], r_row, r_col);
          if (r_col == 63) r_row++;  // Move to the next row at the end of a line
      }
      r_row++;  // Move to next row after the loop
  }
  return NULL;
}


char HID_to_ASCII(char keycode, char modifier) {
    static bool caps_en;
    if (keycode == 0x39) caps_en ^= 1;

    // Define character mapping for printable keys
    if (keycode >= 0x04 && keycode <= 0x1D) { // a-z or A-Z
        return (caps_en || modifier == 0x20 || modifier == 0x02) ? ('A' + keycode - 0x04) : ('a' + keycode - 0x04);
    }
    if (keycode >= 0x1E && keycode <= 0x27) { // 1-0
        const char nums[] = "1234567890";
        return (caps_en || modifier == 0x20 || modifier == 0x02) ? "!@#$%^&*()"[keycode - 0x1E] : nums[keycode - 0x1E];
    }

    // Special symbols
    switch (keycode) {
        case 0x28: return '\n';
        case 0x2C: return ' ';
        case 0x2D: return (caps_en || modifier == 0x20 || modifier == 0x02) ? '_' : '-';
        case 0x2E: return (caps_en || modifier == 0x20 || modifier == 0x02) ? '+' : '=';
        case 0x2F: return (caps_en || modifier == 0x20 || modifier == 0x02) ? '{' : '[';
        case 0x30: return (caps_en || modifier == 0x20 || modifier == 0x02) ? '}' : ']';
        case 0x31: return (caps_en || modifier == 0x20 || modifier == 0x02) ? '|' : '\\';
        case 0x33: return (caps_en || modifier == 0x20 || modifier == 0x02) ? ':' : ';';
        case 0x34: return (caps_en || modifier == 0x20 || modifier == 0x02) ? '"' : '\'';
        case 0x35: return (caps_en || modifier == 0x20 || modifier == 0x02) ? '~' : '`';
        case 0x36: return (caps_en || modifier == 0x20 || modifier == 0x02) ? '<' : ',';
        case 0x37: return (caps_en || modifier == 0x20 || modifier == 0x02) ? '>' : '.';
        case 0x38: return (caps_en || modifier == 0x20 || modifier == 0x02) ? '?' : '/';
        default: return 0;
    }
}