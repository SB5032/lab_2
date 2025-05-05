#include "vga_top.h"
#include "vga_interface.h"
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>


// r*c: 40*30       r:0-29      c:0-39
// n means image number, number of the image stored in memory
void write_tile_to_kernel(unsigned char r, unsigned char c, unsigned char n) 
{
  vga_top_arg_t vla;
  vla.r = r;
  vla.c = c;
  vla.n = n;
  if (ioctl(vga_fd, VGA_TOP_WRITE_TILE, &vla)) {
    perror("ioctl(VGA_TOP_WRITE_TILE) failed");
    return;
  }
}

// sprite r and c is pixel, r range is 0 - 639, c range is 0-479
void write_sprite_to_kernel(unsigned char active,   //active == 1, display, active == 0 not display
                            unsigned short r,
                            unsigned short c,
                            unsigned char n,
                            unsigned short register_n) 
{
  vga_top_arg_s vla;
  vla.active = active;
  vla.r = r;
  vla.c = c;
  vla.n = n;
  vla.register_n = register_n;
  // printf("act:%i   r:%i  c:%i  n:%i  register_n:%i\n", active, r, c, n, register_n);
  if (ioctl(vga_fd, VGA_TOP_WRITE_SPRITE, &vla)) {
    perror("ioctl(VGA_TOP_WRITE_SPRITE) failed");
    return;
  }
}


// tile operations: r*c: 40*30       r:0-29      c:0-39

// for when needing to reset the background to empty
void cleartiles()
{
  int i, j;
  for(i=0;i<30;i++)
  {
     for(j=0;j<40;j++)
     {
        write_tile_to_kernel(i, j, BLANKTILE);
     }
  }
}

void clearSprites(){
  for(int i = 0; i <12; i++){
    write_sprite_to_kernel(0, 0, 0, 0, i);
  }
}

// write a single digit number at a specific tile
void write_number(unsigned int num, unsigned int row, unsigned int col)
{
  write_tile_to_kernel((unsigned short) row, (unsigned short) col, NUMBERTILE(num));
}

// write a letter at a specific tile, only works with lower case
void write_letter(unsigned char letter, unsigned int row, unsigned int col)
{
  letter = letter - 97; // ascii conversion
  write_tile_to_kernel(row, col, LETTERTILE(letter));
}

// if digits longer than actual number, the front will be padded with zero
void write_numbers(unsigned int nums, unsigned int digits, unsigned int row, unsigned int col)
{
  if ((col + digits) > 40) {
    printf("number too long!\n");
  }
  else {
    for (unsigned int i = col + digits - 1; i >= col; i--)
    {
      write_number(nums % 10, row, i);
      nums = nums / 10;
    }
  }
}


// update displayed score at a pre-determined location, max digit length of 4
// only work with positive integers
void write_score(int new_score)
{
  write_numbers((unsigned int) new_score, SCORE_MAX_LENGTH, SCORE_COORD_R, SCORE_COORD_C);
}
//40 * 30

// write a string at a specific tile, only works with lower case letters, make sure to keep the string short
void write_text(unsigned char *text, unsigned int length, unsigned int row, unsigned int col)
{
  if ((col + length) > 40) {
    printf("string too long!\n");
  }
  else {
    for (unsigned int i = col; i < (col + length); i++) {
      write_letter(*(text + i - col), row, i);
    }
  }
}






// sprite operations
