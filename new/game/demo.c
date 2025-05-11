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
// NEW: Define the row number for the visible top of the tower
#define TOWER_TOP_VISIBLE_ROW 21 
// NEW: Calculate Y position for chicken to stand on the tower
#define CHICKEN_ON_TOWER_Y ((TOWER_TOP_VISIBLE_ROW * TILE_SIZE) - CHICKEN_H)

// ───── lives/score & controller ──────────────────────────────────────────────
#define INITIAL_LIVES      5
#define INIT_JUMP_VY     -20    // base jump velocity
#define BASE_DELAY       2000   // base jump delay (µs)

// ───── bar-config limits ─────────────────────────────────────────────────────
// MODIFIED: Number of bars per alternating group
#define BAR_COUNT_PER_GROUP 4  
#define BAR_HEIGHT_ROWS    2     // tiles tall
#define BAR_SPEED_BASE     4     // pixels/frame
// MODIFIED: Bar sizes made smaller
#define MIN_BAR_TILES      3     // Min length in tiles (3 * 16px = 48px)
#define MAX_BAR_TILES      6     // Max length in tiles (6 * 16px = 96px)
#define BAR_TILE_IDX      39

// ───── bar Y-bounds & positioning ───────────────────────────────────────────
#define BAR_MIN_Y_GROUP_A (WALL + 100) // Y position for the first group of bars
#define BAR_Y_OFFSET_GROUP_B 150       // Vertical offset for the second group of bars
#define BAR_MAX_Y         (WIDTH - BAR_HEIGHT_ROWS * TILE_SIZE - WALL) // Max Y for the top of any bar
#define BAR_INITIAL_X_STAGGER_GROUP_B 96 // Horizontal stagger for the second group

int vga_fd, audio_fd; // File descriptors for VGA and audio hardware
struct controller_output_packet controller_state; // Holds current controller input
bool towerEnabled = true; // Flag to indicate if the initial tower is active

// Structure for the chicken character
typedef struct {
    int x, y, vy;
    bool jumping;
} Chicken;

// Structure for a moving bar
typedef struct {
    int x;      // Horizontal position (left edge)
    int y_px;   // Vertical position (top edge, in pixels)
    int length; // Length in number of tiles
} MovingBar;

// ───── Initialize a bar-array with potential initial horizontal stagger ───────────────────
// MODIFIED: Added initial_x_stagger parameter
void initBars(MovingBar bars[], int count, int y_px, int initial_x_stagger) {
    // Calculate spacing between bars WITHIN this group
    int spawnInterval = LENGTH / count; 
    if (count == 0) spawnInterval = LENGTH; // Avoid division by zero if count is 0

    for (int i = 0; i < count; i++) {
        // Apply stagger to the whole group, then interval for individual bars
        bars[i].x      = LENGTH + initial_x_stagger + i * spawnInterval; 
        bars[i].y_px   = y_px;
        // Randomize bar length within defined limits
        bars[i].length = rand() % (MAX_BAR_TILES - MIN_BAR_TILES + 1) 
                         + MIN_BAR_TILES;
    }
}

