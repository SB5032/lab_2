#ifndef _VGA_BIRD_H_
#define _VGA_BIRD_H_

#include <linux/ioctl.h>
#include "vga_bird.h"
#include "aud.h"

void send_sound(const aud_mem_t *c, const int aud_fd);



int get_aud_data(const int aud_fd);

int get_button_value(const int aud_fd);

void send_sprite_positions(const vga_bird_data_t *c, const int vga_bird_fd);

void send_score(const vga_bird_data_t *c, const int vga_bird_fd);
void send_combo(const vga_bird_data_t *c, const int vga_bird_fd);

#endif
