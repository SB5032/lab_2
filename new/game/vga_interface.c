#include "vga_top.h"      // For VGA hardware interaction structures and ioctl commands
#include "vga_interface.h" // For function declarations and shared definitions
#include <stdio.h>         // For perror, sprintf (used in write_numbers)
#include <sys/ioctl.h>     // For ioctl
#include <sys/types.h>     // Standard types
#include <sys/stat.h>      // Standard types
#include <fcntl.h>         // For O_RDWR (used in main to open vga_fd)
#include <string.h>        // For memset (can be used for buffer initialization)
#include <unistd.h>        // For usleep
#include <stdlib.h>        // For rand()
#include <stdbool.h>       // For bool, true, false

// --- Software Double Buffering for Tiles ---
// Two software buffers to represent the "front" (last presented) and "back" (current drawing) states for tiles.
static unsigned char display_buffer_A[TILE_ROWS][TILE_COLS];
static unsigned char display_buffer_B[TILE_ROWS][TILE_COLS];

// Pointers to designate which tile buffer is currently the back buffer (for drawing)
// and which is the front buffer (representing what's conceptually on screen).
static unsigned char (*current_back_buffer)[TILE_COLS] = display_buffer_A;
static unsigned char (*current_front_buffer)[TILE_COLS] = display_buffer_B;

// Shadow map for actual hardware tile writes. This tracks what's physically on the screen's tilemap
// to avoid redundant ioctl calls when presenting the tile frame.
static unsigned char shadow_hardware_map[TILE_ROWS][TILE_COLS];
static bool vga_initialized = false; // Flag to ensure initialization happens once.

// --- Sprite State Buffering ---
// desired_sprite_states: Stores the state (active, position, tile) sprites SHOULD have at the end of the current game logic update.
static SpriteHWState desired_sprite_states[MAX_HARDWARE_SPRITES];
// actual_hw_sprites: Stores the state that was last written to the actual hardware sprite registers.
static SpriteHWState actual_hw_sprites[MAX_HARDWARE_SPRITES];

// Internal function to write a single tile to the hardware.
// This is only called by vga_present_frame() if a tile in the
// back buffer differs from what's currently on the hardware (tracked by shadow_hardware_map).
static void vga_hardware_write_tile(unsigned char r, unsigned char c, unsigned char n) {
    if (r >= TILE_ROWS || c >= TILE_COLS) { // Boundary check
        // fprintf(stderr, "vga_hardware_write_tile: Coords out of bounds (r:%u, c:%u)\n", r, c);
        return;
    }
    // Only issue ioctl if the hardware state needs to change.
    if (shadow_hardware_map[r][c] == n) {
        return; 
    }

    vga_top_arg_t vla; // Structure for ioctl argument
    vla.r = r;
    vla.c = c;
    vla.n = n;
    if (ioctl(vga_fd, VGA_TOP_WRITE_TILE, &vla)) {
        perror("ioctl(VGA_TOP_WRITE_TILE) failed in vga_hardware_write_tile");
        return; // If ioctl fails, shadow map isn't updated, retry might happen next frame.
    }
    shadow_hardware_map[r][c] = n; // Update hardware shadow map on successful write.
}

// Internal function to write a single sprite's state to the hardware.
// This is only called by present_sprites() if a desired sprite state
// differs from what's currently configured in the hardware.
static void vga_hardware_write_sprite(unsigned char active, unsigned short r, unsigned short c, unsigned char n, unsigned short register_n) {
  if (register_n >= MAX_HARDWARE_SPRITES) { // Boundary check
      // fprintf(stderr, "vga_hardware_write_sprite: Register %u out of bounds.\n", register_n);
      return;
  }
  
  vga_top_arg_s vla; // Structure for ioctl argument
  vla.active = active; 
  vla.r = r; 
  vla.c = c; 
  vla.n = n; 
  vla.register_n = register_n;
  if (ioctl(vga_fd, VGA_TOP_WRITE_SPRITE, &vla)) {
    // perror("ioctl(VGA_TOP_WRITE_SPRITE) failed in vga_hardware_write_sprite"); 
    // This can be noisy if errors are frequent; enable for specific sprite debugging.
  }
}

