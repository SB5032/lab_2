// screamjump_dynamic_start.c
// Adds a start screen and a 3×2 tower on the first screen.

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

// screen & physics
#define LENGTH        640   // VGA width
#define WIDTH         480   // VGA height
#define WALL           16   // margin
#define JUMP_VY      -12
#define GRAVITY        +1

// sprite dimensions
#define CHICKEN_W      32
#define CHICKEN_H      32

// platform config
#define MAX_PLATFORMS   4
#define PLATFORM_H      32
#define PLATFORM_W    (4*32)
#define PLATFORM_SPEED  2
#define PLATFORM_SPACING (LENGTH / MAX_PLATFORMS)

// sprite indices
#define CHICKEN_STAND   8
#define CHICKEN_JUMP   11
#define PLATFORM_SPRITE_IDX 14

// static tower 3 rows × 2 columns
#define TOWER_X         16
#define TOWER_ROWS      3
#define TOWER_COLS      2
#define TOWER_CELLS    (TOWER_ROWS * TOWER_COLS)
#define TOWER_WIDTH    (TOWER_COLS * CHICKEN_W)
#define TOWER_REG_BASE  0    // regs 0…5
#define TOWER_BASE_Y   (WIDTH - WALL - PLATFORM_H)

// platform sprite registers
#define PLATFORM_REG_BASE (TOWER_CELLS) // starts at reg 6…21

// chicken sprite reg
#define CHICKEN_REG     (PLATFORM_REG_BASE + MAX_PLATFORMS*4) // =6+16=22

// game settings
#define INITIAL_LIVES   5

int vga_fd, audio_fd;
struct controller_output_packet controller_state;
bool towerEnabled = true;

typedef struct {int x, y, vy; bool jumping;} Chicken;
typedef struct {int x, y;} Platform;

// controller input thread
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
    libusb_close(ctrl);
    libusb_exit(NULL);
    pthread_exit(NULL);
}

// initialize chicken on top of tower
void initChicken(Chicken *c) {
    c->x = TOWER_X;
    c->y = TOWER_BASE_Y - CHICKEN_H * TOWER_ROWS;
    c->vy = 0;
    c->jumping = false;
}

// apply physics
void moveChicken(Chicken *c) {
    if (!c->jumping && towerEnabled) return;
    c->y += c->vy;
    c->vy += GRAVITY;
}

