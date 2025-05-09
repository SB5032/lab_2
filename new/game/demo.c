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
#define JUMP_VY           -20
#define GRAVITY           +1

// ───── sprite dimensions ─────────────────────────────────────────────────────
#define CHICKEN_W         32
#define CHICKEN_H         32

// ───── MIF indices ───────────────────────────────────────────────────────────
#define CHICKEN_STAND     8     // chicken standing tile
#define CHICKEN_JUMP     11     // chicken jumping tile
#define PLATFORM_SPRITE_IDX 14  // platform graphic
#define TOWER_TILE_IDX    22    // static tower tile

// ───── moving platforms ──────────────────────────────────────────────────────
#define MAX_PLATFORMS     3
#define PLATFORM_W      (4*32)  // 4 sprites × 32px
#define PLATFORM_H      (32)    // 32px
#define PLATFORM_SPACING  (LENGTH / MAX_PLATFORMS)
#define PLATFORM_REG_BASE 1      // sprite register base

// ───── static tower ─────────────────────────────────────────────────────────
#define TOWER_X           16
#define TOWER_WIDTH      CHICKEN_W
#define TOWER_HEIGHT      3      // stacked 3 sprites high originally
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
typedef struct { int x, y; } Platform;

// helper: pick a random Y within region, avoiding top 40px in region 1
int pickY(int region, int minY, int regionHeight, int maxY) {
    int regionMinY = minY + (region - 1) * regionHeight;
    int regionMaxY = (region == 4) ? maxY : (regionMinY + regionHeight - 1);
    if (region == 1 && regionMinY < WALL + 40)
        regionMinY = WALL + 40;
    return rand() % (regionMaxY - regionMinY + 1) + regionMinY;
}

void *controller_input_thread(void *arg) {
    uint8_t ep;
    struct libusb_device_handle *ctrl = opencontroller(&ep);
    if (!ctrl) pthread_exit(NULL);
    while (1) {
        unsigned char buf[GAMEPAD_READ_LENGTH]; int transferred;
        if (libusb_interrupt_transfer(ctrl, ep, buf, GAMEPAD_READ_LENGTH, &transferred, 0) == 0)
            usb_to_output(&controller_state, buf);
    }
    libusb_close(ctrl); libusb_exit(NULL); pthread_exit(NULL);
}

void initChicken(Chicken *c) { c->x=32; c->y=304; c->vy=0; c->jumping=false; }
void moveChicken(Chicken *c) { if (!c->jumping && towerEnabled) return; c->y+=c->vy; c->vy+=GRAVITY; }

