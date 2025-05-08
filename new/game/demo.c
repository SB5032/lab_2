// screamjump_dynamic.c
// 4 moving platforms, static start platform (tile 20), start screen, one‑time scoring per landing.

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
#define LENGTH           640   // VGA width
#define WIDTH            480   // VGA height
#define WALL              16   // margin
#define JUMP_VY          -12
#define GRAVITY            1

// ───── sprite dimensions & tile indices ─────────────────────────────────────
#define CHICKEN_W         32
#define CHICKEN_H         32
#define CHICKEN_STAND      8   // chicken standing tile index
#define CHICKEN_JUMP      11   // chicken jumping tile index
#define PLATFORM_TILE_IDX 14   // moving platform tile index
#define STATIC_PLAT_TILE  37   // static start platform tile index

// ───── platforms config ─────────────────────────────────────────────────────
#define MAX_PLATFORMS      4
#define PLATFORM_W      (4*32)  // 4 sprites × 32px wide
#define PLATFORM_H         32
#define PLATFORM_SPEED      2
#define PLATFORM_SPACING  (LENGTH / MAX_PLATFORMS)
#define STATIC_PLAT_X      16   // same as old tower X
#define STATIC_PLAT_Y   (WIDTH - WALL - PLATFORM_H)

// ───── sprite registers ─────────────────────────────────────────────────────
#define STATIC_PLAT_REG    0       // reg 0 for static start platform
#define PLATFORM_REG_BASE  1       // regs 1..(1+4*MAX_PLATFORMS-1) for moving
#define CHICKEN_REG      (PLATFORM_REG_BASE + MAX_PLATFORMS*4)  // next free reg

// ───── game parameters ──────────────────────────────────────────────────────
#define INITIAL_LIVES      5
#define SCORE_PER_LAND    10       // +10 per landing

int vga_fd, audio_fd;
struct controller_output_packet controller_state;

typedef struct {int x,y,vy;bool jumping;} Chicken;
typedef struct {int x,y;} Platform;

// ───── USB controller thread ─────────────────────────────────────────────────
void *controller_input_thread(void *arg) {
    uint8_t ep;
    struct libusb_device_handle *ctrl = opencontroller(&ep);
    if (!ctrl) pthread_exit(NULL);
    while (1) {
        unsigned char buf[GAMEPAD_READ_LENGTH]; int t;
        if (!libusb_interrupt_transfer(ctrl, ep, buf, GAMEPAD_READ_LENGTH, &t, 0))
            usb_to_output(&controller_state, buf);
    }
}

// ───── chicken init & physics ────────────────────────────────────────────────
void initChicken(Chicken *c) {
    c->x = STATIC_PLAT_X;
    c->y = STATIC_PLAT_Y - CHICKEN_H;  // stand on static platform
    c->vy = 0;
    c->jumping = false;
}

void applyPhysics(Chicken *c) {
    if (!c->jumping) return;
    c->y += c->vy;
    c->vy += GRAVITY;
}

// ───── main ─────────────────────────────────────────────────────────────────
int main(void) {
    // open VGA & audio
    if ((vga_fd   = open("/dev/vga_top",    O_RDWR)) < 0) return -1;
    if ((audio_fd = open("/dev/fpga_audio", O_RDWR)) < 0) return -1;

    // start controller thread
    pthread_t thr; pthread_create(&thr, NULL, controller_input_thread, NULL);

    // ── start screen ─────────────────────────────────────────────────────────
    cleartiles(); clearSprites();
    write_text((unsigned char*)"Screen Jump Chicken", 18, 10, 8);
    write_text((unsigned char*)"Press any key to start", 22, 12, 16);
    while (!(controller_state.a || controller_state.b ||
             controller_state.x || controller_state.y ||
             controller_state.start || controller_state.select ||
             controller_state.updown || controller_state.leftright)) {
        usleep(10000);
    }

    // ── init game state ──────────────────────────────────────────────────────
    cleartiles(); clearSprites(); srand(time(NULL));
    int score = 0, lives = INITIAL_LIVES;
    write_score(score); write_number(lives, 0, 0);

    // seed moving platforms off screen
    Platform plats[MAX_PLATFORMS];
    for (int i = 0; i < MAX_PLATFORMS; i++) {
        plats[i].x = LENGTH + i * PLATFORM_SPACING;
        plats[i].y = WALL + rand() % (WIDTH - 2*WALL - PLATFORM_H);
    }

    Chicken chick; initChicken(&chick);
    bool wasGrounded = true;

    while (lives > 0) {
        // jump on B
        if (controller_state.b && !chick.jumping) {
            chick.vy = JUMP_VY; chick.jumping = true; play_sfx(0);
        }

        int prevBottom = chick.y + CHICKEN_H;
        applyPhysics(&chick);

        // move & wrap platforms
        for (int i = 0; i < MAX_PLATFORMS; i++) {
            plats[i].x -= PLATFORM_SPEED;
            if (plats[i].x < -PLATFORM_W) {
                int maxX = plats[0].x;
                for (int j = 1; j < MAX_PLATFORMS; j++)
                    if (plats[j].x > maxX) maxX = plats[j].x;
                plats[i].x = maxX + PLATFORM_SPACING;
                plats[i].y = WALL + rand() % (WIDTH - 2*WALL - PLATFORM_H);
            }
        }

        // check moving platform landing
        bool grounded = false;
        for (int i = 0; i < MAX_PLATFORMS; i++) {
            int top = plats[i].y;
            if (prevBottom <= top && (chick.y + CHICKEN_H) >= top &&
                chick.x + CHICKEN_W > plats[i].x &&
                chick.x < plats[i].x + PLATFORM_W) {
                // snap to platform
                chick.y = top - CHICKEN_H;
                chick.vy = 0; chick.jumping = false;
                grounded = true;
                // score on landing transition
                if (!wasGrounded) {
                    score += SCORE_PER_LAND; write_score(score);
                }
                break;
            }
        }
        wasGrounded = grounded;

        // death if off bottom
        if (chick.y > WIDTH) {
            lives--; write_number(lives, 0, 0);
            initChicken(&chick); wasGrounded = true;
            continue;
        }

        // ── redraw ────────────────────────────────────────────────────────
        clearSprites();
        // static start platform (reg 0)
        write_sprite_to_kernel(1,
            STATIC_PLAT_Y,
            STATIC_PLAT_X,
            STATIC_PLAT_TILE,
            STATIC_PLAT_REG);
        // moving platforms (regs 1–...)
        for (int i = 0; i < MAX_PLATFORMS; i++) {
            for (int k = 0; k < 4; k++) {
                write_sprite_to_kernel(1,
                    plats[i].y,
                    plats[i].x + k*32,
                    PLATFORM_TILE_IDX,
                    PLATFORM_REG_BASE + i*4 + k);
            }
        }
        // chicken (last reg)
        write_sprite_to_kernel(1,
            chick.y, chick.x,
            chick.jumping ? CHICKEN_JUMP : CHICKEN_STAND,
            CHICKEN_REG);

        usleep(16666);
    }

    // game over
    cleartiles(); clearSprites();
    write_text((unsigned char*)"gameover", 8, 12, 16);
    sleep(2);
    return 0;
}
