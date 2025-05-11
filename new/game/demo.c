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
#define LENGTH            640   // VGA width (pixels)
#define WIDTH             480   // VGA height (pixels)
#define TILE_SIZE          16   // background tile size (pixels)
#define WALL               16   // top/bottom margin (pixels)
#define GRAVITY            +1

// ───── sprite dimensions ─────────────────────────────────────────────────────
#define CHICKEN_W         32
#define CHICKEN_H         32

// ───── MIF indices ───────────────────────────────────────────────────────────
#define CHICKEN_STAND      8   // chicken standing tile
#define CHICKEN_JUMP       11  // chicken jumping tile
#define TOWER_TILE_IDX     42  // static tower tile
#define SUN_TILE           20

// ───── static tower ─────────────────────────────────────────────────────────
#define TOWER_BASE_Y    (WIDTH - WALL - 32)

// ───── lives/score & controller ──────────────────────────────────────────────
#define INITIAL_LIVES      5
#define INIT_JUMP_VY     -20    // base jump velocity
#define BASE_DELAY       2000   // base jump delay (µs)

// ───── bar-config limits ─────────────────────────────────────────────────────
#define BAR_COUNT          6     // bars per group
#define BAR_HEIGHT_ROWS    2     // tiles tall
#define BAR_SPEED_BASE     4     // pixels/frame
#define MIN_BAR_TILES      3
#define MAX_BAR_TILES     10
#define BAR_TILE_IDX      39

// ───── bar Y-bounds ─────────────────────────────────────────────────────────
#define BAR_MIN_Y         (WALL + 120)
#define BAR_MAX_Y         (WIDTH - BAR_HEIGHT_ROWS * TILE_SIZE - WALL)

int vga_fd, audio_fd;
struct controller_output_packet controller_state;
bool towerEnabled = true;

typedef struct { int x, y, vy; bool jumping; } Chicken;
typedef struct { int x; int y_px; int length; } MovingBar;

// ───── Initialize a bar-array at fixed Y ─────────────────────────────────────
void initBars(MovingBar bars[], int count, int y_px, int speed) {
    // speed parameter available for future spacing tweaks
    int spawnInterval = LENGTH / count;
    for (int i = 0; i < count; i++) {
        bars[i].x      = LENGTH + i * spawnInterval;
        bars[i].y_px   = y_px;
        bars[i].length = rand() % (MAX_BAR_TILES - MIN_BAR_TILES + 1)
                         + MIN_BAR_TILES;
    }
}

// ───── Move, respawn & draw all bars in one go ───────────────────────────────
void updateAndDrawBars(MovingBar bars[], int count, int speed, int *spawnCounter) {
    int spawnInterval = LENGTH / count;
    int cols = LENGTH / TILE_SIZE;
    for (int b = 0; b < count; b++) {
        // move
        bars[b].x -= speed;
        int wPx = bars[b].length * TILE_SIZE;

        // respawn if off-screen
        if (bars[b].x + wPx <= 0) {
            bars[b].x = LENGTH + ((*spawnCounter) % count) * spawnInterval;
            (*spawnCounter)++;
            bars[b].length = rand() % (MAX_BAR_TILES - MIN_BAR_TILES + 1)
                             + MIN_BAR_TILES;
        }

        // draw tiles
        int col0 = bars[b].x / TILE_SIZE;
        int row0 = bars[b].y_px / TILE_SIZE;
        int row1 = row0 + BAR_HEIGHT_ROWS - 1;
        for (int r = row0; r <= row1; r++) {
            for (int i = 0; i < bars[b].length; i++) {
                int c = col0 + i;
                if (c >= 0 && c < cols)
                    write_tile_to_kernel(r, c, BAR_TILE_IDX);
            }
        }
    }
}

// returns true if we landed on a bar
bool handleBarCollision(
    MovingBar bars[], int count,
    int prevY,
    Chicken *chicken,
    int *score,
    bool *landed,
    int jumpDelay
) {
    // only check when falling
    if (chicken->vy <= 0) return false;

    for (int b = 0; b < count; b++) {
        int by   = bars[b].y_px;
        int botP = prevY + CHICKEN_H;
        int botN = chicken->y + CHICKEN_H;
        int wPx  = bars[b].length * TILE_SIZE;

        if (botP <= by + BAR_HEIGHT_ROWS * TILE_SIZE &&
            botN >= by &&
            chicken->x + CHICKEN_W > bars[b].x &&
            chicken->x < bars[b].x + wPx) {

            // snap chicken to bar top
            chicken->y       = by - CHICKEN_H;
            chicken->vy      = 0;
            chicken->jumping = false;

            // increment only once per landing
            if (!*landed) {
                (*score)++;
                *landed = true;
            }
            usleep(jumpDelay);
            return true;
        }
    }
    return false;
}


void *controller_input_thread(void *arg) {
    uint8_t ep;
    struct libusb_device_handle *ctrl = opencontroller(&ep);
    if (!ctrl) pthread_exit(NULL);
    while (1) {
        unsigned char buf[GAMEPAD_READ_LENGTH];
        int transferred;
        if (libusb_interrupt_transfer(ctrl, ep, buf,
            GAMEPAD_READ_LENGTH, &transferred, 0) == 0)
            usb_to_output(&controller_state, buf);
    }
}

