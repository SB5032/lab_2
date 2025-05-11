// screamjump_dynamic_start.c
// Uses software double buffering via vga_interface.c
// Implements level-based difficulty and fixes multi-digit score display.

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <fcntl.h>
#include "usbcontroller.h"  // Assumed to provide controller input functions
#include "vga_interface.h" // Uses new double buffering interface
#include "audio_interface.h"  // Assumed to provide audio functions

// Screen and physics constants
#define LENGTH            640   // VGA width (pixels)
#define WIDTH             480   // VGA height (pixels)
#define TILE_SIZE          16   // background tile size (pixels)
#define WALL               16   // top/bottom margin (pixels)
#define GRAVITY            +1

// Sprite dimensions
#define CHICKEN_W         32
#define CHICKEN_H         32

// MIF indices
#define CHICKEN_STAND      8   // chicken standing tile
#define CHICKEN_JUMP       11  // chicken jumping tile
#define TOWER_TILE_IDX     42  // static tower tile
#define SUN_TILE           20
// SKY_TILE_IDX, GRASS_TILE_IDX are defined in vga_interface.h (ensure they are set correctly!)

// Tower properties
#define TOWER_TOP_VISIBLE_ROW 21 
#define CHICKEN_ON_TOWER_Y ((TOWER_TOP_VISIBLE_ROW * TILE_SIZE) - CHICKEN_H)

// Game settings
#define INITIAL_LIVES      5
#define INIT_JUMP_VY     -20    // base jump velocity
#define BASE_DELAY       2000   // base jump delay (µs)
#define SCORE_PER_LEVEL    5     // Points needed to advance to the next level
#define MAX_GAME_LEVEL     5     // Maximum number of levels
#define MAX_SCORE_DISPLAY_DIGITS 3 // For displaying score like 000 to 999

// Bar properties (some of these will be overridden by level settings)
#define BAR_ARRAY_SIZE     10    // Max bars stored per group (A or B)
#define WAVE_SWITCH_TRIGGER_OFFSET_PX 100 // How far the last bar of a wave moves *onto the screen* before next group activates
#define BAR_HEIGHT_ROWS    2     // tiles tall
#define BAR_TILE_IDX      39    // Tile index for the bars - USER MUST VERIFY THIS VALUE
#define BAR_INACTIVE_X  -1000 // Sentinel X value for inactive/off-screen bars

// Default Bar positioning (can be overridden by level settings, especially for Y)
#define DEFAULT_BAR_MIN_Y_GROUP_A (WALL + 100) 
#define DEFAULT_BAR_Y_OFFSET_GROUP_B 150       
#define BAR_MAX_Y_POS         (WIDTH - BAR_HEIGHT_ROWS * TILE_SIZE - WALL - CHICKEN_H - TILE_SIZE) // Max Y for bar top, ensuring chicken can stand
#define BAR_MIN_Y_POS         (WALL + 60) // Min Y for bar top
#define BAR_RANDOM_Y_RANGE    120      // Max +/- random offset for group B in random levels

// Global variables
int vga_fd; 
int audio_fd; 
struct controller_output_packet controller_state; 
bool towerEnabled = true; 

// Structures
typedef struct { int x, y, vy; bool jumping; } Chicken;
typedef struct { int x, y_px, length; } MovingBar;

// Helper function to draw bars to the current back buffer
void draw_all_active_bars_to_back_buffer(MovingBar bars_a[], MovingBar bars_b[], int array_size) {
    MovingBar* current_bar_group;
    for (int group = 0; group < 2; group++) {
        current_bar_group = (group == 0) ? bars_a : bars_b;
        for (int b = 0; b < array_size; b++) {
            if (current_bar_group[b].x == BAR_INACTIVE_X || current_bar_group[b].length == 0) continue;
            
            int bar_pixel_width = current_bar_group[b].length * TILE_SIZE;
            if (current_bar_group[b].x < LENGTH && current_bar_group[b].x + bar_pixel_width > 0) { 
                int col0 = current_bar_group[b].x / TILE_SIZE;         
                int row0 = current_bar_group[b].y_px / TILE_SIZE;      
                int row1 = row0 + BAR_HEIGHT_ROWS - 1;    
                for (int r_tile = row0; r_tile <= row1; r_tile++) { 
                    if (r_tile < 0 || r_tile >= TILE_ROWS) continue;
                    for (int i = 0; i < current_bar_group[b].length; i++) {
                        int c_tile = col0 + i; 
                        if (c_tile >= 0 && c_tile < TILE_COLS)
                            write_tile_to_kernel(r_tile, c_tile, BAR_TILE_IDX);
                    }
                }
            }
        }
    }
}

