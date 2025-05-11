// screamjump_dynamic_start.c
// Implements precise alternating wave bar generation.

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <fcntl.h>
#include "usbcontroller.h"  // Assumed to provide controller input functions
#include "vga_interface.h"  // Assumed to provide VGA drawing functions
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
#define WAVE_SWITCH_TRIGGER_OFFSET_PX 100 // How far the last bar of a wave moves left before next group activates

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
    int length; // Length in tiles
} MovingBar;

// ───── Moves and draws bars, marks off-screen bars as inactive ───────────────
void updateAndDrawBars(MovingBar bars[], int array_size, int speed) {
    int screen_cols = LENGTH / TILE_SIZE;
    for (int b = 0; b < array_size; b++) {
        // Skip if bar is marked as inactive
        if (bars[b].x == BAR_INACTIVE_X) {
            continue;
        }

        // Move the bar
        bars[b].x -= speed;
        int bar_pixel_width = bars[b].length * TILE_SIZE;

        // If bar is now completely off-screen to the left, mark as inactive
        if (bars[b].x + bar_pixel_width <= 0) {
            bars[b].x = BAR_INACTIVE_X;
            continue; 
        }

        // Draw the bar if any part of it is potentially on screen
        // (A more precise check would be bars[b].x < LENGTH && bars[b].x + bar_pixel_width > 0)
        if (bars[b].x < LENGTH) { 
            int col0 = bars[b].x / TILE_SIZE;         
            int row0 = bars[b].y_px / TILE_SIZE;      
            int row1 = row0 + BAR_HEIGHT_ROWS - 1;    

            for (int r = row0; r <= row1; r++) {
                for (int i = 0; i < bars[b].length; i++) {
                    int c = col0 + i;
                    // Only draw if the tile is within screen boundaries
                    if (c >= 0 && c < screen_cols && r >=0 && r < (WIDTH/TILE_SIZE))
                        write_tile_to_kernel(r, c, BAR_TILE_IDX);
                }
            }
        }
    }
}


