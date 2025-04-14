#!/bin/sh
rmmod vga_bird
rmmod aud 
make clean
make
insmod vga_bird.ko
insmod aud.ko