// Helper function to move bars (updates their state)
void move_all_active_bars(MovingBar bars_a[], MovingBar bars_b[], int array_size, int speed) {
    MovingBar* current_bar_group;
    for (int group = 0; group < 2; group++) {
        current_bar_group = (group == 0) ? bars_a : bars_b;
        for (int b = 0; b < array_size; b++) {
            if (current_bar_group[b].x == BAR_INACTIVE_X) continue;
            current_bar_group[b].x -= speed;
            int bar_pixel_width = current_bar_group[b].length * TILE_SIZE;
            if (current_bar_group[b].x + bar_pixel_width <= 0) {
                current_bar_group[b].x = BAR_INACTIVE_X; 
            }
        }
    }
}

bool handleBarCollision(MovingBar bars[], int array_size, int prevY_chicken, Chicken *chicken, int *score, bool *has_landed_this_jump, int jumpDelayMicroseconds) {
    if (chicken->vy <= 0) return false;
    for (int b = 0; b < array_size; b++) {
        if (bars[b].x == BAR_INACTIVE_X) continue;
        int bar_top_y = bars[b].y_px; int bar_bottom_y = bars[b].y_px + BAR_HEIGHT_ROWS * TILE_SIZE;
        int bar_left_x = bars[b].x; int bar_right_x = bars[b].x + bars[b].length * TILE_SIZE;
        int chicken_bottom_prev = prevY_chicken + CHICKEN_H; int chicken_bottom_curr = chicken->y + CHICKEN_H;
        int chicken_right_x = chicken->x + CHICKEN_W;
        if (chicken_bottom_prev <= bar_top_y && chicken_bottom_curr >= bar_top_y && chicken_bottom_curr <= bar_bottom_y && chicken_right_x > bar_left_x && chicken->x < bar_right_x) {          
            chicken->y = bar_top_y - CHICKEN_H; chicken->vy = 0; chicken->jumping = false;                 
            if (!(*has_landed_this_jump)) { (*score)++; *has_landed_this_jump = true; }
            usleep(jumpDelayMicroseconds); return true; 
        }
    }
    return false; 
}

void *controller_input_thread(void *arg) {
    uint8_t endpoint_address;
    struct libusb_device_handle *controller_handle = opencontroller(&endpoint_address);
    if (!controller_handle) { perror("USB controller open failed in thread"); pthread_exit(NULL); }
    unsigned char buffer[GAMEPAD_READ_LENGTH]; int actual_length_transferred;
    while (1) {
        int status = libusb_interrupt_transfer(controller_handle, endpoint_address, buffer, GAMEPAD_READ_LENGTH, &actual_length_transferred, 0);
        if (status == 0 && actual_length_transferred == GAMEPAD_READ_LENGTH) usb_to_output(&controller_state, buffer); 
        else usleep(10000); 
    }
}

void initChicken(Chicken *c) { c->x = 32; c->y = CHICKEN_ON_TOWER_Y; c->vy = 0; c->jumping = false; }
void moveChicken(Chicken *c) { if (!c->jumping && towerEnabled) return; c->y += c->vy; c->vy += GRAVITY; }

void update_sun_sprite(int current_level_display) { // Takes display level (1-5)
    const int max_sun_level = MAX_GAME_LEVEL; 
    const int start_x_sun = 32; const int end_x_sun = 608; const int base_y_sun = 64;      
    double fraction = (current_level_display > 1) ? (double)(current_level_display - 1) / (max_sun_level - 1) : 0.0;
    if (current_level_display >= max_sun_level) fraction = 1.0; 
    int sun_x_pos = start_x_sun + (int)((end_x_sun - start_x_sun) * fraction + 0.5);
    write_sprite_to_kernel(1, base_y_sun, sun_x_pos, SUN_TILE, 1);
}

void resetBarArray(MovingBar bars[], int array_size) {
    for (int i = 0; i < array_size; i++) { bars[i].x = BAR_INACTIVE_X; bars[i].length = 0; }
}

