#include "vga_top.h"
#include "vga_interface.h"
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h> // For bool type

// --- Software Double Buffering ---
// Two software buffers to represent the "front" (last presented) and "back" (current drawing) states.
static unsigned char display_buffer_A[TILE_ROWS][TILE_COLS];
static unsigned char display_buffer_B[TILE_ROWS][TILE_COLS];

// Pointers to designate which buffer is currently the back buffer (for drawing)
// and which is the front buffer (representing what's on screen).
static unsigned char (*current_back_buffer)[TILE_COLS] = display_buffer_A;
static unsigned char (*current_front_buffer)[TILE_COLS] = display_buffer_B;

// Shadow map for actual hardware writes. This tracks what's physically on the screen
// to avoid redundant ioctl calls when presenting the frame.
static unsigned char shadow_hardware_map[TILE_ROWS][TILE_COLS];
static bool vga_initialized = false;

// Internal function to write a single tile to the hardware.
// This is only called by vga_present_frame() if a tile in the
// back buffer differs from what's currently on the hardware (tracked by shadow_hardware_map).
static void vga_hardware_write_tile(unsigned char r, unsigned char c, unsigned char n) {
    // Boundary check for safety.
    if (r >= TILE_ROWS || c >= TILE_COLS) {
        // fprintf(stderr, "vga_hardware_write_tile: Coords out of bounds (r:%d, c:%d)\n", r, c);
        return;
    }
    // Only issue ioctl if the hardware state is different.
    if (shadow_hardware_map[r][c] == n) {
        return; 
    }

    vga_top_arg_t vla; // Structure for ioctl argument
    vla.r = r;
    vla.c = c;
    vla.n = n;
    if (ioctl(vga_fd, VGA_TOP_WRITE_TILE, &vla)) {
        perror("ioctl(VGA_TOP_WRITE_TILE) failed in vga_hardware_write_tile");
        return; // If ioctl fails, shadow map isn't updated, retry might happen.
    }
    shadow_hardware_map[r][c] = n; // Update hardware shadow map on successful write.
}

// Initializes the VGA interface, software buffers, and hardware shadow map.
void init_vga_interface(void) {
    if (vga_initialized) return; // Prevent re-initialization

    // Initialize both software buffers to a known state (e.g., BLANKTILE).
    // Initialize hardware shadow map to a state that forces the first full draw (e.g., 0xFF).
    for (int r = 0; r < TILE_ROWS; r++) {
        for (int c = 0; c < TILE_COLS; c++) {
            display_buffer_A[r][c] = BLANKTILE; 
            display_buffer_B[r][c] = BLANKTILE; 
            shadow_hardware_map[r][c] = 0xFF; // Different from any valid tile to ensure first hardware write.
        }
    }
    // Set initial roles for the buffers.
    current_back_buffer = display_buffer_A;
    current_front_buffer = display_buffer_B;

    // Perform an initial clear of the actual hardware screen to match the software buffers.
    for (int r = 0; r < TILE_ROWS; r++) {
        for (int c = 0; c < TILE_COLS; c++) {
            vga_hardware_write_tile(r, c, BLANKTILE);
        }
    }
    vga_initialized = true;
}

// MODIFIED: Writes tile data to the current software back_buffer.
// No direct hardware write happens here.
void write_tile_to_kernel(unsigned char r, unsigned char c, unsigned char n) {
  if (!vga_initialized) {
      // This is a safeguard. init_vga_interface() should always be called first from main.
      // fprintf(stderr, "Warning: VGA interface not initialized. Attempting to initialize now.\n");
      // init_vga_interface(); // This could be problematic if vga_fd isn't set yet.
      return; // Better to fail or log if not initialized properly.
  }
  // Boundary check.
  if (r >= TILE_ROWS || c >= TILE_COLS) {
    // fprintf(stderr, "write_tile_to_kernel (back_buffer): Coords out of bounds (r:%d, c:%d)\n", r, c);
    return;
  }
  current_back_buffer[r][c] = n;
}

// NEW: Presents the drawn frame (current_back_buffer) to the screen.
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

    // Swap the roles of the software buffers.
    // The current_back_buffer (which was just drawn to and presented) becomes the new current_front_buffer.
    // The old current_front_buffer becomes the new current_back_buffer, ready for the next frame's drawing.
    unsigned char (*temp_buffer)[TILE_COLS] = current_front_buffer;
    current_front_buffer = current_back_buffer;
    current_back_buffer = temp_buffer;

    // Optional: Clear the new back buffer immediately after swapping.
    // This ensures each frame starts drawing on a clean slate.
    // If not cleared, the new back buffer contains the previous frame's content,
    // which might be desired if only parts of the screen change.
    // For a full-screen update game like this, clearing is often preferred.
    // for (int r = 0; r < TILE_ROWS; r++) {
    //     for (int c = 0; c < TILE_COLS; c++) {
    //         current_back_buffer[r][c] = BLANKTILE; 
    //     }
    // }
}

