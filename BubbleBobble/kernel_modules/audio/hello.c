#include <stdio.h>
#include "fpga_audio.h"
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

int fpga_audio_fd;


void bgm_startstop(const unsigned char play)
{
  fpga_audio_arg_t vla;
  vla.play = play;
  if (ioctl(fpga_audio_fd, FPGA_AUDIO_BGM_STARTSTOP, &vla)) {
      perror("ioctl(FPGA_AUDIO_BGM_STARTSTOP) failed");
      return;
  }
}

void play_sfx(const unsigned char audio_num)
{
  fpga_audio_arg_t vla;
  vla.play = audio_num;
  if (ioctl(fpga_audio_fd, FPGA_AUDIO_SET_AUDIO_ADDR, &vla)) {
      perror("ioctl(FPGA_AUDIO_SET_AUDIO_ADDR) failed");
      return;
  }
}

int main(int argc, char *argv[])
{
  if (argc != 3)
  {
    printf("usage: hello 0/1, 0-5\n");
    return 0;  
  }
  static const char filename[] = "/dev/fpga_audio";

  printf("fpga audio Userspace program started\n");

  if ( (fpga_audio_fd = open(filename, O_RDWR)) == -1) {
    fprintf(stderr, "could not open %s\n", filename);
    return -1;
  }
  
  if(atoi(argv[1]) == 0)
    bgm_startstop(atoi(argv[2]));
  else
    play_sfx(atoi(argv[2]));

  printf("fpga audio Userspace program terminating\n");
  return 0;
}