int main(void) {
    if ((vga_fd = open("/dev/vga_top", O_RDWR)) < 0) { perror("VGA open failed"); return -1; }
    if ((audio_fd = open("/dev/fpga_audio", O_RDWR)) < 0) { perror("Audio open failed"); close(vga_fd); return -1; }
    
    init_vga_interface(); 

    pthread_t controller_thread_id;
    if (pthread_create(&controller_thread_id, NULL, controller_input_thread, NULL) != 0) {
        perror("Controller thread create failed"); close(vga_fd); close(audio_fd); return -1;
    }

    cleartiles(); clearSprites(); fill_sky_and_grass(); vga_present_frame(); 
    write_text((unsigned char *)"scream", 6, 13, 13); write_text((unsigned char *)"jump", 4, 13, 20);
    write_text((unsigned char *)"press", 5, 19, 8); write_text((unsigned char *)"any", 3, 19, 14); 
    write_text((unsigned char *)"key", 3, 19, 20); write_text((unsigned char *)"to", 2, 19, 26); 
    write_text((unsigned char *)"start", 5, 19, 29);
    vga_present_frame(); 
    
    while (!(controller_state.a || controller_state.b || controller_state.start)) { usleep(10000); }

    cleartiles(); clearSprites(); fill_sky_and_grass(); 

    srand(time(NULL)); 
    int score = 0;
    int game_level = 1; // Actual game level used for logic
    int lives = INITIAL_LIVES;
    int jump_velocity = INIT_JUMP_VY; int jump_pause_delay = BASE_DELAY;
    const int hud_center_col = TILE_COLS / 2; const int hud_offset = 12; 

    MovingBar barsA[BAR_ARRAY_SIZE]; MovingBar barsB[BAR_ARRAY_SIZE];
    resetBarArray(barsA, BAR_ARRAY_SIZE); resetBarArray(barsB, BAR_ARRAY_SIZE);

    // Level-dependent parameters
    int current_min_bar_tiles;
    int current_max_bar_tiles;
    int current_bar_count_per_wave;
    int current_bar_speed_base;
    int current_bar_inter_spacing_px;
    int current_y_pos_A;
    int current_y_pos_B;
    int current_wave_switch_trigger_offset_px;
    int current_bar_initial_x_stagger_group_B;


    Chicken chicken; initChicken(&chicken); 
    bool has_landed_this_jump = false; 
    bool group_A_is_active_spawner = true; bool needs_to_spawn_wave_A = true; bool needs_to_spawn_wave_B = false;
    int next_bar_slot_A = 0; int next_bar_slot_B = 0; 
    int watching_bar_idx_A = -1; int watching_bar_idx_B = -1; 

    // --- Main Game Loop ---
    while (lives > 0) {
        // Update game_level based on score
        game_level = 1 + (score / SCORE_PER_LEVEL);
        if (game_level > MAX_GAME_LEVEL) {
            game_level = MAX_GAME_LEVEL;
        }

        // Set parameters based on current game_level
        switch (game_level) {
            case 1:
                current_min_bar_tiles = 5; current_max_bar_tiles = 8;
                current_bar_count_per_wave = 4;
                current_bar_speed_base = 3;
                current_bar_inter_spacing_px = 180; // Wider spacing for easier level
                current_y_pos_A = DEFAULT_BAR_MIN_Y_GROUP_A;
                current_y_pos_B = DEFAULT_BAR_MIN_Y_GROUP_A + DEFAULT_BAR_Y_OFFSET_GROUP_B;
                break;
            case 2:
                current_min_bar_tiles = 4; current_max_bar_tiles = 7;
                current_bar_count_per_wave = 3;
                current_bar_speed_base = 4;
                current_bar_inter_spacing_px = 160;
                current_y_pos_A = DEFAULT_BAR_MIN_Y_GROUP_A - 20; // Slightly different Y
                current_y_pos_B = DEFAULT_BAR_MIN_Y_GROUP_A + DEFAULT_BAR_Y_OFFSET_GROUP_B - 50;
                break;
            case 3:
                current_min_bar_tiles = 3; current_max_bar_tiles = 6;
                current_bar_count_per_wave = 3;
                current_bar_speed_base = 5;
                current_bar_inter_spacing_px = 150;
                // Random Y positions
                current_y_pos_A = BAR_MIN_Y_POS + rand() % (BAR_MAX_Y_POS - BAR_MIN_Y_POS - BAR_RANDOM_Y_RANGE + 1);
                current_y_pos_B = current_y_pos_A + (rand() % (2 * BAR_RANDOM_Y_RANGE + 1)) - BAR_RANDOM_Y_RANGE;
                if (current_y_pos_B < BAR_MIN_Y_POS) current_y_pos_B = BAR_MIN_Y_POS;
                if (current_y_pos_B > BAR_MAX_Y_POS) current_y_pos_B = BAR_MAX_Y_POS;
                break;
            case 4:
                current_min_bar_tiles = 2; current_max_bar_tiles = 5;
                current_bar_count_per_wave = 3;
                current_bar_speed_base = 6;
                current_bar_inter_spacing_px = 140;
                 // Random Y positions, potentially tighter or more varied than L3
                current_y_pos_A = BAR_MIN_Y_POS + rand() % (BAR_MAX_Y_POS - BAR_MIN_Y_POS - BAR_RANDOM_Y_RANGE + 1);
                current_y_pos_B = current_y_pos_A + (rand() % (2 * BAR_RANDOM_Y_RANGE + 1)) - BAR_RANDOM_Y_RANGE;
                if (current_y_pos_B < BAR_MIN_Y_POS) current_y_pos_B = BAR_MIN_Y_POS;
                if (current_y_pos_B > BAR_MAX_Y_POS) current_y_pos_B = BAR_MAX_Y_POS;
                break;
            case 5:
            default: // Default to hardest if level somehow exceeds MAX_GAME_LEVEL
                current_min_bar_tiles = 2; current_max_bar_tiles = 4;
                current_bar_count_per_wave = 2;
                current_bar_speed_base = 7;
                current_bar_inter_spacing_px = 130;
                // Random Y positions
                current_y_pos_A = BAR_MIN_Y_POS + rand() % (BAR_MAX_Y_POS - BAR_MIN_Y_POS - BAR_RANDOM_Y_RANGE + 1);
                current_y_pos_B = current_y_pos_A + (rand() % (2 * BAR_RANDOM_Y_RANGE + 1)) - BAR_RANDOM_Y_RANGE;
                if (current_y_pos_B < BAR_MIN_Y_POS) current_y_pos_B = BAR_MIN_Y_POS;
                if (current_y_pos_B > BAR_MAX_Y_POS) current_y_pos_B = BAR_MAX_Y_POS;
                break;
        }
        // These can also be level-dependent if desired
        current_wave_switch_trigger_offset_px = WAVE_SWITCH_TRIGGER_OFFSET_PX;
        current_bar_initial_x_stagger_group_B = BAR_INITIAL_X_STAGGER_GROUP_B;


        int actual_bar_speed = current_bar_speed_base + (game_level -1); // Overall speed still slightly increases with raw level

        // ───── 1. INPUT PROCESSING ─────
        if (controller_state.b && !chicken.jumping) {
            chicken.vy = jump_velocity; chicken.jumping = true;
            has_landed_this_jump = false; towerEnabled = false; play_sfx(0); 
        }

        // ───── 2. UPDATE GAME STATE ─────
        int prevY_chicken = chicken.y; moveChicken(&chicken);      
        move_all_active_bars(barsA, barsB, BAR_ARRAY_SIZE, actual_bar_speed);
        
        if (group_A_is_active_spawner && needs_to_spawn_wave_A) {
            int spawned_count = 0, last_idx = -1;
            for (int i = 0; i < current_bar_count_per_wave; i++) {
                int slot = -1;
                for (int j = 0; j < BAR_ARRAY_SIZE; j++) {
                    int cur_idx = (next_bar_slot_A + j) % BAR_ARRAY_SIZE;
                    if (barsA[cur_idx].x == BAR_INACTIVE_X) { slot = cur_idx; break; }
                }
                if (slot != -1) {
                    barsA[slot].x = LENGTH + (i * current_bar_inter_spacing_px); 
                    barsA[slot].y_px = current_y_pos_A;
                    barsA[slot].length = rand() % (current_max_bar_tiles - current_min_bar_tiles + 1) + current_min_bar_tiles;
                    last_idx = slot; spawned_count++;
                } else break;
            }
            if (spawned_count > 0) { watching_bar_idx_A = last_idx; next_bar_slot_A = (last_idx + 1) % BAR_ARRAY_SIZE; }
            needs_to_spawn_wave_A = false;
        } 
        else if (!group_A_is_active_spawner && needs_to_spawn_wave_B) {
            int spawned_count = 0, last_idx = -1;
            for (int i = 0; i < current_bar_count_per_wave; i++) {
                int slot = -1;
                for (int j = 0; j < BAR_ARRAY_SIZE; j++) {
                    int cur_idx = (next_bar_slot_B + j) % BAR_ARRAY_SIZE;
                    if (barsB[cur_idx].x == BAR_INACTIVE_X) { slot = cur_idx; break; }
                }
                if (slot != -1) {
                    barsB[slot].x = LENGTH + current_bar_initial_x_stagger_group_B + (i * current_bar_inter_spacing_px);
                    barsB[slot].y_px = current_y_pos_B;
                    barsB[slot].length = rand() % (current_max_bar_tiles - current_min_bar_tiles + 1) + current_min_bar_tiles;
                    last_idx = slot; spawned_count++;
                } else break;
            }
            if (spawned_count > 0) { watching_bar_idx_B = last_idx; next_bar_slot_B = (last_idx + 1) % BAR_ARRAY_SIZE; }
            needs_to_spawn_wave_B = false;
        }

        if (group_A_is_active_spawner && watching_bar_idx_A != -1 && barsA[watching_bar_idx_A].x != BAR_INACTIVE_X) {
            if (barsA[watching_bar_idx_A].x < LENGTH - current_wave_switch_trigger_offset_px) {
                group_A_is_active_spawner = false; needs_to_spawn_wave_B = true; watching_bar_idx_A = -1;         
            }
        } 
        else if (!group_A_is_active_spawner && watching_bar_idx_B != -1 && barsB[watching_bar_idx_B].x != BAR_INACTIVE_X) {
            if (barsB[watching_bar_idx_B].x < LENGTH - current_wave_switch_trigger_offset_px) {
                group_A_is_active_spawner = true; needs_to_spawn_wave_A = true; watching_bar_idx_B = -1;         
            }
        }

        if (chicken.vy > 0) {
            bool landed = handleBarCollision(barsA, BAR_ARRAY_SIZE, prevY_chicken, &chicken, &score, &has_landed_this_jump, jump_pause_delay);
            if (!landed) handleBarCollision(barsB, BAR_ARRAY_SIZE, prevY_chicken, &chicken, &score, &has_landed_this_jump, jump_pause_delay);
        }
        
        if (chicken.y < WALL + 40 && chicken.jumping) { chicken.y = WALL + 40; if (chicken.vy < 0) chicken.vy = 0; }
        if (chicken.y + CHICKEN_H > WIDTH - WALL) { 
            lives--; towerEnabled = true; initChicken(&chicken); has_landed_this_jump = false;
            group_A_is_active_spawner = true; needs_to_spawn_wave_A = true; needs_to_spawn_wave_B = false;
            watching_bar_idx_A = -1; watching_bar_idx_B = -1; next_bar_slot_A = 0; next_bar_slot_B = 0;
            resetBarArray(barsA, BAR_ARRAY_SIZE); resetBarArray(barsB, BAR_ARRAY_SIZE);
            cleartiles(); fill_sky_and_grass(); clearSprites(); 
            vga_present_frame(); 
            if (lives > 0) { play_sfx(1); usleep(2000000); } 
            continue; 
        }

        // ───── 3. DRAW TO BACK BUFFER ─────
        fill_sky_and_grass(); 
        draw_all_active_bars_to_back_buffer(barsA, barsB, BAR_ARRAY_SIZE);
        
        write_text((unsigned char *)"lives", 5, 1, hud_center_col - hud_offset); 
        write_number(lives, 1, hud_center_col - hud_offset + 6); // Single digit for lives
        write_text((unsigned char *)"score", 5, 1, hud_center_col - hud_offset + 12); 
        // MODIFICATION: Use write_numbers for multi-digit score
        write_numbers(score, MAX_SCORE_DISPLAY_DIGITS, 1, hud_center_col - hud_offset + 18); 
        write_text((unsigned char *)"level", 5, 1, hud_center_col - hud_offset + 24); 
        write_number(game_level, 1, hud_center_col - hud_offset + 30); // Display current game level
        
        if (towerEnabled) {
            for (int r_tower = TOWER_TOP_VISIBLE_ROW; r_tower < TOWER_TOP_VISIBLE_ROW + 9; ++r_tower) { 
                 if (r_tower >= TILE_ROWS ) break; 
                for (int c_tower = 0; c_tower < 5; ++c_tower) { write_tile_to_kernel(r_tower, c_tower, TOWER_TILE_IDX); }
            }
        }
        
        // ───── 4. PRESENT THE FRAME ─────
        vga_present_frame();

        // ───── 5. DRAW SPRITES (OVERLAYS) ─────
        clearSprites(); 
        write_sprite_to_kernel(1, chicken.y, chicken.x, chicken.jumping ? CHICKEN_JUMP : CHICKEN_STAND, 0);
        update_sun_sprite(game_level); // Sun position based on game level

        usleep(16666); 
    }

    cleartiles(); fill_sky_and_grass(); clearSprites();
    write_text((unsigned char *)"gameover", 8, 12, (TILE_COLS / 2) - 4); 
    write_text((unsigned char *)"score", 5, 14, (TILE_COLS/2) - 6);
    write_numbers(score, MAX_SCORE_DISPLAY_DIGITS, 14, (TILE_COLS/2) ); 
    vga_present_frame(); 
    play_sfx(2); 
    sleep(3); 

    close(vga_fd); close(audio_fd);
    return 0;
}