// Sprites are still written directly to hardware.
// This is because sprites are often hardware overlays and might not be part of the tilemap memory.
// If sprite flickering is an issue, a similar buffering strategy for sprite states might be needed.
void write_sprite_to_kernel(unsigned char active, unsigned short r, unsigned short c, unsigned char n, unsigned short register_n) {
  vga_top_arg_s vla;
  vla.active = active;
  vla.r = r;
  vla.c = c;
  vla.n = n;
  vla.register_n = register_n;
  if (ioctl(vga_fd, VGA_TOP_WRITE_SPRITE, &vla)) {
    perror("ioctl(VGA_TOP_WRITE_SPRITE) failed");
    // No return here, as it's a common pattern to attempt all sprite writes.
  }
}

// MODIFIED: cleartiles now clears the current_back_buffer by writing BLANKTILE to it.
void cleartiles() {
  if (!vga_initialized) {
      // init_vga_interface(); // Safeguard, but should be called from main.
      return;
  }
  for(int i=0; i < TILE_ROWS; i++) {
     for(int j=0; j < TILE_COLS; j++) {
        current_back_buffer[i][j] = BLANKTILE; // Write to software back buffer
     }
  }
}

// Clears all hardware sprites.
void clearSprites(){
  for(int i = 0; i < 12; i++){ // Assuming 12 sprite registers based on typical usage.
    write_sprite_to_kernel(0, 0, 0, 0, i); // Deactivate sprite and move off-screen.
  }
}

// Writes a single digit number to the current_back_buffer.
void write_number(unsigned int num, unsigned int row, unsigned int col) {
  if (num > 9) num = 9; // Ensure it's a single digit.
  write_tile_to_kernel((unsigned char) row, (unsigned char) col, NUMBERTILE(num));
}

// Writes a single letter to the current_back_buffer.
void write_letter(unsigned char letter, unsigned int row, unsigned int col) {
  if (letter >= 'a' && letter <= 'z') {
    letter = letter - 'a'; // Convert 'a' to 0, 'b' to 1, etc. for LETTERTILE macro.
    write_tile_to_kernel(row, col, LETTERTILE(letter));
  } else {
    write_tile_to_kernel(row, col, BLANKTILE); // Default for non-lowercase letters.
  }
}

// Writes multiple numbers (a string of digits) to the current_back_buffer.
void write_numbers(unsigned int nums, unsigned int digits, unsigned int row, unsigned int col) {
  if ((col + digits) > TILE_COLS) { // Prevent writing out of bounds.
    // fprintf(stderr, "write_numbers: Number string out of bounds.\n");
    return;
  }
  if (digits == 0 ) digits = 1; // Default to 1 digit if 0 is passed.
  if (digits > 10) digits = 10; // Cap digits for practical limits of unsigned int.

  char temp_num_str[11]; // Max 10 digits + null terminator.
  sprintf(temp_num_str, "%0*u", digits, nums); // Format with leading zeros.

  for (unsigned int i = 0; i < digits; i++) {
      // Check bounds for each character, though initial check should cover it.
      if ((col + i) < TILE_COLS) { 
          write_number(temp_num_str[i] - '0', row, col + i);
      } else { 
          break; 
      }
  }
}

// Writes the score to the current_back_buffer.
void write_score(int new_score) {
  if (new_score < 0) new_score = 0; // Display non-negative scores.
  write_numbers((unsigned int) new_score, SCORE_MAX_LENGTH, SCORE_COORD_R, SCORE_COORD_C);
}

// Writes a text string to the current_back_buffer.
void write_text(unsigned char *text, unsigned int length, unsigned int row, unsigned int col) {
  if (!text) return;
  unsigned int write_len = length;
  // Truncate if the string would go out of bounds.
  if ((col + length) > TILE_COLS) {
    write_len = TILE_COLS - col; 
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

// MODIFIED: Fills the sky and grass in the current_back_buffer.
void fill_sky_and_grass(void) {
    if (!vga_initialized) {
        // init_vga_interface(); // Safeguard
        return;
    }
    int r, c;
    // Draw sky tiles to the back buffer.
    for (r = 0; r < GRASS_ROW_START; ++r) { 
        for (c = 0; c < TILE_COLS; ++c) {
            write_tile_to_kernel(r, c, SKY_TILE_IDX); 
        }
    }
    // Draw grass tiles to the back buffer.
    for (r = GRASS_ROW_START; r < TILE_ROWS; ++r) { 
        for (c = 0; c < TILE_COLS; ++c) {
            write_tile_to_kernel(r, c, GRASS_TILE_IDX); 
        }
    }
}
