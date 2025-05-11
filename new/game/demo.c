// screamjump_dynamic_start.c
// Adds a start screen and runs the "Scream Jump" game.

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

// Constants for screen and physics
#define LENGTH       640  // screen width in pixels
#define WIDTH        480  // screen height in pixels
#define TILE_SIZE     16  // tile size in pixels
#define WALL          16  // top/bottom margin
#define GRAVITY       1   // gravity per frame

// Sprite dimensions
#define CHICKEN_W    32
#define CHICKEN_H    32

// Tile indices
#define CHICKEN_STAND  8
#define CHICKEN_JUMP  11
#define BAR_TILE_IDX  39
#define TOWER_TILE_IDX 42
#define SUN_TILE      20

// Game settings
#define INITIAL_LIVES  5
#define INIT_JUMP_VY  -20  // initial jump velocity
#define BASE_DELAY   2000  // microsecond pause after landing

// Bar settings
#define BAR_COUNT      6   // number of bars
#define BAR_ROWS       2   // height in tiles
#define MIN_TILES      3   // min length in tiles
#define MAX_TILES      5   // max length in tiles
#define BAR_SPEED      4   // pixels per frame
#define BAR_GAP       96   // gap between bars in pixels

#define BAR_Y_MIN   (WALL + 40)
#define BAR_Y_MAX   (WIDTH - BAR_ROWS * TILE_SIZE - WALL)

// Clamp column start so text + length stays on-screen
static inline int clampCol(int col, int len) {
    int maxCols = LENGTH / TILE_SIZE;
    if (col + len > maxCols) return maxCols - len;
    if (col < 0) return 0;
    return col;
}

// Data structures
typedef struct { int x, y, vy; bool jumping; } Chicken;
typedef struct { int x, y_px, length; } MovingBar;

int vga_fd, audio_fd;
struct controller_output_packet controller_state;
bool towerEnabled = true;

// Thread for controller input
void *controller_thread(void *arg) {
    uint8_t ep;
    struct libusb_device_handle *ctrl = opencontroller(&ep);
    if (!ctrl) pthread_exit(NULL);
    while (1) {
        unsigned char buf[GAMEPAD_READ_LENGTH];
        int transferred;
        if (libusb_interrupt_transfer(ctrl, ep, buf, GAMEPAD_READ_LENGTH,
                                      &transferred, 0) == 0) {
            usb_to_output(&controller_state, buf);
        }
    }
}

// Initialize chicken
void initChicken(Chicken *c) {
    c->x = 32;
    c->y = WIDTH / 2;
    c->vy = 0;
    c->jumping = false;
}

// Move chicken each frame
void moveChicken(Chicken *c) {
    if (!c->jumping && towerEnabled) return;
    c->y += c->vy;
    c->vy += GRAVITY;
}

// Initialize bar positions and lengths
void initBars(MovingBar bars[], int count, int y_px) {
    int xPos = LENGTH;
    for (int i = 0; i < count; i++) {
        bars[i].length = rand() % (MAX_TILES - MIN_TILES + 1) + MIN_TILES;
        bars[i].x = xPos;
        bars[i].y_px = y_px;
        xPos += bars[i].length * TILE_SIZE + BAR_GAP;
    }
}

// Update and draw bars each frame
void updateDrawBars(MovingBar bars[], int count, int speed) {
    int maxX = 0;
    for (int i = 0; i < count; i++) {
        if (bars[i].x > maxX) maxX = bars[i].x;
    }
    int cols = LENGTH / TILE_SIZE;
    for (int i = 0; i < count; i++) {
        bars[i].x -= speed;
        int widthPx = bars[i].length * TILE_SIZE;
        if (bars[i].x + widthPx <= 0) {
            bars[i].length = rand() % (MAX_TILES - MIN_TILES + 1) + MIN_TILES;
            bars[i].x = maxX + BAR_GAP;
            maxX = bars[i].x;
            widthPx = bars[i].length * TILE_SIZE;
        }
        int colStart = bars[i].x / TILE_SIZE;
        int rowStart = bars[i].y_px / TILE_SIZE;
        for (int r = rowStart; r < rowStart + BAR_ROWS; r++) {
            for (int t = 0; t < bars[i].length; t++) {
                int c = colStart + t;
                if (c >= 0 && c < cols) write_tile_to_kernel(r, c, BAR_TILE_IDX);
            }
        }
    }
}

