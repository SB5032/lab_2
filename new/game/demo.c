// screamjump_dynamic_start.c
// Adds a start screen: displays title and "Press any key to start", with restart support

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

#define LENGTH            640
#define WIDTH             480
#define TILE_SIZE          16
#define WALL              16
#define GRAVITY           +1

#define CLOUD_TILE       14
#define SUN_TILE         15

#define CHICKEN_STAND     8
#define CHICKEN_JUMP     11
#define CHICKEN_W        32
#define CHICKEN_H        32

#define TOWER_TILE_IDX   22
#define TOWER_X           16
#define TOWER_WIDTH      CHICKEN_W
#define PLATFORM_H        32
#define TOWER_BASE_Y    (WIDTH - WALL - PLATFORM_H)

#define BAR_TILE_IDX     23
#define BAR_TILE_COUNT    6
#define BAR_WIDTH         3
#define BAR_HEIGHT        2
#define BAR_SPEED         4
#define BAR_COUNT         4

#define INITIAL_LIVES     5
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

void display_start_screen() {
    cleartiles(); clearSprites(); fill_sky_and_grass();
    write_text("scream",6,13,13); write_text("jump",4,13,20);
    write_text("press",5,19,8); write_text("any",3,19,14);
    write_text("key",3,19,20); write_text("to",2,19,26);
    write_text("start",5,19,29);
}

void wait_for_restart() {
    write_text("press",5,21,12);
    write_text("b",1,21,18);
    write_text("to",2,21,20);
    write_text("restart",7,21,23);
    while (!controller_state.b) usleep(10000);
    while (controller_state.b) usleep(10000); // debounce
}

void run_game() {
    cleartiles(); clearSprites(); fill_sky_and_grass();
    int score=0, lives=INITIAL_LIVES;
    // write_text("Lives",0,0); write_number(lives,0,6);
    // write_text("Score",0,10); write_number(score,0,16);
    Chicken chicken; initChicken(&chicken);

    int sprite_regs[8] = {2,3,4,5,6,7,8};
    for(int i=0;i<6;i++){
        int x = i*(LENGTH/6) + (LENGTH/12) - CHICKEN_W/2;
        int y = WALL;
        write_sprite_to_kernel(1,y,x,CLOUD_TILE,sprite_regs[i]);
    }
    write_sprite_to_kernel(1, WALL, WALL, SUN_TILE, sprite_regs[6]);

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

        clearSprites();

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

        for(int r=(TOWER_BASE_Y - 3*PLATFORM_H)/TILE_SIZE; r<=TOWER_BASE_Y/TILE_SIZE; r++)
            for(int c=TOWER_X/TILE_SIZE; c<=(TOWER_X+TOWER_WIDTH)/TILE_SIZE; c++)
                write_tile_to_kernel(r,c, TOWER_TILE_IDX);

        write_sprite_to_kernel(1, chicken.y, chicken.x,
            chicken.jumping?CHICKEN_JUMP:CHICKEN_STAND, 0);

        usleep(16666);
    }

    cleartiles(); clearSprites(); fill_sky_and_grass();
    write_text("gameover",8,12,16); sleep(2);
}

int main(void) {
    if ((vga_fd=open("/dev/vga_top",O_RDWR))<0) return -1;
    if ((audio_fd=open("/dev/fpga_audio",O_RDWR))<0) return -1;
    pthread_t tid; pthread_create(&tid,NULL,controller_input_thread,NULL);

    while (true) {
        display_start_screen();
        while (!(controller_state.a||controller_state.b)) usleep(10000);
        while (controller_state.a || controller_state.b) usleep(10000);

        run_game();
        wait_for_restart();
    }

    return 0;
}
