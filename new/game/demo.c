// screamjump_dynamic_start.c
// Uses software double buffering for tiles and sprite state buffering.
// Implements level-based difficulty, enhanced game over screen with restart.
// Includes level-dependent jump initiation delay.
// Corrected lives decrement and restart logic.
// Fixed label followed by declaration error.
// Adjusted bar Y randomization for levels 3+ and game over screen.

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <fcntl.h>
#include "usbcontroller.h"  // Assumed to provide controller input functions
#include "vga_interface.h" // Uses new sprite and tile buffering interface
#include "audio_interface.h"  // Assumed to provide audio functions
#include <string.h>

// Screen and physics constants
#define LENGTH            640   // VGA width (pixels)
#define WIDTH             480   // VGA height (pixels)
#define TILE_SIZE          16   // background tile size (pixels)
#define WALL               16   // top/bottom margin (pixels)
#define GRAVITY            +1

// Sprite dimensions
#define CHICKEN_W         32
#define CHICKEN_H         32
#define COIN_SPRITE_W     16 // Assuming coin sprite is 16x16, adjust if different
#define COIN_SPRITE_H     16

// MIF indices
#define CHICKEN_STAND      8   // chicken standing tile
#define CHICKEN_JUMP       11  // chicken jumping tile
#define TOWER_TILE_IDX     42  // static tower tile
#define SUN_TILE           20  // Tile index for the sun sprite
#define COIN_SPRITE_IDX    4   // Sprite ID for the coin, as requested
// SKY_TILE_IDX, GRASS_TILE_IDX are defined in vga_interface.h (ensure they are set correctly!)

// Tower properties
#define TOWER_TOP_VISIBLE_ROW 21 
#define CHICKEN_ON_TOWER_Y ((TOWER_TOP_VISIBLE_ROW * TILE_SIZE) - CHICKEN_H)

// Game settings
#define INITIAL_LIVES      5
#define INIT_JUMP_VY     -20    // base jump velocity
#define BASE_JUMP_INITIATION_DELAY   2000   // Original base delay for jump start (µs) for levels 4-5
#define LONG_JUMP_INITIATION_DELAY   4000   // Longer delay for levels 1-3 (µs)
#define SCORE_PER_LEVEL    5     // Points needed to advance to the next level
#define MAX_GAME_LEVEL     5     // Maximum number of levels
#define MAX_SCORE_DISPLAY_DIGITS 3 // For displaying score like 000 to 999
#define MAX_COINS_DISPLAY_DIGITS 2 // For displaying coins collected (00-99)

// Bar properties (some of these will be overridden by level settings)
#define BAR_ARRAY_SIZE     10    // Max bars stored per group (A or B) - Should be >= BAR_COUNT_PER_WAVE
#define WAVE_SWITCH_TRIGGER_OFFSET_PX 100 // How far the last bar of a wave moves *onto the screen* before next group activates
#define BAR_HEIGHT_ROWS    2     // tiles tall
#define BAR_TILE_IDX      39    // Tile index for the bars - USER MUST VERIFY THIS VALUE
#define BAR_INACTIVE_X  -1000 // Sentinel X value for inactive/off-screen bars
#define BAR_INITIAL_X_STAGGER_GROUP_B 96 // Initial X offset for the entire Group B wave


// Default Bar positioning (can be overridden by level settings, especially for Y)
#define DEFAULT_BAR_MIN_Y_GROUP_A (WALL + 100) 
#define DEFAULT_BAR_Y_OFFSET_GROUP_B 150       
#define BAR_MAX_Y_POS         (WIDTH - BAR_HEIGHT_ROWS * TILE_SIZE - WALL - CHICKEN_H - TILE_SIZE - COIN_SPRITE_H) // Max Y for bar top
#define BAR_MIN_Y_POS         (WALL + 60 + COIN_SPRITE_H) // Min Y for bar top
// MODIFICATION: Different random ranges for Y positions based on level
#define BAR_RANDOM_Y_RANGE_L3    80  // Gentler random Y offset for level 3
#define BAR_RANDOM_Y_RANGE_L4_L5 120 // Larger random Y offset for levels 4 & 5

