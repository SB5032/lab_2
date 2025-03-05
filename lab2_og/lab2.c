/*
 *
 * CSEE 4840 Lab 2 for 2019
 *
 * Name/UNI: Please Changeto Yourname (pcy2301)
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
int row, col, cursor, ptr;
int rowput = 3;
int buffer[BUFFER_SIZE];

struct libusb_device_handle *keyboard;
uint8_t endpoint_address;

pthread_t network_thread;
void *network_thread_f(void *);
char *hex_to_ascii(char *keyid);

int main()
{
  int err, col;

  struct sockaddr_in serv_addr;

  struct usb_keyboard_packet packet;
  int transferred;
  char keystate[12];
  int keyvalue[2];
  keyvalue[1] = '\0';
  if ((err = fbopen()) != 0) {
    fprintf(stderr, "Error: Could not open framebuffer: %d\n", err);
    exit(1);
  }
  fbclear();

  /* Draw rows of asterisks across the top and bottom of the screen */
  for (col = 0 ; col < 64 ; col++) {
    fbputchar('*', 0, col);
    fbputchar('*', 23, col);
    fbputchar('-', 20, col); //create text box
  }

  //fbputs("Hello CSEE 4840 World!", 4, 10);

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
			packet.keycode[1]); //input
		fbputs(keystate, 19, 0);
		char *output_char = hex_to_ascii(keystate);
		printf("Output_char = %s\n", output_char);
      
		if (packet.modifiers == 0x00 && packet.keycode[0] == 0x00 && packet.keycode[1] == 0x00)
		{
			continue;
		}
		//enter key
		else if (packet.keycode[0] == 0x28 && packet.keycode[1] == 0x00 && packet.modifiers == 0x00)
		{
			fbclear_txtbox();

			write(sockfd, buffer, BUFFER_SIZE - 1);
			ptr = 0; //reset buffer ptr
			//clearing buffer after enter
			for (int i = 0; i < BUFFER_SIZE; i++)
				buffer[i] = '\0';

			col = 0;
			row = 21;
		}
		else
		{
			buffer[ptr] = *output_char;
			ptr++;
			fbputs(output_char, row, col); 
			col++;
			
			// if (col == 63)
			// {
			// col = 0;
			// row++;
			// }
			if (row > 22)
			{
				row = 21;
				fbclear_txtbox();
			}
		}
			
		if (packet.keycode[0] == 0x29) { /* ESC pressed? */
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

void *network_thread_f(void *ignored)
{
  char recvBuf[BUFFER_SIZE];
  int n;
  /* Receive data */
  while ( (n = read(sockfd, &recvBuf, BUFFER_SIZE - 1)) > 0 ) {
    recvBuf[n] = '\0';
    printf("%s", recvBuf);
    fbputs(recvBuf, 8, 0);
	rowput++; // sending on next row
	if (rowput > 18)
	{
		rowput = 3;
		fbclear_half();
	}
  }

  return NULL;
}

char *hex_to_ascii(char * keyid)
{
   // Maps hex keycodes to ASCII characters.
   static char symbol[2]; // Use static to return a string to caller
   int num[3];
   int i = 0;
 
   // Tokenize the input string of key codes (e.g., "02 04 00").
   char *token = strtok(keyid, " ");
   while (token != NULL)
   {
		num[i] = (int)strtol(token, NULL, 16);
		printf("%d", num[i]);
		token = strtok(NULL, " ");
		i++;
   }
 
   // Check the modifier state (Shift, Control, etc.)
   int modifiers = num[0];
 
   // Check the keycode (actual key pressed)
   int keycode = num[1];

   // Handle Backspace Input
   if (keycode == 0x2a)
   {
     symbol[0] = keycode - 34;
   }
   // Handle Enter Key;
   else if (keycode == 0x28)
   {
     symbol[0] = keycode - 30;
   }
   // Handle Spacebar Input.
   else if (keycode == 0x2c)
   {
     symbol[0] = keycode - 12;
   }
   // Handle commas
   else if (keycode == 0x36)
   {
     symbol[0] = keycode - 10;
   }
   // Left Arrow key
   else if (keycode == 0x50)
   {
     symbol[0] = keycode - 80+32;
   }
   // Right Arrow
   else if (keycode == 0x4f)
   {
     symbol[0] = keycode - 80+32;
   }
   // Handle Apostrophe
   else if (keycode == 0x34)
   {
     symbol[0] = keycode - 13;
   }
   // Both shift key behavior: if Shift is pressed, transform to uppercase
   else if (modifiers & 0x02 || modifiers & 0x20)
   { 
     if (keycode >= 0x04 && keycode <= 0x1d)
     {
       // Adjust keycode to represent uppercase letters
       symbol[0] = keycode + 61; // Convert to uppercase (A=0x04 -> A)
     }
     else if (keycode == 0x37)
     {
       symbol[0] = keycode + 7;
     }
     // Question Mark handling.
     else if (keycode == 0x38)
     {
       symbol[0] = keycode + 7;
     }
     // Map to number symbols
     else if (keycode >= 0x1e && keycode <= 0x27)
     {
       symbol[0] = keycode + 19; // Convert to lowercase (a=0x04 -> a)
     }
     else
     {
       symbol[0] = keycode; // For other characters, keep as is
     }
   }
   else
   {
     // Without modifier -> lowercase/regular symbols
     if (keycode >= 0x04 && keycode <= 0x1d)
     {
       symbol[0] = keycode + 93; // a -> z
     }
     else if (keycode >= 0x1e && keycode <= 0x26)
     {
       symbol[0] = keycode + 19; // 1 -> 9
     }
     else if (keycode == 0x27)
     {
       symbol[0] = keycode + 9; // 0
     }
     else if (keycode == 0x37)
     {
       symbol[0] = keycode - 9; // .
     }
     else if (keycode == 0x30)
     {
       symbol[0] = keycode + 45; // ]
     }
     else
     {
       // Handle other keycodes as regular symbols
       symbol[0] = keycode;
     }
   }
 
   symbol[1] = '\0'; // Terminating the string
   return symbol;
} 
