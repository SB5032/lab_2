// screamjump_dynamic_start.c
// Implements precise alternating wave bar generation with robust slot finding.
// Refined game loop for potentially smoother rendering by separating updates and drawing.
// Uses optimized vga_interface with shadow tilemap.

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <fcntl.h>
#include "usbcontroller.h"  // Assumed to provide controller input functions
#include "vga_interface.h"  // NOW INCLUDES THE OPTIMIZED VERSION'S DECLARATIONS
#include "audio_interface.h"  // Assumed to provide audio functions

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
// Note: SKY_TILE_IDX and GRASS_TILE_IDX are defined in vga_interface.h

// ───── static tower ─────────────────────────────────────────────────────────
#define TOWER_TOP_VISIBLE_ROW 21 
#define CHICKEN_ON_TOWER_Y ((TOWER_TOP_VISIBLE_ROW * TILE_SIZE) - CHICKEN_H)

// ───── lives/score & controller ──────────────────────────────────────────────
#define INITIAL_LIVES      5
#define INIT_JUMP_VY     -20    // base jump velocity
#define BASE_DELAY       2000   // base jump delay (µs)

// ───── bar-config limits & wave properties ───────────────────────────────────
#define BAR_ARRAY_SIZE     10    // Max bars stored per group (A or B)
#define BAR_COUNT_PER_WAVE 5     // Number of bars spawned in a single wave
#define BAR_INTER_SPACING_PX 100 // Horizontal distance between start of bars in a wave
#define WAVE_SWITCH_TRIGGER_OFFSET_PX 100 // How far the last bar of a wave moves *onto the screen* before next group activates

#define BAR_HEIGHT_ROWS    2     // tiles tall
#define BAR_SPEED_BASE     4     // pixels/frame
#define MIN_BAR_TILES      3     // Min length in tiles (3 * 16px = 48px)
#define MAX_BAR_TILES      6     // Max length in tiles (6 * 16px = 96px)
#define BAR_TILE_IDX      39
#define BAR_INACTIVE_X  -1000 // Sentinel X value for inactive/off-screen bars

// ───── bar Y-bounds & positioning ───────────────────────────────────────────
#define BAR_MIN_Y_GROUP_A (WALL + 100) 
#define BAR_Y_OFFSET_GROUP_B 150       
#define BAR_MAX_Y         (WIDTH - BAR_HEIGHT_ROWS * TILE_SIZE - WALL) 
#define BAR_INITIAL_X_STAGGER_GROUP_B 96 // Initial X offset for the entire Group B wave

// MODIFICATION: Definition of vga_fd, as expected by vga_interface.h's extern declaration
int vga_fd; 
int audio_fd; 
struct controller_output_packet controller_state; 
bool towerEnabled = true; 

typedef struct {
    int x, y, vy;
    bool jumping;
} Chicken;

typedef struct {
    int x;      
    int y_px;   
    int length; // Length in tiles
} MovingBar;


// Helper function to draw bars (called from main draw phase)
// This function now relies on the optimized write_tile_to_kernel from vga_interface.c
void draw_all_active_bars(MovingBar bars_a[], MovingBar bars_b[], int array_size) {
    int screen_cols = TILE_COLS; // Using TILE_COLS from vga_interface.h
    MovingBar* current_bar_group;

    for (int group = 0; group < 2; group++) {
        current_bar_group = (group == 0) ? bars_a : bars_b;
        for (int b = 0; b < array_size; b++) {
            if (current_bar_group[b].x == BAR_INACTIVE_X || current_bar_group[b].length == 0) {
                continue;
            }
            
            int bar_pixel_width = current_bar_group[b].length * TILE_SIZE;
            // Only attempt to draw if the bar is potentially on screen
            if (current_bar_group[b].x < LENGTH && current_bar_group[b].x + bar_pixel_width > 0) { 
                int col0 = current_bar_group[b].x / TILE_SIZE;         
                int row0 = current_bar_group[b].y_px / TILE_SIZE;      
                int row1 = row0 + BAR_HEIGHT_ROWS - 1;    

                for (int r = row0; r <= row1; r++) {
                    if (r < 0 || r >= TILE_ROWS) continue; // Row boundary check for safety
                    for (int i = 0; i < current_bar_group[b].length; i++) {
                        int c = col0 + i;
                        // Ensure column is within bounds before drawing
                        if (c >= 0 && c < screen_cols)
                            write_tile_to_kernel(r, c, BAR_TILE_IDX); // This is now optimized
                    }
                }
            }
        }
    }
}

