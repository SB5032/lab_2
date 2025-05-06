#ifndef _FPGA_AUDIO_H
#define _FPGA_AUDIO_H

#include <linux/ioctl.h>

typedef struct {
  unsigned char play;
} fpga_audio_arg_t;

#define FPGA_AUDIO_MAGIC 'a'

/* ioctls and their arguments */
#define FPGA_AUDIO_BGM_STARTSTOP _IOW(FPGA_AUDIO_MAGIC, 1, fpga_audio_arg_t *)
#define FPGA_AUDIO_SET_AUDIO_ADDR _IOW(FPGA_AUDIO_MAGIC, 2, fpga_audio_arg_t *)

#endif
