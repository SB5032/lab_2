// screamjump_dynamic_start.c
// Adds a start screen: displays title and "Press any key to start"

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

// ───── screen & physics ──────────────────────────────────────────────────────
#define LENGTH        640   // VGA width
#define WIDTH         480   // VGA height
#define WALL           16   // top/bottom margin
#define JUMP_VY      -20
#define GRAVITY        +1

// ───── sprite dimensions ─────────────────────────────────────────────────────
#define CHICKEN_W      32
#define CHICKEN_H      32

// ───── MIF indices ───────────────────────────────────────────────────────────
#define CHICKEN_STAND   8    // chicken standing tile
#define CHICKEN_JUMP   11    // chicken jumping tile
#define PLATFORM_SPRITE_IDX 14// platform graphic
#define TOWER_TILE_IDX  22    // static tower tile

// ───── moving platforms ──────────────────────────────────────────────────────
#define MAX_PLATFORMS   3 //4
#define PLATFORM_W    (4*32)  // 4 sprites × 32px
#define PLATFORM_H     32
#define PLATFORM_SPEED 2
#define PLATFORM_SPACING (LENGTH / MAX_PLATFORMS)
#define PLATFORM_REG_BASE 1   // regs 3…(3+4*MAX_PLATFORMS-1)

// ───── static tower ──────────────────────────────────────────────────────────
#define TOWER_X         16
#define TOWER_WIDTH     CHICKEN_W
#define TOWER_HEIGHT    3     // stacked 3 sprites high originally
#define TOWER_BASE_Y   (WIDTH - WALL - PLATFORM_H)

// ───── chicken sprite reg ───────────────────────────────────────────────────
#define CHICKEN_REG (/*unused now*/)  /* was: TOWER_REG_BASE + TOWER_HEIGHT + PLATFORM_REG_BASE + MAX_PLATFORMS*4 */

// ───── lives/score & controller ──────────────────────────────────────────────
#define INITIAL_LIVES   5

int vga_fd, audio_fd;
struct controller_output_packet controller_state;
bool towerEnabled = true;  // enforce tower until first platform landing

typedef struct {
    int x, y, vy;
    bool jumping;
} Chicken;
typedef struct {
    int x, y;
} Platform;

// ───── USB controller thread ─────────────────────────────────────────────────
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

// ───── chicken init & physics ────────────────────────────────────────────────
void initChicken(Chicken *c) {
 //   c->x = TOWER_X;
      c->x = 32;
   // c->y = TOWER_BASE_Y - CHICKEN_H * TOWER_HEIGHT; // atop the tower
    c->y = 304; // atop the tower
    c->vy = 0;
    c->jumping = false;
}

// freeze on tower until jump, otherwise physics apply
void moveChicken(Chicken *c) {
    if (!c->jumping && towerEnabled) return;
    c->y  += c->vy;
    c->vy += GRAVITY;
}