// Check collision and scoring
bool checkCollision(MovingBar bars[], int count, int prevY,
                    Chicken *c, int *score, bool *landed) {
    if (c->vy <= 0) return false;
    for (int i = 0; i < count; i++) {
        int topBar = bars[i].y_px;
        int botPrev = prevY + CHICKEN_H;
        int botCurr = c->y + CHICKEN_H;
        int barHeightPx = BAR_ROWS * TILE_SIZE;
        int barWidthPx = bars[i].length * TILE_SIZE;
        if (botPrev <= topBar + barHeightPx && botCurr >= topBar &&
            c->x + CHICKEN_W > bars[i].x && c->x < bars[i].x + barWidthPx) {
            c->y = topBar - CHICKEN_H;
            c->vy = 0;
            c->jumping = false;
            if (!*landed) { (*score)++; *landed = true; }
            return true;
        }
    }
    return false;
}

// Draw sun based on level
void drawSun(int level) {
    int levels = 5;
    int sx = 32, ex = 608, sy = 64;
    double frac = (double)(level - 1) / (levels - 1);
    int x = sx + (int)((ex - sx) * frac + 0.5);
    write_sprite_to_kernel(1, sy, x, SUN_TILE, 1);
}

int main(void) {
    if ((vga_fd = open("/dev/vga_top", O_RDWR)) < 0) return -1;
    if ((audio_fd = open("/dev/fpga_audio", O_RDWR)) < 0) return -1;
    pthread_t tid;
    pthread_create(&tid, NULL, controller_thread, NULL);

    // Start screen
    cleartiles(); clearSprites(); fill_sky_and_grass();
    write_text("scream", 6, 13, clampCol(13, 6));
    write_text("jump",   4, 13, clampCol(20, 4));
    write_text("press",  5, 19, clampCol(8, 5));
    write_text("any",    3, 19, clampCol(14, 3));
    write_text("key",    3, 19, clampCol(20, 3));
    write_text("to",     2, 19, clampCol(26, 2));
    write_text("start",  5, 19, clampCol(29, 5));
    while (!(controller_state.a || controller_state.b ||
             controller_state.start)) usleep(10000);

    // Game init
    cleartiles(); clearSprites(); fill_sky_and_grass();
    srand(time(NULL));
    Chicken chicken; initChicken(&chicken);
    MovingBar bars[BAR_COUNT]; initBars(bars, BAR_COUNT, BAR_Y_MIN);
    int score = 0, lives = INITIAL_LIVES, level = 1;
    bool landed = false;

    // Main loop
    while (lives > 0) {
        if (controller_state.b && !chicken.jumping) {
            chicken.vy = INIT_JUMP_VY;
            chicken.jumping = true;
            landed = false;
            play_sfx(0);
        }
        int prevY = chicken.y;
        moveChicken(&chicken);
        if (chicken.y < WALL + 40) chicken.y = WALL + 40;
        checkCollision(bars, BAR_COUNT, prevY, &chicken, &score, &landed);
        if (chicken.y > WIDTH) {
            lives--;
            towerEnabled = true;
            initChicken(&chicken);
            landed = false;
            usleep(3000000);
            continue;
        }
        clearSprites(); fill_sky_and_grass();
        int baseCol = (LENGTH / TILE_SIZE) / 2;
        write_text("Lives", 5, 1, clampCol(baseCol - 12, 5));
        write_number(lives, 1, clampCol(baseCol -  6, 1));
        write_text("Score", 5, 1, clampCol(baseCol    , 5));
        write_number(score, 2, clampCol(baseCol +  6, 2));
        write_text("Level", 5, 1, clampCol(baseCol + 18, 5));
        write_number(level, 1, clampCol(baseCol + 24, 1));
        updateDrawBars(bars, BAR_COUNT, BAR_SPEED);
        for (int r = (WIDTH - WALL - TILE_SIZE * BAR_ROWS) / TILE_SIZE;
             r <= (WIDTH - WALL) / TILE_SIZE; r++) {
            for (int c = 0; c < CHICKEN_W / TILE_SIZE; c++) {
                write_tile_to_kernel(r, c,
                                     towerEnabled ? TOWER_TILE_IDX : 0);
            }
        }
        write_sprite_to_kernel(
            0,
            chicken.y,
            chicken.x,
            chicken.jumping ? CHICKEN_JUMP : CHICKEN_STAND,
            0
        );
        drawSun(level);
        usleep(16666);
    }

    // Game over
    cleartiles(); clearSprites(); fill_sky_and_grass();
    write_text("gameover", 8, 12, clampCol(16, 8));
    sleep(2);
    return 0;
}
