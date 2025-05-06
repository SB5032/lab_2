// screamjump_dynamic_start.c – Tower 3×2, score +10 per landing, 5 tunable platforms
// Copy‑paste ready. Replace your existing C file.

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

// ───── Screen & physics ──────────────────────────────────────────────
#define LENGTH        640
#define WIDTH         480
#define WALL           16
#define JUMP_VY       -12
#define GRAVITY         +1

// ───── Sprite sizes ─────────────────────────────────────────────────
#define TILE           32            // 32×32 tiles
#define CHICKEN_W      TILE
#define CHICKEN_H      TILE

// ───── .mif indices ────────────────────────────────────────────────
#define CHICKEN_STAND   8
#define CHICKEN_JUMP   11
#define PLATFORM_SPRITE_IDX 14

// ───── Tower 3×2 (6 sprites, regs 0‒5) ────────────────────────────
#define TOWER_ROWS      3
#define TOWER_COLS      2
#define TOWER_X         16
#define TOWER_BASE_Y   (WIDTH - WALL - TILE)
#define TOWER_REG_BASE  0             // 6 regs: 0…5

// ───── Dynamic platforms config ────────────────────────────────────
#define MAX_PLATFORMS      5          // we’ll instantiate five
#define MAX_SEGMENTS       6          // widest platform (sprites)
#define PLATFORM_AREA_REGS (MAX_PLATFORMS * MAX_SEGMENTS)
#define PLATFORM_REG_BASE  (TOWER_REG_BASE + TOWER_ROWS*TOWER_COLS) // =6

// ───── Chicken sprite reg ──────────────────────────────────────────
#define CHICKEN_REG (PLATFORM_REG_BASE + PLATFORM_AREA_REGS)        // = 6+30 = 36

// ───── Game parameters ─────────────────────────────────────────────
#define INITIAL_LIVES   5
#define SCORE_PER_LAND 10            // +10 per successful landing

// ───── Data structures ─────────────────────────────────────────────
int vga_fd, audio_fd;
struct controller_output_packet controller_state;
bool towerEnabled = true;

typedef struct {
    int x, y, vy;
    bool jumping;             // true = in air
} Chicken;

typedef struct {
    int x, y;                 // top‑left of first segment
    int segments;             // 1…MAX_SEGMENTS
    int speed;                // px / frame, positive => leftwards
} Platform;

// ───── Controller reader thread ────────────────────────────────────
void *controller_input_thread(void *arg) {
    uint8_t ep; struct libusb_device_handle *h = opencontroller(&ep);
    if (!h) pthread_exit(NULL);
    while (1) {
        unsigned char buf[GAMEPAD_READ_LENGTH]; int t;
        if (!libusb_interrupt_transfer(h, ep, buf, GAMEPAD_READ_LENGTH, &t, 0))
            usb_to_output(&controller_state, buf);
    }
}

// ───── Helpers ─────────────────────────────────────────────────────
static inline int platformWidth(const Platform *p) { return p->segments * TILE; }

void initChicken(Chicken *c) {
    c->x = TOWER_X;
    c->y = TOWER_BASE_Y - TOWER_ROWS*TILE;
    c->vy = 0;
    c->jumping = false;      // standing on tower => no gravity
}

void applyPhysics(Chicken *c) {
    if (!c->jumping && towerEnabled) return; // frozen on tower only
    c->y += c->vy;
    c->vy += GRAVITY;
}

void initPlatform(Platform *p, int startX, int y, int segments, int speed) {
    p->x = startX;
    p->y = y;
    p->segments = segments;
    p->speed = speed;
}

// Detect landing on a platform. Returns index if landed, else -1.
int checkPlatformLanding(Chicken *ch, int prevBottom, Platform *plats, int n) {
    int bottomNow = ch->y + CHICKEN_H;
    for (int i = 0; i < n; ++i) {
        int top = plats[i].y;
        if (prevBottom <= top && bottomNow >= top &&
            ch->x + CHICKEN_W  > plats[i].x &&
            ch->x             < plats[i].x + platformWidth(&plats[i]))
            return i;
    }
    return -1;
}

