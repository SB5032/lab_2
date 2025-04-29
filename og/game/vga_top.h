#ifndef _VGA_TOP_H
#define _VGA_TOP_H

#include <linux/ioctl.h>

// def of argument for tiles
typedef struct {
  unsigned char r;
  unsigned char c;
  unsigned char n;
} vga_top_arg_t;

// def of argument for sprites
typedef struct {
  unsigned char active;
  unsigned short r;
  unsigned short c;
  unsigned char n;
  unsigned short register_n; // the corresponding sprite register, start from 0
} vga_top_arg_s;

// function top dec
void write_tile_to_kernel(unsigned char r, unsigned char c, unsigned char n);

void write_sprite_to_kernel(unsigned char active,
                            unsigned short r,
                            unsigned short c,
                            unsigned char n,
                            unsigned short register_n);


#define VGA_TOP_MAGIC 'q'

/* ioctls and their arguments */
#define VGA_TOP_WRITE_TILE _IOW(VGA_TOP_MAGIC, 1, vga_top_arg_t *)
#define VGA_TOP_WRITE_SPRITE _IOW(VGA_TOP_MAGIC, 2, vga_top_arg_s *)

#endif