// Coin properties
#define MAX_COINS_ON_SCREEN 5       // Max active coins at a time
#define COIN_POINTS          10     // Points for collecting a coin
#define COIN_SPAWN_LEVEL     3      // Level at which coins start spawning
#define COIN_SPAWN_CHANCE    100    // Percentage chance (0-100) to spawn a coin on a new bar
#define COIN_COLLECT_DELAY_US (500000) // 0.5 seconds in microseconds to stay on bar for collection
#define FIRST_COIN_SPRITE_REGISTER 2 // Sprite registers 0 (chicken) and 1 (sun) are used.

// Global variables
int vga_fd; 
int audio_fd; 
struct controller_output_packet controller_state; 
bool towerEnabled = true; 
int coins_collected_this_game = 0; // Track coins collected per game attempt

// Structures
typedef struct { 
    int x, y, vy; bool jumping;
    int collecting_coin_idx; int on_bar_collect_timer_us;
} Chicken;
typedef struct { int x, y_px, length; bool has_coin; int coin_idx; } MovingBar;
typedef struct { int bar_idx; int bar_group_id; bool active; int sprite_register; } Coin;

Coin active_coins[MAX_COINS_ON_SCREEN]; // Global array for managing active coins

// Helper function to draw bars to the current tile back buffer
void draw_all_active_bars_to_back_buffer(MovingBar bars_a[], MovingBar bars_b[], int array_size) {
    MovingBar* current_bar_group;
    for (int group = 0; group < 2; group++) { // Iterate through both bar groups
        current_bar_group = (group == 0) ? bars_a : bars_b;
        for (int b = 0; b < array_size; b++) { // Iterate through bars in the current group
            if (current_bar_group[b].x == BAR_INACTIVE_X || current_bar_group[b].length == 0) continue; // Skip inactive bars
            
            int bar_pixel_width = current_bar_group[b].length * TILE_SIZE;
            // Only attempt to draw if the bar is potentially on screen
            if (current_bar_group[b].x < LENGTH && current_bar_group[b].x + bar_pixel_width > 0) { 
                int col0 = current_bar_group[b].x / TILE_SIZE; // Starting tile column         
                int row0 = current_bar_group[b].y_px / TILE_SIZE; // Starting tile row     
                int row1 = row0 + BAR_HEIGHT_ROWS - 1; // Ending tile row   
                for (int r_tile = row0; r_tile <= row1; r_tile++) { 
                    if (r_tile < 0 || r_tile >= TILE_ROWS) continue; // Row boundary check
                    for (int i = 0; i < current_bar_group[b].length; i++) {
                        int c_tile = col0 + i; 
                        // Ensure column is within bounds before drawing
                        if (c_tile >= 0 && c_tile < TILE_COLS)
                            write_tile_to_kernel(r_tile, c_tile, BAR_TILE_IDX); // Writes to software tile back buffer
                    }
                }
            }
        }
    }
}

// Helper function to move bars (updates their state and handles coin deactivation if parent bar goes off-screen)
void move_all_active_bars(MovingBar bars_a[], MovingBar bars_b[], int array_size, int speed) {
    MovingBar* current_bar_group;
    for (int group = 0; group < 2; group++) {
        current_bar_group = (group == 0) ? bars_a : bars_b;
        for (int b = 0; b < array_size; b++) {
            if (current_bar_group[b].x == BAR_INACTIVE_X) continue;
            current_bar_group[b].x -= speed; // Move bar left
            int bar_pixel_width = current_bar_group[b].length * TILE_SIZE;
            if (current_bar_group[b].x + bar_pixel_width <= 0) { // Bar has moved completely off-screen
                current_bar_group[b].x = BAR_INACTIVE_X; 
                // If bar goes inactive, and it had an associated coin, deactivate the coin
                if (current_bar_group[b].has_coin && current_bar_group[b].coin_idx != -1) {
                    int coin_idx = current_bar_group[b].coin_idx;
                     if (coin_idx >= 0 && coin_idx < MAX_COINS_ON_SCREEN && active_coins[coin_idx].active) {
                        active_coins[coin_idx].active = false; // Mark coin as inactive
                        // The sprite will be turned off by present_sprites() based on this desired state
                    }
                    current_bar_group[b].has_coin = false; 
                    current_bar_group[b].coin_idx = -1;
                }
            }
        }
    }
}