// ───── Move, respawn & draw all bars in one go ───────────────────────────────
void updateAndDrawBars(MovingBar bars[], int count, int speed, int *spawnCounter, int initial_x_stagger_for_respawn) {
    if (count == 0) return; // Do nothing if there are no bars in this group
    
    // Calculate spacing for respawning bars to distribute them
    int spawnInterval = LENGTH / count;
    if (count == 0) spawnInterval = LENGTH;

    int screen_cols = LENGTH / TILE_SIZE; // Number of tile columns on screen

    for (int b = 0; b < count; b++) {
        // Move the bar
        bars[b].x -= speed;
        int bar_width_px = bars[b].length * TILE_SIZE;

        // Respawn if bar is completely off-screen to the left
        if (bars[b].x + bar_width_px <= 0) {
            // MODIFIED: Respawn logic now can also consider an initial stagger if needed,
            // though current use in main passes 0 for simplicity after initial setup.
            // The spawnCounter helps distribute respawns if multiple bars go off-screen near simultaneously.
            bars[b].x = LENGTH + initial_x_stagger_for_respawn + ((*spawnCounter) % count) * spawnInterval;
            (*spawnCounter)++; // Increment spawn counter for this group
            // Re-randomize length for the newly respawned bar
            bars[b].length = rand() % (MAX_BAR_TILES - MIN_BAR_TILES + 1) 
                             + MIN_BAR_TILES;
        }

        // Draw the bar tiles
        int col0 = bars[b].x / TILE_SIZE;         // Starting column for this bar
        int row0 = bars[b].y_px / TILE_SIZE;      // Starting row for this bar
        int row1 = row0 + BAR_HEIGHT_ROWS - 1;    // Ending row for this bar

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

// ───── Handle collision between chicken and a group of bars ──────────────────
// Returns true if chicken landed on a bar in this group
bool handleBarCollision(
    MovingBar bars[], int count,
    int prevY_chicken,         // Chicken's Y position in the previous frame
    Chicken *chicken,
    int *score,
    bool *has_landed_this_jump, // Flag to ensure score increments only once per landing
    int jumpDelayMicroseconds
) {
    // Only check for collision when the chicken is falling (vy > 0)
    if (chicken->vy <= 0) return false;

    for (int b = 0; b < count; b++) {
        int bar_top_y    = bars[b].y_px;
        int bar_bottom_y = bars[b].y_px + BAR_HEIGHT_ROWS * TILE_SIZE;
        int bar_left_x   = bars[b].x;
        int bar_right_x  = bars[b].x + bars[b].length * TILE_SIZE;

        int chicken_bottom_prev = prevY_chicken + CHICKEN_H;
        int chicken_bottom_curr = chicken->y + CHICKEN_H;
        int chicken_right_x = chicken->x + CHICKEN_W;

        // Collision detection logic:
        // 1. Chicken's bottom edge was above or at bar's top in previous frame.
        // 2. Chicken's bottom edge is below or at bar's top in current frame.
        // 3. Chicken is horizontally overlapping with the bar.
        if (chicken_bottom_prev <= bar_top_y &&  // Was above or at top
            chicken_bottom_curr >= bar_top_y &&  // Is now below or at top (passed through top)
            chicken_bottom_curr <= bar_bottom_y && // And not fallen completely through bar yet
            chicken_right_x > bar_left_x &&      // Horizontal overlap (right edge of chicken vs left edge of bar)
            chicken->x < bar_right_x) {          // Horizontal overlap (left edge of chicken vs right edge of bar)

            // Collision detected!
            chicken->y       = bar_top_y - CHICKEN_H; // Snap chicken to the top of the bar
            chicken->vy      = 0;                     // Stop vertical movement
            chicken->jumping = false;                 // Chicken is no longer jumping (it's landed)

            // Increment score only once per successful landing
            if (!(*has_landed_this_jump)) {
                (*score)++;
                *has_landed_this_jump = true;
            }
            usleep(jumpDelayMicroseconds); // Brief pause after landing
            return true; // Collision handled
        }
    }
    return false; // No collision with this group of bars
}

// ───── Thread for handling controller input ───────────────────────────────────
void *controller_input_thread(void *arg) {
    uint8_t endpoint_address;
    // Attempt to open the USB controller
    struct libusb_device_handle *controller_handle = opencontroller(&endpoint_address);
    if (!controller_handle) {
        perror("Failed to open USB controller");
        pthread_exit(NULL);
    }

    unsigned char buffer[GAMEPAD_READ_LENGTH]; // Buffer for controller data
    int actual_length_transferred;

    while (1) {
        // Perform an interrupt transfer to read controller state
        int transfer_status = libusb_interrupt_transfer(
            controller_handle,
            endpoint_address,
            buffer,
            GAMEPAD_READ_LENGTH,
            &actual_length_transferred,
            0 // No timeout (blocking)
        );

        if (transfer_status == 0 && actual_length_transferred == GAMEPAD_READ_LENGTH) {
            usb_to_output(&controller_state, buffer); // Convert raw USB data to controller_state
        } else {
            // Handle potential errors or disconnections
            fprintf(stderr, "Controller read error or disconnect: %s\n", libusb_error_name(transfer_status));
            // Consider attempting to reopen controller or exiting thread
            // For now, just retry.
            usleep(100000); // Wait a bit before retrying
        }
    }
    // libusb_close(controller_handle); // Should be called on cleanup, but loop is infinite
    // pthread_exit(NULL); // Redundant due to infinite loop
}


// ───── Initialize chicken's state and position ────────────────────────────────
void initChicken(Chicken *c) {
    c->x = 32; // Initial X position
    // MODIFIED: Chicken starts on the predefined tower Y position
    c->y = CHICKEN_ON_TOWER_Y; 
    c->vy = 0;        // Initial vertical velocity
    c->jumping = false; // Initially not jumping
}

// ───── Update chicken's position based on velocity and gravity ────────────────
void moveChicken(Chicken *c) {
    // If chicken is on the tower and not jumping, don't apply physics
    if (!c->jumping && towerEnabled) return; 

    c->y += c->vy;    // Update Y position based on vertical velocity
    c->vy += GRAVITY; // Apply gravity to vertical velocity
}

// ───── Update sun sprite position based on level ──────────────────────────────
void update_sun(int current_level) {
    const int max_level = 5;        // Max game level for sun positioning
    const int start_x_sun = 32;     // Sun's starting X
    const int end_x_sun = 608;      // Sun's ending X
    const int base_y_sun = 64;      // Sun's Y position

    // Calculate fractional progress through levels
    double fraction = (current_level > 1) ? (double)(current_level - 1) / (max_level - 1) : 0.0;
    if (current_level >= max_level) fraction = 1.0; // Cap at max level

    int sun_x_pos = start_x_sun + (int)((end_x_sun - start_x_sun) * fraction + 0.5);
    write_sprite_to_kernel(1, base_y_sun, sun_x_pos, SUN_TILE, 1); // Draw sun sprite
}


// ───── Main game function ─────────────────────────────────────────────────────
int main(void) {
    // Open VGA and Audio devices
    if ((vga_fd = open("/dev/vga_top", O_RDWR)) < 0) {
        perror("Failed to open /dev/vga_top");
        return -1;
    }
    if ((audio_fd = open("/dev/fpga_audio", O_RDWR)) < 0) {
        perror("Failed to open /dev/fpga_audio");
        close(vga_fd); // Close already opened vga_fd
        return -1;
    }

    // Create thread for controller input
    pthread_t controller_thread_id;
    if (pthread_create(&controller_thread_id, NULL, controller_input_thread, NULL) != 0) {
        perror("Failed to create controller thread");
        close(vga_fd);
        close(audio_fd);
        return -1;
    }

    // ── Start screen ──────────────────────────────────────────────────────────
    cleartiles(); clearSprites(); fill_sky_and_grass();
    write_text("scream", 6, 13, 13);
    write_text("jump",   4, 13, 20);
    write_text("press",  5, 19, 8);
    write_text("any",    3, 19, 14);
    write_text("key",    3, 19, 20);
    write_text("to",     2, 19, 26);
    write_text("start",  5, 19, 29);

    // Wait for any key press to start the game
    while (!(controller_state.a || controller_state.b || controller_state.start)) {
        usleep(10000); // Check every 10ms
    }

    // ── Init game ──────────────────────────────────────────────────────────────
    cleartiles(); clearSprites(); fill_sky_and_grass();
    srand(time(NULL)); // Seed random number generator

    int score = 0, lives = INITIAL_LIVES, level = 1;
    int jump_velocity    = INIT_JUMP_VY;
    int jump_pause_delay = BASE_DELAY;

    const int screen_tile_cols = LENGTH / TILE_SIZE;
    const int hud_center_col   = screen_tile_cols / 2;
    const int hud_offset       = 12; // Offset for HUD elements from center

    // NEW: Prepare two groups of bars
    MovingBar barsA[BAR_COUNT_PER_GROUP];
    MovingBar barsB[BAR_COUNT_PER_GROUP];
    static int spawnCounterA = 0; // Counter for respawning bars in group A
    static int spawnCounterB = 0; // Counter for respawning bars in group B

    // NEW: Define Y positions for the two groups of bars
    int y_pos_group_A = BAR_MIN_Y_GROUP_A;
    int y_pos_group_B = BAR_MIN_Y_GROUP_A + BAR_Y_OFFSET_GROUP_B;
    
    // Ensure group B bars are within valid Y range
    if (y_pos_group_B > BAR_MAX_Y) {
        y_pos_group_B = BAR_MAX_Y;
    }
    if (y_pos_group_B + BAR_HEIGHT_ROWS * TILE_SIZE > WIDTH - WALL) {
         y_pos_group_B = WIDTH - WALL - (BAR_HEIGHT_ROWS * TILE_SIZE);
    }


    // NEW: Initialize the two groups of bars with their respective Y positions and staggers
    // Group A starts with no horizontal stagger
    initBars(barsA, BAR_COUNT_PER_GROUP, y_pos_group_A, 0);
    // Group B starts lower and with a horizontal stagger
    initBars(barsB, BAR_COUNT_PER_GROUP, y_pos_group_B, BAR_INITIAL_X_STAGGER_GROUP_B);

    Chicken chicken;
    initChicken(&chicken); // Initialize chicken's state
    bool has_landed_this_jump = false; // Tracks if chicken has landed after a jump (for scoring)

    // ── Main game loop ────────────────────────────────────────────────────────
    while (lives > 0) {
        int current_bar_speed = BAR_SPEED_BASE + (level - 1); // Bar speed increases with level

        // Handle jump input
        if (controller_state.b && !chicken.jumping) {
            chicken.vy      = jump_velocity;
            chicken.jumping = true;
            has_landed_this_jump = false; // Reset landing flag on new jump
            towerEnabled    = false;      // Disable tower once chicken jumps off it
            play_sfx(0); // Play jump sound effect
        }

        // Prevent chicken from going too high (above screen top + small margin)
        if (chicken.y < WALL + 40 && chicken.jumping) { // Only apply ceiling if jumping
             chicken.y = WALL + 40;
             if (chicken.vy < 0) chicken.vy = 0; // Stop upward motion if hit ceiling
        }


        int prevY_chicken = chicken.y; // Store chicken's Y before moving
        moveChicken(&chicken);         // Update chicken's position

        // Handle collisions if chicken is falling
        if (chicken.vy > 0) {
            // Try collision with the first group of bars
            bool landed_on_A = handleBarCollision(
                barsA, BAR_COUNT_PER_GROUP,
                prevY_chicken, &chicken,
                &score, &has_landed_this_jump,
                jump_pause_delay
            );
            // If not landed on group A, try collision with the second group of bars
            if (!landed_on_A) {
                handleBarCollision(
                    barsB, BAR_COUNT_PER_GROUP,
                    prevY_chicken, &chicken,
                    &score, &has_landed_this_jump,
                    jump_pause_delay
                );
            }
        }

        // Check if chicken has fallen off the bottom of the screen
        if (chicken.y + CHICKEN_H > WIDTH - WALL) { // Check against bottom wall
            lives--;
            towerEnabled = true; // Re-enable tower for respawn
            initChicken(&chicken); // Reset chicken to starting position
            has_landed_this_jump = false;
            if (lives > 0) {
                play_sfx(1); // Play death/fall sound effect if not game over
                usleep(2000000); // Pause for 2 seconds after falling
            }
            continue; // Skip rest of the loop iteration
        }

        // Redraw background + HUD
        clearSprites(); 
        fill_sky_and_grass(); // Redraw static background elements

        // Display HUD: Lives, Score, Level
        // NOTE: If score shows as letters, 'write_number' implementation needs checking.
        write_text("lives", 5, 1, hud_center_col - hud_offset);
        write_number(lives, 1, hud_center_col - hud_offset + 6);
        write_text("score", 5, 1, hud_center_col - hud_offset + 12);
        write_number(score, 1, hud_center_col - hud_offset + 18);
        write_text("level", 5, 1, hud_center_col - hud_offset + 24);
        write_number(level, 1, hud_center_col - hud_offset + 30);

        // Update and draw both groups of bars
        // Pass 0 for initial_x_stagger_for_respawn as the main stagger is for initial setup.
        // Respawn will use the spawnCounter and spawnInterval for distribution.
        updateAndDrawBars(barsA, BAR_COUNT_PER_GROUP, current_bar_speed, &spawnCounterA, 0);
        // Optionally, group B bars could move at a slightly different speed for more variety
        updateAndDrawBars(barsB, BAR_COUNT_PER_GROUP, current_bar_speed, &spawnCounterB, 0); 


        // Draw the initial tower if it's enabled
        if (towerEnabled) {
            for (int r = TOWER_TOP_VISIBLE_ROW; r < TOWER_TOP_VISIBLE_ROW + 9; ++r) { // Draw a tower of 9 tiles high
                 if (r >= (WIDTH/TILE_SIZE) ) break; //  Ensure tower doesn't draw off screen
                for (int c = 0; c < 5; ++c) { // Tower is 5 tiles wide
                    write_tile_to_kernel(r, c, TOWER_TILE_IDX);
                }
            }
        }
        
        // Draw chicken sprite
        write_sprite_to_kernel(
            1, chicken.y, chicken.x,
            chicken.jumping ? CHICKEN_JUMP : CHICKEN_STAND,
            0 // Sprite priority/palette (adjust if needed)
        );
        update_sun(level); // Update and draw the sun

        usleep(16666); // Approximately 60 FPS (1,000,000 / 60)
    }

    // ── Game over ────────────────────────────────────────────────────────────
    play_sfx(2); // Play game over sound
    cleartiles(); clearSprites(); fill_sky_and_grass();
    write_text("gameover", 8, 12, (screen_tile_cols / 2) - 4); // Center "gameover"
    write_text("score", 5, 14, (screen_tile_cols/2) - 6);
    write_number(score, 14, (screen_tile_cols/2) );


    sleep(3); // Display game over message for 3 seconds

    // Clean up (though in embedded systems, main might not exit)
    // pthread_cancel(controller_thread_id); // Or use a flag to signal thread termination
    // pthread_join(controller_thread_id, NULL);
    close(vga_fd);
    close(audio_fd);
    return 0;
}

