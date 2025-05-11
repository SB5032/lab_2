// screamjump_dynamic_start.c
// Adds a start screen: displays title and "Press any key to start"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <fcntl.h>
#include "usbcontroller.h"  // Assumed to provide controller input functions
#include "vga_interface.h"  // Assumed to provide VGA drawing functions (write_tile_to_kernel, etc.)
#include "audio_interface.h"  // Assumed to provide audio functions (play_sfx)

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
#define SUN_TILE           20

// ───── static tower ─────────────────────────────────────────────────────────
#define TOWER_TOP_VISIBLE_ROW 21 
#define CHICKEN_ON_TOWER_Y ((TOWER_TOP_VISIBLE_ROW * TILE_SIZE) - CHICKEN_H)

// ───── lives/score & controller ──────────────────────────────────────────────
#define INITIAL_LIVES      5
#define INIT_JUMP_VY     -20    // base jump velocity
#define BASE_DELAY       2000   // base jump delay (µs)

// ───── bar-config limits ─────────────────────────────────────────────────────
#define BAR_COUNT_PER_GROUP 4  
#define BAR_HEIGHT_ROWS    2     // tiles tall
#define BAR_SPEED_BASE     4     // pixels/frame
#define MIN_BAR_TILES      3     // Min length in tiles (3 * 16px = 48px)
#define MAX_BAR_TILES      6     // Max length in tiles (6 * 16px = 96px)
#define BAR_TILE_IDX      39
#define BAR_SWITCH_TRIGGER_X (LENGTH - 96) // X-coordinate for switching turns

// ───── bar Y-bounds & positioning ───────────────────────────────────────────
#define BAR_MIN_Y_GROUP_A (WALL + 100) 
#define BAR_Y_OFFSET_GROUP_B 150       
#define BAR_MAX_Y         (WIDTH - BAR_HEIGHT_ROWS * TILE_SIZE - WALL) 
#define BAR_INITIAL_X_STAGGER_GROUP_B 96 

int vga_fd, audio_fd; 
struct controller_output_packet controller_state; 
bool towerEnabled = true; 

typedef struct {
    int x, y, vy;
    bool jumping;
} Chicken;

typedef struct {
    int x;      
    int y_px;   
    int length; 
} MovingBar;

void initBars(MovingBar bars[], int count, int y_px, int initial_x_stagger) {
    int spawnInterval = (count > 0) ? (LENGTH / count) : LENGTH; 

    for (int i = 0; i < count; i++) {
        bars[i].x      = LENGTH + initial_x_stagger + i * spawnInterval; 
        bars[i].y_px   = y_px;
        bars[i].length = rand() % (MAX_BAR_TILES - MIN_BAR_TILES + 1) 
                         + MIN_BAR_TILES;
    }
}

// MODIFIED: Added enable_spawning and current_turn_spawn_count parameters
void updateAndDrawBars(MovingBar bars[], int count, int speed, int *spawnCounter, 
                       int initial_x_stagger_for_respawn, bool enable_spawning, 
                       int *current_turn_spawn_count) {
    if (count == 0) return; 
    
    int spawnInterval = LENGTH / count;
    int screen_cols = LENGTH / TILE_SIZE; 

    for (int b = 0; b < count; b++) {
        bars[b].x -= speed;
        int bar_width_px = bars[b].length * TILE_SIZE;

        // MODIFIED: Respawn logic now conditional on enable_spawning
        if (enable_spawning && (bars[b].x + bar_width_px <= 0)) {
            bars[b].x = LENGTH + initial_x_stagger_for_respawn + ((*spawnCounter) % count) * spawnInterval;
            (*spawnCounter)++; 
            (*current_turn_spawn_count)++; // Increment count for the current turn's spawns
            bars[b].length = rand() % (MAX_BAR_TILES - MIN_BAR_TILES + 1) 
                             + MIN_BAR_TILES;
        }

        int col0 = bars[b].x / TILE_SIZE;         
        int row0 = bars[b].y_px / TILE_SIZE;      
        int row1 = row0 + BAR_HEIGHT_ROWS - 1;    

        for (int r = row0; r <= row1; r++) {
            for (int i = 0; i < bars[b].length; i++) {
                int c = col0 + i;
                if (c >= 0 && c < screen_cols && r >=0 && r < (WIDTH/TILE_SIZE))
                    write_tile_to_kernel(r, c, BAR_TILE_IDX);
            }
        }
    }
}

