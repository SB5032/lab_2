/*
 * Userspace program that communicates with the vga_ball device driver
 * through ioctls
 *
 * Submitted by Sharwari Bhosale (sb5032) & Kamala Vennela Vasireddy (kv2446)
 * Stephen A. Edwards
 * Columbia University
 */

 #include <stdio.h>
 #include "vga_ball.h"
 #include <sys/ioctl.h>
 #include <sys/types.h>
 #include <sys/stat.h>
 #include <fcntl.h>
 #include <string.h>
 #include <unistd.h>
 #include <signal.h>
 #include <stdlib.h>
 
 int vga_ball_fd;
 volatile sig_atomic_t keep_running = 1;
 
 #define SCREEN_WIDTH 640   // 640 in hex changed 0x280
 #define SCREEN_HEIGHT 480  // 480 in hex changed 0x1E0
 #define BALL_SIZE 10       // 16 pixels in hex changed 0x10
 
 /* Read and print the background color */
 void print_background_color() {
   vga_ball_arg_t vla;
   
   if (ioctl(vga_ball_fd, VGA_BALL_READ_BACKGROUND, &vla)) {
	   perror("ioctl(VGA_BALL_READ_BACKGROUND) failed");
	   return;
   }
   printf("%02x %02x %02x\n",
	  vla.background.red, vla.background.green, vla.background.blue);
 }
 
 vga_ball_arg_t recv_background_color() {
   vga_ball_arg_t vla;
   
   if (ioctl(vga_ball_fd, VGA_BALL_READ_BACKGROUND, &vla)) {
	   perror("ioctl(VGA_BALL_READ_BACKGROUND) failed");
	   return;
   }
   printf("%02x %02x %02x\n",
	  vla.background.red, vla.background.green, vla.background.blue);
	return vla; 
 }
 
 /* Set the background color */
 void set_background_color(const vga_ball_color_t *c)
 {
   vga_ball_arg_t vla;
   vla.background = *c;
   if (ioctl(vga_ball_fd, VGA_BALL_WRITE_BACKGROUND, &vla)) {
	   perror("ioctl(VGA_BALL_WRITE_BACKGROUND) failed");
	   return;
   }
   printf("%02x %02x %02x\n",
	 vla.background.red, vla.background.green, vla.background.blue);
 }
 
 /* Read and print the location */
 void print_location() {
   vga_ball_arg_l vlocation;
   
   if (ioctl(vga_ball_fd, VGA_BALL_READ_LOCATION, &vlocation)) {
	   perror("ioctl(VGA_BALL_READ_LOCATION) failed");
	   return;
   }
   printf("%02x %02x \n",
	  vlocation.location.ball_x, vlocation.location.ball_y);
 }
 
 vga_ball_location recv_location() {
   vga_ball_arg_l vlocation;
   
   if (ioctl(vga_ball_fd, VGA_BALL_READ_LOCATION, &vlocation)) {
	   perror("ioctl(VGA_BALL_READ_LOCATION) failed");
	   return;
   }
   printf("%02x %02x \n",
	  vlocation.location.ball_x, vlocation.location.ball_y);
   return vlocation.location;  
 }
 
 /*Set location coordinates*/
 void set_location(const vga_ball_location *l)
 {
   vga_ball_arg_l vloc;
   vloc.location = *l;
   printf("Sending Location data: %02x %02x\n", vloc.location.ball_x, vloc.location.ball_y);
   if (ioctl(vga_ball_fd, VGA_BALL_WRITE_LOCATION, &vloc)) {
	   perror("ioctl(VGA_BALL_WRITE_LOCATION) failed");
	   return;
   }
 }
 
 void handle_sigint(int sig) {
   keep_running = 0;
 }
 
 int main()
 {
   //vga_ball_arg_t vla; changed
   //vga_ball_arg_l vloc; changed
   //int i; changed
 
   static const char filename[] = "/dev/vga_ball";
 
   static const vga_ball_color_t colors[] = {
	 { 0xff, 0x00, 0x00 }, /* Red */
 { 0x00, 0xff, 0x00 }, /* Green */
 { 0x00, 0x00, 0xff }, /* Blue */
 { 0xff, 0xff, 0x00 }, /* Yellow */
 { 0x00, 0xff, 0xff }, /* Cyan */
 { 0xff, 0x00, 0xff }, /* Magenta */
 { 0x80, 0x80, 0x80 }, /* Gray */
 { 0x00, 0x00, 0x00 }, /* Black */
 // { 0xff, 0xff, 0xff }, /* White */
 { 0x80, 0x00, 0x00 }, /* Maroon */
 { 0x80, 0x80, 0x00 }, /* Olive */
 { 0x00, 0x80, 0x00 }, /* Dark Green */
 { 0x00, 0x80, 0x80 }, /* Teal */
 { 0x00, 0x00, 0x80 }, /* Navy */
 { 0x80, 0x00, 0x80 }, /* Purple */
 { 0xa5, 0x2a, 0x2a }, /* Brown */
 { 0xff, 0xa5, 0x00 }, /* Orange */
 { 0xff, 0xc0, 0xcb }, /* Pink */
 { 0xad, 0xd8, 0xe6 }, /* Light Blue */
 { 0x70, 0x80, 0x90 }, /* Slate Gray */
 { 0xf5, 0xde, 0xb3 }, /* Wheat */
 { 0xff, 0xe4, 0xb5 }, /* Moccasin */
 { 0xda, 0xa5, 0x20 }, /* Goldenrod */
 { 0xb0, 0xc4, 0xde }  /* Light Steel Blue */
 
	 // { 0xff, 0xff, 0xff }  /* White */
   };
 
 # define COLORS 9
 
 vga_ball_arg_t vla; 
 
 vla = recv_background_color();
 
 printf("BG: %02x %02x %02x\n",
   vla.background.red, vla.background.green, vla.background.blue);
 
 vga_ball_location ball_pos = { SCREEN_WIDTH/2, SCREEN_HEIGHT/2 };  // Start in center //CHANGED
 
 short ball_vel_x = 2;  // X velocity (positive = right) //changed 0x4 and 0x3 and short
 short ball_vel_y = 2;  // Y velocity (positive = down) //changed
 
 int color_index = 0;   // Start with red
 
 signal(SIGINT, handle_sigint);
 
   printf("VGA ball Userspace program started\n");
 
   if ( (vga_ball_fd = open(filename, O_RDWR)) == -1) {
	 fprintf(stderr, "could not open %s\n", filename);
	 return -1;
   }
   set_location(&ball_pos);
   printf("initial position: ");
   print_location(); //changed
   usleep(5000000); 
	
 
   // Set initial background color
   set_background_color(&colors[color_index]); //changed
 
   // Main animation loop
   while (keep_running) {
	 printf("Reading and setting");  
	 vga_ball_location read_pos = recv_location(); 
 
	 ball_pos.ball_x = read_pos.ball_x + ball_vel_x; 
	 ball_pos.ball_y = read_pos.ball_y + ball_vel_y; 
 
	 // Update position based on velocity
	 // ball_pos.ball_x += ball_vel_x;
	 // ball_pos.ball_y += ball_vel_y;
 
	 // Print current position and velocity for debugging
	 printf("Position: %d,%d  Velocity: %d,%d\n", 
	 ball_pos.ball_x, ball_pos.ball_y, 
	 ball_vel_x, ball_vel_y);
 
	 // Left edge
	 if (ball_pos.ball_x <= BALL_SIZE) {
		 ball_pos.ball_x = BALL_SIZE; // Prevent going off-screen
		 ball_vel_x = -ball_vel_x;      // Reverse direction
		 color_index = (color_index + 1) % COLORS;
		 // set_background_color(&colors[color_index]);
		 printf("** BOUNCE LEFT **\n");
	 }
	 
	 // Right edge
	 if (ball_pos.ball_x >= (SCREEN_WIDTH - BALL_SIZE)) {
		 ball_pos.ball_x = SCREEN_WIDTH - BALL_SIZE; // Prevent going off-screen
		 ball_vel_x = -ball_vel_x;      // Reverse direction
		 color_index = (color_index + 1) % COLORS;
		 // set_background_color(&colors[color_index]);
		 printf("** BOUNCE RIGHT **\n");
	 }
	 
	 // Top edge
	 if (ball_pos.ball_y <= BALL_SIZE) {
		 ball_pos.ball_y = BALL_SIZE; // Prevent going off-screen
		 ball_vel_y = -ball_vel_y;      // Reverse direction
		 color_index = (color_index + 1) % COLORS;
		 // set_background_color(&colors[color_index]);
		 printf("** BOUNCE TOP **\n");
	 }
	 
	 // Bottom edge
	 if (ball_pos.ball_y >= (SCREEN_HEIGHT - BALL_SIZE)) {
		 ball_pos.ball_y = SCREEN_HEIGHT - BALL_SIZE; // Prevent going off-screen
		 ball_vel_y = -ball_vel_y;      // Reverse direction
		 color_index = (color_index + 1) % COLORS;
		 printf("** BOUNCE BOTTOM **\n");
	 }
	 
	 // Update ball position in hardware
	 set_location(&ball_pos);
	 print_location();
	 // color_index = (color_index + 1) % COLORS;
	 set_background_color(&colors[color_index]);
	 print_background_color();
	 // Slow down the animation to make it visible
	 usleep(30000);  // 100ms delay, adjust as needed for your FPGA
   }
   
 
   // for (i = 0 ; i < 24 ; i++) {
   //   set_background_color(&colors[i % COLORS ]);
   //   print_background_color();
   //   usleep(400000);
   // }
   
   printf("VGA BALL Userspace program terminating\n");
   return 0;
 }
 