// Handles collision between chicken and a group of bars
bool handleBarCollision(MovingBar bars[], int bar_group_id, int array_size, int prevY_chicken, Chicken *chicken, int *score, bool *has_landed_this_jump) {
    if (chicken->vy <= 0) return false; // Only check when chicken is falling
    for (int b = 0; b < array_size; b++) {
        if (bars[b].x == BAR_INACTIVE_X) continue; // Skip inactive bars

        int bar_top_y = bars[b].y_px; 
        int bar_bottom_y = bars[b].y_px + BAR_HEIGHT_ROWS * TILE_SIZE;
        int bar_left_x = bars[b].x; 
        int bar_right_x = bars[b].x + bars[b].length * TILE_SIZE;

        int chicken_bottom_prev = prevY_chicken + CHICKEN_H; 
        int chicken_bottom_curr = chicken->y + CHICKEN_H;
        int chicken_right_x = chicken->x + CHICKEN_W;

        // Check for collision
        if (chicken_bottom_prev <= bar_top_y && chicken_bottom_curr >= bar_top_y && 
            chicken_bottom_curr <= bar_bottom_y && chicken_right_x > bar_left_x && 
            chicken->x < bar_right_x) {          
            chicken->y = bar_top_y - CHICKEN_H; // Snap chicken to bar top
            chicken->vy = 0;                     // Stop vertical movement
            chicken->jumping = false;                 
            if (!(*has_landed_this_jump)) { // Score only once per landing
                (*score)++; 
                *has_landed_this_jump = true; 
            }
            // Check if there's an active coin on this bar to start collection process
            if (bars[b].has_coin && bars[b].coin_idx != -1 && active_coins[bars[b].coin_idx].active) {
                chicken->collecting_coin_idx = bars[b].coin_idx; 
                chicken->on_bar_collect_timer_us = 0; // Reset or start collection timer
            } else {
                chicken->collecting_coin_idx = -1; // No coin, or coin already collected
            }
            return true; // Collision handled
        }
    }
    return false; // No collision with this group
}

// Thread for handling controller input
void *controller_input_thread(void *arg) {
    uint8_t endpoint_address;
    struct libusb_device_handle *controller_handle = opencontroller(&endpoint_address);
    if (!controller_handle) { 
        perror("USB controller open failed in thread"); 
        pthread_exit(NULL); 
    }
    unsigned char buffer[GAMEPAD_READ_LENGTH]; 
    int actual_length_transferred;
    while (1) { 
        int status = libusb_interrupt_transfer(controller_handle, endpoint_address, buffer, GAMEPAD_READ_LENGTH, &actual_length_transferred, 0);
        if (status == 0 && actual_length_transferred == GAMEPAD_READ_LENGTH) {
            usb_to_output(&controller_state, buffer); 
        } else {
            usleep(10000); // Wait a bit before retrying on error
        }
    }
}

// Initializes chicken state
void initChicken(Chicken *c) { 
    c->x = 32; 
    c->y = CHICKEN_ON_TOWER_Y; 
    c->vy = 0; 
    c->jumping = false; 
    c->collecting_coin_idx = -1; 
    c->on_bar_collect_timer_us = 0;
}

// Updates chicken position based on physics
void moveChicken(Chicken *c) { 
    if (!c->jumping && towerEnabled) return; 
    c->y += c->vy; 
    c->vy += GRAVITY; 
}

// Updates the desired state of the sun sprite (buffered)
void update_sun_sprite_buffered(int current_level_display) { 
    const int max_sun_level = MAX_GAME_LEVEL; 
    const int start_x_sun = 32; 
    const int end_x_sun = 608; 
    const int base_y_sun = 64;      
    double fraction = (current_level_display > 1) ? (double)(current_level_display - 1) / (max_sun_level - 1) : 0.0;
    if (current_level_display >= max_sun_level) fraction = 1.0; 
    int sun_x_pos = start_x_sun + (int)((end_x_sun - start_x_sun) * fraction + 0.5);
    // Sprite register 1 is used for the sun.
    write_sprite_to_kernel_buffered(1, base_y_sun, sun_x_pos, SUN_TILE, 1); 
}

// Resets all bars in an array to an inactive state
void resetBarArray(MovingBar bars[], int array_size) {
    for (int i = 0; i < array_size; i++) { 
        bars[i].x = BAR_INACTIVE_X; 
        bars[i].length = 0; 
        bars[i].has_coin = false; 
        bars[i].coin_idx = -1;
    }
}

