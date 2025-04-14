#ifndef _AUD_H
#define _AUD_H

#include <linux/ioctl.h>

// recieve
// typedef struct {
// 	int data;
// } aud_amp_t;

typedef struct {
	int data;
	int sound;

  int button_value;
} aud_mem_t;

typedef struct {
//	aud_amp_t audio;
	aud_mem_t memory;
} aud_arg_t;

typedef struct {
  int data;
  int sound;
  int button_value;
} mem;


#define AUD_MAGIC 'q'

/* ioctls and their arguments */
#define AUD_READ_DATA  	    _IOR(AUD_MAGIC, 1, aud_arg_t *)
#define AUD_READ_BUTTON     _IOR(AUD_MAGIC, 2, aud_arg_t *)
#define AUD_WRITE_SOUND     _IOW(AUD_MAGIC, 3, aud_arg_t *)

#endif
