// screamjump_dynamic_start.c
// Adds a start screen and runs the "Scream Jump" game where the chicken jumps on moving bars.

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

// ─── Constants for screen dimensions and physics ─────────────────────────────
#define LENGTH         640    // VGA width (pixels)
#define WIDTH          480    // VGA height (pixels)
#define TILE_SIZE       16    // size of one tile (pixels)
#define WALL            16    // margin at top and bottom (pixels)
#define GRAVITY         +1    // gravity acceleration (pixels/frame)

// ─── Sprite dimensions ────────────────────────────────────────────────────────
#define CHICKEN_W       32    // chicken sprite width (pixels)
#define CHICKEN_H       32    // chicken sprite height (pixels)

// ─── Tile indices in MIF ─────────────────────────────────────────────────────
#define CHICKEN_STAND    8    // chicken standing frame
#define CHICKEN_JUMP    11    // chicken jumping frame
#define BAR_TILE_IDX    39    // tile for bars
#define TOWER_TILE_IDX  42    // tile for tower base
#define SUN_TILE        20    // tile for sun sprite

// ─── Game parameters ──────────────────────────────────────────────────────────
#define INITIAL_LIVES    5    // starting lives
#define INIT_JUMP_VY   -20    // initial jump velocity
#define BASE_DELAY    2000    // pause after landing (microseconds)

// ─── Bar configuration ───────────────────────────────────────────────────────
#define BAR_COUNT        6    // number of bars on screen at once
#define BAR_ROWS         2    // bar height in tiles
#define MIN_TILES        3    // minimum bar length in tiles (1/16 screen)
#define MAX_TILES        5    // maximum bar length in tiles (1/8 screen)
#define BAR_SPEED       4    // base bar movement speed (pixels/frame)
#define BAR_GAP        96    // desired gap between bars (pixels)

// ─── Y-position bounds for bars ──────────────────────────────────────────────
#define BAR_Y_MIN      (WALL + 40)
#define BAR_Y_MAX      (WIDTH - BAR_ROWS * TILE_SIZE - WALL)

// ─── Global file descriptors and controller state ────────────────────────────
int vga_fd, audio_fd;
struct controller_output_packet controller_state;
bool towerEnabled = true;

// ─── Data structures ──────────────────────────────────────────────────────────
typedef struct { int x, y, vy; bool jumping; } Chicken;
typedef struct { int x, y_px, length; } MovingBar;

// ─── THREAD: Reads controller input continuously ─────────────────────────────
void *controller_thread(void *arg) {
    uint8_t ep;
    struct libusb_device_handle *ctrl = opencontroller(&ep);
    if (!ctrl) pthread_exit(NULL);
    while (1) {
        unsigned char buf[GAMEPAD_READ_LENGTH];
        int transferred;
        if (!libusb_interrupt_transfer(ctrl, ep, buf, GAMEPAD_READ_LENGTH, &transferred, 0)) {
            usb_to_output(&controller_state, buf);
        }
    }
}

// ─── Initialize chicken position and state ───────────────────────────────────
void initChicken(Chicken *c) {
    c->x = 32;
    c->y = 289;
    c->vy = 0;
    c->jumping = false;
}

// ─── Apply gravity and movement to chicken ───────────────────────────────────
void moveChicken(Chicken *c) {
    if (!c->jumping && towerEnabled) return;
    c->y += c->vy;
    c->vy += GRAVITY;
}

// ─── Initialize bars with continuous spacing ─────────────────────────────────
void initBars(MovingBar bars[], int count, int y_px) {
    // seed random lengths
    int xPos = LENGTH;  // start just off the right edge
    for (int i = 0; i < count; i++) {
        bars[i].length = rand() % (MAX_TILES - MIN_TILES + 1) + MIN_TILES;
        bars[i].x = xPos;
        bars[i].y_px = y_px;
        // next bar's x: current x + width + fixed gap
        xPos += bars[i].length * TILE_SIZE + BAR_GAP;
    }
}

// ─── Move, wrap-around and draw bars with fixed gap ────────────────────────
void updateDrawBars(MovingBar bars[], int count, int speed) {
    // find the rightmost x among bars for respawn logic
    int maxX = 0;
    for (int i = 0; i < count; i++) {
        if (bars[i].x > maxX) maxX = bars[i].x;
    }

    int cols = LENGTH / TILE_SIZE;
    for (int i = 0; i < count; i++) {
        // move bar leftwards
        bars[i].x -= speed;
        int widthPx = bars[i].length * TILE_SIZE;

        // if bar has left the screen, respawn it to the right of the farthest bar
        if (bars[i].x + widthPx <= 0) {
            bars[i].length = rand() % (MAX_TILES - MIN_TILES + 1) + MIN_TILES;
            bars[i].x = maxX + BAR_GAP;
            bars[i].y_px = bars[i].y_px; // same height
            maxX = bars[i].x;  // update maxX
            widthPx = bars[i].length * TILE_SIZE;
        }

        // draw the bar as tiles
        int colStart = bars[i].x / TILE_SIZE;
        int rowStart = bars[i].y_px / TILE_SIZE;
        for (int row = rowStart; row < rowStart + BAR_ROWS; row++) {
            for (int t = 0; t < bars[i].length; t++) {
                int col = colStart + t;
                if (col >= 0 && col < cols) {
                    write_tile_to_kernel(row, col, BAR_TILE_IDX);
                }
            }
        }
    }
}

