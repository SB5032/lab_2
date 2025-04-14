#include <stdio.h>
#include "interfaces.h"
#include "vga_bird.h"
#include "aud.h"
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

void send_sound(const aud_mem_t *c, const int aud_fd) {
	aud_arg_t amt;
	amt.memory = *c;
	if (ioctl(aud_fd, AUD_WRITE_SOUND, &amt)) {
		perror("ioctl(AUD_WRITE_SOUND) failed");
		return;
	}
}

int get_aud_data(const int aud_fd) {
	aud_arg_t aat;
	if (ioctl(aud_fd, AUD_READ_DATA, &aat)) {
		perror("ioctl(AUD_READ_DATA) failed");
		return 0;
	}
	return aat.memory.data;
}

int get_button_value(const int aud_fd) {
	aud_arg_t aat;
	if (ioctl(aud_fd, AUD_READ_BUTTON, &aat)) {
		perror("ioctl(AUD_READ_BUTTON) failed");
		return 0;
	}
	return aat.memory.button_value;
}

void send_sprite_positions(const vga_bird_data_t *c, const int vga_bird_fd) {
	vga_bird_arg_t vzat;
	vzat.packet = *c;
	if (ioctl(vga_bird_fd, VGA_BIRD_WRITE_PACKET, &vzat)) {
		perror("ioctl(VGA_BIRD_WRITE_PACKET) failed");
		return;
	}
}
void send_score(const vga_bird_data_t *c, const int vga_bird_fd) {
	vga_bird_arg_t vzat;
	vzat.packet = *c;
	if (ioctl(vga_bird_fd, VGA_BIRD_WRITE_SCORE, &vzat)) {
		perror("ioctl(VGA_BIRD_WRITE_SCORE) failed");
		return;
	}
}
void send_combo(const vga_bird_data_t *c, const int vga_bird_fd) {
	vga_bird_arg_t vzat;
	vzat.packet = *c;
	if (ioctl(vga_bird_fd, VGA_BIRD_WRITE_COMBO, &vzat)) {
		perror("ioctl(VGA_BIRD_WRITE_COMBO) failed");
		return;
	}
}
