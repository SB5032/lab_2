// screamjump_dynamic_start.c
// Uses software double buffering via vga_interface.c

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

// Bar properties
#define BAR_ARRAY_SIZE     10    // Max bars stored per group (A or B)
#define BAR_COUNT_PER_WAVE 5     // Number of bars spawned in a single wave
#define BAR_INTER_SPACING_PX 100 // Horizontal distance between start of bars in a wave
#define WAVE_SWITCH_TRIGGER_OFFSET_PX 100 // How far the last bar of a wave moves *onto the screen* before next group activates

#define BAR_HEIGHT_ROWS    2     // tiles tall
#define BAR_SPEED_BASE     4     // pixels/frame
#define MIN_BAR_TILES      3     // Min length in tiles (3 * 16px = 48px)
#define MAX_BAR_TILES      6     // Max length in tiles (6 * 16px = 96px)
#define BAR_TILE_IDX      39    // Tile index for the bars - USER MUST VERIFY THIS VALUE
#define BAR_INACTIVE_X  -1000 // Sentinel X value for inactive/off-screen bars

// Bar positioning
#define BAR_MIN_Y_GROUP_A (WALL + 100) 
#define BAR_Y_OFFSET_GROUP_B 150       
#define BAR_MAX_Y         (WIDTH - BAR_HEIGHT_ROWS * TILE_SIZE - WALL) 
#define BAR_INITIAL_X_STAGGER_GROUP_B 96 // Initial X offset for the entire Group B wave