// Initializes the VGA interface: tile buffers, sprite buffers, and clears the hardware screen/sprites.
void init_vga_interface(void) {
    if (vga_initialized) return; // Prevent re-initialization

    // Initialize tile buffers and hardware shadow map
    for (int r = 0; r < TILE_ROWS; r++) {
        for (int c = 0; c < TILE_COLS; c++) {
            display_buffer_A[r][c] = BLANKTILE; 
            display_buffer_B[r][c] = BLANKTILE; 
            shadow_hardware_map[r][c] = 0xFF; // Different from any valid tile to ensure first hardware write.
        }
    }
    current_back_buffer = display_buffer_A;  // Arbitrary start for back buffer
    current_front_buffer = display_buffer_B; // Arbitrary start for front buffer

    // Perform an initial clear of the actual hardware tilemap to match the software buffers.
    for (int r = 0; r < TILE_ROWS; r++) {
        for (int c = 0; c < TILE_COLS; c++) {
            vga_hardware_write_tile(r, c, BLANKTILE);
        }
    }

    // Initialize sprite state buffers and clear hardware sprites
    for (int i = 0; i < MAX_HARDWARE_SPRITES; i++) {
        // Set desired and actual states to inactive initially.
        desired_sprite_states[i] = (SpriteHWState){.active = false, .r = 0, .c = 0, .n = 0};
        actual_hw_sprites[i]   = (SpriteHWState){.active = false, .r = 0, .c = 0, .n = 0};
        // Explicitly turn off each hardware sprite.
        vga_hardware_write_sprite(0, 0, 0, 0, i); 
    }
    vga_initialized = true;
}

// Writes tile data to the current software back_buffer for tiles.
// No direct hardware write happens here.
void write_tile_to_kernel(unsigned char r, unsigned char c, unsigned char n) {
  if (!vga_initialized) {
      // fprintf(stderr, "Warning: VGA interface not initialized when calling write_tile_to_kernel.\n");
      return; 
  }
  if (r >= TILE_ROWS || c >= TILE_COLS) { // Boundary check
    // fprintf(stderr, "write_tile_to_kernel (back_buffer): Coords out of bounds (r:%u, c:%u)\n", r, c);
    return;
  }
  current_back_buffer[r][c] = n;
}

// Presents the drawn tile frame (current_back_buffer) to the screen.
// It compares the back buffer with the hardware shadow map and only writes differences.
void vga_present_frame(void) {
    if (!vga_initialized) return;

    for (int r = 0; r < TILE_ROWS; r++) {
        for (int c = 0; c < TILE_COLS; c++) {
            // If the tile in the back buffer is different from what's on hardware, update hardware.
            if (current_back_buffer[r][c] != shadow_hardware_map[r][c]) {
                 vga_hardware_write_tile(r, c, current_back_buffer[r][c]);
            }
        }
    }

    // Swap the roles of the software tile buffers.
    unsigned char (*temp_buffer)[TILE_COLS] = current_front_buffer;
    current_front_buffer = current_back_buffer;
    current_back_buffer = temp_buffer;
    // The new back_buffer (which was the old front_buffer) now contains the previously presented frame.
    // Game logic should draw the *next* frame into this new back_buffer.
}

// Updates the desired state for a specific sprite in the software buffer.
// No direct hardware write happens here.
void write_sprite_to_kernel_buffered(unsigned char active, unsigned short r, unsigned short c, unsigned char n, unsigned short register_n) {
    if (!vga_initialized || register_n >= MAX_HARDWARE_SPRITES) {
        // fprintf(stderr, "write_sprite_to_kernel_buffered: Invalid register %u or not initialized.\n", register_n);
        return;
    }
    desired_sprite_states[register_n].active = active;
    desired_sprite_states[register_n].r = r;
    desired_sprite_states[register_n].c = c;
    desired_sprite_states[register_n].n = n;
}

