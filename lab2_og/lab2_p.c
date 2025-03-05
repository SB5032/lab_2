/*
 *
 * CSEE 4840 Lab 2 for 2019
 *
 * Name/UNI: Ananya Maan Singh(am6542) & Pranav Asuri(pa2708)

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
#include <unistd.h>

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
int recvRow = 3;
int displayRow = 1;
int displayColumn = 0;
int inputRow = 21;
int inputColumn = 0;

struct libusb_device_handle *keyboard;
uint8_t endpoint_address;

pthread_t network_thread;
pthread_t cursor_display_thread;
void *cursor_display(void *);
void *network_thread_f(void *);
char *key_trans(char *keyid);
char message[BUFFER_SIZE];
int message_pointer = 0;

int main()
{
  int err, col;

  struct sockaddr_in serv_addr;

  struct usb_keyboard_packet packet;
  int transferred;
  char keystate[12];

  if ((err = fbopen()) != 0)
  {
    fprintf(stderr, "Error: Could not open framebuffer: %d\n", err);
    exit(1);
  }

  fbclear();

  /* Draw rows of asterisks across the top and bottom of the screen */
  for (col = 0; col < 64; col++)
  {
    fbputchar('*', 0, col);
    fbputchar('*', 23, col);
    fbputchar('*', 20, col);
  }

  // fbputs("Hello CSEE 4840 World!", 1, 10);

  /* Open the keyboard */
  if ((keyboard = openkeyboard(&endpoint_address)) == NULL)
  {
    fprintf(stderr, "Did not find a keyboard\n");
    exit(1);
  }

  /* Create a TCP communications socket */
  if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
  {
    fprintf(stderr, "Error: Could not create socket\n");
    exit(1);
  }

  /* Get the server address */
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(SERVER_PORT);
  if (inet_pton(AF_INET, SERVER_HOST, &serv_addr.sin_addr) <= 0)
  {
    fprintf(stderr, "Error: Could not convert host IP \"%s\"\n", SERVER_HOST);
    exit(1);
  }

  /* Connect the socket to the server */
  if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
  {
    fprintf(stderr, "Error: connect() failed.  Is the server running?\n");
    exit(1);
  }

  /* Start the network thread */
  pthread_create(&network_thread, NULL, network_thread_f, NULL);
  pthread_create(&cursor_display_thread, NULL, cursor_display, NULL);

  /* Look for and handle keypresses */

  // printf("HereNow\n");

  for (;;)
  {
    libusb_interrupt_transfer(keyboard, endpoint_address,
                              (unsigned char *)&packet, sizeof(packet),
                              &transferred, 0);

    if (transferred == sizeof(packet))
    {
      sprintf(keystate, "%02x %02x %02x", packet.modifiers, packet.keycode[0], packet.keycode[1]);
      // printf("Keystate= %s\n", keystate);
      fbputs(keystate, 19, 0);

      // To display each ele
      //  for (int i = 0; i < 12; i++)
      //  {
      //    printf("keystate[%d] = %c\n", i, keystate[i]);
      //  }

      // printf("Here!");
      char *output_char = key_trans(keystate);
      // message[message_row][message_col] = output_char;
      // char message[256];

      printf("Output_char = %s\n", output_char);

      // To handle extra null input at release of every keypress
      if (packet.modifiers == 0x00 && packet.keycode[0] == 0x00 && packet.keycode[1] == 0x00)
      {
        // printf("Null Key!!");
        continue;
      }

      else if (packet.keycode[0] == 0x2a && packet.keycode[1] == 0x00 && packet.modifiers == 0x00)
      {
        printf("\nBackspace Pressed.\n");
        if (inputColumn == 0 && inputRow == 21)
        {
          fbputchar(output_char, inputRow, inputColumn);

          // inputRow--;
          // inputColumn = 63;
          // inputRow--;
          // fbputchar(' ', inputRow, inputColumn);
        }
        else if (inputRow == 22 && inputColumn == 0)
        {
          fbputchar(output_char, inputRow, inputColumn);
          inputRow--;
          message_pointer--;
          message[message_pointer] = '\0';
          inputColumn = 63;
          // inputRow--;
          fbputchar(' ', inputRow, inputColumn);
        }
        else
        {
          fbputchar(output_char, inputRow, inputColumn);
          // inputRow--;
          inputColumn--;
          message_pointer--;
          // message_pointer--;
          message[message_pointer] = '\0';
          // inputRow--;
          fbputchar(' ', inputRow, inputColumn);
        }
        // printf("\nInput Column: %d", inputColumn);
        // printf("\nInput Row: %d", inputRow);
      }

      else if (packet.keycode[0] == 0x2c && packet.keycode[1] == 0x00 && packet.modifiers == 0x00)
      {
        printf("\n Spacebar Pressed.\n");
        inputColumn++;
        message[message_pointer] = *output_char;
        message_pointer++;
        // inputRow--;
        fbputchar(' ', inputRow, inputColumn - 1);
        printf("\nInput Column: %d", inputColumn);
        printf("\nInput Row: %d", inputRow);
      }

      else if (packet.keycode[0] == 0x00 && packet.keycode[1] == 0x00 && (packet.modifiers == 0x20 || packet.modifiers == 0x02))
      {
        printf("\n Shift Pressed.\n");
        // inputColumn++;
        // inputRow--;
        // fbputchar(' ', inputRow, inputColumn - 1);
        // printf("\nInput Column: %d", inputColumn);
        // printf("\nInput Row: %d", inputRow);
      }
      else if (packet.keycode[0] == 0x28 && packet.keycode[1] == 0x00 && packet.modifiers == 0x00)
      {
        printf("\n Enter Pressed.\n");
        // inputColumn++;
        // inputRow--;
        // fbputchar(' ', inputRow, inputColumn - 1);
        // printf("\nInput Column: %d", inputColumn);
        // printf("\nInput Row: %d", inputRow);
        fbclear_half();

        write(sockfd, message, BUFFER_SIZE - 1);
        // Clear the buffer here...
        printf("Message = %s\n", message);
        message_pointer = 0;
        for (int i = 0; i < BUFFER_SIZE; i++)
          message[i] = '\0';
        // printf("Cleared Message = %s\n", message);
        for (int i = 0; i < 23; i++)
          printf("\nMessage-%d = %d", i, message[i]);
        inputColumn = 0;
        inputRow = 21;
      }
      // Left Arrow Key Handle.
      else if (packet.keycode[0] == 0x50 && packet.keycode[1] == 0x00 && packet.modifiers == 0x00)
      {
        printf("\n Left Arrow Pressed.\n");

         if (inputColumn > 0){
          fbputchar(message[message_pointer], inputRow, inputColumn);
          inputColumn--;
          message_pointer--;
        }
      }
      // Right Arrow Key Handle.
      else if (packet.keycode[0] == 0x4f && packet.keycode[1] == 0x00 && packet.modifiers == 0x00)
      {
        printf("\n Right Arrow Pressed.\n");
        // inputColumn++;
        // inputRow--;
        // fbputchar(' ', inputRow, inputColumn - 1);
        // printf("\nInput Column: %d", inputColumn);
        // printf("\nInput Row: %d", inputRow);
        // fbclear_input();

        // write(sockfd, message, BUFFER_SIZE - 1);
        // message_pointer=0;
        if (inputColumn == 63 && inputRow == 22)
        {
          continue;
        }
        else if (inputColumn == 63 && inputRow == 21)
        {
          inputColumn = 0;
          inputRow++;
        }
        else
        {
          inputColumn++;
        }
        // if (message_pointer < BUFFER_SIZE -1 && message[message_pointer]!= '\0'){
        //   fbputchar(message[message_pointer], inputRow, inputColumn);
        //   inputColumn++;
        //   message_pointer++;
        // }

        // inputRow = 21;
      }
      // To handle every valid keypress
      else
      {
        // printf("Here!!!");
        // const char * pointer_var = message[inputRow][inputColumn];
        // fbputs(pointer_var, inputRow-3, inputColumn);
        // strcpy(message[message_row][message_col], output_char);

        for(int i = BUFFER_SIZE -2; i >message_pointer; i--){
          message[i] = message[i-1];
        }
        message[message_pointer] = *output_char;
        message_pointer++;

        fbputs(output_char, inputRow, inputColumn);
        for (int i = 0; i < 23; i++)
          printf("\nMessage-%d = %d", i, message[i]);
        // printf("\nMessage1 = %c", message[1]);
        inputColumn++;
        // message_col++;

        printf("\nInput Column: %d", inputColumn);
        printf("\nInput Row: %d", inputRow);

        if (inputColumn == 63)
        {
          inputColumn = 0;
          // message_col = 0;
          inputRow++;
          // message_row++;
        }
        if (inputRow > 22)
        {
          inputRow = 21;
          fbclear_half();
        }
      }

      // To handle escape keypress; Terminates program listening to keyboard interrupts.
      if (packet.keycode[0] == 0x29)
      { /* ESC pressed? */
        break;
      }
    }
    // printf("\nHereNowlast\n");
  }

  /* Terminate the network thread */
  pthread_cancel(network_thread);

  /* Wait for the network thread to finish */
  pthread_join(network_thread, NULL);
  pthread_join(cursor_display_thread, NULL);

  return 0;
}