// Global variables
int vga_fd; // Defined here, used by vga_interface.c via extern
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
            // Only attempt to draw if the bar is potentially on screen
            if (current_bar_group[b].x < LENGTH && current_bar_group[b].x + bar_pixel_width > 0) { 
                int col0 = current_bar_group[b].x / TILE_SIZE;         
                int row0 = current_bar_group[b].y_px / TILE_SIZE;      
                int row1 = row0 + BAR_HEIGHT_ROWS - 1;    

                for (int r_tile = row0; r_tile <= row1; r_tile++) { 
                    if (r_tile < 0 || r_tile >= TILE_ROWS) continue; // Row boundary check
                    for (int i = 0; i < current_bar_group[b].length; i++) {
                        int c_tile = col0 + i; 
                        // Ensure column is within bounds before drawing
                        if (c_tile >= 0 && c_tile < TILE_COLS)
                            write_tile_to_kernel(r_tile, c_tile, BAR_TILE_IDX); // Writes to software back buffer
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

// Handles collision between chicken and a group of bars
bool handleBarCollision(MovingBar bars[], int array_size, int prevY_chicken, Chicken *chicken, int *score, bool *has_landed_this_jump, int jumpDelayMicroseconds) {
    if (chicken->vy <= 0) return false; // Only check when falling
    for (int b = 0; b < array_size; b++) {
        if (bars[b].x == BAR_INACTIVE_X) continue; // Skip inactive bars

        int bar_top_y = bars[b].y_px; 
        int bar_bottom_y = bars[b].y_px + BAR_HEIGHT_ROWS * TILE_SIZE;
        int bar_left_x = bars[b].x; 
        int bar_right_x = bars[b].x + bars[b].length * TILE_SIZE;

        int chicken_bottom_prev = prevY_chicken + CHICKEN_H; 
        int chicken_bottom_curr = chicken->y + CHICKEN_H;
        int chicken_right_x = chicken->x + CHICKEN_W;

        // Collision condition
        if (chicken_bottom_prev <= bar_top_y && chicken_bottom_curr >= bar_top_y && 
            chicken_bottom_curr <= bar_bottom_y && chicken_right_x > bar_left_x && 
            chicken->x < bar_right_x) {          
            chicken->y = bar_top_y - CHICKEN_H; // Snap to bar top
            chicken->vy = 0;                     // Stop falling
            chicken->jumping = false;                 
            if (!(*has_landed_this_jump)) { // Score only once per landing
                (*score)++; 
                *has_landed_this_jump = true; 
            }
            usleep(jumpDelayMicroseconds); // Brief pause
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
    while (1) { // Loop indefinitely to read controller state
        int status = libusb_interrupt_transfer(controller_handle, endpoint_address, buffer, GAMEPAD_READ_LENGTH, &actual_length_transferred, 0);
        if (status == 0 && actual_length_transferred == GAMEPAD_READ_LENGTH) {
            usb_to_output(&controller_state, buffer); 
        } else {
            // fprintf(stderr, "Controller read error: %s\n", libusb_error_name(status)); // Can be noisy
            usleep(10000); // Wait a bit before retrying on error
        }
    }
    // libusb_close(controller_handle); // Should be called if thread could exit, but it's an infinite loop.
    // pthread_exit(NULL);
}

// Initializes chicken state
void initChicken(Chicken *c) { 
    c->x = 32; 
    c->y = CHICKEN_ON_TOWER_Y; 
    c->vy = 0; 
    c->jumping = false; 
}

// Updates chicken position based on physics
void moveChicken(Chicken *c) { 
    if (!c->jumping && towerEnabled) return; // Don't move if on tower and not jumping
    c->y += c->vy; 
    c->vy += GRAVITY; 
}

// Updates and draws the sun sprite
void update_sun_sprite(int current_level) { 
    const int max_level = 5; 
    const int start_x_sun = 32; 
    const int end_x_sun = 608; 
    const int base_y_sun = 64;      
    double fraction = (current_level > 1) ? (double)(current_level - 1) / (max_level - 1) : 0.0;
    if (current_level >= max_level) fraction = 1.0; 
    int sun_x_pos = start_x_sun + (int)((end_x_sun - start_x_sun) * fraction + 0.5);
    // Sprite register 1 is arbitrarily chosen for the sun.
    write_sprite_to_kernel(1, base_y_sun, sun_x_pos, SUN_TILE, 1); 
}

// Resets all bars in an array to an inactive state
void resetBarArray(MovingBar bars[], int array_size) {
    for (int i = 0; i < array_size; i++) { 
        bars[i].x = BAR_INACTIVE_X; 
        bars[i].length = 0; 
    }
}

// Main game function
int main(void) {
    // Open device files for VGA and Audio
    if ((vga_fd = open("/dev/vga_top", O_RDWR)) < 0) { 
        perror("VGA open failed"); 
        return -1; 
    }
    if ((audio_fd = open("/dev/fpga_audio", O_RDWR)) < 0) { 
        perror("Audio open failed"); 
        close(vga_fd); // Close already opened vga_fd
        return -1; 
    }
    
    // Initialize the VGA interface (software double buffers, hardware shadow map)
    init_vga_interface(); 

    // Create thread for controller input
    pthread_t controller_thread_id;
    if (pthread_create(&controller_thread_id, NULL, controller_input_thread, NULL) != 0) {
        perror("Controller thread create failed"); 
        close(vga_fd); 
        close(audio_fd); 
        return -1;
    }

    // --- Start Screen Setup ---
    cleartiles(); // Clears current software back buffer
    clearSprites(); // Clears hardware sprites
    fill_sky_and_grass(); // Fills current software back buffer with sky and grass
    vga_present_frame(); // Present the initial empty screen with background

    // Draw start screen text to the (new) back buffer
    // Cast string literals to (unsigned char *) for write_text
    write_text((unsigned char *)"scream", 6, 13, 13); 
    write_text((unsigned char *)"jump", 4, 13, 20);
    write_text((unsigned char *)"press", 5, 19, 8); 
    write_text((unsigned char *)"any", 3, 19, 14); 
    write_text((unsigned char *)"key", 3, 19, 20);
    write_text((unsigned char *)"to", 2, 19, 26); 
    write_text((unsigned char *)"start", 5, 19, 29);
    vga_present_frame(); // Present the start screen text
    
    // Wait for player to press a key to start the game
    while (!(controller_state.a || controller_state.b || controller_state.start)) { 
        usleep(10000); // Check controller state every 10ms
    }

    // --- Game Initialization ---
    cleartiles(); // Clear back buffer for game screen
    clearSprites(); // Clear hardware sprites
    fill_sky_and_grass(); // Draw initial game background to back buffer
    // Initial game state will be drawn and presented in the first iteration of the game loop.

    srand(time(NULL)); // Seed random number generator
    int score = 0, lives = INITIAL_LIVES, level = 1;
    int jump_velocity = INIT_JUMP_VY; 
    int jump_pause_delay = BASE_DELAY;
    const int hud_center_col = TILE_COLS / 2; // TILE_COLS from vga_interface.h
    const int hud_offset = 12; 

    MovingBar barsA[BAR_ARRAY_SIZE]; 
    MovingBar barsB[BAR_ARRAY_SIZE];
    resetBarArray(barsA, BAR_ARRAY_SIZE); // Initialize bars to inactive
    resetBarArray(barsB, BAR_ARRAY_SIZE);

    int y_pos_group_A = BAR_MIN_Y_GROUP_A; 
    int y_pos_group_B = BAR_MIN_Y_GROUP_A + BAR_Y_OFFSET_GROUP_B;
    // Ensure Group B bars are within screen limits
    if (y_pos_group_B > BAR_MAX_Y) y_pos_group_B = BAR_MAX_Y;
    if (y_pos_group_B + BAR_HEIGHT_ROWS * TILE_SIZE > WIDTH - WALL) {
        y_pos_group_B = WIDTH - WALL - (BAR_HEIGHT_ROWS * TILE_SIZE);
    }

    Chicken chicken; 
    initChicken(&chicken); 
    bool has_landed_this_jump = false; 
    bool group_A_is_active_spawner = true; // Group A starts spawning
    bool needs_to_spawn_wave_A = true; 
    bool needs_to_spawn_wave_B = false;
    int next_bar_slot_A = 0; // Tracks next available slot for spawning in barsA
    int next_bar_slot_B = 0; // Tracks next available slot for spawning in barsB
    int watching_bar_idx_A = -1; // Index of the last bar in Group A's current wave
    int watching_bar_idx_B = -1; // Index of the last bar in Group B's current wave

    // --- Main Game Loop ---
    while (lives > 0) {
        int current_bar_speed = BAR_SPEED_BASE + (level - 1); // Bar speed increases with level

        // ───── 1. INPUT PROCESSING ─────
        if (controller_state.b && !chicken.jumping) { // 'B' button for jump
            chicken.vy = jump_velocity; 
            chicken.jumping = true;
            has_landed_this_jump = false; // Reset landing flag
            towerEnabled = false;      // Chicken has left the tower
            play_sfx(0); // Play jump sound
        }

        // ───── 2. UPDATE GAME STATE ─────
        int prevY_chicken = chicken.y; // Store Y before moving for collision detection
        moveChicken(&chicken);      // Apply physics to chicken
        move_all_active_bars(barsA, barsB, BAR_ARRAY_SIZE, current_bar_speed); // Move all bars
        
        // Wave Spawning Logic
        if (group_A_is_active_spawner && needs_to_spawn_wave_A) {
            int spawned_count = 0, last_idx_in_wave = -1;
            for (int i = 0; i < BAR_COUNT_PER_WAVE; i++) { // Attempt to spawn a full wave
                int slot_to_use = -1;
                // Find an inactive slot in barsA array
                for (int j = 0; j < BAR_ARRAY_SIZE; j++) {
                    int current_search_idx = (next_bar_slot_A + j) % BAR_ARRAY_SIZE;
                    if (barsA[current_search_idx].x == BAR_INACTIVE_X) { 
                        slot_to_use = current_search_idx; 
                        break; 
                    }
                }
                if (slot_to_use != -1) { // If a free slot is found
                    barsA[slot_to_use].x = LENGTH + (i * BAR_INTER_SPACING_PX); // Position new bar
                    barsA[slot_to_use].y_px = y_pos_group_A;
                    barsA[slot_to_use].length = rand() % (MAX_BAR_TILES - MIN_BAR_TILES + 1) + MIN_BAR_TILES;
                    last_idx_in_wave = slot_to_use; 
                    spawned_count++;
                } else { break; } // No free slot, wave might be shorter or not spawn fully
            }
            if (spawned_count > 0) { // If any bars were spawned for this wave
                watching_bar_idx_A = last_idx_in_wave; // Track the last bar of this wave
                next_bar_slot_A = (last_idx_in_wave + 1) % BAR_ARRAY_SIZE; // Update hint for next search
            }
            needs_to_spawn_wave_A = false; // Wave A spawning attempt done for now
        } 
        else if (!group_A_is_active_spawner && needs_to_spawn_wave_B) { // Similar logic for Group B
            int spawned_count = 0, last_idx_in_wave = -1;
            for (int i = 0; i < BAR_COUNT_PER_WAVE; i++) {
                int slot_to_use = -1;
                for (int j = 0; j < BAR_ARRAY_SIZE; j++) {
                    int current_search_idx = (next_bar_slot_B + j) % BAR_ARRAY_SIZE;
                    if (barsB[current_search_idx].x == BAR_INACTIVE_X) { 
                        slot_to_use = current_search_idx; 
                        break; 
                    }
                }
                if (slot_to_use != -1) {
                    barsB[slot_to_use].x = LENGTH + BAR_INITIAL_X_STAGGER_GROUP_B + (i * BAR_INTER_SPACING_PX);
                    barsB[slot_to_use].y_px = y_pos_group_B;
                    barsB[slot_to_use].length = rand() % (MAX_BAR_TILES - MIN_BAR_TILES + 1) + MIN_BAR_TILES;
                    last_idx_in_wave = slot_to_use; 
                    spawned_count++;
                } else { break; }
            }
            if (spawned_count > 0) {
                watching_bar_idx_B = last_idx_in_wave;
                next_bar_slot_B = (last_idx_in_wave + 1) % BAR_ARRAY_SIZE;
            }
            needs_to_spawn_wave_B = false;
        }

        // Turn Switching Logic (based on last bar of current wave's position)
        if (group_A_is_active_spawner && watching_bar_idx_A != -1 && barsA[watching_bar_idx_A].x != BAR_INACTIVE_X) {
            if (barsA[watching_bar_idx_A].x < LENGTH - WAVE_SWITCH_TRIGGER_OFFSET_PX) { // If last bar is sufficiently on screen
                group_A_is_active_spawner = false; // Switch to Group B
                needs_to_spawn_wave_B = true;    // Group B needs to spawn
                watching_bar_idx_A = -1;         // Stop watching Group A's bar for this wave
            }
        } 
        else if (!group_A_is_active_spawner && watching_bar_idx_B != -1 && barsB[watching_bar_idx_B].x != BAR_INACTIVE_X) {
            if (barsB[watching_bar_idx_B].x < LENGTH - WAVE_SWITCH_TRIGGER_OFFSET_PX) {
                group_A_is_active_spawner = true;  // Switch back to Group A
                needs_to_spawn_wave_A = true;    // Group A needs to spawn
                watching_bar_idx_B = -1;         // Stop watching Group B's bar
            }
        }

        // Collision Detection with bars
        if (chicken.vy > 0) { // Only check collision if chicken is falling
            bool landed_on_A = handleBarCollision(barsA, BAR_ARRAY_SIZE, prevY_chicken, &chicken, &score, &has_landed_this_jump, jump_pause_delay);
            if (!landed_on_A) { // If not landed on Group A, check Group B
                handleBarCollision(barsB, BAR_ARRAY_SIZE, prevY_chicken, &chicken, &score, &has_landed_this_jump, jump_pause_delay);
            }
        }
        
        // Boundary and Death Checks
        if (chicken.y < WALL + 40 && chicken.jumping) { // Ceiling check
             chicken.y = WALL + 40; 
             if (chicken.vy < 0) chicken.vy = 0; // Stop upward motion if hit ceiling
        }
        if (chicken.y + CHICKEN_H > WIDTH - WALL) { // Floor check (death condition)
            lives--; 
            towerEnabled = true; // Re-enable tower for respawn
            initChicken(&chicken); // Reset chicken
            has_landed_this_jump = false;
            // Reset wave spawning state fully on death
            group_A_is_active_spawner = true; 
            needs_to_spawn_wave_A = true; 
            needs_to_spawn_wave_B = false;
            watching_bar_idx_A = -1; 
            watching_bar_idx_B = -1; 
            next_bar_slot_A = 0; 
            next_bar_slot_B = 0;
            resetBarArray(barsA, BAR_ARRAY_SIZE); // Clear all bars from internal state
            resetBarArray(barsB, BAR_ARRAY_SIZE);
            
            // Prepare screen for death pause or next life
            cleartiles(); // Clear back buffer
            fill_sky_and_grass(); // Redraw static background to back buffer
            clearSprites(); // Clear hardware sprites
            // Optional: Could draw "You Died" or lives remaining to back buffer here
            vga_present_frame(); // Present the cleared state

            if (lives > 0) { 
                play_sfx(1); // Play death/fall sound
                usleep(2000000); // Pause for 2 seconds
            } 
            continue; // Skip rest of the loop iteration, start next life/game over
        }

        // ───── 3. DRAW TO BACK BUFFER ─────
        // All tile-based drawing operations now write to the software back buffer.
        fill_sky_and_grass(); // Redraws background to back buffer (optimized by vga_interface)
        draw_all_active_bars_to_back_buffer(barsA, barsB, BAR_ARRAY_SIZE); // Draw bars to back buffer
        
        // Draw HUD elements to back buffer
        write_text((unsigned char *)"lives", 5, 1, hud_center_col - hud_offset); 
        write_number(lives, 1, hud_center_col - hud_offset + 6);
        write_text((unsigned char *)"score", 5, 1, hud_center_col - hud_offset + 12); 
        write_number(score, 1, hud_center_col - hud_offset + 18);
        write_text((unsigned char *)"level", 5, 1, hud_center_col - hud_offset + 24); 
        write_number(level, 1, hud_center_col - hud_offset + 30);
        
        // Draw Tower to back buffer if enabled
        if (towerEnabled) {
            for (int r_tower = TOWER_TOP_VISIBLE_ROW; r_tower < TOWER_TOP_VISIBLE_ROW + 9; ++r_tower) { 
                 if (r_tower >= TILE_ROWS ) break; // Boundary check
                for (int c_tower = 0; c_tower < 5; ++c_tower) { // Tower width
                    write_tile_to_kernel(r_tower, c_tower, TOWER_TILE_IDX);
                }
            }
        }
        
        // ───── 4. PRESENT THE FRAME ─────
        // This copies the completed back buffer to the hardware screen efficiently.
        vga_present_frame();

        // ───── 5. DRAW SPRITES (OVERLAYS) ─────
        // Sprites are drawn directly to hardware after the tilemap frame is presented.
        // This ensures they appear on top of the tile-based graphics.
        clearSprites(); // Clear previous hardware sprite positions before drawing new ones
        write_sprite_to_kernel(1, chicken.y, chicken.x, chicken.jumping ? CHICKEN_JUMP : CHICKEN_STAND, 0); // Chicken on sprite reg 0
        update_sun_sprite(level); // Sun on sprite reg 1 (update_sun_sprite calls write_sprite_to_kernel)

        // Frame delay to aim for approximately 60 FPS
        usleep(16666); 
    }

    // --- Game Over Sequence ---
    cleartiles(); // Clear back buffer
    fill_sky_and_grass(); // Draw background to back buffer
    clearSprites(); // Clear hardware sprites

    // Draw "Game Over" and final score to back buffer
    write_text((unsigned char *)"gameover", 8, 12, (TILE_COLS / 2) - 4); 
    write_text((unsigned char *)"score", 5, 14, (TILE_COLS/2) - 6);
    write_number(score, 14, (TILE_COLS/2) ); // Display final score
    
    vga_present_frame(); // Present Game Over screen
    play_sfx(2); // Play game over sound
    sleep(3); // Display game over message for 3 seconds

    // Cleanup
    close(vga_fd); 
    close(audio_fd);
    // For a very clean exit, you might signal the controller_thread to terminate and then pthread_join it.
    // This is often omitted in simpler embedded programs where main doesn't return or the system reboots.
    return 0;
}
