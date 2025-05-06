// screamjump_with_tower_once.c
// ScreamJump Chicken: static 3-sprite tower only on the very first screen.

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

// screen dims
#define LENGTH        640
#define WIDTH         480
#define WALL           16

// chicken sprite
#define CHICKEN_REG    11
#define CHICKEN_W      32
#define CHICKEN_H      32
#define CHICKEN_STAND  8      // tile index in your .mif
#define CHICKEN_JUMP   11      // tile index in your .mif
#define JUMP_SFX        0

// jump physics
#define JUMP_VY      -12
#define GRAVITY        +1

// moving platforms
#define MAX_PLATFORMS   2
#define PLATFORM_W    (4*32)
#define PLATFORM_H     32
#define PLATFORM_SPEED  2
#define PLATFORM_SPACING ((LENGTH + PLATFORM_W)/MAX_PLATFORMS)
#define PLATFORM_SPRITE_IDX 14

// static tower
#define TOWER_X               16
#define TOWER_HEIGHT_SPRITES  3
#define TOWER_REG_BASE        8
#define TOWER_BASE_Y  (WIDTH - WALL - PLATFORM_H)

// initial lives
#define INITIAL_LIVES    5

// global state
int vga_fd, audio_fd;
struct controller_output_packet controller_state;
bool towerEnabled = true;     // ← only draw tower while true

typedef struct {
    int x, y;
    int vy;
    bool jumping;
} Chicken;
typedef struct {
    int x, y;
} Platform;

// controller thread (unchanged)
void *controller_input_thread(void *arg) {
    uint8_t ep;
    struct libusb_device_handle *ctrl = opencontroller(&ep);
    if (!ctrl) pthread_exit(NULL);
    while (1) {
        unsigned char buf[GAMEPAD_READ_LENGTH];
        int transferred;
        if (libusb_interrupt_transfer(ctrl, ep, buf, GAMEPAD_READ_LENGTH, &transferred, 0) == 0)
            usb_to_output(&controller_state, buf);
    }
    libusb_close(ctrl);
    libusb_exit(NULL);
    pthread_exit(NULL);
}

// init chicken on top of tower
void initChicken(Chicken *c) {
    c->x = TOWER_X;
    c->y = TOWER_BASE_Y - CHICKEN_H * TOWER_HEIGHT_SPRITES;
    c->vy = 0;
    c->jumping = false;
}

void moveChicken(Chicken *c) {
    c->y  += c->vy;
    c->vy += GRAVITY;
}

int main(void) {
    if ((vga_fd   = open("/dev/vga_top",    O_RDWR)) < 0) return -1;
    if ((audio_fd = open("/dev/fpga_audio", O_RDWR)) < 0) return -1;

    pthread_t tid;
    pthread_create(&tid, NULL, controller_input_thread, NULL);

    cleartiles();
    clearSprites();

    int score = 0, lives = INITIAL_LIVES;
    write_score(score);
    write_number(lives, 0, 0);

    // seed moving platforms
    srand(time(NULL));
    Platform plats[MAX_PLATFORMS];
    for (int i = 0; i < MAX_PLATFORMS; i++) {
        plats[i].x = LENGTH + i * PLATFORM_SPACING;
        plats[i].y = rand() % (WIDTH - 2*WALL - PLATFORM_H) + WALL;
    }

    Chicken chicken;
    initChicken(&chicken);

    while (lives > 0) {
        // jump on B
        if (controller_state.b && !chicken.jumping) {
            chicken.vy      = JUMP_VY;
            chicken.jumping = true;
            play_sfx(JUMP_SFX);
        }

        int prevY = chicken.y;
        moveChicken(&chicken);

        // move & recycle platforms
        for (int i = 0; i < MAX_PLATFORMS; i++) {
            plats[i].x -= PLATFORM_SPEED;
            if (plats[i].x < -PLATFORM_W) {
                int maxX = plats[0].x;
                for (int j = 1; j < MAX_PLATFORMS; j++)
                    if (plats[j].x > maxX) maxX = plats[j].x;
                plats[i].x = maxX + PLATFORM_SPACING;
                plats[i].y = rand() % (WIDTH - 2*WALL - PLATFORM_H) + WALL;
            }
        }

        // landing on a moving platform
        if (chicken.vy > 0) {
            int botPrev = prevY + CHICKEN_H;
            int botNow  = chicken.y  + CHICKEN_H;
            for (int i = 0; i < MAX_PLATFORMS; i++) {
                if (botPrev <= plats[i].y &&
                    botNow  >= plats[i].y &&
                    (chicken.x + CHICKEN_W) > plats[i].x &&
                    chicken.x  < (plats[i].x + PLATFORM_W)) {
                    // landed
                    chicken.y       = plats[i].y - CHICKEN_H;
                    chicken.vy      = 0;
                    chicken.jumping = false;
                    score++;
                    write_score(score);
                    // ** first landing: disable tower forever **
                    if (towerEnabled) towerEnabled = false;
                }
            }
        }

        // death?
        if (chicken.y > WIDTH) {
            lives--;
            write_number(lives, 0, 0);
            initChicken(&chicken);
            usleep(500000);
            continue;
        }

        // redraw
        clearSprites();

        // 1) draw tower only if still enabled
        if (towerEnabled) {
            for (int i = 0; i < TOWER_HEIGHT_SPRITES; i++) {
                write_sprite_to_kernel(
                    1,
                    TOWER_BASE_Y - i*PLATFORM_H,
                    TOWER_X,
                    PLATFORM_SPRITE_IDX,
                    TOWER_REG_BASE + i
                );
            }
        }

        // 2) moving platforms (regs 0–7)
        for (int i = 0; i < MAX_PLATFORMS; i++) {
            for (int k = 0; k < 4; k++) {
                write_sprite_to_kernel(
                    1,
                    plats[i].y,
                    plats[i].x + k*32,
                    PLATFORM_SPRITE_IDX,
                    i*4 + k
                );
            }
        }

        // 3) chicken (reg 11)
        write_sprite_to_kernel(
            1,
            chicken.y,
            chicken.x,
            chicken.jumping ? CHICKEN_JUMP : CHICKEN_STAND,
            CHICKEN_REG
        );

        usleep(16666);  // ~60 Hz
    }

    // game over
    cleartiles();
    clearSprites();
    write_text((unsigned char*)"gameover", 8, 12, 16);
    sleep(3);
    return 0;
}