// Compares desired sprite states with actual hardware states and updates hardware only for differences.
// This should be called once per game loop, after all sprite states for the frame have been set.
void present_sprites(void) {
    if (!vga_initialized) return;
    for (int i = 0; i < MAX_HARDWARE_SPRITES; i++) {
        // Check if the desired state differs from the last known hardware state.
        if (desired_sprite_states[i].active != actual_hw_sprites[i].active ||
            (desired_sprite_states[i].active && // Only compare position/tile if the sprite is active
             (desired_sprite_states[i].r != actual_hw_sprites[i].r ||
              desired_sprite_states[i].c != actual_hw_sprites[i].c ||
              desired_sprite_states[i].n != actual_hw_sprites[i].n))) {
            
            // State has changed, so update the hardware.
            vga_hardware_write_sprite(
                desired_sprite_states[i].active,
                desired_sprite_states[i].r,
                desired_sprite_states[i].c,
                desired_sprite_states[i].n,
                i // register_n is the loop index
            );
            // Update our record of the actual hardware state.
            actual_hw_sprites[i] = desired_sprite_states[i]; 
        }
    }
}

// Clears the current software back_buffer for tiles by writing BLANKTILE to it.
void cleartiles() {
  if (!vga_initialized) return;
  for(int i=0; i < TILE_ROWS; i++) {
     for(int j=0; j < TILE_COLS; j++) {
        current_back_buffer[i][j] = BLANKTILE; 
     }
  }
}

// Sets all desired sprite states to inactive in the software buffer.
// The actual hardware sprites will be turned off when present_sprites() is called.
void clearSprites_buffered(){
  if (!vga_initialized) return;
  for(int i = 0; i < MAX_HARDWARE_SPRITES; i++){ 
    desired_sprite_states[i].active = false;
    // Optionally, set r, c, n to 0 for desired_sprite_states as well,
    // but active=false is the key for turning them off via present_sprites.
    // desired_sprite_states[i].r = 0;
    // desired_sprite_states[i].c = 0;
    // desired_sprite_states[i].n = 0;
  }
}

// Writes a single digit number to the current_back_buffer for tiles.
void write_number(unsigned int num, unsigned int row, unsigned int col) {
  if (num > 9) num = 9; // Ensure it's a single digit.
  write_tile_to_kernel((unsigned char) row, (unsigned char) col, NUMBERTILE(num));
}

// Writes a single letter to the current_back_buffer for tiles.
void write_letter(unsigned char letter, unsigned int row, unsigned int col) {
  if (letter >= 'a' && letter <= 'z') {
    letter = letter - 'a'; // Convert 'a' to 0, 'b' to 1, etc. for LETTERTILE macro.
    write_tile_to_kernel(row, col, LETTERTILE(letter));
  } else {
    write_tile_to_kernel(row, col, BLANKTILE); // Default for non-lowercase or invalid letters.
  }
}

// Writes multiple numbers (a string of digits) to the current_back_buffer for tiles.
void write_numbers(unsigned int nums, unsigned int digits, unsigned int row, unsigned int col) {
  if ((col + digits) > TILE_COLS) { // Prevent writing out of bounds.
    // fprintf(stderr, "write_numbers: Number string out of column bounds.\n");
    return;
  }
  if (digits == 0 ) digits = 1; // Default to 1 digit if 0 is passed.
  if (digits > 10) digits = 10; // Cap digits for practical limits of unsigned int.

  char temp_num_str[11]; // Max 10 digits + null terminator.
  sprintf(temp_num_str, "%0*u", digits, nums); // Format with leading zeros.

  for (unsigned int i = 0; i < digits; i++) {
      if ((col + i) < TILE_COLS) { // Ensure writing within bounds for each digit.
          write_number(temp_num_str[i] - '0', row, col + i);
      } else { 
          break; // Should not happen if initial check is correct.
      }
  }
}

// Writes the score to the current_back_buffer for tiles.
void write_score(int new_score) {
  if (new_score < 0) new_score = 0; // Display non-negative scores.
  write_numbers((unsigned int) new_score, SCORE_MAX_LENGTH, SCORE_COORD_R, SCORE_COORD_C);
}

