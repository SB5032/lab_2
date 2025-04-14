#ifndef _VGA_BIRD_H
#define _VGA_BIRD_H

#include <linux/ioctl.h>

//number of supported sprites
#define SIZE 42

typedef struct {
  int data[SIZE];
  int score;
  int combo;
} vga_bird_data_t;
  
typedef struct {
  	vga_bird_data_t packet;
} vga_bird_arg_t;

typedef struct {
	int x, y, dx, dy, id, index, hit;
} sprite;

typedef struct {
  int pipe_num, top_pipe_max_index, bottom_pipe_max_index;
} vga_pipe_position_t;

#define VGA_BIRD_MAGIC 'q'

/* ioctls and their arguments */
#define VGA_BIRD_WRITE_PACKET _IOW(VGA_BIRD_MAGIC, 4, vga_bird_arg_t *)
#define VGA_BIRD_WRITE_SCORE _IOW(VGA_BIRD_MAGIC, 5, vga_bird_arg_t *)
#define VGA_BIRD_WRITE_COMBO _IOW(VGA_BIRD_MAGIC, 6, vga_bird_arg_t *)
#define VGA_BIRD_READ_PACKET _IOR(VGA_BIRD_MAGIC, 7, vga_bird_arg_t *)

#endif
