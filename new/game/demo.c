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
#define LENGTH            640   // VGA width
#define WIDTH             480   // VGA height
#define WALL              16    // top/bottom margin
#define JUMP_VY           -20   // base jump velocity
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

// ───── moving platforms ──────────────────────────────────────────────────────
#define MAX_PLATFORMS     3
#define PLATFORM_W      (4*32)   // 4 sprites × 32px
#define PLATFORM_H         32    // platform height
#define PLATFORM_SPACING  (LENGTH / MAX_PLATFORMS)
#define PLATFORM_REG_BASE  1     // sprite register base

// ───── static tower ─────────────────────────────────────────────────────────
#define TOWER_X           16
#define TOWER_WIDTH      CHICKEN_W
#define TOWER_HEIGHT      3     // stacked 3 sprites high originally
#define TOWER_BASE_Y    (WIDTH - WALL - PLATFORM_H)

// ───── lives/score & controller ──────────────────────────────────────────────
#define INITIAL_LIVES     5

// ───── level & difficulty ────────────────────────────────────────────────────
#define BASE_PLATFORM_SPEED 4
#define BASE_JUMP_DELAY   2000  // microseconds

int vga_fd, audio_fd;
struct controller_output_packet controller_state;
bool towerEnabled = true;

typedef struct { int x, y, vy; bool jumping; } Chicken;
typedef struct { int x, y; bool special; int specialIdx; } Platform;

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
    c->x = 32; c->y = 304; c->vy = 0; c->jumping = false;
}
void moveChicken(Chicken *c) {
    if (!c->jumping && towerEnabled) return;
    c->y += c->vy; c->vy += GRAVITY;
}

