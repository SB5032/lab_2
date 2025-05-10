
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
#define WALL              16    // top/bottom margin (pixels)
#define GRAVITY           +1

// ───── sprite dimensions ─────────────────────────────────────────────────────
#define CHICKEN_W         32
#define CHICKEN_H         32

// ───── MIF indices ───────────────────────────────────────────────────────────
#define CHICKEN_STAND     8     // chicken standing tile
#define CHICKEN_JUMP     11     // chicken jumping tile
#define PLATFORM_TILE    14     // normal platform tile
#define SPECIAL_TILE     15     // special falling tile
#define TOWER_TILE_IDX   22     // static tower tile

// ───── static tower ─────────────────────────────────────────────────────────
#define TOWER_X           16
#define TOWER_WIDTH      CHICKEN_W
#define TOWER_HEIGHT      3     // stacked 3 sprites high
#define PLATFORM_H        32
#define TOWER_BASE_Y    (WIDTH - WALL - PLATFORM_H)

// ───── platform constants ───────────────────────────────────────────────────
#define MAX_SPRITES       11    // total sprite registers for platforms
#define PLATFORM_REG_BASE  1    // sprite register base

// ───── lives/score & controller ──────────────────────────────────────────────
#define INITIAL_LIVES     5

// ───── initial parameters ─────────────────────────────────────────────────────
#define INIT_PLATFORMS    4     // starting number of platforms
#define INIT_JUMP_VY     -20    // base jump velocity
#define BASE_SPEED        4     // base platform speed (pixels/frame)
#define BASE_DELAY      2000    // base jump delay (µs)

// ───── moving bars (background tiles) ───────────────────────────────────────
#define BAR_TILE_IDX      23    // tile index for moving bars
#define BAR_WIDTH          5    // columns per bar
#define BAR_HEIGHT         2    // rows per bar
#define BAR_SPEED          4    // pixels per frame
#define BAR_Y_PX1        200    // top Y pixel for bar 1
#define BAR_Y_PX2        300    // top Y pixel for bar 2
#define BAR_COUNT          2

int vga_fd, audio_fd;
struct controller_output_packet controller_state;
bool towerEnabled = true;

// ───── data structures ───────────────────────────────────────────────────────
typedef struct { int x, y, vy; bool jumping; } Chicken;
typedef struct { int x, y; int segCount; bool special; int specialIdx; } Platform;
typedef struct { int x; int y_px; } MovingBar;

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

