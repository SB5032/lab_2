#include "fpga_audio.h"
#include "audio_interface.h"
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

void bgm_startstop(const unsigned char play)
{
  fpga_audio_arg_t vla;
  vla.play = play;
  if (ioctl(audio_fd, FPGA_AUDIO_BGM_STARTSTOP, &vla)) {
      perror("ioctl(FPGA_AUDIO_BGM_STARTSTOP) failed");
      return;
  }
}

void play_sfx(const unsigned char audio_num)
{
  fpga_audio_arg_t vla;
  vla.play = audio_num;
  if (ioctl(audio_fd, FPGA_AUDIO_SET_AUDIO_ADDR, &vla)) {
      perror("ioctl(FPGA_AUDIO_SET_AUDIO_ADDR) failed");
      return;
  }
}