// ─── Check for collision and scoring when landing on bar ────────────────────
bool checkCollision(MovingBar bars[], int count, int prevY,
                    Chicken *c, int *score, bool *landed) {
    if (c->vy <= 0) return false;  // only when falling
    for (int i = 0; i < count; i++) {
        int topBar = bars[i].y_px;
        int botPrev = prevY + CHICKEN_H;
        int botCurr = c->y + CHICKEN_H;
        int barHeightPx = BAR_ROWS * TILE_SIZE;
        int barWidthPx = bars[i].length * TILE_SIZE;
        // collision bounds check
        if (botPrev <= topBar + barHeightPx && botCurr >= topBar &&
            c->x + CHICKEN_W > bars[i].x && c->x < bars[i].x + barWidthPx) {
            // land on bar
            c->y = topBar - CHICKEN_H;
            c->vy = 0;
            c->jumping = false;
            if (!*landed) {
                (*score)++;
                *landed = true;
            }
            return true;
        }
    }
    return false;
}

// ─── Draw sun based on level ─────────────────────────────────────────────────
void drawSun(int level) {
    const int levels = 5;
    const int startX = 32, endX = 608, y = 64;
    double frac = (double)(level - 1) / (levels - 1);
    int x = startX + (int)((endX - startX) * frac + 0.5);
    write_sprite_to_kernel(1, y, x, SUN_TILE, 1);
}

int main(void) {
    // open VGA and audio devices
    if ((vga_fd = open("/dev/vga_top", O_RDWR)) < 0) return -1;
    if ((audio_fd = open("/dev/fpga_audio", O_RDWR)) < 0) return -1;
    pthread_t tid;
    pthread_create(&tid, NULL, controller_thread, NULL);

    // ── Start screen ──────────────────────────────────────────────────────────
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

    // ── Game initialization ───────────────────────────────────────────────────
    cleartiles(); clearSprites(); fill_sky_and_grass();
    srand(time(NULL));
    Chicken chicken; initChicken(&chicken);
    MovingBar bars[BAR_COUNT];
    initBars(bars, BAR_COUNT, BAR_Y_MIN);
    int score = 0, lives = INITIAL_LIVES, level = 1;
    bool landed = false;

    // ── Main game loop ─────────────────────────────────────────────────────────
    while (lives > 0) {
        // handle jump input
        if (controller_state.b && !chicken.jumping) {
            chicken.vy = INIT_JUMP_VY;
            chicken.jumping = true;
            landed = false;
            play_sfx(0);
        }
        
        // apply movement and gravity
        int prevY = chicken.y;
        moveChicken(&chicken);
        if (chicken.y < WALL + 40) chicken.y = WALL + 40;

        // collision with bars
        checkCollision(bars, BAR_COUNT, prevY, &chicken, &score, &landed);

        // lose life if fallen off
        if (chicken.y > WIDTH) {
            lives--;
            towerEnabled = true;
            initChicken(&chicken);
            landed = false;
            usleep(3000000);
            continue;
        }

        // redraw background and HUD
        clearSprites(); fill_sky_and_grass();
        write_text("lives", 5, 1, (LENGTH/TILE_SIZE)/2 - 12);
        write_number(lives, 1, (LENGTH/TILE_SIZE)/2 -  6);
        write_text("score", 5, 1, (LENGTH/TILE_SIZE)/2     );
        write_number(score, 2, (LENGTH/TILE_SIZE)/2 +  6);
        write_text("level", 5, 1, (LENGTH/TILE_SIZE)/2 + 18);
        write_number(level, 1, (LENGTH/TILE_SIZE)/2 + 24);

        // update and draw bars
        updateDrawBars(bars, BAR_COUNT, BAR_SPEED);

        // draw tower, chicken, sun
            for (int r = 21; r < 30; ++r) {
                for (int c = 0; c < 5; ++c) {
                    write_tile_to_kernel(r, c,
                        towerEnabled ? TOWER_TILE_IDX : 0);
                }
            }
        write_sprite_to_kernel(0, chicken.y, chicken.x,
                               chicken.jumping ? CHICKEN_JUMP : CHICKEN_STAND, 0);
        drawSun(level);

        usleep(16666); // ~60 FPS
    }

    // ── Game over ─────────────────────────────────────────────────────────────
    cleartiles(); clearSprites(); fill_sky_and_grass();
    write_text("gameover", 8, 12, 16);
    sleep(2);
    return 0;
}