int main(void) {
    // open VGA & audio
    if ((vga_fd   = open("/dev/vga_top",    O_RDWR)) < 0)   return -1;
    if ((audio_fd = open("/dev/fpga_audio", O_RDWR)) < 0)   return -1;

    // start controller thread
    pthread_t tid;
    pthread_create(&tid, NULL, controller_input_thread, NULL);

    // ───── START SCREEN ──────────────────────────────────────────────────────
    cleartiles();
    clearSprites();
    int index = 8;
        write_text("scream", 6, 13, 13);
        write_text("jump", 4, 13, 20);
        write_text("press", 5, 19, index);
        write_text("any", 3, 19, index + 6);
        write_text("key", 3, 19, index + 10);
        write_text("to", 2, 19, index + 14);
        write_text("start", 5, 19, index + 17);
    // write_text((unsigned char*)"ScreamJump Chicken", 18, 10, 8);
    // write_text((unsigned char*)"Press any key to start", 22, 12, 16);
    while (!(
        controller_state.a || controller_state.b || controller_state.x ||
        controller_state.y || controller_state.start || controller_state.select ||
        controller_state.updown || controller_state.leftright)) {
        usleep(10000);
    }

    // ───── INITIALIZATION ─────────────────────────────────────────────────────
    cleartiles();
    clearSprites();

    int score = 0, lives = INITIAL_LIVES;
    write_score(score);
    write_number(lives, 0, 0);

    srand(time(NULL));
    Platform plats[MAX_PLATFORMS];
    for (int i = 0; i < MAX_PLATFORMS; i++) {
        plats[i].x = LENGTH + i * PLATFORM_SPACING;
        plats[i].y = rand() % (WIDTH - 2*WALL - PLATFORM_H) + WALL;
    }

    Chicken chicken;
    initChicken(&chicken);

    // ───── MAIN GAME LOOP ─────────────────────────────────────────────────────
    while (lives > 0) {
        // input & physics
        if (controller_state.b && !chicken.jumping) {
            chicken.vy      = JUMP_VY;
            chicken.jumping = true;
            play_sfx(0);
        }
        int prevY = chicken.y;
        moveChicken(&chicken);

        // move & respawn platforms
        for (int i = 0; i < MAX_PLATFORMS; i++) {
            plats[i].x -= PLATFORM_SPEED;
            if (plats[i].x < -PLATFORM_W) {
                int maxX = plats[0].x;
                for (int j = 1; j < MAX_PLATFORMS; j++)
                    if (plats[j].x > maxX) maxX = plats[j].x;
                plats[i].x = maxX + PLATFORM_SPACING;
                int prevIdx = (i + MAX_PLATFORMS-1) % MAX_PLATFORMS;
                int delta   = (rand() % PLATFORM_H) - (PLATFORM_H/2);
                int newY    = plats[prevIdx].y + delta;
                if (newY < WALL) newY = WALL;
                if (newY > WIDTH - WALL - PLATFORM_H)
                    newY = WIDTH - WALL - PLATFORM_H;
                plats[i].y = newY;
            }
        }

        // landing on tower?
        // if (towerEnabled && chicken.vy > 0) {
        //     int botPrev = prevY + CHICKEN_H;
        //     int botNow  = chicken.y  + CHICKEN_H;
        //     if (botPrev <= TOWER_BASE_Y && botNow >= TOWER_BASE_Y &&
        //         chicken.x + CHICKEN_W > TOWER_X &&
        //         chicken.x < TOWER_X + TOWER_WIDTH) {

        //         chicken.y       = TOWER_BASE_Y - CHICKEN_H;
        //         chicken.vy      = 0;
        //         chicken.jumping = false;
        //     } else if (botPrev <= TOWER_BASE_Y && chicken.y > TOWER_BASE_Y) {
        //         lives--;
        //         write_number(lives, 0, 0);
        //         initChicken(&chicken);
        //         usleep(300000);
        //         continue;
        //     }
        // }

        // landing on platforms?
        if (chicken.vy > 0) {
			towerEnabled = false;
            for (int i = 0; i < MAX_PLATFORMS; i++) {
                int botPrev = prevY + CHICKEN_H;
                int botNow  = chicken.y  + CHICKEN_H;
                if (botPrev <= plats[i].y && botNow >= plats[i].y &&
                    chicken.x + CHICKEN_W > plats[i].x &&
                    chicken.x < plats[i].x + PLATFORM_W) {

                    chicken.y       = plats[i].y - CHICKEN_H;
                    chicken.vy      = 0;
                    chicken.jumping = false;
                    score++;
                    write_score(score);
                    towerEnabled = false;
                    break;
                }
            }
        }

        // fell off bottom?
        if (chicken.y > WIDTH) {
            lives--;
            write_number(lives, 0, 0);
            initChicken(&chicken);
            usleep(300000);
            continue;
        }

        // redraw
        clearSprites();

        // tower (tile-based)
        {
            int rowStart = (TOWER_BASE_Y - TOWER_HEIGHT * PLATFORM_H) / 16;
            int rowEnd   = TOWER_BASE_Y / 16;
            int colStart = TOWER_X / 16;
            int colEnd   = (TOWER_X + TOWER_WIDTH) / 16;
            for (int r = 21; r < 30; ++r) {
                for (int c = 0; c < 5; ++c) {
                    write_tile_to_kernel(r, c,
                        towerEnabled ? TOWER_TILE_IDX : 0);
                }
            }
        }

        // platforms
        for (int i = 0; i < MAX_PLATFORMS; i++) {
            for (int k = 0; k < 4; k++) {
                write_sprite_to_kernel(
                    1, plats[i].y, plats[i].x + k*32,
                    PLATFORM_SPRITE_IDX,
                    PLATFORM_REG_BASE + i*4 + k
                );
            }
        }

        // chicken
        write_sprite_to_kernel(
            1, chicken.y, chicken.x,
            chicken.jumping ? CHICKEN_JUMP : CHICKEN_STAND,
            /* new reg */  0
        );

        usleep(16666); // ~60Hz
    }

    // game over
    cleartiles(); clearSprites();
    write_text((unsigned char*)"gameover",8,12,16);
    sleep(2);
    return 0;
}