// Initializes all coins to an inactive state and sets their sprite registers
void init_all_coins(void) {
    for (int i = 0; i < MAX_COINS_ON_SCREEN; i++) {
        active_coins[i].active = false; 
        active_coins[i].bar_idx = -1;
        active_coins[i].bar_group_id = -1;
        active_coins[i].sprite_register = FIRST_COIN_SPRITE_REGISTER + i;
        // The actual hardware sprites will be cleared by clearSprites_buffered -> present_sprites
    }
}

// Sets the desired state for active coin sprites (buffered)
void draw_active_coins_buffered(MovingBar bars_a[], MovingBar bars_b[]) {
    for (int i = 0; i < MAX_COINS_ON_SCREEN; i++) {
        if (active_coins[i].active) {
            MovingBar *parent_bar_array = (active_coins[i].bar_group_id == 0) ? bars_a : bars_b;
            int bar_idx = active_coins[i].bar_idx;
            if (bar_idx != -1 && bar_idx < BAR_ARRAY_SIZE && parent_bar_array[bar_idx].x != BAR_INACTIVE_X) {
                int bar_center_x = parent_bar_array[bar_idx].x + (parent_bar_array[bar_idx].length * TILE_SIZE) / 2;
                int coin_x = bar_center_x - (COIN_SPRITE_W / 2);
                int coin_y = parent_bar_array[bar_idx].y_px - COIN_SPRITE_H - (TILE_SIZE / 4) ; 
                bool on_screen_x = (coin_x + COIN_SPRITE_W > 0) && (coin_x < LENGTH);
                bool on_screen_y = (coin_y + COIN_SPRITE_H > 0) && (coin_y < WIDTH);
                if (on_screen_x && on_screen_y) {
                     write_sprite_to_kernel_buffered(1, coin_y, coin_x, COIN_SPRITE_IDX, active_coins[i].sprite_register);
                } else {
                     write_sprite_to_kernel_buffered(0, 0, 0, 0, active_coins[i].sprite_register);
                }
            } else { 
                 if (active_coins[i].active) { 
                    write_sprite_to_kernel_buffered(0,0,0,0, active_coins[i].sprite_register); 
                    active_coins[i].active = false; 
                 }
            }
        } else { 
            write_sprite_to_kernel_buffered(0,0,0,0, active_coins[i].sprite_register);
        }
    }
}

// MODIFICATION: Renamed from reset_game_state_full.
// This function resets elements for a new attempt (e.g., after losing a life),
// but DOES NOT reset score, game_level, lives count, or total_coins_collected.
void reset_for_level_attempt(Chicken *c, MovingBar bA[], MovingBar bB[], bool *tEnabled, bool *grpA_act, bool *needs_A, bool *needs_B, int *wA_idx, int *wB_idx, int *next_sA, int *next_sB) {
    initChicken(c); // Resets chicken position and coin collection state
    *tEnabled = true;
    resetBarArray(bA, BAR_ARRAY_SIZE);
    resetBarArray(bB, BAR_ARRAY_SIZE);
    init_all_coins(); // Deactivates all coins and prepares their sprite desired states for clearing
    *grpA_act = true;
    *needs_A = true;
    *needs_B = false;
    *wA_idx = -1;
    *wB_idx = -1;
    *next_sA = 0;
    *next_sB = 0;
    
    // Prepare screen buffers for the next attempt
    cleartiles(); // Clear tile back buffer
    fill_sky_and_grass(); // Draw background to tile back buffer
    clearSprites_buffered(); // Set all desired sprite states to inactive
    // The main loop will then call vga_present_frame() and present_sprites()
}