// Writes a text string to the current_back_buffer for tiles.
void write_text(unsigned char *text, unsigned int length, unsigned int row, unsigned int col) {
  if (!text) return;
  unsigned int write_len = length;
  // Truncate if the string would go out of bounds.
  if ((col + length) > TILE_COLS) {
    write_len = TILE_COLS - col; 
    // fprintf(stderr, "write_text: Text truncated to fit screen.\n");
  }
  
  for (unsigned int i = 0; i < write_len; i++) {
    // Final check for each character, though truncation should handle it.
    if ((col + i) < TILE_COLS) {
        write_letter(*(text + i), row, col + i);
    } else { 
        break; 
    }
  }
}

// Fills the sky and randomly distributes three types of grass tiles in the current_back_buffer.
void fill_sky_and_grass(void) {
    if (!vga_initialized) {
        // This should ideally not be hit if init_vga_interface is called at the start of main.
        // For robustness, one might call init_vga_interface() here, but it's better if main handles it.
        // If vga_fd is not open yet, init_vga_interface might fail or behave unexpectedly.
        // fprintf(stderr, "Warning: fill_sky_and_grass called before VGA interface initialized.\n");
        return; 
    }
    int r, c;
    unsigned char grass_tile_to_use;

    // Draw sky tiles to the back buffer.
    for (r = 0; r < GRASS_ROW_START; ++r) { 
        for (c = 0; c < TILE_COLS; ++c) {
            write_tile_to_kernel(r, c, SKY_TILE_IDX); 
        }
    }
    // Draw grass tiles randomly to the back buffer.
    for (r = GRASS_ROW_START; r < TILE_ROWS; ++r) { 
        for (c = 0; c < TILE_COLS; ++c) {
            int rand_choice = rand() % 3; // Generates 0, 1, or 2
            switch (rand_choice) {
                case 0:
                    grass_tile_to_use = GRASS_TILE_1_IDX;
                    break;
                case 1:
                    grass_tile_to_use = GRASS_TILE_2_IDX;
                    break;
                case 2:
                default: // Fallback, though rand()%3 should only give 0,1,2
                    grass_tile_to_use = GRASS_TILE_3_IDX;
                    break;
            }
            write_tile_to_kernel(r, c, grass_tile_to_use); 
        }
    }
}
#include "vga_top.h"      // For VGA hardware interaction structures and ioctl commands
#include "vga_interface.h" // For function declarations and shared definitions
#include <stdio.h>         // For perror, sprintf (used in write_numbers)
#include <sys/ioctl.h>     // For ioctl
#include <sys/types.h>     // Standard types
#include <sys/stat.h>      // Standard types
#include <fcntl.h>         // For O_RDWR (used in main to open vga_fd)
#include <string.h>        // For memset (can be used for buffer initialization)
#include <unistd.h>        // For usleep
#include <stdlib.h>        // For rand()
#include <stdbool.h>       // For bool, true, false

// --- Software Double Buffering for Tiles ---
// Two software buffers to represent the "front" (last presented) and "back" (current drawing) states for tiles.
static unsigned char display_buffer_A[TILE_ROWS][TILE_COLS];
static unsigned char display_buffer_B[TILE_ROWS][TILE_COLS];

// Pointers to designate which tile buffer is currently the back buffer (for drawing)
// and which is the front buffer (representing what's conceptually on screen).
static unsigned char (*current_back_buffer)[TILE_COLS] = display_buffer_A;
static unsigned char (*current_front_buffer)[TILE_COLS] = display_buffer_B;

// Shadow map for actual hardware tile writes. This tracks what's physically on the screen's tilemap
// to avoid redundant ioctl calls when presenting the tile frame.
static unsigned char shadow_hardware_map[TILE_ROWS][TILE_COLS];
static bool vga_initialized = false; // Flag to ensure initialization happens once.

// --- Sprite State Buffering ---
// desired_sprite_states: Stores the state (active, position, tile) sprites SHOULD have at the end of the current game logic update.
static SpriteHWState desired_sprite_states[MAX_HARDWARE_SPRITES];
// actual_hw_sprites: Stores the state that was last written to the actual hardware sprite registers.
static SpriteHWState actual_hw_sprites[MAX_HARDWARE_SPRITES];