// ───── Main ────────────────────────────────────────────────────────
int main(void) {
    if ((vga_fd   = open("/dev/vga_top",    O_RDWR)) < 0) return -1;
    if ((audio_fd = open("/dev/fpga_audio", O_RDWR)) < 0) return -1;

    pthread_t thr; pthread_create(&thr, NULL, controller_input_thread, NULL);

    // ── Splash screen ──────────────────────────────────────────────
    cleartiles(); clearSprites();
        int index = 8;
        write_text("scream", 6, 14, 13);
        write_text("jump", 4, 14, 20);

        write_text("press", 5, 20, index);
        write_text("any", 3, 20, index + 6);
        write_text("key", 3, 20, index + 10);
        write_text("to", 2, 20, index + 14);
        write_text("start", 5, 20, index + 17);

    // write_text((unsigned char*)"ScreamJump Chicken", 18, 10, 8);
    // write_text((unsigned char*)"Press any key to start", 22, 12, 16);
    while (!(controller_state.a||controller_state.b||controller_state.x||controller_state.y||
             controller_state.start||controller_state.select||
             controller_state.updown||controller_state.leftright))
        usleep(10000);

    // ── Init game state ────────────────────────────────────────────
    cleartiles(); clearSprites(); srand(time(NULL));
    int score = 0, lives = INITIAL_LIVES;
    write_score(score); write_number(lives, 0, 0);

    // 5 platforms with varying width & speed
    Platform plats[MAX_PLATFORMS];
    initPlatform(&plats[0], LENGTH +  0,  300, 4,  2);
    initPlatform(&plats[1], LENGTH +160, 240, 3,  3);
    initPlatform(&plats[2], LENGTH +320, 180, 6,  2);
    initPlatform(&plats[3], LENGTH +480, 120, 5,  4);
    initPlatform(&plats[4], LENGTH +640, 200, 4,  3);

    Chicken chick; initChicken(&chick);

    bool wasGrounded = false;

    while (lives > 0) {
        // Handle jump (B button)
        if (controller_state.b && !chick.jumping) {
            chick.vy = JUMP_VY; chick.jumping = true; play_sfx(0);
        }

        int prevBottom = chick.y + CHICKEN_H;
        applyPhysics(&chick);

        // Move platforms & wrap
        for (int i = 0; i < MAX_PLATFORMS; ++i) {
            plats[i].x -= plats[i].speed;
            if (plats[i].x < -platformWidth(&plats[i])) {
                // respawn to right of right‑most
                int maxX = plats[0].x;
                for (int j = 1; j < MAX_PLATFORMS; ++j)
                    if (plats[j].x > maxX) maxX = plats[j].x;
                plats[i].x = maxX + platformWidth(&plats[i]) + 80;
                plats[i].y = WALL + rand() % (WIDTH - 2*WALL - TILE);
            }
        }

        // Tower landing / miss logic
        if (towerEnabled && chick.vy > 0) {
            int towerTop = TOWER_BASE_Y;
            int botPrev = prevBottom, botNow = chick.y + CHICKEN_H;
            if (botPrev <= towerTop && botNow >= towerTop &&
                chick.x + CHICKEN_W > TOWER_X && chick.x < TOWER_X + TOWER_COLS*TILE) {
                chick.y = towerTop - CHICKEN_H; chick.vy = 0; chick.jumping = false;
            } else if (botPrev <= towerTop && chick.y > towerTop) {
                // missed tower
                lives--; write_number(lives,0,0); initChicken(&chick); continue;
            }
        }

        // Platform landing detection
        int landedIdx = -1;
        if (chick.vy > 0) landedIdx = checkPlatformLanding(&chick, prevBottom, plats, MAX_PLATFORMS);
        if (landedIdx >= 0) {
            chick.y = plats[landedIdx].y - CHICKEN_H;
            chick.vy = 0; chick.jumping = false;
            towerEnabled = false;
        }

        // Score update: +10 only when transitioning from air → ground (not tower)
        bool grounded = !chick.jumping;
        if (!wasGrounded && grounded && landedIdx >= 0) {
            score += SCORE_PER_LAND; write_score(score);
        }
        wasGrounded = grounded;

        // Death off bottom
        if (chick.y > WIDTH) {
            lives--; write_number(lives,0,0); initChicken(&chick); towerEnabled = true; wasGrounded=false; continue;
        }

        // ── Draw ────────────────────────────────────────────────────
        clearSprites();
        // Tower (6 sprites)
        if (towerEnabled) {
            for (int r=0; r<TOWER_ROWS; ++r)
                for (int c=0; c<TOWER_COLS; ++c) {
                    int reg = TOWER_REG_BASE + r*TOWER_COLS + c;
                    write_sprite_to_kernel(1,
                        TOWER_BASE_Y - r*TILE,
                        TOWER_X + c*TILE,
                        PLATFORM_SPRITE_IDX,
                        reg);
                }
        }
        // Platforms
        for (int i=0;i<MAX_PLATFORMS;++i)
            for (int s=0;s<plats[i].segments;++s) {
                int reg = PLATFORM_REG_BASE + i*MAX_SEGMENTS + s;
                write_sprite_to_kernel(1,
                    plats[i].y,
                    plats[i].x + s*TILE,
                    PLATFORM_SPRITE_IDX,
                    reg);
            }
        // Chicken
        write_sprite_to_kernel(1, chick.y, chick.x,
            chick.jumping?CHICKEN_JUMP:CHICKEN_STAND, CHICKEN_REG);

        usleep(16666);
    }

    cleartiles(); clearSprites(); write_text((unsigned char*)"gameover",8,12,16); sleep(2);
    return 0;
}
