// screamjump_dynamic_start_double_buffered_vsync.c
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include "usbcontroller.h"
#include "vga_interface.h"
#include "audio_interface.h"

// ───── global handles & state ──────────────────────────────────────────────
int vga_fd, audio_fd;
struct controller_output_packet controller_state;

// ───── VSYNC ioctl definitions ──────────────────────────────────────────────
#ifndef VGA_WAIT_VSYNC
#define VGA_WAIT_VSYNC _IO('V', 0x01)
#endif
static inline void wait_for_vsync() {
    ioctl(vga_fd, VGA_WAIT_VSYNC, 0);
}

// ───── screen & physics ──────────────────────────────────────────────────────
#define LENGTH            640
#define WIDTH             480
#define TILE_SIZE         16
#define WALL              16
#define GRAVITY           +1

// ───── sprite dimensions ────────────────────────────────────────────────────
#define CHICKEN_W         32
#define CHICKEN_H         32

// ───── MIF indices ───────────────────────────────────────────────────────────
#define CHICKEN_STAND      8
#define CHICKEN_JUMP       11
#define TOWER_TILE_IDX     42

// ───── static tower & bars ──────────────────────────────────────────────────
#define PLATFORM_H         32
#define TOWER_HEIGHT       3
#define BAR_COUNT          4
#define BAR_HEIGHT_ROWS    2
#define BAR_SPEED_BASE     4
#define MIN_BAR_TILES      3
#define MAX_BAR_TILES     10
#define BAR_TILE_IDX      39

// ───── initial game params ──────────────────────────────────────────────────
#define INITIAL_LIVES      5
#define INIT_JUMP_VY     -20
#define BASE_DELAY       2000

// ───── double buffering for sprites ─────────────────────────────────────────
#define MAX_SPRITES_BUF   16
typedef struct { int y, x, tile, reg; } SpriteEntry;
static SpriteEntry spriteBuf[MAX_SPRITES_BUF];
static int spriteBufLen;
static inline void beginSprites() { spriteBufLen = 0; }
static inline void addSprite(int y, int x, int tile, int reg) {
    if (spriteBufLen < MAX_SPRITES_BUF)
        spriteBuf[spriteBufLen++] = (SpriteEntry){y, x, tile, reg};
}
static inline void flushSprites() {
    clearSprites();
    for (int i = 0; i < spriteBufLen; ++i)
        write_sprite_to_kernel(1,
            spriteBuf[i].y,
            spriteBuf[i].x,
            spriteBuf[i].tile,
            spriteBuf[i].reg);
}

// ───── double buffering for tiles ───────────────────────────────────────────
#define MAX_TILE_BUF     1024
typedef struct { int row, col, idx; } TileEntry;
static TileEntry tileBuf[MAX_TILE_BUF];
static int tileBufLen;
static inline void beginTiles() { tileBufLen = 0; }
static inline void addTile(int row, int col, int idx) {
    if (tileBufLen < MAX_TILE_BUF)
        tileBuf[tileBufLen++] = (TileEntry){row, col, idx};
}
static inline void flushTiles() {
    for (int i = 0; i < tileBufLen; ++i)
        write_tile_to_kernel(
            tileBuf[i].row,
            tileBuf[i].col,
            tileBuf[i].idx);
}

// ───── helper: random bar Y ──────────────────────────────────────────────────
static inline int randBarY(void) {
    return (rand() % ((WIDTH - BAR_HEIGHT_ROWS * TILE_SIZE - WALL) - (WALL + 40) + 1)) + (WALL + 40);
}

// ───── data structures ──────────────────────────────────────────────────────
typedef struct { int x, y, vy; bool jumping; } Chicken;
typedef struct { int x, y_px, length; } MovingBar;

// ───── encapsulated bar logic ───────────────────────────────────────────────
static void generateBars(MovingBar bars[], int barCount, int barSpeed) {
    static int spawnCounter;
    int cols = LENGTH / TILE_SIZE;
    for (int i = 0; i < barCount; ++i) {
        bars[i].x -= barSpeed;
        if (bars[i].x + bars[i].length * TILE_SIZE <= 0) {
            bars[i].x = LENGTH + (spawnCounter++ % barCount) * (LENGTH / barCount);
            bars[i].y_px = randBarY();
            bars[i].length = MIN_BAR_TILES + rand() % (MAX_BAR_TILES - MIN_BAR_TILES + 1);
        }
        int col0 = bars[i].x / TILE_SIZE;
        int row0 = bars[i].y_px / TILE_SIZE;
        for (int r = 0; r < BAR_HEIGHT_ROWS; ++r)
            for (int j = 0; j < bars[i].length; ++j) {
                int c = col0 + j;
                if (c >= 0 && c < cols)
                    addTile(row0 + r, c, BAR_TILE_IDX);
            }
    }
}

