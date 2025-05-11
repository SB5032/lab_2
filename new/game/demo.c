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
#define CHICKEN_STAND      8    // chicken standing tile
#define CHICKEN_JUMP      11    // chicken jumping tile
#define TOWER_TILE_IDX    42    // static tower tile

// ───── static tower ─────────────────────────────────────────────────────────
#define TOWER_X           16
#define TOWER_WIDTH      CHICKEN_W
#define TOWER_HEIGHT       3    // stacked 3 sprites high
#define PLATFORM_H        32
#define TOWER_BASE_Y    (WIDTH - WALL - PLATFORM_H)

// ───── lives/score & controller ──────────────────────────────────────────────
#define INITIAL_LIVES      5
#define INIT_JUMP_VY     -20    // base jump velocity
#define BASE_DELAY       2000   // base jump delay (µs)

// ───── bar-config limits ─────────────────────────────────────────────────────
#define BAR_COUNT          4     // # of moving bars on-screen
#define BAR_HEIGHT_ROWS    2     // each bar is 2 tiles tall
#define BAR_SPEED_BASE     4     // start speed (pixels/frame)
#define MIN_BAR_TILES      3     // shortest bar: 3 tiles
#define MAX_BAR_TILES     10     // longest bar: 10 tiles
#define BAR_GAP_BASE     128    // initial pixel gap between bars
#define BAR_TILE_IDX      39    // tile index for bars
#define BAR_MIN_Y         40    // bars never above this
#define BAR_MAX_Y       (WIDTH - BAR_HEIGHT_ROWS * TILE_SIZE)

int vga_fd, audio_fd;
struct controller_output_packet controller_state;
bool towerEnabled = true;

// ───── data structures ───────────────────────────────────────────────────────
typedef struct { int x, y, vy; bool jumping; } Chicken;
typedef struct { int x; int y_px; int length; } MovingBar;

void *controller_input_thread(void *arg) {
    uint8_t ep;
    struct libusb_device_handle *ctrl = opencontroller(&ep);
    if (!ctrl) pthread_exit(NULL);
    while (1) {
        unsigned char buf[GAMEPAD_READ_LENGTH]; int transferred;
        if (libusb_interrupt_transfer(ctrl, ep, buf, GAMEPAD_READ_LENGTH, &transferred, 0) == 0)
            usb_to_output(&controller_state, buf);
    }
}

void initChicken(Chicken *c) {
    c->x = 32;
    c->y = TOWER_BASE_Y - CHICKEN_H * TOWER_HEIGHT;
    c->vy = 0;
    c->jumping = false;
}

void moveChicken(Chicken *c) {
    if (!c->jumping && towerEnabled) return;
    c->y += c->vy;
    c->vy += GRAVITY;
}