int main(void) {
    if ((vga_fd=open("/dev/vga_top",O_RDWR))<0) return -1;
    if ((audio_fd=open("/dev/fpga_audio",O_RDWR))<0) return -1;
    pthread_t tid; pthread_create(&tid,NULL,controller_input_thread,NULL);

    // start screen
    cleartiles(); clearSprites();
    write_text("scream",6,13,13); write_text("jump",4,13,20);
    write_text("press",5,19,8); write_text("any",3,19,14);
    write_text("key",3,19,20); write_text("to",2,19,24);
    write_text("start",5,19,27);
    while (!(controller_state.a||controller_state.b||controller_state.x||
             controller_state.y||controller_state.start||controller_state.select||
             controller_state.updown||controller_state.leftright)) usleep(10000);

    cleartiles(); clearSprites();
    int score=0, lives=INITIAL_LIVES;
    int level=1, platformSpeed=BASE_PLATFORM_SPEED, jumpDelay=BASE_JUMP_DELAY;
    write_score(score); write_number(lives,0,0);
    write_number(level,0,38);

    Chicken chicken; initChicken(&chicken); bool landed=false;
    srand(time(NULL));
    int minY=WALL, maxY=WIDTH-WALL-PLATFORM_H;
    int regionHeight=(maxY-minY+1)/4;

    Platform plats[MAX_PLATFORMS];
    for(int i=0;i<MAX_PLATFORMS;i++){
        plats[i].x=LENGTH+i*PLATFORM_SPACING;
        int r=(chicken.y-minY)/regionHeight+1;
        if(r<1)r=1; if(r>4)r=4;
        int prev=(r==1)?4:r-1, next=(r==4)?1:r+1;
        int cand[2]={prev,next},cnt=2;
        for(int j=0;j<cnt;j++) if(towerEnabled&&cand[j]==4){cand[j]=cand[cnt-1];cnt--;j--;}
        if(r==4) for(int j=0;j<cnt;j++) if(cand[j]==1){cand[j]=cand[cnt-1];cnt--;j--;}
        int target=cand[rand()%cnt];
        if(r==2||r==3||r==4) {
            int lo=chicken.y-200, hi=chicken.y+200;
            if(lo<minY) lo=minY; if(hi>maxY) hi=maxY;
            plats[i].y=rand()%(hi-lo+1)+lo;
        } else {
            plats[i].y=pickY(target,minY,regionHeight,maxY);
        }
    }

    while(lives>0){
        if(controller_state.b&&!chicken.jumping){chicken.vy=JUMP_VY;chicken.jumping=true;landed=false;play_sfx(0);}
        int prevY=chicken.y; moveChicken(&chicken); if(chicken.y< WALL) chicken.y=WALL;

        for(int i=0;i<MAX_PLATFORMS;i++){
            plats[i].x-=platformSpeed;
            if(plats[i].x<-PLATFORM_W){
                int maxX=plats[0].x;
                for(int j=1;j<MAX_PLATFORMS;j++) if(plats[j].x>maxX) maxX=plats[j].x;
                plats[i].x=maxX+PLATFORM_SPACING;
                int r=(chicken.y-minY)/regionHeight+1;
                if(r<1)r=1; if(r>4)r=4;
                int prevb=(r==1)?4:r-1, nextb=(r==4)?1:r+1;
                int cand2[2]={prevb,nextb},cnt2=2;
                for(int j=0;j<cnt2;j++) if(towerEnabled&&cand2[j]==4){cand2[j]=cand2[cnt2-1];cnt2--;j--;}
                if(r==4) for(int j=0;j<cnt2;j++) if(cand2[j]==1){cand2[j]=cand2[cnt2-1];cnt2--;j--;}
                if(r==2||r==3||r==4) {
                    int lo=chicken.y-200, hi=chicken.y+200;
                    if(lo<minY) lo=minY; if(hi>maxY) hi=maxY;
                    plats[i].y=rand()%(hi-lo+1)+lo;
                } else {
                    plats[i].y=pickY(cand2[rand()%cnt2],minY,regionHeight,maxY);
                }
            }
        }

        if(chicken.vy>0){
            towerEnabled=false;
            for(int i=0;i<MAX_PLATFORMS;i++){
                int botPrev=prevY+CHICKEN_H, botNow=chicken.y+CHICKEN_H;
                if(botPrev<=plats[i].y&&botNow>=plats[i].y&&
                   chicken.x+CHICKEN_W>plats[i].x&&
                   chicken.x<plats[i].x+PLATFORM_W){
                    chicken.y=plats[i].y-CHICKEN_H; chicken.vy=0; chicken.jumping=false;
                    if(!landed){
                        score++; write_score(score); landed=true;
                        if(score%20==0){
                            level++; write_number(level,0,38);
                            platformSpeed=BASE_PLATFORM_SPEED+(level-1);
                            jumpDelay=BASE_JUMP_DELAY-(level-1)*400;
                            if(jumpDelay<400)jumpDelay=400;
                        }
                        usleep(jumpDelay);
                    }
                    break;
                }
            }
        }

        if(chicken.y>WIDTH){ lives--; towerEnabled=true;
            write_number(lives,0,0); initChicken(&chicken); landed=false; usleep(3000000); continue; }

        clearSprites();
        for(int r=21;r<30;r++) for(int c=0;c<5;c++) write_tile_to_kernel(r,c,towerEnabled?TOWER_TILE_IDX:0);
        for(int i=0;i<MAX_PLATFORMS;i++) for(int k=0;k<4;k++)
            write_sprite_to_kernel(1,plats[i].y,plats[i].x+k*32,PLATFORM_SPRITE_IDX,PLATFORM_REG_BASE+i*4+k);
        write_sprite_to_kernel(1,chicken.y,chicken.x, chicken.jumping?CHICKEN_JUMP:CHICKEN_STAND,0);
        usleep(16666);
    }

    cleartiles(); clearSprites(); write_text((unsigned char*)"gameover",8,12,16); sleep(2);
    return 0;
}
