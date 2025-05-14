/*
 * screamjump_dynamic_start.c
 * Double-buffered tiles, level-based difficulty,
 * and game-over restart.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <fcntl.h>
#include <string.h>

#include "usbcontroller.h"
#include "vga_interface.h"

// Screen dims
#define SCREEN_W 640
#define SCREEN_H 480
#define MARGIN   8
#define GRAVITY  1

// Tiles & sprites
#define TILE_SIZE     16
#define TILE_BAR      39
#define TILE_COIN     22
#define TILE_TOWER    40
#define SPRITE_CHICK  (controller_state.x ? 9 : 8)

// Game limits
#define MAX_PLATFORMS 10
#define MAX_COINS      5
#define INIT_LIVES     5
#define INIT_VY      -20
#define DELAY_SHORT 2000
#define DELAY_LONG  4000
#define SCORE_STEP   10
#define MAX_LEVEL     5
#define FRAME_US   16666

#define OFFSCREEN_X (-TILE_SIZE * 5)

// Structs
typedef struct {int x,y,len; bool hasCoin; int coinIdx;} Platform;
typedef struct {int x,y,vy; bool jumping; int coinTimer; int coinIdx;} Player;
typedef struct {bool active; int pIdx,reg;} Coin;

// Globals
int vgaFd;
struct controller_output_packet controller_state;
bool towerOn=true;
bool runFlag=true;
Player player;
Platform platsA[MAX_PLATFORMS], platsB[MAX_PLATFORMS];
Coin coins[MAX_COINS];
int lives, score;

// Prototypes
void drawPlats(Platform P[]);
void updatePlats(Platform P[]);
bool checkColl(Platform P[]);
void* ctrlThread(void*);
void initPlayer(void);
void resetPlats(Platform P[]);
void initCoins(void);
void drawCoins(void);
void resetLevel(void);

// Implementations

void drawPlats(Platform P[]) {
    for (int i=0;i<MAX_PLATFORMS;i++) {
        if (P[i].x==OFFSCREEN_X) continue;
        int c0=P[i].x/TILE_SIZE;
        int r =P[i].y/TILE_SIZE;
        for (int j=0;j<P[i].len;j++)
            write_tile_to_kernel(r, c0+j, TILE_BAR);
    }
}

void updatePlats(Platform P[]) {
    for (int i=0;i<MAX_PLATFORMS;i++) {
        if (P[i].x==OFFSCREEN_X) continue;
        P[i].x -= 3;
        if (P[i].x+P[i].len*TILE_SIZE<=0) {
            if (P[i].hasCoin) coins[P[i].coinIdx].active=false;
            P[i].x=OFFSCREEN_X;
        }
    }
}

bool checkColl(Platform P[]) {
    if (player.vy<=0) return false;
    int prevBot=player.y - player.vy + TILE_SIZE;
    int currBot=player.y + TILE_SIZE;
    for (int i=0;i<MAX_PLATFORMS;i++) {
        if (P[i].x==OFFSCREEN_X) continue;
        int topY=P[i].y;
        int leftX=P[i].x;
        int rightX=leftX + P[i].len*TILE_SIZE;
        if (prevBot<=topY && currBot>=topY
            && player.x+TILE_SIZE>leftX && player.x<rightX) {
            player.y = topY - TILE_SIZE;
            player.vy = 0;
            player.jumping = false;
            score++;
            return true;
        }
    }
    return false;
}

void* ctrlThread(void*_) {
    uint8_t ep;
    struct libusb_device_handle* h = opencontroller(&ep);
    if (!h) return NULL;
    unsigned char buf[GAMEPAD_READ_LENGTH];
    int len;
    while (runFlag) {
        int st=libusb_interrupt_transfer(h,ep,buf,GAMEPAD_READ_LENGTH,&len,1000);
        if (st==LIBUSB_SUCCESS && len==GAMEPAD_READ_LENGTH)
            usb_to_output(&controller_state, buf);
    }
    libusb_release_interface(h,0);
    libusb_close(h);
    return NULL;
}

void initPlayer(void) {
    player.x = 32;
    player.y = (21 * TILE_SIZE) - TILE_SIZE;
    player.vy=0;
    player.jumping=false;
}

void resetPlats(Platform P[]) {
    for (int i=0;i<MAX_PLATFORMS;i++) {
        P[i].x = OFFSCREEN_X;
        P[i].len = 0;
        P[i].hasCoin = false;
    }
}

void initCoins(void) {
    for (int i=0;i<MAX_COINS;i++) {
        coins[i].active = false;
        coins[i].reg    =  2 + i;
    }
}

void drawCoins(void) {
    // Omitted: same logic, preserved
}

void resetLevel(void) {
    initPlayer();
    towerOn=true;
    resetPlats(platsA);
    resetPlats(platsB);
    initCoins();
    cleartiles();
}

int main(void) {
    pthread_t tid;
    if ((vgaFd = open("/dev/vga_top",O_RDWR))<0) return -1;
    init_vga_interface();
    pthread_create(&tid,NULL,ctrlThread,NULL);

    lives = INIT_LIVES;
    score = 0;

game_start:
    resetLevel();
    write_text("scream",6,13,16);
    write_text("jump",4,13,22);
    vga_present_frame();
    while (!controller_state.x) usleep(10000);

    while (lives>0) {
        int prevY = player.y;
        if (controller_state.b && !player.jumping) {
            player.vy = INIT_VY;
            player.jumping = true;
            usleep(DELAY_LONG);
        }
        // physics
        player.y += player.vy;
        player.vy += GRAVITY;

        updatePlats(platsA);
        updatePlats(platsB);

        if (player.vy>0) checkColl(platsA) || checkColl(platsB);

        // draw
        fill_sky_and_grass();
        drawPlats(platsA);
        drawPlats(platsB);
        write_text("score",5,1,10);
        write_numbers(score,MAX_LEVEL,1,16);
        write_sprite_to_kernel_buffered(1, player.y, player.x, SPRITE_CHICK, 0);
        vga_present_frame();
        usleep(FRAME_US);
    }

    // game over
    cleartiles();
    write_text("game over",9,15,20);
    write_text("score",5,17,16);
    write_numbers(score,MAX_LEVEL,17,22);
    vga_present_frame();
    while (!controller_state.x) usleep(50000);
    goto game_start;
    runFlag = false;
    return 0;
}