void initChicken(Chicken *c) {
    c->x = 32;
    c->y = 368; // atop the tower
    c->vy = 0;
    c->jumping = false;
}
void moveChicken(Chicken *c) {
    if (!c->jumping && towerEnabled) return;
    c->y += c->vy;
    c->vy += GRAVITY;
}

void update_sun(int level) {
    const int maxLv = 5, startX = 32, endX = 608, baseY = 64;
    double frac = (double)(level - 1) / (maxLv - 1);
    int x = startX + (int)((endX - startX) * frac + 0.5);
    write_sprite_to_kernel(1, baseY, x, SUN_TILE, 1);
}

int main(void) {
    if ((vga_fd = open("/dev/vga_top", O_RDWR)) < 0) return -1;
    if ((audio_fd = open("/dev/fpga_audio", O_RDWR)) < 0) return -1;
    pthread_t tid;
    pthread_create(&tid, NULL, controller_input_thread, NULL);

    // ── start screen ──────────────────────────────────────────────────────────
    cleartiles(); clearSprites(); fill_sky_and_grass();
    write_text("scream", 6, 13, 13);
    write_text("jump",   4, 13, 20);
    write_text("press",  5, 19, 8);
    write_text("any",    3, 19, 14);
    write_text("key",    3, 19, 20);
    write_text("to",     2, 19, 26);
    write_text("start",  5, 19, 29);
    while (!(controller_state.a || controller_state.b || controller_state.start))
        usleep(10000);

    // ── init game ──────────────────────────────────────────────────────────────
    cleartiles(); clearSprites(); fill_sky_and_grass();
    srand(time(NULL));
    int score = 0, lives = INITIAL_LIVES, level = 1;
    int jumpVy    = INIT_JUMP_VY;
    int jumpDelay = BASE_DELAY;

    const int cols   = LENGTH / TILE_SIZE;
    const int center = cols / 2;
    const int offset = 12;

    // prepare two bar-groups
    MovingBar barsA[BAR_COUNT]; //, barsB[BAR_COUNT];
    static int spawnCounterA = 0 ; //, spawnCounterB = 0;

    // launch group A low, group B higher
    initBars(barsA, BAR_COUNT, BAR_MIN_Y,       BAR_SPEED_BASE);
   // initBars(barsB, BAR_COUNT, BAR_MIN_Y + 80,  BAR_SPEED_BASE + 1);

    // init chicken
    Chicken chicken; initChicken(&chicken);
    bool landed = false;

    // ── main loop ─────────────────────────────────────────────────────────────
    while (lives > 0) {
        int baseSpeed = BAR_SPEED_BASE + (level - 1);

        // jump input
        if (controller_state.b && !chicken.jumping) {
            chicken.vy      = jumpVy;
            chicken.jumping = true;
            landed          = false;
            play_sfx(0);
        }

        if (chicken.y < WALL + 40) chicken.y = WALL + 40;

        // collision & score on descending
    int prevY = chicken.y;
    moveChicken(&chicken);

    // if falling, try each bar‐group
    if (chicken.vy > 0) {
        towerEnabled = false;
        // first group
     //   if (!
            handleBarCollision(
                barsA, BAR_COUNT,
                prevY,
                &chicken,
                &score,
                &landed,
                jumpDelay
            );
        // {
        //     // then second group
        //     handleBarCollision(
        //         barsB, BAR_COUNT,
        //         prevY,
        //         &chicken,
        //         &score,
        //         &landed,
        //         jumpDelay
        //     );
        // }
    }


        // fallen off?
        if (chicken.y > WIDTH) {
            lives--; towerEnabled = true;
            initChicken(&chicken); landed = false;
            usleep(3000000);
            continue;
        }

        // redraw background + HUD
        clearSprites(); fill_sky_and_grass();
        write_text("lives", 5, 1, center - offset);
        write_number(lives, 1, center - offset + 6);
        write_text("score", 5, 1, center - offset + 12);
        write_number(score, 1, center - offset + 18);
        write_text("level", 5, 1, center - offset + 24);
        write_number(level, 1, center - offset + 30);

        // **now just two calls**—no loops or tile logic in main
        updateAndDrawBars(barsA, BAR_COUNT, baseSpeed,     &spawnCounterA);
        //updateAndDrawBars(barsB, BAR_COUNT, baseSpeed + 1, &spawnCounterB);

        // draw tower & chicken & sun…
            for (int r = 21; r < 30; ++r) {
                for (int c = 0; c < 5; ++c) {
                    write_tile_to_kernel(r, c,
                        towerEnabled ? TOWER_TILE_IDX : 0);
                }

        write_sprite_to_kernel(
            1, chicken.y, chicken.x,
            chicken.jumping ? CHICKEN_JUMP : CHICKEN_STAND,
            0
        );
        update_sun(level);

        usleep(16666);
    }

    // ── game over ────────────────────────────────────────────────────────────
    cleartiles(); clearSprites(); fill_sky_and_grass();
    write_text("gameover", 8, 12, 16);
    sleep(2);
    return 0;
}