bool handleBarCollision(
    MovingBar bars[], int count,
    int prevY_chicken,        
    Chicken *chicken,
    int *score,
    bool *has_landed_this_jump, 
    int jumpDelayMicroseconds
) {
    if (chicken->vy <= 0) return false;

    for (int b = 0; b < count; b++) {
        int bar_top_y    = bars[b].y_px;
        int bar_bottom_y = bars[b].y_px + BAR_HEIGHT_ROWS * TILE_SIZE;
        int bar_left_x   = bars[b].x;
        int bar_right_x  = bars[b].x + bars[b].length * TILE_SIZE;

        int chicken_bottom_prev = prevY_chicken + CHICKEN_H;
        int chicken_bottom_curr = chicken->y + CHICKEN_H;
        int chicken_right_x = chicken->x + CHICKEN_W;

        if (chicken_bottom_prev <= bar_top_y &&  
            chicken_bottom_curr >= bar_top_y &&  
            chicken_bottom_curr <= bar_bottom_y && 
            chicken_right_x > bar_left_x &&      
            chicken->x < bar_right_x) {          

            chicken->y       = bar_top_y - CHICKEN_H; 
            chicken->vy      = 0;                     
            chicken->jumping = false;                 

            if (!(*has_landed_this_jump)) {
                (*score)++;
                *has_landed_this_jump = true;
            }
            usleep(jumpDelayMicroseconds); 
            return true; 
        }
    }
    return false; 
}

void *controller_input_thread(void *arg) {
    uint8_t endpoint_address;
    struct libusb_device_handle *controller_handle = opencontroller(&endpoint_address);
    if (!controller_handle) {
        perror("Failed to open USB controller");
        pthread_exit(NULL);
    }

    unsigned char buffer[GAMEPAD_READ_LENGTH]; 
    int actual_length_transferred;

    while (1) {
        int transfer_status = libusb_interrupt_transfer(
            controller_handle,
            endpoint_address,
            buffer,
            GAMEPAD_READ_LENGTH,
            &actual_length_transferred,
            0 
        );

        if (transfer_status == 0 && actual_length_transferred == GAMEPAD_READ_LENGTH) {
            usb_to_output(&controller_state, buffer); 
        } else {
            fprintf(stderr, "Controller read error or disconnect: %s\n", libusb_error_name(transfer_status));
            usleep(100000); 
        }
    }
}

void initChicken(Chicken *c) {
    c->x = 32; 
    c->y = CHICKEN_ON_TOWER_Y; 
    c->vy = 0;        
    c->jumping = false; 
}

void moveChicken(Chicken *c) {
    if (!c->jumping && towerEnabled) return; 
    c->y += c->vy;    
    c->vy += GRAVITY; 
}

void update_sun(int current_level) {
    const int max_level = 5;        
    const int start_x_sun = 32;     
    const int end_x_sun = 608;      
    const int base_y_sun = 64;      

    double fraction = (current_level > 1) ? (double)(current_level - 1) / (max_level - 1) : 0.0;
    if (current_level >= max_level) fraction = 1.0; 

    int sun_x_pos = start_x_sun + (int)((end_x_sun - start_x_sun) * fraction + 0.5);
    write_sprite_to_kernel(1, base_y_sun, sun_x_pos, SUN_TILE, 1); 
}

