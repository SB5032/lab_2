// screamjump_dynamic.c
// 4 incoming platforms, must land on tower first or die, then chain platforms.

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
#define JUMP_VY      -12
#define GRAVITY        +1

// ───── MIF indices ───────────────────────────────────────────────────────────
#define CHICKEN_STAND   8    // your chicken standing tile
#define CHICKEN_JUMP   11    // your chicken jumping tile
#define PLATFORM_SPRITE_IDX 14// your platform graphic

// ───── static tower ──────────────────────────────────────────────────────────
#define TOWER_X         16
#define TOWER_WIDTH     32    // one 32×32 sprite wide
#define TOWER_HEIGHT    3     // stacked 3 sprites tall
#define TOWER_REG_BASE  0     // uses sprite regs 0,1,2
#define TOWER_BASE_Y   (WIDTH - WALL - PLATFORM_H)

// ───── moving platforms ──────────────────────────────────────────────────────
#define MAX_PLATFORMS   4
#define PLATFORM_W    (4*32)  // 4 sprites of 32px each
#define PLATFORM_H     32
#define PLATFORM_SPEED 2
// spawn every 640/4 = 160px
#define PLATFORM_SPACING  (LENGTH / MAX_PLATFORMS)
// platform regs start at 3 → uses regs 3…(3+4*MAX_PLATFORMS-1)
#define PLATFORM_REG_BASE 3

// ───── chicken sprite reg ───────────────────────────────────────────────────
#define CHICKEN_REG   (TOWER_REG_BASE + TOWER_HEIGHT + PLATFORM_REG_BASE + MAX_PLATFORMS*4)
// that equals 0+3 + 3 + 16 = 22 or pick any free reg; just ensure no overlap

// ───── lives/score & controller ──────────────────────────────────────────────
#define INITIAL_LIVES   5

static int vga_fd, audio_fd;
static struct controller_output_packet controller_state;
static bool towerEnabled = true;  // show & enforce tower only until you land on a platform

typedef struct {
    int x, y;
    int vy;
    bool jumping;
} Chicken;

typedef struct {
    int x, y;
} Platform;


// ───── USB controller thread (unchanged) ─────────────────────────────────────
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
    c->y = TOWER_BASE_Y - CHICKEN_H * TOWER_HEIGHT; // atop the top‐sprite
    c->vy = 0;
    c->jumping = false;
}

// only freeze while standing on tower
void moveChicken(Chicken *c) {
    if (!c->jumping && towerEnabled) return;
    c->y  += c->vy;
    c->vy += GRAVITY;
}


int main(void) {
    // open VGA & audio
    if ((vga_fd   = open("/dev/vga_top",    O_RDWR)) < 0)   return -1;
    if ((audio_fd = open("/dev/fpga_audio", O_RDWR)) < 0)   return -1;

    // start reading controller
    pthread_t tid;
    pthread_create(&tid, NULL, controller_input_thread, NULL);

    cleartiles();
    clearSprites();

    int score = 0, lives = INITIAL_LIVES;
    write_score(score);
    write_number(lives, 0, 0);

    // seed platforms just off the right edge
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
            play_sfx(/*your jump SFX idx*/ 0);
        }

        int prevY = chicken.y;
        moveChicken(&chicken);

        // slide & respawn platforms with slight height jitter
        for (int i = 0; i < MAX_PLATFORMS; i++) {
            plats[i].x -= PLATFORM_SPEED;
            if (plats[i].x < -PLATFORM_W) {
                // find rightmost X
                int maxX = plats[0].x;
                for (int j = 1; j < MAX_PLATFORMS; j++)
                    if (plats[j].x > maxX) maxX = plats[j].x;
                plats[i].x = maxX + PLATFORM_SPACING;
                // small variation around previous height
                int prevIdx = (i + MAX_PLATFORMS - 1) % MAX_PLATFORMS;
                int delta   = (rand() % PLATFORM_H) - (PLATFORM_H/2);
                int newY    = plats[prevIdx].y + delta;
                if (newY < WALL) newY = WALL;
                if (newY > WIDTH - WALL - PLATFORM_H)
                    newY = WIDTH - WALL - PLATFORM_H;
                plats[i].y = newY;
            }
        }

        // ─── landing on the static tower? ───────────────────────────────
        if (towerEnabled && chicken.vy > 0) {
            int botPrev = prevY + CHICKEN_H;
            int botNow  = chicken.y  + CHICKEN_H;
            // did we cross the tower’s top edge?
            if (botPrev <= TOWER_BASE_Y && botNow >= TOWER_BASE_Y) {
                // x‐overlap?
                if (chicken.x + CHICKEN_W  > TOWER_X &&
                     chicken.x           < TOWER_X + TOWER_WIDTH) {
                    // landed safely on tower
                    chicken.y       = TOWER_BASE_Y - CHICKEN_H;
                    chicken.vy      = 0;
                    chicken.jumping = false;
                } else {
                    // missed the tower → instant death
                    lives--;
                    write_number(lives, 0, 0);
                    initChicken(&chicken);
                    usleep(300000);
                    continue;
                }
            }
        }

        // ─── landing on any platform? ─────────────────────────────────
        if (chicken.vy > 0) {
            for (int i = 0; i < MAX_PLATFORMS; i++) {
                int botPrev = prevY + CHICKEN_H;
                int botNow  = chicken.y  + CHICKEN_H;
                if (botPrev <= plats[i].y && botNow >= plats[i].y &&
                    (chicken.x + CHICKEN_W) > plats[i].x &&
                     chicken.x < (plats[i].x + PLATFORM_W)) {
                    // snap, score, disable the tower forever
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

        // ─── fell off bottom? ───────────────────────────────────────────
        if (chicken.y > WIDTH) {
            lives--;
            write_number(lives, 0, 0);
            initChicken(&chicken);
            usleep(300000);
            continue;
        }

        // ─── redraw all sprites ────────────────────────────────────────
        clearSprites();

        // 1) tower (regs 0–2)
        if (towerEnabled) {
            for (int i = 0; i < TOWER_HEIGHT; i++) {
                write_sprite_to_kernel(
                    1,
                    TOWER_BASE_Y - i*PLATFORM_H,
                    TOWER_X,
                    PLATFORM_SPRITE_IDX,
                    TOWER_REG_BASE + i
                );
            }
        }

        // 2) platforms (regs 3–...)
        for (int i = 0; i < MAX_PLATFORMS; i++) {
            for (int k = 0; k < 4; k++) {
                write_sprite_to_kernel(
                    1,
                    plats[i].y,
                    plats[i].x + k*32,
                    PLATFORM_SPRITE_IDX,
                    PLATFORM_REG_BASE + i*4 + k
                );
            }
        }

        // 3) chicken (reg 19)
        write_sprite_to_kernel(
            1,
            chicken.y,
            chicken.x,
            chicken.jumping ? CHICKEN_JUMP : CHICKEN_STAND,
            CHICKEN_REG
        );

        usleep(16666);  // ~60 Hz
    }

    // ─── game over ────────────────────────────────────────────────────
    cleartiles();
    clearSprites();
    write_text((unsigned char*)"gameover", 8, 12, 16);
    sleep(2);
    return 0;
}