int main(void) {
    // open devices
    if ((vga_fd   = open("/dev/vga_top",    O_RDWR)) < 0) return -1;
    if ((audio_fd = open("/dev/fpga_audio", O_RDWR)) < 0) return -1;

    // start controller thread
    pthread_t tid;
    pthread_create(&tid, NULL, controller_input_thread, NULL);

    // start screen
    cleartiles();
    clearSprites();
    int index = 8;
        write_text("scream", 6, 14, 13);
        write_text("jump", 4, 14, 20);

        write_text("press", 5, 20, index);
        write_text("any", 3, 20, index + 6);
        write_text("key", 3, 20, index + 10);
        write_text("to", 2, 20, index + 14);
        write_text("start", 5, 20, index + 17);
    // write_text((unsigned char*)"ScreamJump Chicken", 18, 10, 8);
    // write_text((unsigned char*)"Press any key to start", 22, 12, 16);
    while (!(
        controller_state.a || controller_state.b||
        controller_state.x || controller_state.y||
        controller_state.start||controller_state.select||
        controller_state.updown||controller_state.leftright)) {
        usleep(10000);
    }

    // initialize game
    cleartiles(); clearSprites();
    int score = 0, lives = INITIAL_LIVES;
    write_score(score);
    write_number(lives, 0, 0);

    // seed platforms
    srand(time(NULL));
    Platform plats[MAX_PLATFORMS];
    for (int i=0; i<MAX_PLATFORMS; i++) {
        plats[i].x = LENGTH + i*PLATFORM_SPACING;
        plats[i].y = rand() % (WIDTH-2*WALL-PLATFORM_H) + WALL;
    }

    Chicken chicken;
    initChicken(&chicken);

    // main loop
    while (lives>0) {
        // jump on B
        if (controller_state.b && !chicken.jumping) {
            chicken.vy = JUMP_VY;
            chicken.jumping = true;
            play_sfx(0);
        }

        int prevY = chicken.y;
        moveChicken(&chicken);

        // update platforms
        for (int i=0; i<MAX_PLATFORMS; i++) {
            plats[i].x -= PLATFORM_SPEED;
            if (plats[i].x < -PLATFORM_W) {
                int maxX = plats[0].x;
                for (int j=1; j<MAX_PLATFORMS; j++) if (plats[j].x>maxX) maxX=plats[j].x;
                plats[i].x = maxX + PLATFORM_SPACING;
                int prev = (i+MAX_PLATFORMS-1)%MAX_PLATFORMS;
                int delta = (rand()%PLATFORM_H) - PLATFORM_H/2;
                int newY = plats[prev].y + delta;
                if (newY< WALL) newY= WALL;
                if (newY>WIDTH-WALL-PLATFORM_H) newY=WIDTH-WALL-PLATFORM_H;
                plats[i].y=newY;
            }
        }

        // check tower landing/miss
        if (towerEnabled && chicken.vy>0) {
            int botPrev=prevY+CHICKEN_H, botNow=chicken.y+CHICKEN_H;
            if (botPrev<=TOWER_BASE_Y && botNow>=TOWER_BASE_Y &&
                chicken.x+CHICKEN_W> TOWER_X &&
                chicken.x< TOWER_X+TOWER_WIDTH) {
                chicken.y = TOWER_BASE_Y-CHICKEN_H;
                chicken.vy=0; chicken.jumping=false;
            } else if (botPrev<=TOWER_BASE_Y && chicken.y>TOWER_BASE_Y) {
                lives--; write_number(lives,0,0);
                initChicken(&chicken); usleep(300000);
                continue;
            }
        }

        // check platform landings
        if (chicken.vy>0) {
            for (int i=0;i<MAX_PLATFORMS;i++){
                int botPrev=prevY+CHICKEN_H, botNow=chicken.y+CHICKEN_H;
                if (botPrev<=plats[i].y && botNow>=plats[i].y &&
                    chicken.x+CHICKEN_W>plats[i].x &&
                    chicken.x<plats[i].x+PLATFORM_W) {
                    chicken.y=plats[i].y-CHICKEN_H;
                    chicken.vy=0; chicken.jumping=false;
                    score++; write_score(score);
                    towerEnabled=false; break;
                }
            }
        }

        // fell off
        if (chicken.y>WIDTH) {
            lives--; write_number(lives,0,0);
            initChicken(&chicken); usleep(300000);
            continue;
        }

        // redraw
        clearSprites();

        // draw tower 3×2
        if (towerEnabled) {
            int idx=TOWER_REG_BASE;
            for (int r=0;r<TOWER_ROWS;r++){
                for (int c=0;c<TOWER_COLS;c++,idx++){
                    write_sprite_to_kernel(
                        1,
                        TOWER_BASE_Y - r*CHICKEN_H,
                        TOWER_X + c*CHICKEN_W,
                        PLATFORM_SPRITE_IDX,
                        idx
                    );
                }
            }
        }
        // draw platforms
        for (int i=0;i<MAX_PLATFORMS;i++){
            for (int k=0;k<4;k++){
                write_sprite_to_kernel(
                    1,
                    plats[i].y,
                    plats[i].x + k*32,
                    PLATFORM_SPRITE_IDX,
                    PLATFORM_REG_BASE + i*4 + k
                );
            }
        }
        // draw chicken
        write_sprite_to_kernel(
            1,
            chicken.y,
            chicken.x,
            chicken.jumping?CHICKEN_JUMP:CHICKEN_STAND,
            CHICKEN_REG
        );
        usleep(16666);
    }

    cleartiles(); clearSprites();
    write_text((unsigned char*)"gameover",8,12,16);
    sleep(2);
    return 0;
}