// Internal function to write a single tile to the hardware.
// This is only called by vga_present_frame() if a tile in the
// back buffer differs from what's currently on the hardware (tracked by shadow_hardware_map).
static void vga_hardware_write_tile(unsigned char r, unsigned char c, unsigned char n) {
    if (r >= TILE_ROWS || c >= TILE_COLS) { // Boundary check
        // fprintf(stderr, "vga_hardware_write_tile: Coords out of bounds (r:%u, c:%u)\n", r, c);
        return;
    }
    // Only issue ioctl if the hardware state needs to change.
    if (shadow_hardware_map[r][c] == n) {
        return; 
    }

    vga_top_arg_t vla; // Structure for ioctl argument
    vla.r = r;
    vla.c = c;
    vla.n = n;
    if (ioctl(vga_fd, VGA_TOP_WRITE_TILE, &vla)) {
        perror("ioctl(VGA_TOP_WRITE_TILE) failed in vga_hardware_write_tile");
        return; // If ioctl fails, shadow map isn't updated, retry might happen next frame.
    }
    shadow_hardware_map[r][c] = n; // Update hardware shadow map on successful write.
}

// Internal function to write a single sprite's state to the hardware.
// This is only called by present_sprites() if a desired sprite state
// differs from what's currently configured in the hardware.
static void vga_hardware_write_sprite(unsigned char active, unsigned short r, unsigned short c, unsigned char n, unsigned short register_n) {
  if (register_n >= MAX_HARDWARE_SPRITES) { // Boundary check
      // fprintf(stderr, "vga_hardware_write_sprite: Register %u out of bounds.\n", register_n);
      return;
  }
  
  vga_top_arg_s vla; // Structure for ioctl argument
  vla.active = active; 
  vla.r = r; 
  vla.c = c; 
  vla.n = n; 
  vla.register_n = register_n;
  if (ioctl(vga_fd, VGA_TOP_WRITE_SPRITE, &vla)) {
    // perror("ioctl(VGA_TOP_WRITE_SPRITE) failed in vga_hardware_write_sprite"); 
    // This can be noisy if errors are frequent; enable for specific sprite debugging.
  }
}

// Initializes the VGA interface: tile buffers, sprite buffers, and clears the hardware screen/sprites.
void init_vga_interface(void) {
    if (vga_initialized) return; // Prevent re-initialization

    // Initialize tile buffers and hardware shadow map
    for (int r = 0; r < TILE_ROWS; r++) {
        for (int c = 0; c < TILE_COLS; c++) {
            display_buffer_A[r][c] = BLANKTILE; 
            display_buffer_B[r][c] = BLANKTILE; 
            shadow_hardware_map[r][c] = 0xFF; // Different from any valid tile to ensure first hardware write.
        }
    }
    current_back_buffer = display_buffer_A;  // Arbitrary start for back buffer
    current_front_buffer = display_buffer_B; // Arbitrary start for front buffer

    // Perform an initial clear of the actual hardware tilemap to match the software buffers.
    for (int r = 0; r < TILE_ROWS; r++) {
        for (int c = 0; c < TILE_COLS; c++) {
            vga_hardware_write_tile(r, c, BLANKTILE);
        }
    }

    // Initialize sprite state buffers and clear hardware sprites
    for (int i = 0; i < MAX_HARDWARE_SPRITES; i++) {
        // Set desired and actual states to inactive initially.
        desired_sprite_states[i] = (SpriteHWState){.active = false, .r = 0, .c = 0, .n = 0};
        actual_hw_sprites[i]   = (SpriteHWState){.active = false, .r = 0, .c = 0, .n = 0};
        // Explicitly turn off each hardware sprite.
        vga_hardware_write_sprite(0, 0, 0, 0, i); 
    }
    vga_initialized = true;
}

// Writes tile data to the current software back_buffer for tiles.
// No direct hardware write happens here.
void write_tile_to_kernel(unsigned char r, unsigned char c, unsigned char n) {
  if (!vga_initialized) {
      // fprintf(stderr, "Warning: VGA interface not initialized when calling write_tile_to_kernel.\n");
      return; 
  }
  if (r >= TILE_ROWS || c >= TILE_COLS) { // Boundary check
    // fprintf(stderr, "write_tile_to_kernel (back_buffer): Coords out of bounds (r:%u, c:%u)\n", r, c);
    return;
  }
  current_back_buffer[r][c] = n;
}