void *controller_input_thread(void *arg) {
    uint8_t ep;
    struct libusb_device_handle *ctrl = opencontroller(&ep);
    if (!ctrl) pthread_exit(NULL);
    while (true) {
        unsigned char buf[GAMEPAD_READ_LENGTH];
        int transferred;
        if (libusb_interrupt_transfer(ctrl, ep, buf, GAMEPAD_READ_LENGTH, &transferred, 0) == 0)
            usb_to_output(&controller_state, buf);
    }
}

void initChicken(Chicken *c) {
    c->x = 32;
    c->y = WIDTH - WALL - PLATFORM_H - CHICKEN_H * TOWER_HEIGHT;
    c->vy = 0;
    c->jumping = false;
}

void moveChicken(Chicken *c) {
    if (!c->jumping) return;
    c->y += c->vy;
    c->vy += GRAVITY;
}

int main(void) {
    // open devices
    vga_fd = open("/dev/vga_top", O_RDWR);
    audio_fd = open("/dev/fpga_audio", O_RDWR);
    if (vga_fd < 0 || audio_fd < 0) return -1;
    pthread_t tid;
    pthread_create(&tid, NULL, controller_input_thread, NULL);

    // ── initial draw ─────────────────────────────────────────────────────────
    cleartiles(); clearSprites(); fill_sky_and_grass();

    // ── init game ─────────────────────────────────────────────────────────────
    int lives = INITIAL_LIVES, score = 0, level = 1;
    int jumpVy = INIT_JUMP_VY, jumpDelay = BASE_DELAY;
    const int cols = LENGTH / TILE_SIZE;
    const int offset = 2;
    Chicken chicken; initChicken(&chicken);
    MovingBar bars[BAR_COUNT];
    srand(time(NULL));
    for (int i = 0; i < BAR_COUNT; ++i) {
        bars[i].x = LENGTH + i * (LENGTH / BAR_COUNT);
        bars[i].y_px = randBarY();
        bars[i].length = MIN_BAR_TILES + rand() % (MAX_BAR_TILES - MIN_BAR_TILES + 1);
    }

    // ── main loop ─────────────────────────────────────────────────────────────
    while (lives > 0) {
        int barSpeed = BAR_SPEED_BASE + (level - 1);

        // input & physics
        if (controller_state.b && !chicken.jumping) {
            chicken.vy = jumpVy; chicken.jumping = true; usleep(jumpDelay);
        }
        int prevY = chicken.y;
        moveChicken(&chicken);
        if (chicken.y < WALL + 40) chicken.y = WALL + 40;

        // collision & score
        if (chicken.vy > 0) {
            for (int b = 0; b < BAR_COUNT; ++b) {
                int by = bars[b].y_px;
                int botP = prevY + CHICKEN_H;
                int botN = chicken.y + CHICKEN_H;
                int wPx = bars[b].length * TILE_SIZE;
                if (botP <= by + BAR_HEIGHT_ROWS*TILE_SIZE && botN >= by &&
                    chicken.x+CHICKEN_W > bars[b].x && chicken.x < bars[b].x+wPx) {
                    chicken.y = by - CHICKEN_H;
                    chicken.vy = 0;
                    chicken.jumping = false;
                    score++; usleep(jumpDelay);
                    break;
                }
            }
        }
        if (chicken.y > WIDTH) { lives--; initChicken(&chicken); usleep(3000000); continue; }

        // render: HUD only clears sprites
        clearSprites();
        write_text("lives",5,1,offset); write_number(lives,1,offset+6);
        write_text("score",5,1,offset+12); write_number(score,1,offset+18);
        write_text("level",5,1,offset+28); write_number(level,1,offset+34);

        // tiles (bars + tower)
        beginTiles(); generateBars(bars,BAR_COUNT,barSpeed);
        for (int r=21;r<=29;++r)for(int c=0;c<=4;++c) addTile(r,c,TOWER_TILE_IDX);
        flushTiles();

        // sprites
        beginSprites(); addSprite(chicken.y,chicken.x,
            chicken.jumping?CHICKEN_JUMP:CHICKEN_STAND,0);
        flushSprites();

        // wait for next frame
        wait_for_vsync();
    }

    // game over
    cleartiles(); clearSprites(); fill_sky_and_grass();
    write_text("gameover",8,12,16); sleep(2);
    return 0;
}