// Main game function
int main(void) {
    if ((vga_fd = open("/dev/vga_top", O_RDWR)) < 0) { perror("VGA open failed"); return -1; }
    if ((audio_fd = open("/dev/fpga_audio", O_RDWR)) < 0) { perror("Audio open failed"); close(vga_fd); return -1; }
    
    init_vga_interface(); // Initializes tile buffers, sprite buffers, and hardware shadow maps

    pthread_t controller_thread_id;
    if (pthread_create(&controller_thread_id, NULL, controller_input_thread, NULL) != 0) {
        perror("Controller thread create failed"); close(vga_fd); close(audio_fd); return -1;
    }
    
    game_restart_point: ; // Label for restarting the game (null statement added)

    // MODIFICATION: These are now explicitly set for a full game restart
    int score = 0;
    int game_level = 1; 
    int lives = INITIAL_LIVES;
    coins_collected_this_game = 0; 

    init_all_coins(); // Initialize/reset coin states at the start of each game attempt

    // --- Start Screen Setup ---
    cleartiles(); 
    clearSprites_buffered(); 
    fill_sky_and_grass(); 
    vga_present_frame(); 
    present_sprites();   

    write_text((unsigned char *)"scream", 6, 13, 13); write_text((unsigned char *)"jump", 4, 13, 20);
    write_text((unsigned char *)"press", 5, 19, 8); write_text((unsigned char *)"any", 3, 19, 14); 
    write_text((unsigned char *)"key", 3, 19, 20); write_text((unsigned char *)"to", 2, 19, 26); 
    write_text((unsigned char *)"start", 5, 19, 29);
    vga_present_frame(); 
    
    while (!(controller_state.a || controller_state.b || controller_state.start)) { usleep(10000); }

    // --- Game Initialization (after start screen) ---
    cleartiles(); 
    clearSprites_buffered(); 
    fill_sky_and_grass(); 
    // Initial game state will be drawn and presented in the first iteration of the game loop.

    srand(time(NULL)); 
    // Score, game_level, lives, coins_collected_this_game are already set for a new game.
    int jump_velocity = INIT_JUMP_VY; 
    const int hud_center_col = TILE_COLS / 2; 
    const int hud_offset = 12; 

    MovingBar barsA[BAR_ARRAY_SIZE]; MovingBar barsB[BAR_ARRAY_SIZE];
    resetBarArray(barsA, BAR_ARRAY_SIZE); resetBarArray(barsB, BAR_ARRAY_SIZE);

    int current_min_bar_tiles, current_max_bar_tiles, current_bar_count_per_wave, current_bar_speed_base;
    int current_bar_inter_spacing_px, current_y_pos_A, current_y_pos_B;
    int current_wave_switch_trigger_offset_px, current_bar_initial_x_stagger_group_B;
    int current_jump_initiation_delay; 
    int current_random_y_range; // MODIFICATION: For level-specific Y randomness

    Chicken chicken; initChicken(&chicken); 
    bool has_landed_this_jump = false; 
    bool group_A_is_active_spawner = true; bool needs_to_spawn_wave_A = true; bool needs_to_spawn_wave_B = false;
    int next_bar_slot_A = 0; int next_bar_slot_B = 0; 
    int watching_bar_idx_A = -1; int watching_bar_idx_B = -1; 

    // --- Main Game Loop ---
    while (lives > 0) { // Loop continues as long as player has lives
        int old_game_level = game_level;
        game_level = 1 + (score / SCORE_PER_LEVEL);
        if (game_level > MAX_GAME_LEVEL) game_level = MAX_GAME_LEVEL;


        // Set parameters based on current game_level
        switch (game_level) {
            case 1: 
                current_min_bar_tiles = 5; current_max_bar_tiles = 8; current_bar_count_per_wave = 4;
                current_bar_speed_base = 3; current_bar_inter_spacing_px = 180; 
                current_y_pos_B = DEFAULT_BAR_MIN_Y_GROUP_A;
                current_y_pos_A = DEFAULT_BAR_MIN_Y_GROUP_A + DEFAULT_BAR_Y_OFFSET_GROUP_B;
                current_jump_initiation_delay = LONG_JUMP_INITIATION_DELAY;
                current_random_y_range = 0; // No random Y for level 1
                break;
            case 2: 
                current_min_bar_tiles = 4; current_max_bar_tiles = 7; current_bar_count_per_wave = 3;
                current_bar_speed_base = 4; current_bar_inter_spacing_px = 160;
                current_y_pos_B = DEFAULT_BAR_MIN_Y_GROUP_A - 20; 
                current_y_pos_A = DEFAULT_BAR_MIN_Y_GROUP_A + DEFAULT_BAR_Y_OFFSET_GROUP_B - 50;
                current_jump_initiation_delay = LONG_JUMP_INITIATION_DELAY;
                current_random_y_range = 0; // No random Y for level 2
                break;
            case 3: 
                current_min_bar_tiles = 3; current_max_bar_tiles = 6; current_bar_count_per_wave = 3;
                current_bar_speed_base = 5; current_bar_inter_spacing_px = 150;
                current_random_y_range = BAR_RANDOM_Y_RANGE_L3; // MODIFICATION: Gentler random Y
                current_y_pos_B = BAR_MIN_Y_POS + rand() % (BAR_MAX_Y_POS - BAR_MIN_Y_POS - current_random_y_range + 1);
                current_y_pos_A = current_y_pos_A + (rand() % (2 * current_random_y_range + 1)) - current_random_y_range;
                if (current_y_pos_B < BAR_MIN_Y_POS) current_y_pos_B = BAR_MIN_Y_POS;
                if (current_y_pos_B > BAR_MAX_Y_POS) current_y_pos_B = BAR_MAX_Y_POS;
                current_jump_initiation_delay = LONG_JUMP_INITIATION_DELAY;
                break;
            case 4: 
                current_min_bar_tiles = 2; current_max_bar_tiles = 5; current_bar_count_per_wave = 3;
                current_bar_speed_base = 6; current_bar_inter_spacing_px = 140;
                current_random_y_range = BAR_RANDOM_Y_RANGE_L4_L5; // MODIFICATION: Larger random Y
                current_y_pos_B = BAR_MIN_Y_POS + rand() % (BAR_MAX_Y_POS - BAR_MIN_Y_POS - current_random_y_range + 1);
                current_y_pos_A = current_y_pos_A + (rand() % (2 * current_random_y_range + 1)) - current_random_y_range;
                if (current_y_pos_B < BAR_MIN_Y_POS) current_y_pos_B = BAR_MIN_Y_POS;
                if (current_y_pos_B > BAR_MAX_Y_POS) current_y_pos_B = BAR_MAX_Y_POS;
                current_jump_initiation_delay = BASE_JUMP_INITIATION_DELAY;
                break;
            case 5: default: 
                current_min_bar_tiles = 2; current_max_bar_tiles = 4; current_bar_count_per_wave = 2;
                current_bar_speed_base = 7; current_bar_inter_spacing_px = 130;
                current_random_y_range = BAR_RANDOM_Y_RANGE_L4_L5; // MODIFICATION: Larger random Y
                current_y_pos_B = BAR_MIN_Y_POS + rand() % (BAR_MAX_Y_POS - BAR_MIN_Y_POS - current_random_y_range + 1);
                current_y_pos_A = current_y_pos_A + (rand() % (2 * current_random_y_range + 1)) - current_random_y_range;
                if (current_y_pos_B < BAR_MIN_Y_POS) current_y_pos_B = BAR_MIN_Y_POS;
                if (current_y_pos_B > BAR_MAX_Y_POS) current_y_pos_B = BAR_MAX_Y_POS;
                current_jump_initiation_delay = BASE_JUMP_INITIATION_DELAY;
                break;
        }
        current_wave_switch_trigger_offset_px = WAVE_SWITCH_TRIGGER_OFFSET_PX;
        current_bar_initial_x_stagger_group_B = BAR_INITIAL_X_STAGGER_GROUP_B;
        int actual_bar_speed = current_bar_speed_base + (game_level -1); 

        // ───── 1. INPUT PROCESSING ─────
        if (controller_state.b && !chicken.jumping) {
            chicken.vy = jump_velocity; chicken.jumping = true;
            has_landed_this_jump = false; towerEnabled = false; play_sfx(0); 
            if(chicken.collecting_coin_idx != -1) { 
                chicken.on_bar_collect_timer_us = 0; chicken.collecting_coin_idx = -1;
            }
            usleep(current_jump_initiation_delay); 
        }

        // ───── 2. UPDATE GAME STATE ─────
        int prevY_chicken = chicken.y; moveChicken(&chicken);      
        move_all_active_bars(barsA, barsB, BAR_ARRAY_SIZE, actual_bar_speed);
        
        // Wave Spawning Logic (with coin spawning)
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
                    barsA[slot].has_coin = false; barsA[slot].coin_idx = -1;
                    if (game_level >= COIN_SPAWN_LEVEL && (rand() % 100) < COIN_SPAWN_CHANCE) {
                        for (int c_idx = 0; c_idx < MAX_COINS_ON_SCREEN; c_idx++) {
                            if (!active_coins[c_idx].active) {
                                active_coins[c_idx].active = true; active_coins[c_idx].bar_idx = slot;
                                active_coins[c_idx].bar_group_id = 0; 
                                barsA[slot].has_coin = true; barsA[slot].coin_idx = c_idx;
                                break; 
                            }
                        }
                    }
                    last_idx = slot; spawned_count++;
                } else { break; }
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
                    barsB[slot].has_coin = false; barsB[slot].coin_idx = -1;
                    if (game_level >= COIN_SPAWN_LEVEL && (rand() % 100) < COIN_SPAWN_CHANCE) {
                        for (int c_idx = 0; c_idx < MAX_COINS_ON_SCREEN; c_idx++) {
                            if (!active_coins[c_idx].active) {
                                active_coins[c_idx].active = true; active_coins[c_idx].bar_idx = slot;
                                active_coins[c_idx].bar_group_id = 1; 
                                barsB[slot].has_coin = true; barsB[slot].coin_idx = c_idx;
                                break;
                            }
                        }
                    }
                    last_idx = slot; spawned_count++;
                } else { break; }
            }
            if (spawned_count > 0) { watching_bar_idx_B = last_idx; next_bar_slot_B = (last_idx + 1) % BAR_ARRAY_SIZE; }
            needs_to_spawn_wave_B = false;
        }

        // Turn Switching Logic
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

        // Coin Collection Timer Update
        if (chicken.collecting_coin_idx != -1 && !chicken.jumping) {
            chicken.on_bar_collect_timer_us += 16666; 
            if (chicken.on_bar_collect_timer_us >= COIN_COLLECT_DELAY_US) {
                Coin* coin_to_collect = &active_coins[chicken.collecting_coin_idx];
                if (coin_to_collect->active) { 
                    score += COIN_POINTS;
                    coins_collected_this_game++; 
                    play_sfx(3); 
                    coin_to_collect->active = false; 
                    MovingBar* parent_bars = (coin_to_collect->bar_group_id == 0) ? barsA : barsB;
                    if(coin_to_collect->bar_idx != -1 && coin_to_collect->bar_idx < BAR_ARRAY_SIZE && 
                       parent_bars[coin_to_collect->bar_idx].coin_idx == chicken.collecting_coin_idx) {
                        parent_bars[coin_to_collect->bar_idx].has_coin = false;
                        parent_bars[coin_to_collect->bar_idx].coin_idx = -1;
                    }
                }
                chicken.collecting_coin_idx = -1; chicken.on_bar_collect_timer_us = 0;
            }
        } else if (chicken.collecting_coin_idx != -1 && chicken.jumping) { 
             chicken.on_bar_collect_timer_us = 0; chicken.collecting_coin_idx = -1;    
        }

        // Bar Collision Detection
        if (chicken.vy > 0) {
            bool landed_on_A = handleBarCollision(barsA, 0, BAR_ARRAY_SIZE, prevY_chicken, &chicken, &score, &has_landed_this_jump);
            if (!landed_on_A) {
                handleBarCollision(barsB, 1, BAR_ARRAY_SIZE, prevY_chicken, &chicken, &score, &has_landed_this_jump);
            }
        }
        
        // Boundary and Death Checks
        if (chicken.y < WALL + 40 && chicken.jumping) { chicken.y = WALL + 40; if (chicken.vy < 0) chicken.vy = 0; }
        if (chicken.y + CHICKEN_H > WIDTH - WALL) { 
            lives--; 
            
            if (lives > 0) { 
                reset_for_level_attempt(&chicken, barsA, barsB, &towerEnabled,
                                  &group_A_is_active_spawner, &needs_to_spawn_wave_A, &needs_to_spawn_wave_B,
                                  &watching_bar_idx_A, &watching_bar_idx_B, &next_bar_slot_A, &next_bar_slot_B);
                
                vga_present_frame(); 
                present_sprites();   
                
                play_sfx(1); 
                usleep(2000000); 
                continue; 
            }
        }

        // ───── 3. DRAW TO TILE BACK BUFFER ─────
        fill_sky_and_grass(); 
        draw_all_active_bars_to_back_buffer(barsA, barsB, BAR_ARRAY_SIZE);
        
        write_text((unsigned char *)"lives", 5, 1, hud_center_col - hud_offset); 
        write_number(lives, 1, hud_center_col - hud_offset + 6);
        write_text((unsigned char *)"score", 5, 1, hud_center_col - hud_offset + 12); 
        write_numbers(score, MAX_SCORE_DISPLAY_DIGITS, 1, hud_center_col - hud_offset + 18); 
        write_text((unsigned char *)"level", 5, 1, hud_center_col - hud_offset + 24); 
        write_number(game_level, 1, hud_center_col - hud_offset + 30);
        
        if (towerEnabled) {
            for (int r_tower = TOWER_TOP_VISIBLE_ROW; r_tower < TOWER_TOP_VISIBLE_ROW + 9; ++r_tower) { 
                 if (r_tower >= TILE_ROWS ) break; 
                for (int c_tower = 0; c_tower < 5; ++c_tower) { write_tile_to_kernel(r_tower, c_tower, TOWER_TILE_IDX); }
            }
        }
        
        // ───── 4. PRESENT THE TILE FRAME ─────
        vga_present_frame();

        // ───── 5. UPDATE DESIRED SPRITE STATES & PRESENT SPRITES ─────
        clearSprites_buffered(); 
        write_sprite_to_kernel_buffered(1, chicken.y, chicken.x, chicken.jumping ? CHICKEN_JUMP : CHICKEN_STAND, 0); 
        update_sun_sprite_buffered(game_level); 
        draw_active_coins_buffered(barsA, barsB); 
        present_sprites(); 

        usleep(16666); 
    }

    // --- Game Over Sequence ---
    cleartiles(); fill_sky_and_grass(); 
    clearSprites_buffered(); 
    
    unsigned char game_over_text_str[] = "game over"; 
    unsigned char final_score_text_str[] = "score "; // Added space for colon
    unsigned char coins_collected_text_str[] = "coins collected "; // Added space for colon
    unsigned char restart_prompt_text_str[] = "press any key to restart"; // MODIFICATION

    int text_row = 10; 
    // Center "game over"
    write_text(game_over_text_str, sizeof(game_over_text_str) - 1, text_row, (TILE_COLS - (sizeof(game_over_text_str) - 1)) / 2);
    
    text_row += 2;
    // Center "score : XXX"
    int score_line_len = (sizeof(final_score_text_str)-1) + MAX_SCORE_DISPLAY_DIGITS;
    int score_start_col = (TILE_COLS - score_line_len) / 2;
    write_text(final_score_text_str, sizeof(final_score_text_str)-1, text_row, score_start_col);
    write_numbers(score, MAX_SCORE_DISPLAY_DIGITS, text_row, score_start_col + sizeof(final_score_text_str)-1);

    text_row += 2;
    // Center "coins collected : XX"
    int coins_line_len = (sizeof(coins_collected_text_str)-1) + MAX_COINS_DISPLAY_DIGITS;
    int coins_start_col = (TILE_COLS - coins_line_len) / 2;
    write_text(coins_collected_text_str, sizeof(coins_collected_text_str)-1, text_row, coins_start_col);
    write_numbers(coins_collected_this_game, MAX_COINS_DISPLAY_DIGITS, text_row, coins_start_col + sizeof(coins_collected_text_str)-1);
    
    text_row += 3;
    // Center "press any key to restart"
    write_text(restart_prompt_text_str, sizeof(restart_prompt_text_str)-1, text_row, (TILE_COLS - (sizeof(restart_prompt_text_str)-1)) / 2);

    vga_present_frame(); 
    present_sprites(); 
    play_sfx(2); 
    
    // Wait for "any key" to restart
    // Clear controller state once before checking to avoid immediate restart from a held button
    memset(&controller_state, 0, sizeof(controller_state)); 
    usleep(100000); // Small delay to ensure controller state is cleared

    while(1) {
        // Check for a press on main action buttons or start/select
        if (controller_state.a || controller_state.b || controller_state.start || 
            controller_state.x || controller_state.y || controller_state.select) {
            goto game_restart_point; 
        }
        usleep(50000); 
    }

    close(vga_fd); close(audio_fd);
    return 0;
}
