// screamjump_platforms.c
// A ScreamJump Chicken demo with moving platforms, lives & score.
// Replace your old demo.c with this.

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <fcntl.h>
#include "usbcontroller.h"
#include "vga_interface.h"
#include "audio_interface.h"

// screen dimensions
#define LENGTH        640
#define WIDTH         480
#define WALL           16

// chicken sprite
#define CHICKEN_REG   11      // sprite register
#define CHICKEN_W     32
#define CHICKEN_H     32
#define CHICKEN_STAND  8      // tile index in your .mif
#define CHICKEN_JUMP   11      // tile index in your .mif
#define JUMP_SFX       0      // audio index for chicken scream

// jump physics
#define JUMP_VY      -12
#define GRAVITY        +1

// moving platforms
#define MAX_PLATFORMS   2
#define PLATFORM_W    (4*32)  // 4 sprites of 32px each
#define PLATFORM_H     32
#define PLATFORM_SPEED  2
// horizontal spacing so they stay evenly apart:
#define PLATFORM_SPACING  ((LENGTH + PLATFORM_W) / MAX_PLATFORMS)
#define PLATFORM_SPRITE_IDX  14  // your .mif index for platform graphic

// game state
static int vga_fd, audio_fd;
static struct controller_output_packet controller_state;

typedef struct {
    int x, y;
    bool jumping;  // are we in the air?
} Chicken;
typedef struct {
    int x, y;
} Platform;

// chicken functions
void initChicken(Chicken *c) {
    c->x = LENGTH/2 - CHICKEN_W/2;
    c->y = WIDTH - WALL - CHICKEN_H;
    c->vy = 0;
    c->jumping = false;
}
void moveChicken(Chicken *c) {
    c->y += c->vy;
    c->vy += GRAVITY;
}

// controller thread (unchanged)
void *controller_input_thread(void *arg) {
    uint8_t ep;
    struct libusb_device_handle *ctrl = opencontroller(&ep);
    if (!ctrl) pthread_exit(NULL);
    while (1) {
        unsigned char buf[GAMEPAD_READ_LENGTH];
        int transferred;
        if (libusb_interrupt_transfer(ctrl, ep, buf, GAMEPAD_READ_LENGTH, &transferred, 0) == 0) {
            usb_to_output(&controller_state, buf);
        }
    }
    libusb_close(ctrl);
    libusb_exit(NULL);
    pthread_exit(NULL);
}

int main(void) {
    // open devices
    if ((vga_fd   = open("/dev/vga_top",    O_RDWR)) < 0) return -1;
    if ((audio_fd = open("/dev/fpga_audio", O_RDWR)) < 0) return -1;

    // start controller reader
    pthread_t tid;
    pthread_create(&tid, NULL, controller_input_thread, NULL);

    // clear screen & sprites
    cleartiles();
    clearSprites();

    // initialize score & lives display
    int score = 0, lives = 3;
    write_score(score);
    write_number(lives, 0, 0);

    // set up platforms
    srand(time(NULL));
    Platform plats[MAX_PLATFORMS];
    for (int i = 0; i < MAX_PLATFORMS; i++) {
        plats[i].x = LENGTH + i * PLATFORM_SPACING;
        plats[i].y = rand() % (WIDTH - 2*WALL - PLATFORM_H) + WALL;
    }

    // init chicken
    Chicken chicken;
    initChicken(&chicken);

    // main game loop: runs until you lose all lives
    while (lives > 0) {
        // jump button
        if (controller_state.a && !chicken.jumping) {
            chicken.vy      = JUMP_VY;
            chicken.jumping = true;
            play_sfx(JUMP_SFX);
        }

        // save prev position for collision check
        int prevY = chicken.y;
        moveChicken(&chicken);

        // update & recycle platforms for even spacing
        for (int i = 0; i < MAX_PLATFORMS; i++) {
            plats[i].x -= PLATFORM_SPEED;
            if (plats[i].x < -PLATFORM_W) {
                // find current rightmost
                int maxX = plats[0].x;
                for (int j = 1; j < MAX_PLATFORMS; j++)
                    if (plats[j].x > maxX) maxX = plats[j].x;
                plats[i].x = maxX + PLATFORM_SPACING;
                plats[i].y = rand() % (WIDTH - 2*WALL - PLATFORM_H) + WALL;
            }
        }

        // collision: landing on a platform
        if (chicken.vy > 0) {
            int botPrev = prevY + CHICKEN_H;
            int botNow  = chicken.y  + CHICKEN_H;
            for (int i = 0; i < MAX_PLATFORMS; i++) {
                if (botPrev <= plats[i].y && botNow >= plats[i].y &&
                    (chicken.x + CHICKEN_W) > plats[i].x &&
                     chicken.x < (plats[i].x + PLATFORM_W)) {
                    // landed
                    chicken.y       = plats[i].y - CHICKEN_H;
                    chicken.vy      = 0;
                    chicken.jumping = false;
                    score++;
                    write_score(score);
                }
            }
        }

        // death: fall off bottom
        if (chicken.y > WIDTH) {
            lives--;
            write_number(lives, 0, 0);
            // reset chicken to top
            initChicken(&chicken);
            // short pause
            usleep(500000);
            continue;
        }

        // redraw all sprites
        clearSprites();
        // draw platforms
        for (int i = 0; i < MAX_PLATFORMS; i++) {
            for (int k = 0; k < 4; k++) {
                int reg = i*4 + k;
                write_sprite_to_kernel(
                    1,
                    plats[i].y,
                    plats[i].x + k*32,
                    PLATFORM_SPRITE_IDX,
                    reg
                );
            }
        }
        // draw chicken
        write_sprite_to_kernel(
            1,
            chicken.y,
            chicken.x,
            chicken.jumping ? CHICKEN_JUMP : CHICKEN_STAND,
            CHICKEN_REG
        );

        // ~60 Hz
        usleep(16666);
    }

    // game over screen
    cleartiles();
    clearSprites();
    write_text((unsigned char*)"gameover", 8, 12, 16);
    sleep(3);

    return 0;
}
