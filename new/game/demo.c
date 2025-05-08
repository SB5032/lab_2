// screamjump_dynamic.c
// 4 incoming platforms, must land on tower first or die, then chain platforms.
// Added: start screen, bottom border only (fixed to bottom row).

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
#define JUMP_VY      -12
#define GRAVITY        +1

// ───── sprite dimensions ─────────────────────────────────────────────────────
#define CHICKEN_W      32
#define CHICKEN_H      32
#define TILE           CHICKEN_H

// ───── MIF indices ───────────────────────────────────────────────────────────
#define CHICKEN_STAND   8    // chicken standing tile
#define CHICKEN_JUMP   11    // chicken jumping tile
#define PLATFORM_SPRITE_IDX 14// platform graphic
#define WALL_TILE_IDX    1   // bottom-wall tile index in the MIF

// ───── moving platforms ──────────────────────────────────────────────────────
#define MAX_PLATFORMS   4
#define PLATFORM_W    (4*TILE)  // 4 sprites × TILE px
#define PLATFORM_H      TILE
#define PLATFORM_SPEED   2
#define PLATFORM_SPACING (LENGTH / MAX_PLATFORMS)
#define PLATFORM_REG_BASE 3     // regs 3…(3+4*MAX_PLATFORMS-1)

// ───── static tower ──────────────────────────────────────────────────────────
#define TOWER_X         16
#define TOWER_HEIGHT    3       // stacked 3 sprites
#define TOWER_REG_BASE  0       // uses regs 0,1,2
#define TOWER_BASE_Y   (WIDTH - TILE * TOWER_HEIGHT)
#define TOWER_WIDTH     CHICKEN_W

// ───── bottom border tile map ───────────────────────────────────────────────
#define BOTTOM_TILE_ROW  ((WIDTH / TILE) - 1)  // last tile row (0-indexed)
#define NUM_TILE_COLS    (LENGTH / TILE)

// ───── chicken sprite reg ───────────────────────────────────────────────────
#define CHICKEN_REG     (TOWER_REG_BASE + TOWER_HEIGHT + PLATFORM_REG_BASE + MAX_PLATFORMS*4)

// ───── lives/score & controller ──────────────────────────────────────────────
#define INITIAL_LIVES    5

int vga_fd, audio_fd;
struct controller_output_packet controller_state;
bool towerEnabled = true;

typedef struct { int row, col, tile; } Wall;
typedef struct { int x, y, vy; bool jumping; } Chicken;
typedef struct { int x, y; } Platform;

// ───── Draw only bottom border ───────────────────────────────────────────────
void drawBottomBorder(void) {
    for (int col = 0; col < NUM_TILE_COLS; col++) {
        write_tile_to_kernel(BOTTOM_TILE_ROW, col, WALL_TILE_IDX);
    }
}

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
    c->x = TOWER_X;
    c->y = TOWER_BASE_Y - CHICKEN_H;
    c->vy = 0;
    c->jumping = false;
}

void moveChicken(Chicken *c) {
    if (!c->jumping && towerEnabled) return;
    c->y  += c->vy;
    c->vy += GRAVITY;
}