// Presents the drawn tile frame (current_back_buffer) to the screen.
// It compares the back buffer with the hardware shadow map and only writes differences.
void vga_present_frame(void) {
    if (!vga_initialized) return;

    for (int r = 0; r < TILE_ROWS; r++) {
        for (int c = 0; c < TILE_COLS; c++) {
            // If the tile in the back buffer is different from what's on hardware, update hardware.
            if (current_back_buffer[r][c] != shadow_hardware_map[r][c]) {
                 vga_hardware_write_tile(r, c, current_back_buffer[r][c]);
            }
        }
    }

    // Swap the roles of the software tile buffers.
    unsigned char (*temp_buffer)[TILE_COLS] = current_front_buffer;
    current_front_buffer = current_back_buffer;
    current_back_buffer = temp_buffer;
    // The new back_buffer (which was the old front_buffer) now contains the previously presented frame.
    // Game logic should draw the *next* frame into this new back_buffer.
}

// Updates the desired state for a specific sprite in the software buffer.
// No direct hardware write happens here.
void write_sprite_to_kernel_buffered(unsigned char active, unsigned short r, unsigned short c, unsigned char n, unsigned short register_n) {
    if (!vga_initialized || register_n >= MAX_HARDWARE_SPRITES) {
        // fprintf(stderr, "write_sprite_to_kernel_buffered: Invalid register %u or not initialized.\n", register_n);
        return;
    }
    desired_sprite_states[register_n].active = active;
    desired_sprite_states[register_n].r = r;
    desired_sprite_states[register_n].c = c;
    desired_sprite_states[register_n].n = n;
}

// Compares desired sprite states with actual hardware states and updates hardware only for differences.
// This should be called once per game loop, after all sprite states for the frame have been set.
void present_sprites(void) {
    if (!vga_initialized) return;
    for (int i = 0; i < MAX_HARDWARE_SPRITES; i++) {
        // Check if the desired state differs from the last known hardware state.
        if (desired_sprite_states[i].active != actual_hw_sprites[i].active ||
            (desired_sprite_states[i].active && // Only compare position/tile if the sprite is active
             (desired_sprite_states[i].r != actual_hw_sprites[i].r ||
              desired_sprite_states[i].c != actual_hw_sprites[i].c ||
              desired_sprite_states[i].n != actual_hw_sprites[i].n))) {
            
            // State has changed, so update the hardware.
            vga_hardware_write_sprite(
                desired_sprite_states[i].active,
                desired_sprite_states[i].r,
                desired_sprite_states[i].c,
                desired_sprite_states[i].n,
                i // register_n is the loop index
            );
            // Update our record of the actual hardware state.
            actual_hw_sprites[i] = desired_sprite_states[i]; 
        }
    }
}

// Clears the current software back_buffer for tiles by writing BLANKTILE to it.
void cleartiles() {
  if (!vga_initialized) return;
  for(int i=0; i < TILE_ROWS; i++) {
     for(int j=0; j < TILE_COLS; j++) {
        current_back_buffer[i][j] = BLANKTILE; 
     }
  }
}

// Sets all desired sprite states to inactive in the software buffer.
// The actual hardware sprites will be turned off when present_sprites() is called.
void clearSprites_buffered(){
  if (!vga_initialized) return;
  for(int i = 0; i < MAX_HARDWARE_SPRITES; i++){ 
    desired_sprite_states[i].active = false;
    // Optionally, set r, c, n to 0 for desired_sprite_states as well,
    // but active=false is the key for turning them off via present_sprites.
    // desired_sprite_states[i].r = 0;
    // desired_sprite_states[i].c = 0;
    // desired_sprite_states[i].n = 0;
  }
}

// Writes a single digit number to the current_back_buffer for tiles.
void write_number(unsigned int num, unsigned int row, unsigned int col) {
  if (num > 9) num = 9; // Ensure it's a single digit.
  write_tile_to_kernel((unsigned char) row, (unsigned char) col, NUMBERTILE(num));
}