int main(void) {
    if ((vga_fd = open("/dev/vga_top", O_RDWR)) < 0) {
        perror("Failed to open /dev/vga_top");
        return -1;
    }
    if ((audio_fd = open("/dev/fpga_audio", O_RDWR)) < 0) {
        perror("Failed to open /dev/fpga_audio");
        close(vga_fd); 
        return -1;
    }

    pthread_t controller_thread_id;
    if (pthread_create(&controller_thread_id, NULL, controller_input_thread, NULL) != 0) {
        perror("Failed to create controller thread");
        close(vga_fd);
        close(audio_fd);
        return -1;
    }

    cleartiles(); clearSprites(); fill_sky_and_grass();
    write_text("scream", 6, 13, 13);
    write_text("jump",   4, 13, 20);
    write_text("press",  5, 19, 8);
    write_text("any",    3, 19, 14);
    write_text("key",    3, 19, 20);
    write_text("to",     2, 19, 26);
    write_text("start",  5, 19, 29);

    while (!(controller_state.a || controller_state.b || controller_state.start)) {
        usleep(10000); 
    }

    cleartiles(); clearSprites(); fill_sky_and_grass();
    srand(time(NULL)); 

    int score = 0, lives = INITIAL_LIVES, level = 1;
    int jump_velocity    = INIT_JUMP_VY;
    int jump_pause_delay = BASE_DELAY;

    const int screen_tile_cols = LENGTH / TILE_SIZE;
    const int hud_center_col   = screen_tile_cols / 2;
    const int hud_offset       = 12; 

    MovingBar barsA[BAR_COUNT_PER_GROUP];
    MovingBar barsB[BAR_COUNT_PER_GROUP];
    // NEW: Global spawn counters for each group
    static int globalSpawnCounterA = 0; 
    static int globalSpawnCounterB = 0; 

    int y_pos_group_A = BAR_MIN_Y_GROUP_A;
    int y_pos_group_B = BAR_MIN_Y_GROUP_A + BAR_Y_OFFSET_GROUP_B;
    
    if (y_pos_group_B > BAR_MAX_Y) y_pos_group_B = BAR_MAX_Y;
    if (y_pos_group_B + BAR_HEIGHT_ROWS * TILE_SIZE > WIDTH - WALL) {
         y_pos_group_B = WIDTH - WALL - (BAR_HEIGHT_ROWS * TILE_SIZE);
    }

    initBars(barsA, BAR_COUNT_PER_GROUP, y_pos_group_A, 0);
    initBars(barsB, BAR_COUNT_PER_GROUP, y_pos_group_B, BAR_INITIAL_X_STAGGER_GROUP_B);

    Chicken chicken;
    initChicken(&chicken); 
    bool has_landed_this_jump = false; 

    // NEW: State variables for turn-based spawning
    bool is_group_A_spawning_turn = true;
    int current_turn_spawn_count_A = 0;
    int current_turn_spawn_count_B = 0;
    // Index of the last bar in the current wave to monitor for the switch condition
    int watch_bar_idx_A = (globalSpawnCounterA + BAR_COUNT_PER_GROUP - 1 + BAR_COUNT_PER_GROUP) % BAR_COUNT_PER_GROUP;
    int watch_bar_idx_B = (globalSpawnCounterB + BAR_COUNT_PER_GROUP - 1 + BAR_COUNT_PER_GROUP) % BAR_COUNT_PER_GROUP;


    while (lives > 0) {
        int current_bar_speed = BAR_SPEED_BASE + (level - 1); 

        if (controller_state.b && !chicken.jumping) {
            chicken.vy      = jump_velocity;
            chicken.jumping = true;
            has_landed_this_jump = false; 
            towerEnabled    = false;      
            play_sfx(0); 
        }

        if (chicken.y < WALL + 40 && chicken.jumping) { 
             chicken.y = WALL + 40;
             if (chicken.vy < 0) chicken.vy = 0; 
        }

        int prevY_chicken = chicken.y; 
        moveChicken(&chicken);         

        if (chicken.vy > 0) {
            bool landed_on_A = handleBarCollision(
                barsA, BAR_COUNT_PER_GROUP,
                prevY_chicken, &chicken,
                &score, &has_landed_this_jump,
                jump_pause_delay
            );
            if (!landed_on_A) {
                handleBarCollision(
                    barsB, BAR_COUNT_PER_GROUP,
                    prevY_chicken, &chicken,
                    &score, &has_landed_this_jump,
                    jump_pause_delay
                );
            }
        }

        if (chicken.y + CHICKEN_H > WIDTH - WALL) { 
            lives--;
            towerEnabled = true; 
            initChicken(&chicken); 
            has_landed_this_jump = false;
             // NEW: Reset turn logic on death
            is_group_A_spawning_turn = true;
            current_turn_spawn_count_A = 0;
            current_turn_spawn_count_B = 0;
            watch_bar_idx_A = (globalSpawnCounterA + BAR_COUNT_PER_GROUP - 1 + BAR_COUNT_PER_GROUP) % BAR_COUNT_PER_GROUP;
            watch_bar_idx_B = (globalSpawnCounterB + BAR_COUNT_PER_GROUP - 1 + BAR_COUNT_PER_GROUP) % BAR_COUNT_PER_GROUP;

            if (lives > 0) {
                play_sfx(1); 
                usleep(2000000); 
            }
            continue; 
        }

        clearSprites(); 
        fill_sky_and_grass(); 

        write_text("lives", 5, 1, hud_center_col - hud_offset);
        write_number(lives, 1, hud_center_col - hud_offset + 6);
        write_text("score", 5, 1, hud_center_col - hud_offset + 12);
        write_number(score, 1, hud_center_col - hud_offset + 18);
        write_text("level", 5, 1, hud_center_col - hud_offset + 24);
        write_number(level, 1, hud_center_col - hud_offset + 30);

        // MODIFIED: Call updateAndDrawBars with new parameters
        updateAndDrawBars(barsA, BAR_COUNT_PER_GROUP, current_bar_speed, &globalSpawnCounterA, 
                          0, is_group_A_spawning_turn, &current_turn_spawn_count_A);
        updateAndDrawBars(barsB, BAR_COUNT_PER_GROUP, current_bar_speed, &globalSpawnCounterB, 
                          BAR_INITIAL_X_STAGGER_GROUP_B, !is_group_A_spawning_turn, &current_turn_spawn_count_B); 

        // NEW: Turn switching logic
        if (is_group_A_spawning_turn) {
            if (current_turn_spawn_count_A >= BAR_COUNT_PER_GROUP) { // Group A finished its wave
                if (BAR_COUNT_PER_GROUP > 0 && barsA[watch_bar_idx_A].x < BAR_SWITCH_TRIGGER_X) {
                    is_group_A_spawning_turn = false; // Switch to B's turn
                    current_turn_spawn_count_A = 0;   // Reset A's counter for its next turn
                    current_turn_spawn_count_B = 0;   // Reset B's counter for its new turn
                    // Update watch_idx_B for B's upcoming turn
                    if (BAR_COUNT_PER_GROUP > 0) {
                        watch_bar_idx_B = (globalSpawnCounterB + BAR_COUNT_PER_GROUP - 1 + BAR_COUNT_PER_GROUP) % BAR_COUNT_PER_GROUP;
                    }
                }
            }
        } else { // Group B's spawning turn
            if (current_turn_spawn_count_B >= BAR_COUNT_PER_GROUP) { // Group B finished its wave
                 if (BAR_COUNT_PER_GROUP > 0 && barsB[watch_bar_idx_B].x < BAR_SWITCH_TRIGGER_X) {
                    is_group_A_spawning_turn = true; // Switch to A's turn
                    current_turn_spawn_count_B = 0;  // Reset B's counter
                    current_turn_spawn_count_A = 0;  // Reset A's counter
                    // Update watch_idx_A for A's upcoming turn
                    if (BAR_COUNT_PER_GROUP > 0) {
                        watch_bar_idx_A = (globalSpawnCounterA + BAR_COUNT_PER_GROUP - 1 + BAR_COUNT_PER_GROUP) % BAR_COUNT_PER_GROUP;
                    }
                }
            }
        }

        if (towerEnabled) {
            for (int r = TOWER_TOP_VISIBLE_ROW; r < TOWER_TOP_VISIBLE_ROW + 9; ++r) { 
                 if (r >= (WIDTH/TILE_SIZE) ) break; 
                for (int c = 0; c < 5; ++c) { 
                    write_tile_to_kernel(r, c, TOWER_TILE_IDX);
                }
            }
        }
        
        write_sprite_to_kernel(
            1, chicken.y, chicken.x,
            chicken.jumping ? CHICKEN_JUMP : CHICKEN_STAND,
            0 
        );
        update_sun(level); 

        usleep(16666); 
    }

    play_sfx(2); 
    cleartiles(); clearSprites(); fill_sky_and_grass();
    write_text("gameover", 8, 12, (screen_tile_cols / 2) - 4); 
    write_text("score", 5, 14, (screen_tile_cols/2) - 6);
    write_number(score, 14, (screen_tile_cols/2) );

    sleep(3); 

    close(vga_fd);
    close(audio_fd);
    return 0;
}