int main(void) {
    if ((vga_fd = open("/dev/vga_top", O_RDWR)) < 0) return -1;
    if ((audio_fd = open("/dev/fpga_audio", O_RDWR)) < 0) return -1;
    pthread_t tid; pthread_create(&tid, NULL, controller_input_thread, NULL);

    cleartiles(); clearSprites(); fill_sky_and_grass();
    write_text("scream",6,13,13); write_text("jump",4,13,20);
    write_text("press",5,19,8); write_text("any",3,19,14);
    write_text("key",3,19,20); write_text("to",2,19,26);
    write_text("start",5,19,29);
    while (!(controller_state.a||controller_state.b||controller_state.x||
             controller_state.y||controller_state.start||controller_state.select||
             controller_state.updown||controller_state.leftright)) usleep(10000);

    cleartiles(); clearSprites(); fill_sky_and_grass();
    int score=0, lives=INITIAL_LIVES, level=1;
    int platformSpeed=BASE_PLATFORM_SPEED, jumpDelay=BASE_JUMP_DELAY;
    int jumpVy=JUMP_VY;
    write_score(score); write_number(lives,0,0); write_number(level,0,38);

    Chicken chicken; initChicken(&chicken);
    bool landed=false, blockFalling=false;
    int fallX=0, fallY=0, fallReg=0;

    srand(time(NULL));
    int minY=WALL+40, maxY=WIDTH-WALL-PLATFORM_H;
    Platform plats[MAX_PLATFORMS];
    for (int i=0;i<MAX_PLATFORMS;i++){
        plats[i].x=LENGTH + i*PLATFORM_SPACING;
        int low,high;
        if(i==0){ low=chicken.y-150; high=chicken.y+150; }
        else{ low=plats[i-1].y-150; high=plats[i-1].y+150;
            if(plats[i-1].y>maxY-150) high=plats[i-1].y;
        }
        if(low<minY) low=minY; if(high>maxY) high=maxY;
        plats[i].y=rand()%(high-low+1)+low;
        plats[i].special = (level==3 && rand()%4==0);
        if(plats[i].special) plats[i].specialIdx = rand()%4;
    }

    while (lives>0) {
        if(controller_state.b && !chicken.jumping){
            chicken.vy=jumpVy; chicken.jumping=true; landed=false; play_sfx(0);
        }
        int prevY=chicken.y; moveChicken(&chicken);
        if(chicken.y<WALL) chicken.y=WALL;

        for(int i=0;i<MAX_PLATFORMS;i++){
            plats[i].x-=platformSpeed;
            if(plats[i].x<-PLATFORM_W){
                int maxX=plats[0].x;
                for(int j=1;j<MAX_PLATFORMS;j++) if(plats[j].x>maxX) maxX=plats[j].x;
                plats[i].x=maxX+PLATFORM_SPACING;
                int prevIdx=(i+MAX_PLATFORMS-1)%MAX_PLATFORMS;
                int low=plats[prevIdx].y-150, high=plats[prevIdx].y+150;
                if(plats[prevIdx].y>maxY-150) high=plats[prevIdx].y;
                if(low<minY) low=minY; if(high>maxY) high=maxY;
                plats[i].y=rand()%(high-low+1)+low;
                plats[i].special=(level==3 && rand()%4==0);
                if(plats[i].special) plats[i].specialIdx=rand()%4;
            }
        }

        if(chicken.vy>0){
            towerEnabled=false;
            for(int i=0;i<MAX_PLATFORMS;i++){
                int botPrev=prevY+CHICKEN_H, botNow=chicken.y+CHICKEN_H;
                if(botPrev<=plats[i].y && botNow>=plats[i].y &&
                   chicken.x+CHICKEN_W>plats[i].x &&
                   chicken.x<plats[i].x+PLATFORM_W){
                    chicken.y=plats[i].y-CHICKEN_H; chicken.vy=0; chicken.jumping=false;
                    if(!landed){
                        score++; write_score(score); landed=true;
                        if(score%20==0){
                            level++; write_number(level,0,38);
                            platformSpeed=BASE_PLATFORM_SPEED+(level-1);
                            jumpDelay=BASE_JUMP_DELAY-(level-1)*400;
                            if(jumpDelay<500) jumpDelay=500;
                            if(level>=3) jumpVy=JUMP_VY-(level-2)*5;
                        }
                        if(level==3 && plats[i].special){
                            blockFalling=true;
                            fallReg=PLATFORM_REG_BASE+i*4+plats[i].specialIdx;
                            fallX=plats[i].x+plats[i].specialIdx*32;
                            fallY=plats[i].y;
                            plats[i].special=false;
                        }
                        usleep(jumpDelay);
                    }
                    break;
                }
            }
        }

        if(chicken.y>WIDTH){
            lives--; towerEnabled=true; write_number(lives,0,0);
            initChicken(&chicken); landed=false; usleep(3000000);
            continue;
        }

        clearSprites(); fill_sky_and_grass();
        // draw tower
        {
            int rowStart = (TOWER_BASE_Y - TOWER_HEIGHT * PLATFORM_H) / 16;
            int rowEnd   = TOWER_BASE_Y / 16;
            int colStart = TOWER_X / 16;
            int colEnd   = (TOWER_X + TOWER_WIDTH) / 16;
            for (int r = 21; r < 30; ++r)
                for (int c = 0; c < 5; ++c)
                    write_tile_to_kernel(r, c, towerEnabled ? TOWER_TILE_IDX : 0);
        }
        // falling special block
        if(blockFalling){ fallY+=4;
            write_sprite_to_kernel(1,fallY,fallX,SPECIAL_TILE,fallReg);
            if(fallY>WIDTH) blockFalling=false;
        }
        // draw platforms
        for(int i=0;i<MAX_PLATFORMS;i++){
            for(int k=0;k<4;k++){
                int tile=PLATFORM_TILE;
                if(level==3 && plats[i].special && k==plats[i].specialIdx)
                    tile=SPECIAL_TILE;
                write_sprite_to_kernel(
                    1,plats[i].y,plats[i].x+k*32,
                    tile, PLATFORM_REG_BASE+i*4+k);
            }
        }
        // draw chicken
        write_sprite_to_kernel(
            1,chicken.y,chicken.x,
            chicken.jumping?CHICKEN_JUMP:CHICKEN_STAND, 0);
        usleep(16666);
    }

    cleartiles(); clearSprites(); fill_sky_and_grass();
    write_text((unsigned char*)"gameover",8,12,16);
    sleep(2);
    return 0;
}