bool handleBarCollision(
    MovingBar bars[], int array_size, // MODIFIED: Use array_size
    int prevY_chicken,        
    Chicken *chicken,
    int *score,
    bool *has_landed_this_jump, 
    int jumpDelayMicroseconds
) {
    if (chicken->vy <= 0) return false;

    for (int b = 0; b < array_size; b++) {
        // Skip inactive bars
        if (bars[b].x == BAR_INACTIVE_X) continue;

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
        int transfer_status = libusb_interrupt_transfer( controller_handle, endpoint_address, buffer, GAMEPAD_READ_LENGTH, &actual_length_transferred, 0 );
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

// NEW: Helper to reset all bars in an array to inactive
void resetBarArray(MovingBar bars[], int array_size) {
    for (int i = 0; i < array_size; i++) {
        bars[i].x = BAR_INACTIVE_X;
        bars[i].length = 0; // Optional: reset length
    }
}


int main(void) {
    if ((vga_fd = open("/dev/vga_top", O_RDWR)) < 0) { perror("VGA open"); return -1; }
    if ((audio_fd = open("/dev/fpga_audio", O_RDWR)) < 0) { perror("Audio open"); close(vga_fd); return -1; }

    pthread_t controller_thread_id;
    if (pthread_create(&controller_thread_id, NULL, controller_input_thread, NULL) != 0) {
        perror("Thread create"); close(vga_fd); close(audio_fd); return -1;
    }

    cleartiles(); clearSprites(); fill_sky_and_grass();
    write_text("scream", 6, 13, 13); write_text("jump", 4, 13, 20);
    write_text("press", 5, 19, 8); write_text("any", 3, 19, 14); write_text("key", 3, 19, 20);
    write_text("to", 2, 19, 26); write_text("start", 5, 19, 29);
    while (!(controller_state.a || controller_state.b || controller_state.start)) { usleep(10000); }

    cleartiles(); clearSprites(); fill_sky_and_grass();
    srand(time(NULL)); 

    int score = 0, lives = INITIAL_LIVES, level = 1;
    int jump_velocity = INIT_JUMP_VY; int jump_pause_delay = BASE_DELAY;

    const int screen_tile_cols = LENGTH / TILE_SIZE;
    const int hud_center_col = screen_tile_cols / 2; const int hud_offset = 12; 

    MovingBar barsA[BAR_ARRAY_SIZE];
    MovingBar barsB[BAR_ARRAY_SIZE];
    
    // Initialize all bars to inactive state
    resetBarArray(barsA, BAR_ARRAY_SIZE);
    resetBarArray(barsB, BAR_ARRAY_SIZE);

    int y_pos_group_A = BAR_MIN_Y_GROUP_A;
    int y_pos_group_B = BAR_MIN_Y_GROUP_A + BAR_Y_OFFSET_GROUP_B;
    if (y_pos_group_B > BAR_MAX_Y) y_pos_group_B = BAR_MAX_Y;
    if (y_pos_group_B + BAR_HEIGHT_ROWS * TILE_SIZE > WIDTH - WALL) {
         y_pos_group_B = WIDTH - WALL - (BAR_HEIGHT_ROWS * TILE_SIZE);
    }

    Chicken chicken; initChicken(&chicken); 
    bool has_landed_this_jump = false; 

    // Wave management state variables
    bool group_A_is_active_spawner = true; // Group A starts
    bool needs_to_spawn_wave_A = true;     // Group A needs to spawn its first wave
    bool needs_to_spawn_wave_B = false;
    
    int next_bar_slot_A = 0; // Index in barsA for the start of the next wave
    int next_bar_slot_B = 0; // Index in barsB for the start of the next wave
    
    int watching_bar_idx_A = -1; // Index of the last bar of A's current/most recent wave
    int watching_bar_idx_B = -1; // Index of the last bar of B's current/most recent wave
    
    float watched_bar_initial_spawn_x_A = 0; // X-coordinate where the watched bar of A was launched
    float watched_bar_initial_spawn_x_B = 0; // X-coordinate where the watched bar of B was launched


    while (lives > 0) {
        int current_bar_speed = BAR_SPEED_BASE + (level - 1); 

        if (controller_state.b && !chicken.jumping) {
            chicken.vy = jump_velocity; chicken.jumping = true;
            has_landed_this_jump = false; towerEnabled = false;      
            play_sfx(0); 
        }

        if (chicken.y < WALL + 40 && chicken.jumping) { 
             chicken.y = WALL + 40; if (chicken.vy < 0) chicken.vy = 0; 
        }

        int prevY_chicken = chicken.y; moveChicken(&chicken);         

        if (chicken.vy > 0) { // Collision checking
            bool landed = handleBarCollision(barsA, BAR_ARRAY_SIZE, prevY_chicken, &chicken, &score, &has_landed_this_jump, jump_pause_delay);
            if (!landed) {
                handleBarCollision(barsB, BAR_ARRAY_SIZE, prevY_chicken, &chicken, &score, &has_landed_this_jump, jump_pause_delay);
            }
        }

        if (chicken.y + CHICKEN_H > WIDTH - WALL) { // Death condition
            lives--; towerEnabled = true; initChicken(&chicken); has_landed_this_jump = false;
            
            // Reset wave state on death
            group_A_is_active_spawner = true;
            needs_to_spawn_wave_A = true;
            needs_to_spawn_wave_B = false;
            watching_bar_idx_A = -1;
            watching_bar_idx_B = -1;
            next_bar_slot_A = 0; // Reset slot counters too
            next_bar_slot_B = 0;
            resetBarArray(barsA, BAR_ARRAY_SIZE); // Clear all bars from screen
            resetBarArray(barsB, BAR_ARRAY_SIZE);

            if (lives > 0) { play_sfx(1); usleep(2000000); }
            continue; 
        }

        clearSprites(); fill_sky_and_grass(); 
        write_text("lives", 5, 1, hud_center_col - hud_offset); write_number(lives, 1, hud_center_col - hud_offset + 6);
        write_text("score", 5, 1, hud_center_col - hud_offset + 12); write_number(score, 1, hud_center_col - hud_offset + 18);
        write_text("level", 5, 1, hud_center_col - hud_offset + 24); write_number(level, 1, hud_center_col - hud_offset + 30);

        // --- Wave Spawning Logic ---
        if (group_A_is_active_spawner && needs_to_spawn_wave_A) {
            for (int i = 0; i < BAR_COUNT_PER_WAVE; i++) {
                int current_bar_array_idx = (next_bar_slot_A + i) % BAR_ARRAY_SIZE;
                barsA[current_bar_array_idx].x = LENGTH + (i * BAR_INTER_SPACING_PX);
                barsA[current_bar_array_idx].y_px = y_pos_group_A;
                barsA[current_bar_array_idx].length = rand() % (MAX_BAR_TILES - MIN_BAR_TILES + 1) + MIN_BAR_TILES;
                
                if (i == BAR_COUNT_PER_WAVE - 1) { // This is the last bar of the wave
                    watching_bar_idx_A = current_bar_array_idx;
                    watched_bar_initial_spawn_x_A = barsA[current_bar_array_idx].x;
                }
            }
            next_bar_slot_A = (next_bar_slot_A + BAR_COUNT_PER_WAVE) % BAR_ARRAY_SIZE;
            needs_to_spawn_wave_A = false; // Wave A spawned
        } 
        else if (!group_A_is_active_spawner && needs_to_spawn_wave_B) {
            for (int i = 0; i < BAR_COUNT_PER_WAVE; i++) {
                int current_bar_array_idx = (next_bar_slot_B + i) % BAR_ARRAY_SIZE;
                // Apply group B's overall stagger, then inter-bar spacing
                barsB[current_bar_array_idx].x = LENGTH + BAR_INITIAL_X_STAGGER_GROUP_B + (i * BAR_INTER_SPACING_PX);
                barsB[current_bar_array_idx].y_px = y_pos_group_B;
                barsB[current_bar_array_idx].length = rand() % (MAX_BAR_TILES - MIN_BAR_TILES + 1) + MIN_BAR_TILES;

                if (i == BAR_COUNT_PER_WAVE - 1) { // Last bar of B's wave
                    watching_bar_idx_B = current_bar_array_idx;
                    watched_bar_initial_spawn_x_B = barsB[current_bar_array_idx].x;
                }
            }
            next_bar_slot_B = (next_bar_slot_B + BAR_COUNT_PER_WAVE) % BAR_ARRAY_SIZE;
            needs_to_spawn_wave_B = false; // Wave B spawned
        }

        // Update and draw all bars for both groups
        updateAndDrawBars(barsA, BAR_ARRAY_SIZE, current_bar_speed);
        updateAndDrawBars(barsB, BAR_ARRAY_SIZE, current_bar_speed);

        // --- Turn Switching Logic ---
        if (group_A_is_active_spawner && watching_bar_idx_A != -1) {
            // Check if the last bar of A's wave has moved enough
            if (barsA[watching_bar_idx_A].x < watched_bar_initial_spawn_x_A - WAVE_SWITCH_TRIGGER_OFFSET_PX) {
                group_A_is_active_spawner = false; // Switch to B
                needs_to_spawn_wave_B = true;    // B needs to spawn its next wave
                watching_bar_idx_A = -1;         // Stop watching A's bar for this wave
            }
        } 
        else if (!group_A_is_active_spawner && watching_bar_idx_B != -1) {
            // Check if the last bar of B's wave has moved enough
            if (barsB[watching_bar_idx_B].x < watched_bar_initial_spawn_x_B - WAVE_SWITCH_TRIGGER_OFFSET_PX) {
                group_A_is_active_spawner = true;  // Switch back to A
                needs_to_spawn_wave_A = true;    // A needs to spawn its next wave
                watching_bar_idx_B = -1;         // Stop watching B's bar for this wave
            }
        }
        
        // Draw tower, chicken, sun
        if (towerEnabled) {
            for (int r = TOWER_TOP_VISIBLE_ROW; r < TOWER_TOP_VISIBLE_ROW + 9; ++r) { 
                 if (r >= (WIDTH/TILE_SIZE) ) break; 
                for (int c = 0; c < 5; ++c) { write_tile_to_kernel(r, c, TOWER_TILE_IDX); }
            }
        }
        write_sprite_to_kernel(1, chicken.y, chicken.x, chicken.jumping ? CHICKEN_JUMP : CHICKEN_STAND, 0);
        update_sun(level); 

        usleep(16666); // ~60 FPS
    }

    play_sfx(2); 
    cleartiles(); clearSprites(); fill_sky_and_grass();
    write_text("gameover", 8, 12, (screen_tile_cols / 2) - 4); 
    write_text("score", 5, 14, (screen_tile_cols/2) - 6);
    write_number(score, 14, (screen_tile_cols/2) );
    sleep(3); 

    // Proper cleanup would involve signaling controller_thread to exit and then joining it.
    // For simplicity in this context, we might skip robust thread cleanup.
    // pthread_cancel(controller_thread_id);
    // pthread_join(controller_thread_id, NULL);
    close(vga_fd); close(audio_fd);
    return 0;
}