int clamp(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

int main(void) {
    if ((vga_fd = open("/dev/vga_top", O_RDWR)) < 0) return -1;
    if ((audio_fd = open("/dev/fpga_audio", O_RDWR)) < 0) return -1;
    pthread_t tid; pthread_create(&tid, NULL, controller_input_thread, NULL);

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
    int score = 0, lives = INITIAL_LIVES, level = 1;
    int jumpVy    = INIT_JUMP_VY;
    int jumpDelay = BASE_DELAY;

    write_text("Lives", 0, 0, 1);   write_number(lives, 6, 7);
    write_text("Score", 0, 10, 15); write_number(score, 0, 21);
    write_text("Level", 0, 20, 31); write_number(level, 26, 38);

    Chicken chicken; initChicken(&chicken);
    bool landed = false;
    int minY = WALL + 40;

    // ── initialize moving bars ───────────────────────────────────────────────
    MovingBar bars[BAR_COUNT];
    srand(time(NULL));

    // compute initial gap (level=1)
    int barGapPx = BAR_GAP_BASE;
    // first bar: from right edge
    bars[0].x = LENGTH;
    bars[0].length = rand() % (MAX_BAR_TILES - MIN_BAR_TILES + 1) + MIN_BAR_TILES;
    {
        int low  = clamp(chicken.y - 150, BAR_MIN_Y, BAR_MAX_Y);
        int high = clamp(chicken.y + 150, BAR_MIN_Y, BAR_MAX_Y);
        bars[0].y_px = rand() % (high - low + 1) + low;
    }
    // subsequent bars
    for (int i = 1; i < BAR_COUNT; i++) {
        bars[i].x = bars[i-1].x + barGapPx;
        bars[i].length = rand() % (MAX_BAR_TILES - MIN_BAR_TILES + 1) + MIN_BAR_TILES;

        int prevY = bars[i-1].y_px;
        int low, high;
        if (prevY < 100) {
            // if previous near top, pick any lower position
            low  = prevY;
            high = BAR_MAX_Y;
        } else if (prevY > WIDTH - 100) {
            // if previous near bottom, stay within +150 px
            low  = prevY;
            high = clamp(prevY + 150, BAR_MIN_Y, BAR_MAX_Y);
        } else {
            // otherwise +-150
            low  = clamp(prevY - 150, BAR_MIN_Y, BAR_MAX_Y);
            high = clamp(prevY + 150, BAR_MIN_Y, BAR_MAX_Y);
        }
        bars[i].y_px = rand() % (high - low + 1) + low;
    }

    // ── main loop ─────────────────────────────────────────────────────────────
    while (lives > 0) {
        // update per‐level bar parameters
        barGapPx = BAR_GAP_BASE + (level - 1) * TILE_SIZE * 2;
        int barSpeed = BAR_SPEED_BASE + (level - 1);

        // jump input
        if (controller_state.b && !chicken.jumping) {
            chicken.vy      = jumpVy;
            chicken.jumping = true;
            landed          = false;
            play_sfx(0);
        }

        int prevY = chicken.y;
        moveChicken(&chicken);
        if (chicken.y < minY) chicken.y = minY;

        // ── bar collision & score ────────────────────────────────────────────
        if (chicken.vy > 0) {
            towerEnabled = false;
            for (int b = 0; b < BAR_COUNT; b++) {
                int by   = bars[b].y_px;
                int botP = prevY + CHICKEN_H;
                int botN = chicken.y + CHICKEN_H;
                int wPx  = bars[b].length * TILE_SIZE;

                if (botP <= by + BAR_HEIGHT_ROWS * TILE_SIZE &&
                    botN >= by &&
                    chicken.x + CHICKEN_W > bars[b].x &&
                    chicken.x < bars[b].x + wPx) {
                    chicken.y       = by - CHICKEN_H;
                    chicken.vy      = 0;
                    chicken.jumping = false;
                    if (!landed) {
                        score++;
                        write_number(score, 0, 16);
                        landed = true;
                    }
                    usleep(jumpDelay);
                    break;
                }
            }
        }

        // ── lose life if you fall below screen ────────────────────────────────
        if (chicken.y > WIDTH) {
            lives--;
            write_number(lives, 0, 6);
            towerEnabled = true;
            initChicken(&chicken);
            landed = false;
            usleep(3000000);
            continue;
        }

        // ── redraw frame ───────────────────────────────────────────────────────
        clearSprites(); fill_sky_and_grass();

        // draw & move bars
        for (int b = 0; b < BAR_COUNT; b++) {
            bars[b].x -= barSpeed;
            int wPx = bars[b].length * TILE_SIZE;

            if (bars[b].x + wPx <= 0) {
                // respawn off right side
                int prev = (b + BAR_COUNT - 1) % BAR_COUNT;
                bars[b].x = bars[prev].x + barGapPx;
                bars[b].length = rand() % (MAX_BAR_TILES - MIN_BAR_TILES + 1) + MIN_BAR_TILES;

                // vertical logic same as init
                int prevY = bars[prev].y_px;
                int low, high;
                if (prevY < 100) {
                    low  = prevY;
                    high =
