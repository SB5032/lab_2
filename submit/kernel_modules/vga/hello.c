#include <stdio.h>
#include "vga_top.h"
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

int vga_top_fd;


void write_tile(unsigned char r, unsigned char c, unsigned char n)
{
  vga_top_arg_t vla;
  vla.r = r;
  vla.c = c;
  vla.n = n;
  if (ioctl(vga_top_fd, VGA_TOP_WRITE_TILE, &vla)) {
      perror("ioctl(VGA_TOP_WRITE_TILE) failed");
      return;
  }
}

int main(int argc, char *argv[])
{
  if (argc != 4)
    printf("usage: hello r c n\n");
  static const char filename[] = "/dev/vga_top";

  printf("vga userspace program started\n");

  if ( (vga_top_fd = open(filename, O_RDWR)) == -1) {
    fprintf(stderr, "could not open %s\n", filename);
    return -1;
  }

  write_tile(atoi(argv[1]), atoi(argv[2]), atoi(argv[3]));
  
  printf("vga userspace program terminating\n");
  return 0;
}