// Writes a single letter to the current_back_buffer for tiles.
void write_letter(unsigned char letter, unsigned int row, unsigned int col) {
  if (letter >= 'a' && letter <= 'z') {
    letter = letter - 'a'; // Convert 'a' to 0, 'b' to 1, etc. for LETTERTILE macro.
    write_tile_to_kernel(row, col, LETTERTILE(letter));
  } else {
    write_tile_to_kernel(row, col, BLANKTILE); // Default for non-lowercase or invalid letters.
  }
}

// Writes multiple numbers (a string of digits) to the current_back_buffer for tiles.
void write_numbers(unsigned int nums, unsigned int digits, unsigned int row, unsigned int col) {
  if ((col + digits) > TILE_COLS) { // Prevent writing out of bounds.
    // fprintf(stderr, "write_numbers: Number string out of column bounds.\n");
    return;
  }
  if (digits == 0 ) digits = 1; // Default to 1 digit if 0 is passed.
  if (digits > 10) digits = 10; // Cap digits for practical limits of unsigned int.

  char temp_num_str[11]; // Max 10 digits + null terminator.
  sprintf(temp_num_str, "%0*u", digits, nums); // Format with leading zeros.

  for (unsigned int i = 0; i < digits; i++) {
      if ((col + i) < TILE_COLS) { // Ensure writing within bounds for each digit.
          write_number(temp_num_str[i] - '0', row, col + i);
      } else { 
          break; // Should not happen if initial check is correct.
      }
  }
}

// Writes the score to the current_back_buffer for tiles.
void write_score(int new_score) {
  if (new_score < 0) new_score = 0; // Display non-negative scores.
  write_numbers((unsigned int) new_score, SCORE_MAX_LENGTH, SCORE_COORD_R, SCORE_COORD_C);
}

// Writes a text string to the current_back_buffer for tiles.
void write_text(unsigned char *text, unsigned int length, unsigned int row, unsigned int col) {
  if (!text) return;
  unsigned int write_len = length;
  // Truncate if the string would go out of bounds.
  if ((col + length) > TILE_COLS) {
    write_len = TILE_COLS - col; 
    // fprintf(stderr, "write_text: Text truncated to fit screen.\n");
  }
  
  for (unsigned int i = 0; i < write_len; i++) {
    // Final check for each character, though truncation should handle it.
    if ((col + i) < TILE_COLS) {
        write_letter(*(text + i), row, col + i);
    } else { 
        break; 
    }
  }
}

// Fills the sky and randomly distributes three types of grass tiles in the current_back_buffer.
void fill_sky_and_grass(void) {
    if (!vga_initialized) {
        // This should ideally not be hit if init_vga_interface is called at the start of main.
        // For robustness, one might call init_vga_interface() here, but it's better if main handles it.
        // If vga_fd is not open yet, init_vga_interface might fail or behave unexpectedly.
        // fprintf(stderr, "Warning: fill_sky_and_grass called before VGA interface initialized.\n");
        return; 
    }
    int r, c;
    unsigned char grass_tile_to_use;

    // Draw sky tiles to the back buffer.
    for (r = 0; r < GRASS_ROW_START; ++r) { 
        for (c = 0; c < TILE_COLS; ++c) {
            write_tile_to_kernel(r, c, SKY_TILE_IDX); 
        }
    }
    // Draw grass tiles randomly to the back buffer.
    for (r = GRASS_ROW_START; r < TILE_ROWS; ++r) { 
        for (c = 0; c < TILE_COLS; ++c) {
            int rand_choice = rand() % 3; // Generates 0, 1, or 2
            switch (rand_choice) {
                case 0:
                    grass_tile_to_use = GRASS_TILE_1_IDX;
                    break;
                case 1:
                    grass_tile_to_use = GRASS_TILE_2_IDX;
                    break;
                case 2:
                default: // Fallback, though rand()%3 should only give 0,1,2
                    grass_tile_to_use = GRASS_TILE_3_IDX;
                    break;
            }
            write_tile_to_kernel(r, c, grass_tile_to_use); 
        }
    }
}