int main(void) {
    if ((vga_fd = open("/dev/vga_top", O_RDWR)) < 0) return -1;
    if ((audio_fd = open("/dev/fpga_audio", O_RDWR)) < 0) return -1;
    pthread_t tid; pthread_create(&tid, NULL, controller_input_thread, NULL);

    // start screen
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

    // init game
    cleartiles(); clearSprites(); fill_sky_and_grass();
    int score = 0, lives = INITIAL_LIVES, level = 1;
    int jumpVy = INIT_JUMP_VY;
    int platformSpeed = BASE_SPEED;
    int jumpDelay = BASE_DELAY;
    int numPlatforms = INIT_PLATFORMS;
    int platformGap = LENGTH / numPlatforms;
    write_text("Lives", 0, 0, 1);   write_number(lives, 6, 7);
    write_text("Score", 0, 10, 15); write_number(score, 0, 21);
    write_text("Level", 0, 20, 31); write_number(level, 26, 38);

    Chicken chicken; initChicken(&chicken);
    bool landed = false, blockFalling = false;
    int fallX = 0, fallY = 0, fallReg = 0;

    srand(time(NULL));
    int minY = WALL + 40, maxY = WIDTH - WALL - PLATFORM_H;
    Platform plats[MAX_SPRITES];
    for (int i = 0; i < numPlatforms; i++) {
        plats[i].x = LENGTH + i * platformGap;
        int maxSeg = MAX_SPRITES / numPlatforms;
        plats[i].segCount = rand() % maxSeg + 1;
        int low = chicken.y - 150, high = chicken.y + 150;
        if (low < minY) low = minY;
        if (high > maxY) high = maxY;
        plats[i].y = rand() % (high - low + 1) + low;
        plats[i].special = (level == 3 && rand() % plats[i].segCount == 0);
        if (plats[i].special)
            plats[i].specialIdx = rand() % plats[i].segCount;
    }

    // initialize moving bars
    MovingBar bars[BAR_COUNT] = {
        { LENGTH,    BAR_Y_PX1 },
        { LENGTH/2,  BAR_Y_PX2 }
    };

    while (lives > 0) {
        if (controller_state.b && !chicken.jumping) {
            chicken.vy = jumpVy;
            chicken.jumping = true;
            landed = false;
            play_sfx(0);
        }
        int prevY = chicken.y;
        moveChicken(&chicken);
        if (chicken.y < minY) chicken.y = minY;

        // move & respawn platforms
        for (int i = 0; i < numPlatforms; i++) {
            plats[i].x -= platformSpeed;
            int width = plats[i].segCount * CHICKEN_W;
            if (plats[i].x < -width) {
                int mx = plats[0].x;
                for (int j = 1; j < numPlatforms; j++)
                    if (plats[j].x > mx) mx = plats[j].x;
                plats[i].x = mx + platformGap;
                int maxSeg = MAX_SPRITES / numPlatforms;
                plats[i].segCount = rand() % maxSeg + 1;
                int prevIdx = (i + numPlatforms - 1) % numPlatforms;
                int low = plats[prevIdx].y - 150;
                int high = plats[prevIdx].y + 150;
                if (low < minY) low = minY;
                if (high > maxY) high = maxY;
                plats[i].y = rand() % (high - low + 1) + low;
                plats[i].special = (level == 3 && rand() % plats[i].segCount == 0);
                if (plats[i].special)
                    plats[i].specialIdx = rand() % plats[i].segCount;
            }
        }

        // collision & score
        if (chicken.vy > 0) {
            towerEnabled = false;
            bool collided = false;

            // check moving bars collision
            for (int b = 0; b < BAR_COUNT; b++) {
                int barY_px = bars[b].y_px;
                int botPrev = prevY + CHICKEN_H;
                int botNow  = chicken.y + CHICKEN_H;
                if (botPrev <= barY_px + BAR_HEIGHT * TILE_SIZE &&
                    botNow >= barY_px &&
                    chicken.x + CHICKEN_W > bars[b].x &&
                    chicken.x < bars[b].x + BAR_WIDTH * TILE_SIZE) {
                    chicken.y       = barY_px - CHICKEN_H;
                    chicken.vy      = 0;
                    chicken.jumping = false;
                    if (!landed) {
                        score++;
                        write_number(score, 0, 16);
                        landed = true;
                    }
                    usleep(jumpDelay);
                    collided = true;
                    break;
                }
            }

            // existing platform collision
            if (!collided) {
                for (int i = 0; i < numPlatforms; i++) {
                    int botPrev = prevY + CHICKEN_H;
                    int botNow  = chicken.y + CHICKEN_H;
                    int width   = plats[i].segCount * CHICKEN_W;
                    if (botPrev <= plats[i].y && botNow >= plats[i].y &&
                        chicken.x + CHICKEN_W > plats[i].x &&
                        chicken.x < plats[i].x + width) {
                        chicken.y = plats[i].y - CHICKEN_H;
                        chicken.vy = 0;
                        chicken.jumping = false;
                        if (!landed) {
                            score++;
                            write_number(score, 0, 16);
                            landed = true;
                            if (score % 20 == 0) {
                                level++;
                                write_number(level, 0, 26);
                                platformSpeed = BASE_SPEED + (level - 1);
                                platformGap    = LENGTH / (INIT_PLATFORMS + level - 1);
                                jumpDelay      = BASE_DELAY - (level - 1) * 400;
                                if (jumpDelay < 500) jumpDelay = 500;
                                if (level >= 3)
                                    jumpVy = INIT_JUMP_VY - (level - 2) * 5;
                            }
                            if (level == 3 && plats[i].special) {
                                blockFalling = true;
                                fallReg      = PLATFORM_REG_BASE + i * plats[i].segCount + plats[i].specialIdx;
                                fallX        = plats[i].x + plats[i].specialIdx * CHICKEN_W;
                                fallY        = plats[i].y;
                                plats[i].special = false;
                            }
                            usleep(jumpDelay);
                        }
                        break;
                    }
                }
            }
        }

        if (chicken.y > WIDTH) {
            lives--;
            write_number(lives, 0, 6);
            towerEnabled = true;
            initChicken(&chicken);
            landed = false;
            usleep(3000000);
            continue;
        }

        // redraw frame
        clearSprites();
        fill_sky_and_grass();

        // draw moving bars
        for (int b = 0; b < BAR_COUNT; b++) {
            bars[b].x -= BAR_SPEED;
            if (bars[b].x + BAR_WIDTH * TILE_SIZE <= 0)
                bars[b].x = LENGTH;

            int startCol = bars[b].x / TILE_SIZE;
            int maxCols  = LENGTH / TILE_SIZE;
            int barRow   = bars[b].y_px / TILE_SIZE;
            int endRow   = barRow + BAR_HEIGHT - 1;

            for (int r = barRow; r <= endRow; r++) {
                for (int i = 0; i < BAR_WIDTH; i++) {
                    int col = startCol + i;
                    if (col >= 0 && col < maxCols) {
                        write_tile_to_kernel(r, col, BAR_TILE_IDX);
                    }
                }
            }
        }

        // draw tower
        for (int r = (TOWER_BASE_Y - TOWER_HEIGHT * PLATFORM_H) / TILE_SIZE;
             r <= TOWER_BASE_Y / TILE_SIZE; r++) {
            for (int c = TOWER_X / TILE_SIZE;
                 c <= (TOWER_X + TOWER_WIDTH) / TILE_SIZE; c++) {
                write_tile_to_kernel(r, c,
                    towerEnabled ? TOWER_TILE_IDX : 0);
            }
        }

        // falling block
        if (blockFalling) {
            fallY += 4;
            write_sprite_to_kernel(1, fallY, fallX, SPECIAL_TILE, fallReg);
            if (fallY > WIDTH) blockFalling = false;
        }

        // draw platforms
        for (int i = 0; i < numPlatforms; i++) {
            for (int k = 0; k < plats[i].segCount; k++) {
                int tile = PLATFORM_TILE;
                if (level == 3 && plats[i].special && k == plats[i].specialIdx)
                    tile = SPECIAL_TILE;
                write_sprite_to_kernel(
                    1,
                    plats[i].y,
                    plats[i].x + k * CHICKEN_W,
                    tile,
                    PLATFORM_REG_BASE + i * plats[i].segCount + k
                );
            }
        }

        // draw chicken
        write_sprite_to_kernel(
            1,
            chicken.y,
            chicken.x,
            chicken.jumping ? CHICKEN_JUMP : CHICKEN_STAND,
            0
        );

        usleep(16666);
    }

    cleartiles(); clearSprites(); fill_sky_and_grass();
    write_text("gameover", 8, 12, 16);
    sleep(2);
    return 0;
}