void *network_thread_f(void *ignored)
{
  char recvBuf[BUFFER_SIZE];
  int n;
  /* Receive data */
  while ((n = read(sockfd, &recvBuf, BUFFER_SIZE - 1)) > 0)
  {
    recvBuf[n] = '\0';
    printf("%s", recvBuf);
    fbputs(recvBuf, recvRow, 0);

    recvRow++;

    if (recvRow > 18)
    {
      recvRow = 1;
      fbclear_half(); // Special version of fbclear where incoming messages are cleared and starts from top again.
    }
  }
  return NULL;
}

void *cursor_display(void *ignored)
{
  for (;;)
  {
    // for(float i=0; i<60000000; i++){
    //   continue;
    // }
    fbputchar('|', inputRow, inputColumn);
    sleep(1);

    // for(float i=0; i<60000000; i++){
    //   continue;
    // }
    fbputchar(' ', inputRow, inputColumn);
    sleep(1);
  }
}

char *key_trans(char *keyid)
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
  // Shift key behavior: if Shift is pressed, transform to uppercase
  else if (modifiers & 0x02 || modifiers & 0x20)
  { // 0x02 corresponds to the left Shift key
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
    // else
    // {
    //   symbol[0] = keycode; // For other characters, keep as is
    // }
  }
  else
  {
    // Without Shift, just map keycode to lowercase/regular symbol
    if (keycode >= 0x04 && keycode <= 0x1d)
    {
      symbol[0] = keycode + 93; // Convert to lowercase (a=0x04 -> a)
    }
    else if (keycode >= 0x1e && keycode <= 0x26)
    {
      symbol[0] = keycode + 19; // Convert to lowercase (a=0x04 -> a)
    }
    else if (keycode == 0x27)
    {
      symbol[0] = keycode + 9;
    }
    else if (keycode == 0x37)
    {
      symbol[0] = keycode - 9;
    }
    // Square Bracket Keypress
    else if (keycode == 0x30)
    {
      symbol[0] = keycode + 45;
    }
    else
    {
      // Handle other keycodes as regular symbols
      symbol[0] = keycode;
    }
    // else
    // {
    //   // Handle other keycodes as regular symbols
    //   symbol[0] = keycode;
    // }
  }

  symbol[1] = '\0'; // Null-terminate the string
  return symbol;
}
