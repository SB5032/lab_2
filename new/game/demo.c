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

// ───── sprite indices ────────────────────────────────────────────────────────
#define CLOUD_TILE       14    // cloud sprite index
#define SUN_TILE         15    // sun sprite index

// ───── chicken sprite ────────────────────────────────────────────────────────
#define CHICKEN_STAND     8
#define CHICKEN_JUMP     11
#define CHICKEN_W        32
#define CHICKEN_H        32

// ───── tower tiles ───────────────────────────────────────────────────────────
#define TOWER_TILE_IDX   40
#define TOWER_X           16
#define TOWER_WIDTH      CHICKEN_W
#define PLATFORM_H        32
#define TOWER_BASE_Y    (WIDTH - WALL - PLATFORM_H)

// ───── moving bars (background tiles) ───────────────────────────────────────
#define BAR_TILE_IDX     38    // tile index for moving bars
#define BAR_TILE_COUNT    6    // tiles used: 1 up-left,2 mid-up,3 up-right,4 bot-left,5 mid-down,6 bot-right
#define BAR_WIDTH         3    // columns per bar
#define BAR_HEIGHT        2    // rows per bar
#define BAR_SPEED         4    // pixels per frame
#define BAR_COUNT         4    // number of bars

// ───── lives/score & controller ──────────────────────────────────────────────
#define INITIAL_LIVES     5

// ───── initial parameters ─────────────────────────────────────────────────────
#define INIT_JUMP_VY    -20
#define BASE_SPEED       4
#define BASE_DELAY     2000

int vga_fd, audio_fd;
struct controller_output_packet controller_state;
bool towerEnabled = true;

typedef struct { int x, y, vy; bool jumping; } Chicken;
typedef struct { int x_px, y_px; } MovingBar;

enum { BAR_UL=0, BAR_MU, BAR_UR, BAR_BL, BAR_MD, BAR_BR };

void *controller_input_thread(void *arg) {
    uint8_t ep;
    struct libusb_device_handle *ctrl = opencontroller(&ep);
    if (!ctrl) pthread_exit(NULL);
    while (1) {
        unsigned char buf[GAMEPAD_READ_LENGTH]; int transferred;
        if (libusb_interrupt_transfer(ctrl, ep, buf, GAMEPAD_READ_LENGTH, &transferred, 0)==0)
            usb_to_output(&controller_state, buf);
    }
}

void initChicken(Chicken *c) {
    c->x = 32;
    c->y = TOWER_BASE_Y - CHICKEN_H*3;
    c->vy = 0;
    c->jumping = false;
}
void moveChicken(Chicken *c) {
    if (!c->jumping && towerEnabled) return;
    c->y += c->vy;
    c->vy += GRAVITY;
}

int main(void) {
    if ((vga_fd=open("/dev/vga_top",O_RDWR))<0) return -1;
    if ((audio_fd=open("/dev/fpga_audio",O_RDWR))<0) return -1;
    pthread_t tid; pthread_create(&tid,NULL,controller_input_thread,NULL);

    // start screen
    cleartiles(); clearSprites(); fill_sky_and_grass();
    write_text("scream",6,13,13); write_text("jump",4,13,20);
    write_text("press",5,19,8); write_text("any",3,19,14);
    write_text("key",3,19,20); write_text("to",2,19,26);
    write_text("start",5,19,29);
    while (!(controller_state.a||controller_state.b)) usleep(10000);

    // init game
    cleartiles(); clearSprites(); fill_sky_and_grass();
    int score=0, lives=INITIAL_LIVES;
    write_text("Lives",0,0,1); write_number(lives,0,6);
    write_text("Score",0,10,15); write_number(score,0,16);
    Chicken chicken; initChicken(&chicken);

    // setup clouds and sun sprites
    int sprite_regs[8] = {2,3,4,5,6,7,8};
    // clouds: regs 2-7 evenly across upper half
    for(int i=0;i<6;i++){
        int x = i*(LENGTH/6) + (LENGTH/12) - CHICKEN_W/2;
        int y = WALL;
        write_sprite_to_kernel(1,y,x,CLOUD_TILE,sprite_regs[i]);
    }
    // sun: reg 8 in upper-left quadrant
    write_sprite_to_kernel(1, WALL, WALL, SUN_TILE, sprite_regs[6]);

    // init moving bars
    MovingBar bars[BAR_COUNT];
    for(int i=0;i<BAR_COUNT;i++){
        bars[i].x_px = i*(LENGTH/BAR_COUNT);
        bars[i].y_px = WALL + i*(WIDTH/5);
    }

    int platformSpeed = BASE_SPEED;
    int jumpVy = INIT_JUMP_VY;
    int jumpDelay = BASE_DELAY;

    while(lives>0){
        if(controller_state.b && !chicken.jumping){
            chicken.vy=jumpVy; chicken.jumping=true; play_sfx(0);
        }
        int prevY=chicken.y; moveChicken(&chicken);
        if(chicken.y< WALL) chicken.y= WALL;

        // clear only sprites, keep background tiles
        clearSprites();

        // redraw bars (tiles)
        for(int b=0;b<BAR_COUNT;b++){
            bars[b].x_px -= BAR_SPEED;
            if(bars[b].x_px < -BAR_WIDTH*TILE_SIZE) bars[b].x_px = LENGTH;
            int sx = bars[b].x_px / TILE_SIZE;
            int sy = bars[b].y_px / TILE_SIZE;
            for(int row=0;row<BAR_HEIGHT;row++){
                for(int col=0;col<BAR_WIDTH;col++){
                    int tile_idx;
                    if(row==0){ tile_idx = (col==0?BAR_UL: col==BAR_WIDTH-1?BAR_UR: BAR_MU); }
                    else{ tile_idx = (col==0?BAR_BL: col==BAR_WIDTH-1?BAR_BR: BAR_MD); }
                    write_tile_to_kernel(sy+row, sx+col, tile_idx);
                }
            }
        }

        // redraw tower
        for(int r=(TOWER_BASE_Y - 3*PLATFORM_H)/TILE_SIZE; r<=TOWER_BASE_Y/TILE_SIZE; r++)
            for(int c=TOWER_X/TILE_SIZE; c<=(TOWER_X+TOWER_WIDTH)/TILE_SIZE; c++)
                write_tile_to_kernel(r,c, TOWER_TILE_IDX);

        // redraw sprites: clouds and sun remain, redraw chicken
        write_sprite_to_kernel(1, chicken.y, chicken.x,
            chicken.jumping?CHICKEN_JUMP:CHICKEN_STAND, 0);

        usleep(16666);
    }

    cleartiles(); clearSprites(); fill_sky_and_grass();
    write_text("gameover",8,12,16); sleep(2);
    return 0;
}