int main(void) {
    if ((vga_fd   = open("/dev/vga_top",    O_RDWR)) < 0) return -1;
    if ((audio_fd = open("/dev/fpga_audio", O_RDWR)) < 0) return -1;

    pthread_t tid;
    pthread_create(&tid, NULL, controller_input_thread, NULL);

    // ── start screen ──────────────────────────────────────────────────────────
    cleartiles(); clearSprites();
    write_text((unsigned char*)"ScreamJump Chicken", 18, 10, 8);
    write_text((unsigned char*)"Press any key to start", 22, 12, 16);
    while (!(controller_state.a || controller_state.b || controller_state.x ||
             controller_state.y || controller_state.start || controller_state.select ||
             controller_state.updown || controller_state.leftright)) {
        usleep(10000);
    }

    // ── initialize game ───────────────────────────────────────────────────────
    cleartiles(); clearSprites();
    drawBottomBorder();

    int score = 0, lives = INITIAL_LIVES;
    write_score(score); write_number(lives, 0, 0);

    srand(time(NULL));
    Platform plats[MAX_PLATFORMS];
    for (int i = 0; i < MAX_PLATFORMS; i++) {
        plats[i].x = LENGTH + i * PLATFORM_SPACING;
        plats[i].y = rand() % (WIDTH - 2*CHICKEN_H - PLATFORM_H) + CHICKEN_H;
    }

    Chicken chicken; initChicken(&chicken);

    while (lives > 0) {
        if (controller_state.b && !chicken.jumping) {
            chicken.vy = JUMP_VY; chicken.jumping = true; play_sfx(0);
        }
        int prevY = chicken.y;
        moveChicken(&chicken);

        for (int i = 0; i < MAX_PLATFORMS; i++) {
            plats[i].x -= PLATFORM_SPEED;
            if (plats[i].x < -PLATFORM_W) {
                int maxX = plats[0].x;
                for (int j = 1; j < MAX_PLATFORMS; j++) if (plats[j].x > maxX) maxX = plats[j].x;
                plats[i].x = maxX + PLATFORM_SPACING;
                int prevIdx = (i + MAX_PLATFORMS - 1) % MAX_PLATFORMS;
                int delta = (rand() % PLATFORM_H) - (PLATFORM_H/2);
                int newY = plats[prevIdx].y + delta;
                if (newY < CHICKEN_H) newY = CHICKEN_H;
                if (newY > WIDTH - CHICKEN_H - PLATFORM_H) newY = WIDTH - CHICKEN_H - PLATFORM_H;
                plats[i].y = newY;
            }
        }

        // landing on tower
        if (towerEnabled && chicken.vy > 0) {
            int botPrev = prevY + CHICKEN_H;
            int botNow  = chicken.y + CHICKEN_H;
            if (botPrev <= TOWER_BASE_Y && botNow >= TOWER_BASE_Y &&
                chicken.x + CHICKEN_W > TOWER_X && chicken.x < TOWER_X + TOWER_WIDTH) {
                chicken.y = TOWER_BASE_Y - CHICKEN_H; chicken.vy = 0; chicken.jumping = false;
            } else if (botPrev <= TOWER_BASE_Y && chicken.y > TOWER_BASE_Y) {
                lives--; write_number(lives,0,0); initChicken(&chicken); usleep(300000);
                continue;
            }
        }

        // landing on platforms
        if (chicken.vy > 0) {
            for (int i = 0; i < MAX_PLATFORMS; i++) {
                int botPrev = prevY + CHICKEN_H;
                int botNow  = chicken.y + CHICKEN_H;
                if (botPrev <= plats[i].y && botNow >= plats[i].y &&
                    chicken.x + CHICKEN_W > plats[i].x && chicken.x < plats[i].x + PLATFORM_W) {
                    chicken.y = plats[i].y - CHICKEN_H;
                    chicken.vy = 0; chicken.jumping = false;
                    score++; write_score(score); towerEnabled = false; break;
                }
            }
        }

        if (chicken.y > WIDTH) {
            lives--; write_number(lives,0,0); initChicken(&chicken); usleep(300000); continue;
        }

        clearSprites();
        // draw tower
        if (towerEnabled) {
            for (int i = 0; i < TOWER_HEIGHT; i++) {
                write_sprite_to_kernel(1,
                    TOWER_BASE_Y - i*PLATFORM_H,
                    TOWER_X,
                    PLATFORM_SPRITE_IDX,
                    TOWER_REG_BASE + i);
            }
        }
        // draw platforms
        for (int i = 0; i < MAX_PLATFORMS; i++)
            for (int k = 0; k < 4; k++)
                write_sprite_to_kernel(1, plats[i].y, plats[i].x + k*TILE,
                    PLATFORM_SPRITE_IDX, PLATFORM_REG_BASE + i*4 + k);
        // draw chicken
        write_sprite_to_kernel(1, chicken.y, chicken.x,
            chicken.jumping ? CHICKEN_JUMP : CHICKEN_STAND, CHICKEN_REG);

        usleep(16666);
    }

    cleartiles(); clearSprites(); write_text((unsigned char*)"gameover", 8, 12, 16); sleep(2);
    return 0;
}
