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
int inc = 0;
int cursor = 0;
int row = 21;
int buffer[128];
struct libusb_device_handle *keyboard;
uint8_t endpoint_address;

pthread_t network_thread;
void *network_thread_f(void *);
char *key_trans(char * keyid);

int main()
{
  int err, col;

  struct sockaddr_in serv_addr;

  struct usb_keyboard_packet packet;
  int transferred;
  char keystate[12];
	char *keyvalue;

  if ((err = fbopen()) != 0) {
    fprintf(stderr, "Error: Could not open framebuffer: %d\n", err);
    exit(1);
  }
	
	fbclear();

  /* Draw rows of asterisks across the top and bottom of the screen */
  for (col = 0 ; col < 64 ; col++) {
    fbputchar('*', 0, col);
    fbputchar('*', 20, col);	
    fbputchar('*', 23, col);
    fbputchar('-', 11, col);	
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
    if (cursor > inc){ //backspace cursor removal
        fbputs(" ", row, cursor);
	}
    cursor = inc;
    fbputs("|", row, cursor); //maintain cursor

    libusb_interrupt_transfer(keyboard, endpoint_address,
			      (unsigned char *) &packet, sizeof(packet),
			      &transferred, 0);

    if (transferred == sizeof(packet)) {
    printf("Modifiers: %02x, Keycode0: %02x, Keycode1: %02x\n",
       packet.modifiers, packet.keycode[0], packet.keycode[1]);
      
       sprintf(keystate, "%02x %02x %02x", packet.modifiers, packet.keycode[0],
	      packet.keycode[1]);
			
      printf("before func 00 bla %s\n", keystate);

//      fbputs(keystate, 21, 0);
      keyvalue = key_trans(keystate);
    if (inc == 63) { //go to next line
		inc = 0;
		if (row == 23) { //text box full
			fbclear_half();
			row = 22;
		}
		else {
			row = row + 1;
		} 
	}

	if (*keyvalue!=93 && *keyvalue != 61 && *keyvalue!=135)  {   // differentiate between ], =, backspace
    	fbputs(keyvalue, row, inc); //print char in textbox
		buffer[inc] = *keyvalue;
      	inc = inc + 1;
		printf ("inc normal %d \n \n ",inc);
	}

	if (*keyvalue == 135 && inc >= 0) { //if backspace
		inc = inc - 1;
      	fbputs(" ", row, inc);
		buffer[inc] = *keyvalue;
		printf ("inc bs  %d \n \n",inc);
	}
//PRINTING BUFFER
	for (int i = 0; i < 128; i++) {
		printf( "BUFFER - %d ", buffer[i]);  // Set each element explicitly
	}
	printf("size %d \n", sizeof(buffer));
	if (packet.keycode[0] == 0x28) { /* Enter pressed? */
		write (sockfd, buffer, inc);
		fbclear_half();
		for (int i = 0; i < 128; i++) {
			buffer[i] = NULL;  // Set each element explicitly
		}
		inc = 0;
	}

    if (packet.keycode[0] == 0x29) { /* ESC pressed? */
		break;
    }
}
printf ("ibuff = %d \n",buffer[0]);
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
  }

  return NULL;
}

char *key_trans(char * keyid)
{
	char *symbol = NULL;
	int num[3] = {0}; 
	int i = 0;
	char * token = strtok(keyid, " ");
	

	while (token != NULL) {
		num[i] = (int)strtol(token, NULL, 16);
		token = strtok(NULL, " ");
		i++;
	}
	
	printf("%d %d %d \n ", num[0], num[1],num[2]);
	if(num[0] == 2) { 
		num[1] += 61; 
	}
	else { num[1] += 93;}        


	if (num[1] > 92 || num[1] <123) {
	          symbol = (char *)malloc(2); 	
                  symbol[0] = (char)num[1];
                  symbol[1] = '\0';
}
	return symbol;
} 