// Helper function to move bars (called from main update phase)
void move_all_active_bars(MovingBar bars_a[], MovingBar bars_b[], int array_size, int speed) {
    MovingBar* current_bar_group;
    for (int group = 0; group < 2; group++) {
        current_bar_group = (group == 0) ? bars_a : bars_b;
        for (int b = 0; b < array_size; b++) {
            if (current_bar_group[b].x == BAR_INACTIVE_X) {
                continue;
            }
            current_bar_group[b].x -= speed;
            int bar_pixel_width = current_bar_group[b].length * TILE_SIZE;
            if (current_bar_group[b].x + bar_pixel_width <= 0) {
                current_bar_group[b].x = BAR_INACTIVE_X; 
            }
        }
    }
}


bool handleBarCollision(
    MovingBar bars[], int array_size,
    int prevY_chicken,        
    Chicken *chicken,
    int *score,
    bool *has_landed_this_jump, 
    int jumpDelayMicroseconds
) {
    if (chicken->vy <= 0) return false;
    for (int b = 0; b < array_size; b++) {
        if (bars[b].x == BAR_INACTIVE_X) continue;
        int bar_top_y    = bars[b].y_px;
        int bar_bottom_y = bars[b].y_px + BAR_HEIGHT_ROWS * TILE_SIZE;
        int bar_left_x   = bars[b].x;
        int bar_right_x  = bars[b].x + bars[b].length * TILE_SIZE;
        int chicken_bottom_prev = prevY_chicken + CHICKEN_H;
        int chicken_bottom_curr = chicken->y + CHICKEN_H;
        int chicken_right_x = chicken->x + CHICKEN_W;
        if (chicken_bottom_prev <= bar_top_y && chicken_bottom_curr >= bar_top_y && 
            chicken_bottom_curr <= bar_bottom_y && chicken_right_x > bar_left_x && 
            chicken->x < bar_right_x) {          
            chicken->y = bar_top_y - CHICKEN_H; chicken->vy = 0; chicken->jumping = false;                 
            if (!(*has_landed_this_jump)) { (*score)++; *has_landed_this_jump = true; }
            usleep(jumpDelayMicroseconds); 
            return true; 
        }
    }
    return false; 
}

void *controller_input_thread(void *arg) {
    uint8_t endpoint_address;
    struct libusb_device_handle *controller_handle = opencontroller(&endpoint_address);
    if (!controller_handle) { perror("USB controller open"); pthread_exit(NULL); }
    unsigned char buffer[GAMEPAD_READ_LENGTH]; int actual_length_transferred;
    while (1) {
        int status = libusb_interrupt_transfer(controller_handle, endpoint_address, buffer, GAMEPAD_READ_LENGTH, &actual_length_transferred, 0);
        if (status == 0 && actual_length_transferred == GAMEPAD_READ_LENGTH) {
            usb_to_output(&controller_state, buffer); 
        } else { /*fprintf(stderr, "Controller read error: %s\n", libusb_error_name(status));*/ usleep(100000); } 
    }
}

void initChicken(Chicken *c) {
    c->x = 32; c->y = CHICKEN_ON_TOWER_Y; c->vy = 0; c->jumping = false; 
}

void moveChicken(Chicken *c) {
    if (!c->jumping && towerEnabled) return; 
    c->y += c->vy; c->vy += GRAVITY; 
}

