/*
* Submitted by Sharwari Bhosale (sb5032) & Kamala Vennela Vasireddy (kv2446)
*/

#ifndef VGA_BALL_H
#define VGA_BALL_H

#include <linux/ioctl.h>

typedef struct {
  unsigned int red, green, blue;
} vga_ball_color_t;

typedef struct {
  unsigned int b_x, b_y;
} vga_ball_loc;
  

typedef struct {
  vga_ball_color_t background;
} vga_ball_arg_t;

typedef struct {
  vga_ball_loc loc;
} vga_ball_arg_l;

#define VGA_BALL_MAGIC 'q'

/* ioctls and their arguments */
#define VGA_BALL_WRITE_BACKGROUND _IOW(VGA_BALL_MAGIC, 1, vga_ball_arg_t)
#define VGA_BALL_WRITE_LOCATION  _IOW(VGA_BALL_MAGIC, 2, vga_ball_arg_l)
#define VGA_BALL_READ_LOCATION  _IOR(VGA_BALL_MAGIC, 3, vga_ball_arg_l)
#define VGA_BALL_READ_BACKGROUND  _IOR(VGA_BALL_MAGIC, 4, vga_ball_arg_t)

#endif
