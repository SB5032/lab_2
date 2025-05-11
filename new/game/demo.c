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
#define BAR_COUNT          4     // number of moving bars on-screen
#define BAR_HEIGHT_ROWS    2     // each bar is 2 tiles tall
#define BAR_SPEED_BASE     4     // start speed (pixels/frame)
#define MIN_BAR_TILES      3     // shortest bar: 3 tiles
#define MAX_BAR_TILES     10     // longest bar: 10 tiles
#define BAR_TILE_IDX      39     // tile index for bars

// ───── bar Y-bounds ─────────────────────────────────────────────────────────
#define BAR_MIN_Y         (WALL + 40)
#define BAR_MAX_Y         (WIDTH - BAR_HEIGHT_ROWS * TILE_SIZE - WALL)

int vga_fd, audio_fd;
struct controller_output_packet controller_state;
bool towerEnabled = true;

typedef struct { int x, y, vy; bool jumping; }       Chicken;
typedef struct { int x; int y_px; int length; } MovingBar;

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
}

void initChicken(Chicken *c) {
    c->x = 32;
    c->y = 304; // atop the tower TOWER_BASE_Y - CHICKEN_H * TOWER_HEIGHT;
    c->vy = 0;
    c->jumping = false;
}

void moveChicken(Chicken *c) {
    if (!c->jumping && towerEnabled) return;
    c->y += c->vy;
    c->vy += GRAVITY;
}

// uniform random Y within safe landing band
static inline int randBarY(void) {
    return (rand() % (BAR_MAX_Y - BAR_MIN_Y + 1)) + BAR_MIN_Y;
}

int main(void) {
    if ((vga_fd = open("/dev/vga_top", O_RDWR)) < 0) return -1;
    if ((audio_fd = open("/dev/fpga_audio", O_RDWR)) < 0) return -1;
    pthread_t tid;
    pthread_create(&tid, NULL, controller_input_thread, NULL);

    // ── start screen ──────────────────────────────────────────────────────────
    cleartiles(); clearSprites(); fill_sky_and_grass();
    write_text("scream",   6, 13, 13);
    write_text("jump",     4, 13, 20);
    write_text("press",    5, 19,  8);
    write_text("any",      3, 19, 14);
    write_text("key",      3, 19, 20);
    write_text("to",       2, 19, 26);
    write_text("start",    5, 19, 29);
    while (!(controller_state.a || controller_state.b || controller_state.start))
        usleep(10000);

    // ── init game ──────────────────────────────────────────────────────────────
    cleartiles(); clearSprites(); fill_sky_and_grass();
    int score = 0, lives = INITIAL_LIVES, level = 1;
    int jumpVy    = INIT_JUMP_VY;
    int jumpDelay = BASE_DELAY;

    // compute columns & center for HUD
    const int cols   = LENGTH / TILE_SIZE;  // e.g. 40
    const int offset = 3;                  // half HUD width

    // spawn spacing & counter
    const int spawnInterval = LENGTH / BAR_COUNT;
    static int spawnCounter = 0;

    // init chicken
    Chicken chicken; initChicken(&chicken);
    bool landed = false;
    int minY = WALL + 40;

    // init bars
    MovingBar bars[BAR_COUNT];
    srand(time(NULL));
    for (int i = 0; i < BAR_COUNT; i++) {
        bars[i].x      = LENGTH + i * spawnInterval;
        bars[i].y_px   = randBarY();
        bars[i].length = rand() % (MAX_BAR_TILES - MIN_BAR_TILES + 1)
                         + MIN_BAR_TILES;
    }

    // ── main loop ─────────────────────────────────────────────────────────────
    while (lives > 0) {
        int barSpeed = BAR_SPEED_BASE + (level - 1);

        // jump
        if (controller_state.b && !chicken.jumping) {
            chicken.vy      = jumpVy;
            chicken.jumping = true;
            landed          = false;
            play_sfx(0);
        }

        int prevY = chicken.y;
        moveChicken(&chicken);
        if (chicken.y < minY) chicken.y = minY;

        // bar collision & scoring
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
                        landed = true;
                    }
                    usleep(jumpDelay);
                    break;
                }
            }
        }

        // lose life if fallen
        if (chicken.y > WIDTH) {
            lives--;
            towerEnabled = true;
            initChicken(&chicken);
            landed = false;
            usleep(3000000);
            continue;
        }

        // redraw background + top-centre HUD
        clearSprites();
        fill_sky_and_grass();

        write_text("lives", 5, 1, offset);
        write_number(lives, 1, offset + 6);
        write_text("score", 5, 1, offset + 15);
        write_number(score, 1, offset + 21);
        write_text("level", 5, 1, offset + 30);
        write_number(level, 1, offset + 36);

        // move & draw bars (spawn only from right)
        for (int b = 0; b < BAR_COUNT; b++) {
            bars[b].x -= barSpeed;
            int wPx = bars[b].length * TILE_SIZE;

            if (bars[b].x + wPx <= 0) {
                // respawn at right edge, evenly spaced
                bars[b].x      = LENGTH + (spawnCounter % BAR_COUNT) * spawnInterval;
                spawnCounter++;
                bars[b].y_px   = randBarY();
                bars[b].length = rand() % (MAX_BAR_TILES - MIN_BAR_TILES + 1)
                                 + MIN_BAR_TILES;
                wPx = bars[b].length * TILE_SIZE;
            }

            int col0 = bars[b].x / TILE_SIZE;
            int row0 = bars[b].y_px / TILE_SIZE;
            int row1 = row0 + BAR_HEIGHT_ROWS - 1;
            int maxC = cols;

            for (int r = row0; r <= row1; r++) {
                for (int i = 0; i < bars[b].length; i++) {
                    int c = col0 + i;
                    if (c >= 0 && c < maxC)
                        write_tile_to_kernel(r, c, BAR_TILE_IDX);
                }
            }
        }

        // draw tower
        for (int r = 21; r <= 29; r++){
            for (int c = 0; c <= 4; c++){
                write_tile_to_kernel(r, c,
                    towerEnabled ? TOWER_TILE_IDX : 0);
            }
        }

        // draw chicken (sprite 0)
        write_sprite_to_kernel(
            1,
            chicken.y,
            chicken.x,
            chicken.jumping ? CHICKEN_JUMP : CHICKEN_STAND,
            0
        );

        // --- VSync wait for tear-free display ---
        wait_for_vsync();
    }

    // ── game over ────────────────────────────────────────────────────────────
    cleartiles(); clearSprites(); fill_sky_and_grass();
    write_text("gameover", 8, 12, 16);
    sleep(2);
    return 0;
}