void update_sun_sprite(int current_level) { 
    const int max_level = 5; const int start_x_sun = 32; const int end_x_sun = 608; const int base_y_sun = 64;      
    double fraction = (current_level > 1) ? (double)(current_level - 1) / (max_level - 1) : 0.0;
    if (current_level >= max_level) fraction = 1.0; 
    int sun_x_pos = start_x_sun + (int)((end_x_sun - start_x_sun) * fraction + 0.5);
    write_sprite_to_kernel(1, base_y_sun, sun_x_pos, SUN_TILE, 1); // Assuming sprite register 1 for sun
}

void resetBarArray(MovingBar bars[], int array_size) {
    for (int i = 0; i < array_size; i++) {
        bars[i].x = BAR_INACTIVE_X; bars[i].length = 0;
    }
}

int main(void) {
    // Open device files
    if ((vga_fd = open("/dev/vga_top", O_RDWR)) < 0) { perror("VGA open failed"); return -1; }
    if ((audio_fd = open("/dev/fpga_audio", O_RDWR)) < 0) { perror("Audio open failed"); close(vga_fd); return -1; }
    
    // MODIFICATION: Initialize VGA interface (and shadow tilemap)
    // This MUST be called after vga_fd is opened.
    init_vga_interface();

    pthread_t controller_thread_id;
    if (pthread_create(&controller_thread_id, NULL, controller_input_thread, NULL) != 0) {
        perror("Controller thread create failed"); close(vga_fd); close(audio_fd); return -1;
    }

    // Initial screen setup for start screen
    cleartiles(); // Clears actual screen and shadow map
    clearSprites(); 
    fill_sky_and_grass(); // Draws initial background (optimized by shadow map)

    // Display start screen text
    write_text((unsigned char *)"scream", 6, 13, 13); 
    write_text((unsigned char *)"jump", 4, 13, 20);
    write_text((unsigned char *)"press", 5, 19, 8); 
    write_text((unsigned char *)"any", 3, 19, 14); 
    write_text((unsigned char *)"key", 3, 19, 20);
    write_text((unsigned char *)"to", 2, 19, 26); 
    write_text((unsigned char *)"start", 5, 19, 29);
    
    // Wait for player to start
    while (!(controller_state.a || controller_state.b || controller_state.start)) { usleep(10000); }

    // Setup for game start
    cleartiles(); 
    clearSprites();
    fill_sky_and_grass(); // Redraw background for the game start

    srand(time(NULL)); 
    int score = 0, lives = INITIAL_LIVES, level = 1;
    int jump_velocity = INIT_JUMP_VY; int jump_pause_delay = BASE_DELAY;
    const int hud_center_col = TILE_COLS / 2; const int hud_offset = 12; 

    MovingBar barsA[BAR_ARRAY_SIZE]; MovingBar barsB[BAR_ARRAY_SIZE];
    resetBarArray(barsA, BAR_ARRAY_SIZE); resetBarArray(barsB, BAR_ARRAY_SIZE);

    int y_pos_group_A = BAR_MIN_Y_GROUP_A;
    int y_pos_group_B = BAR_MIN_Y_GROUP_A + BAR_Y_OFFSET_GROUP_B;
    if (y_pos_group_B > BAR_MAX_Y) y_pos_group_B = BAR_MAX_Y; // Boundary check
    if (y_pos_group_B + BAR_HEIGHT_ROWS * TILE_SIZE > WIDTH - WALL) {
         y_pos_group_B = WIDTH - WALL - (BAR_HEIGHT_ROWS * TILE_SIZE);
    }

    Chicken chicken; initChicken(&chicken); 
    bool has_landed_this_jump = false; 
    bool group_A_is_active_spawner = true; 
    bool needs_to_spawn_wave_A = true; 
    bool needs_to_spawn_wave_B = false;
    int next_bar_slot_A = 0; 
    int next_bar_slot_B = 0; 
    int watching_bar_idx_A = -1; 
    int watching_bar_idx_B = -1; 

    // Game Loop
    while (lives > 0) {
        int current_bar_speed = BAR_SPEED_BASE + (level - 1); 

        // ───── 1. INPUT PROCESSING ─────
        if (controller_state.b && !chicken.jumping) {
            chicken.vy = jump_velocity; chicken.jumping = true;
            has_landed_this_jump = false; towerEnabled = false; play_sfx(0); 
        }

        // ───── 2. GAME STATE UPDATES ─────
        int prevY_chicken = chicken.y; 
        moveChicken(&chicken);      

        move_all_active_bars(barsA, barsB, BAR_ARRAY_SIZE, current_bar_speed);
        
        // Wave Spawning Logic
        if (group_A_is_active_spawner && needs_to_spawn_wave_A) {
            int wave_bars_spawned_count = 0; int last_spawned_in_wave_idx = -1;
            for (int i = 0; i < BAR_COUNT_PER_WAVE; i++) {
                int slot_to_spawn = -1;
                for (int j = 0; j < BAR_ARRAY_SIZE; j++) { // Find an inactive slot
                    int current_search_idx = (next_bar_slot_A + j) % BAR_ARRAY_SIZE;
                    if (barsA[current_search_idx].x == BAR_INACTIVE_X) { slot_to_spawn = current_search_idx; break; }
                }
                if (slot_to_spawn != -1) {
                    barsA[slot_to_spawn].x = LENGTH + (i * BAR_INTER_SPACING_PX);
                    barsA[slot_to_spawn].y_px = y_pos_group_A;
                    barsA[slot_to_spawn].length = rand() % (MAX_BAR_TILES - MIN_BAR_TILES + 1) + MIN_BAR_TILES;
                    last_spawned_in_wave_idx = slot_to_spawn; wave_bars_spawned_count++;
                } else { break; } // No free slot
            }
            if (wave_bars_spawned_count > 0) {
                watching_bar_idx_A = last_spawned_in_wave_idx;
                next_bar_slot_A = (last_spawned_in_wave_idx + 1) % BAR_ARRAY_SIZE; // Start search from next slot
            }
            needs_to_spawn_wave_A = false;
        } 
        else if (!group_A_is_active_spawner && needs_to_spawn_wave_B) {
            int wave_bars_spawned_count = 0; int last_spawned_in_wave_idx = -1;
            for (int i = 0; i < BAR_COUNT_PER_WAVE; i++) {
                int slot_to_spawn = -1;
                for (int j = 0; j < BAR_ARRAY_SIZE; j++) { // Find an inactive slot
                    int current_search_idx = (next_bar_slot_B + j) % BAR_ARRAY_SIZE;
                    if (barsB[current_search_idx].x == BAR_INACTIVE_X) { slot_to_spawn = current_search_idx; break; }
                }
                if (slot_to_spawn != -1) {
                    barsB[slot_to_spawn].x = LENGTH + BAR_INITIAL_X_STAGGER_GROUP_B + (i * BAR_INTER_SPACING_PX);
                    barsB[slot_to_spawn].y_px = y_pos_group_B;
                    barsB[slot_to_spawn].length = rand() % (MAX_BAR_TILES - MIN_BAR_TILES + 1) + MIN_BAR_TILES;
                    last_spawned_in_wave_idx = slot_to_spawn; wave_bars_spawned_count++;
                } else { break; } // No free slot
            }
            if (wave_bars_spawned_count > 0) {
                watching_bar_idx_B = last_spawned_in_wave_idx;
                next_bar_slot_B = (last_spawned_in_wave_idx + 1) % BAR_ARRAY_SIZE; // Start search from next slot
            }
            needs_to_spawn_wave_B = false;
        }

        // Turn Switching Logic
        if (group_A_is_active_spawner && watching_bar_idx_A != -1 && barsA[watching_bar_idx_A].x != BAR_INACTIVE_X) {
            if (barsA[watching_bar_idx_A].x < LENGTH - WAVE_SWITCH_TRIGGER_OFFSET_PX) {
                group_A_is_active_spawner = false; needs_to_spawn_wave_B = true; watching_bar_idx_A = -1;         
            }
        } 
        else if (!group_A_is_active_spawner && watching_bar_idx_B != -1 && barsB[watching_bar_idx_B].x != BAR_INACTIVE_X) {
            if (barsB[watching_bar_idx_B].x < LENGTH - WAVE_SWITCH_TRIGGER_OFFSET_PX) {
                group_A_is_active_spawner = true; needs_to_spawn_wave_A = true; watching_bar_idx_B = -1;         
            }
        }

        // Collision Detection
        if (chicken.vy > 0) {
            bool landed = handleBarCollision(barsA, BAR_ARRAY_SIZE, prevY_chicken, &chicken, &score, &has_landed_this_jump, jump_pause_delay);
            if (!landed) handleBarCollision(barsB, BAR_ARRAY_SIZE, prevY_chicken, &chicken, &score, &has_landed_this_jump, jump_pause_delay);
        }
        
        // Boundary and Death Checks
        if (chicken.y < WALL + 40 && chicken.jumping) { // Ceiling
             chicken.y = WALL + 40; if (chicken.vy < 0) chicken.vy = 0; 
        }
        if (chicken.y + CHICKEN_H > WIDTH - WALL) { // Floor (death)
            lives--; towerEnabled = true; initChicken(&chicken); has_landed_this_jump = false;
            // Reset wave state on death
            group_A_is_active_spawner = true; needs_to_spawn_wave_A = true; needs_to_spawn_wave_B = false;
            watching_bar_idx_A = -1; watching_bar_idx_B = -1;
            next_bar_slot_A = 0; next_bar_slot_B = 0;
            resetBarArray(barsA, BAR_ARRAY_SIZE); resetBarArray(barsB, BAR_ARRAY_SIZE);
            
            cleartiles(); 
            fill_sky_and_grass(); 

            if (lives > 0) { play_sfx(1); usleep(2000000); } 
            continue; 
        }

        // ───── 3. DRAWING ─────
        // The shadow map in vga_interface.c optimizes these calls.
        fill_sky_and_grass(); 
        clearSprites(); // Clear previous sprite positions before drawing new ones

        draw_all_active_bars(barsA, barsB, BAR_ARRAY_SIZE);
        
        // Draw HUD
        write_text((unsigned char *)"lives", 5, 1, hud_center_col - hud_offset); 
        write_number(lives, 1, hud_center_col - hud_offset + 6);
        write_text((unsigned char *)"score", 5, 1, hud_center_col - hud_offset + 12); 
        write_number(score, 1, hud_center_col - hud_offset + 18);
        write_text((unsigned char *)"level", 5, 1, hud_center_col - hud_offset + 24); 
        write_number(level, 1, hud_center_col - hud_offset + 30);
        
        // Draw Tower
        if (towerEnabled) {
            for (int r_tower = TOWER_TOP_VISIBLE_ROW; r_tower < TOWER_TOP_VISIBLE_ROW + 9; ++r_tower) { 
                 if (r_tower >= TILE_ROWS ) break; 
                for (int c_tower = 0; c_tower < 5; ++c_tower) { write_tile_to_kernel(r_tower, c_tower, TOWER_TILE_IDX); }
            }
        }
        // Draw Sprites
        write_sprite_to_kernel(1, chicken.y, chicken.x, chicken.jumping ? CHICKEN_JUMP : CHICKEN_STAND, 0); // Chicken on sprite reg 0
        update_sun_sprite(level); // Sun on sprite reg 1 (update_sun_sprite calls write_sprite_to_kernel)

        // Frame delay
        usleep(16666); // Approximately 60 FPS
    }

    // Game Over Sequence
    play_sfx(2); 
    cleartiles(); 
    clearSprites(); 
    fill_sky_and_grass();
    write_text((unsigned char *)"gameover", 8, 12, (TILE_COLS / 2) - 4); 
    write_text((unsigned char *)"score", 5, 14, (TILE_COLS/2) - 6);
    write_number(score, 14, (TILE_COLS/2) ); // Display final score
    sleep(3); 

    // Cleanup
    // Consider properly signaling the controller thread to exit and then joining it for a clean shutdown.
    // pthread_cancel(controller_thread_id);
    // pthread_join(controller_thread_id, NULL);
    close(vga_fd); 
    close(audio_fd);
    return 0;
}
