/*
 * Userspace program that communicates with the vga_ball device driver
 * through ioctls
 *
 * Stephen A. Edwards
 * Columbia University
 * 
 * Submitted by Sharwari Bhosale (sb5032) & Kamala Vennela Vasireddy (kv2446)
 * 
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
volatile sig_atomic_t run_on = 1;

#define SCREEN_WIDTH 640   
#define SCREEN_HEIGHT 480  
#define BALL_SIZE 10 

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

vga_ball_arg_t recv_bg_color() 
{
	vga_ball_arg_t vla;

	if (ioctl(vga_ball_fd, VGA_BALL_READ_BACKGROUND, &vla)) {
		perror("ioctl(VGA_BALL_READ_BACKGROUND) failed");
		return;
	}
	printf("%02x %02x %02x\n", vla.background.red, vla.background.green, vla.background.blue);
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
  printf("%02x %02x %02x\n", vla.background.red, vla.background.green, vla.background.blue);
}

void print_loc() 
{
  vga_ball_arg_l vloc;

  if (ioctl(vga_ball_fd, VGA_BALL_READ_LOCATION, &vloc)) {
	  perror("ioctl(VGA_BALL_READ_LOCATION) failed");
	  return;
  }
  printf("%02x %02x \n", vloc.loc.b_x, vloc.loc.b_y);
}

vga_ball_loc recv_loc() 
{
  vga_ball_arg_l vloc;

  if (ioctl(vga_ball_fd, VGA_BALL_READ_LOCATION, &vloc)) {
	  perror("ioctl(VGA_BALL_READ_LOCATION) failed");
	  return;
  }
  printf("%02x %02x \n", vloc.loc.b_x, vloc.loc.b_y);
  return vloc.loc;  
}

void set_loc(const vga_ball_loc *l)
{
  vga_ball_arg_l vloc;
  vloc.loc = *l;
  printf("Sending Location data: %02x %02x\n", vloc.loc.b_x, vloc.loc.b_y);
  if (ioctl(vga_ball_fd, VGA_BALL_WRITE_LOCATION, &vloc)) {
	  perror("ioctl(VGA_BALL_WRITE_LOCATION) failed");
	  return;
  }
}

void handle_sigint(int sig) 
{
  run_on = 0;
}

int main()
{
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
  };

# define COLORS 9

vga_ball_arg_t vla; 

vla = recv_bg_color();

printf("BG: %02x %02x %02x\n", vla.background.red, vla.background.green, vla.background.blue);

vga_ball_loc ball_pos = { SCREEN_WIDTH/2, SCREEN_HEIGHT/2 };

short vel_x = 2;  
short vel_y = 2;  

int color_id = 0; 

signal(SIGINT, handle_sigint);

if ((vga_ball_fd = open(filename, O_RDWR)) == -1) 
{
	fprintf(stderr, "could not open %s\n", filename);
	return -1;
}
set_loc(&ball_pos);
print_loc(); 
usleep(5000000); 

set_background_color(&colors[color_id]);

while (run_on) {
	vga_ball_loc read_pos = recv_loc(); 

	ball_pos.b_x = read_pos.b_x + vel_x; 
	ball_pos.b_y = read_pos.b_y + vel_y; 

	printf("Position: %d,%d  Velocity: %d,%d\n", ball_pos.b_x, ball_pos.b_y, vel_x, vel_y);

	if (ball_pos.b_x <= BALL_SIZE) {
		ball_pos.b_x = BALL_SIZE; 
		vel_x = -vel_x;
		color_id = (color_id + 1) % COLORS;
		printf("** BOUNCE LEFT **\n");
	}
	
	if (ball_pos.b_x >= (SCREEN_WIDTH - BALL_SIZE)) {
		ball_pos.b_x = SCREEN_WIDTH - BALL_SIZE; 
		vel_x = -vel_x;
		color_id = (color_id + 1) % COLORS;
		printf("** BOUNCE RIGHT **\n");
	}
	
	if (ball_pos.b_y <= BALL_SIZE) {
		ball_pos.b_y = BALL_SIZE; 
		vel_y = -vel_y;
		color_id = (color_id + 1) % COLORS;
		printf("** BOUNCE TOP **\n");
	}
	
	if (ball_pos.b_y >= (SCREEN_HEIGHT - BALL_SIZE)) {
		ball_pos.b_y = SCREEN_HEIGHT - BALL_SIZE;
		vel_y = -vel_y;
		color_id = (color_id + 1) % COLORS;
		printf("** BOUNCE BOTTOM **\n");
	}
	
	set_loc(&ball_pos);
	print_loc();
	set_background_color(&colors[color_id]);
	print_background_color();
	usleep(30000);
}

printf("VGA BALL Userspace program terminating\n");
return 0;
}
 