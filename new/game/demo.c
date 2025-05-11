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
#define LENGTH            640   // VGA width (px)
#define WIDTH             480   // VGA height (px)
#define TILE_SIZE          16   // background tile size (px)
#define WALL               16   // top/bottom margin (px)
#define GRAVITY            +1

// ───── sprite dims ───────────────────────────────────────────────────────────
#define CHICKEN_W         32
#define CHICKEN_H         32

// ───── MIF indices ───────────────────────────────────────────────────────────
#define CHICKEN_STAND      8   // chicken standing tile
#define CHICKEN_JUMP       11  // chicken jumping tile
#define TOWER_TILE_IDX     42  // static tower tile
#define SUN_TILE           20

// ───── tower & HUD ──────────────────────────────────────────────────────────
#define INITIAL_LIVES      5
#define INIT_JUMP_VY     -20    // base jump velocity
#define BASE_DELAY       2000   // µs per landing pause

// ───── bar config ────────────────────────────────────────────────────────────
#define BAR_COUNT          6    // now 6 bars
#define BAR_HEIGHT_ROWS    2
#define BAR_SPEED_BASE     4
// bar length in tiles: between 1/16 (40 px) and 1/8 (80 px) of 640 px → 3–5 tiles
#define BAR_MIN_TILES      3
#define BAR_MAX_TILES      5
#define BAR_TILE_IDX      39

// ───── types & globals ───────────────────────────────────────────────────────
typedef struct { int x, y, vy; bool jumping; } Chicken;
typedef struct { int x, y_px, length; } MovingBar;

int vga_fd, audio_fd;
struct controller_output_packet controller_state;
bool towerEnabled = true;

// ───── set up one bar‐group ───────────────────────────────────────────────────
void initBars(MovingBar bars[], int count, int y_px) {
    int spawnInterval = LENGTH / count;
    for (int i = 0; i < count; i++) {
        bars[i].x      = LENGTH + i * spawnInterval;
        bars[i].y_px   = y_px;
        bars[i].length = rand() % (BAR_MAX_TILES - BAR_MIN_TILES + 1)
                         + BAR_MIN_TILES;
    }
}

// ───── move, wrap & draw that one group ──────────────────────────────────────
void updateAndDrawBars(MovingBar bars[], int count, int speed, int *spawnCounter) {
    int spawnInterval = LENGTH / count;
    int cols = LENGTH / TILE_SIZE;
    for (int b = 0; b < count; b++) {
        bars[b].x -= speed;
        int wPx = bars[b].length * TILE_SIZE;

        if (bars[b].x + wPx <= 0) {
            bars[b].x = LENGTH + ((*spawnCounter) % count) * spawnInterval;
            (*spawnCounter)++;
            bars[b].length = rand() % (BAR_MAX_TILES - BAR_MIN_TILES + 1)
                             + BAR_MIN_TILES;
        }

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

// ───── collision & scoring ──────────────────────────────────────────────────
bool handleBarCollision(
    MovingBar bars[], int count,
    int prevY,
    Chicken *c,
    int *score,
    bool *landed,
    int jumpDelay
) {
    if (c->vy <= 0) return false;
    for (int b = 0; b < count; b++) {
        int by   = bars[b].y_px;
        int botP = prevY + CHICKEN_H;
        int botN = c->y   + CHICKEN_H;
        int wPx  = bars[b].length * TILE_SIZE;

        if (botP <= by + BAR_HEIGHT_ROWS*TILE_SIZE &&
            botN >= by &&
            c->x + CHICKEN_W > bars[b].x &&
            c->x < bars[b].x + wPx) {
            c->y       = by - CHICKEN_H;
            c->vy      = 0;
            c->jumping = false;
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

// ───── input thread ─────────────────────────────────────────────────────────
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

// ───── chicken helpers ───────────────────────────────────────────────────────
void initChicken(Chicken *c) {
    c->x = 32;
    c->y = WIDTH/2;
    c->vy = 0;
    c->jumping = false;
}
void moveChicken(Chicken *c) {
    if (!c->jumping && towerEnabled) return;
    c->y += c->vy;
    c->vy += GRAVITY;
}

// ───── sun sprite ────────────────────────────────────────────────────────────
void update_sun(int level) {
    const int maxLv = 5, sx = 32, ex = 608, sy = 64;
    double f = (double)(level-1)/(maxLv-1);
    int x = sx + (int)((ex-sx)*f + .5);
    write_sprite_to_kernel(1, sy, x, SUN_TILE, 1);
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
    write_text("press",  5, 19,  8);
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

    // ── setup one bar-group ───────────────────────────────────────────────────
    MovingBar bars[BAR_COUNT];
    static int spawnCounter = 0;
    initBars(bars, BAR_COUNT, WALL + 80);

    // // second bar-group (commented out)
    // MovingBar bars2[BAR_COUNT];
    // static int spawnCounter2 = 0;
    // initBars(bars2, BAR_COUNT, WALL + 120);

    // ── chicken ────────────────────────────────────────────────────────────────
    Chicken chicken; initChicken(&chicken);
    bool landed = false;

    // ── main loop ─────────────────────────────────────────────────────────────
    while (lives > 0) {
        int speed = BAR_SPEED_BASE + (level - 1);

        // jump input
        if (controller_state.b && !chicken.jumping) {
            chicken.vy      = jumpVy;
            chicken.jumping = true;
            landed          = false;
            play_sfx(0);
        }

        int prevY = chicken.y;
        moveChicken(&chicken);
        if (chicken.y < WALL + 40) chicken.y = WALL + 40;

        // collision & scoring
        towerEnabled = false;
        handleBarCollision(
            bars, BAR_COUNT,
            prevY,
            &chicken,
            &score,
            &landed,
            jumpDelay
        );
        // handleBarCollision(bars2, BAR_COUNT, prevY, &chicken, &score, &landed, jumpDelay);  // commented out

        // fell off
        if (chicken.y > WIDTH) {
            lives--; towerEnabled = true;
            initChicken(&chicken); landed = false;
            usleep(3000000);
            continue;
        }

        // redraw
        clearSprites(); fill_sky_and_grass();
        write_text("lives", 5, 1, center - offset);
        write_number(lives, 1, center - offset + 6);
        write_text("score", 5, 1, center - offset + 12);
        write_number(score, 2, center - offset + 18);
        write_text("level", 5, 1, center - offset + 24);
        write_number(level, 1, center - offset + 30);

        updateAndDrawBars(bars, BAR_COUNT, speed, &spawnCounter);
        // updateAndDrawBars(bars2, BAR_COUNT, speed+1, &spawnCounter2);  // commented out

        // tower
        for (int r = (WIDTH - WALL - 32)/TILE_SIZE - 2;
             r <= (WIDTH - WALL - 32)/TILE_SIZE; r++)
            for (int c = 0; c < CHICKEN_W/TILE_SIZE; c++)
                write_tile_to_kernel(r, c, towerEnabled ? TOWER_TILE_IDX : 0);

        // chicken & sun
        write_sprite_to_kernel(0, chicken.y, chicken.x,
                               chicken.jumping ? CHICKEN_JUMP : CHICKEN_STAND, 0);
        update_sun(level);

        usleep(16666);
    }

    // ── game over ────────────────────────────────────────────────────────────
    cleartiles(); clearSprites(); fill_sky_and_grass();
    write_text("gameover", 8, 12, 16);
    sleep(2);
    return 0;
}
