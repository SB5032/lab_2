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

// Shadow tilemap to keep track of the current state of tiles on screen
// This helps in reducing redundant ioctl calls.
static unsigned char shadow_tilemap[TILE_ROWS][TILE_COLS];
// Flag to ensure shadow map is initialized
static bool shadow_map_initialized = false;

// NEW: Initialization function for the VGA interface and shadow tilemap
void init_vga_interface(void) {
    // Initialize the shadow tilemap with a value that forces the first draw
    // or with BLANKTILE if you want to start with a cleared concept.
    // Using a distinct initial value helps ensure the first full draw occurs.
    for (int r = 0; r < TILE_ROWS; r++) {
        for (int c = 0; c < TILE_COLS; c++) {
            shadow_tilemap[r][c] = 0xFF; // A value different from any valid tile index
                                         // to force the first actual write.
                                         // Or use BLANKTILE if you prefer.
        }
    }
    shadow_map_initialized = true;
    // Optionally, you could call cleartiles() here to ensure the actual screen
    // matches the shadow map's initial blank state if shadow_tilemap is init with BLANKTILE.
    // cleartiles(); // If shadow_tilemap is initialized to BLANKTILE
}


// r*c: 40*30       r:0-29      c:0-39
// n means image number, number of the image stored in memory
void write_tile_to_kernel(unsigned char r, unsigned char c, unsigned char n) 
{
  // Boundary checks
  if (r >= TILE_ROWS || c >= TILE_COLS) {
    // fprintf(stderr, "write_tile_to_kernel: Coords out of bounds (r:%d, c:%d)\n", r, c);
    return;
  }

  // If shadow map isn't initialized, force write (should not happen if init_vga_interface is called)
  if (!shadow_map_initialized) {
      // Fallback: Directly write, but this indicates an issue with initialization flow.
      // Consider initializing it here as a safety, though it's better to ensure init_vga_interface is called first.
      // init_vga_interface(); // Or at least initialize the specific shadow tile
      // shadow_tilemap[r][c] = 0xFF; // Force this write
  }


  // MODIFIED: Only write if the new tile is different from the shadow map
  if (shadow_tilemap[r][c] == n) {
    return; // Tile is already what we want, skip ioctl
  }

  vga_top_arg_t vla;
  vla.r = r;
  vla.c = c;
  vla.n = n;
  if (ioctl(vga_fd, VGA_TOP_WRITE_TILE, &vla)) {
    perror("ioctl(VGA_TOP_WRITE_TILE) failed");
    // If ioctl fails, we don't update the shadow map, so a retry might happen next frame.
    return;
  }
  // Update shadow map on successful write
  shadow_tilemap[r][c] = n;
}

// sprite r and c is pixel, r range is 0 - 639, c range is 0-479
void write_sprite_to_kernel(unsigned char active,   //active == 1, display, active == 0 not display
                            unsigned short r,
                            unsigned short c,
                            unsigned char n,
                            unsigned short register_n) 
{
  vga_top_arg_s vla;
  vla.active = active;
  vla.r = r;
  vla.c = c;
  vla.n = n;
  vla.register_n = register_n;
  if (ioctl(vga_fd, VGA_TOP_WRITE_SPRITE, &vla)) {
    perror("ioctl(VGA_TOP_WRITE_SPRITE) failed");
    return;
  }
}


// tile operations: r*c: 40*30       r:0-29      c:0-39

// for when needing to reset the background to empty
void cleartiles()
{
  if (!shadow_map_initialized) {
    // This is a good place to ensure it's initialized if somehow missed,
    // as cleartiles implies a fresh state.
    init_vga_interface();
  }
  int i, j;
  for(i=0; i < TILE_ROWS; i++)
  {
     for(j=0; j < TILE_COLS; j++)
     {
        // This will now use the optimized write_tile_to_kernel
        write_tile_to_kernel(i, j, BLANKTILE);
     }
  }
}

void clearSprites(){
  for(int i = 0; i <12; i++){ // Assuming 12 sprite registers based on previous context
    write_sprite_to_kernel(0, 0, 0, 0, i);
  }
}

// write a single digit number at a specific tile
void write_number(unsigned int num, unsigned int row, unsigned int col)
{
  if (num > 9) num = 9; // Ensure it's a single digit for NUMBERTILE mapping
  write_tile_to_kernel((unsigned char) row, (unsigned char) col, NUMBERTILE(num));
}

// write a letter at a specific tile, only works with lower case
void write_letter(unsigned char letter, unsigned int row, unsigned int col)
{
  if (letter >= 'a' && letter <= 'z') {
    letter = letter - 'a'; // convert 'a' to 0, 'b' to 1, etc.
    write_tile_to_kernel(row, col, LETTERTILE(letter));
  } else {
    // Handle non-lowercase letters if necessary, e.g., write a blank or a default char
    write_tile_to_kernel(row, col, BLANKTILE); 
  }
}

// if digits longer than actual number, the front will be padded with zero
void write_numbers(unsigned int nums, unsigned int digits, unsigned int row, unsigned int col)
{
  if ((col + digits) > TILE_COLS) {
    // printf("number too long or out of bounds!\n");
    return;
  }
  // Ensure digits is not excessively large to prevent issues with temp_num
  if (digits == 0 || digits > 10) digits = 1; // Max 10 digits for unsigned int, default to 1 if 0

  char temp_num_str[11]; // Max 10 digits + null terminator
  sprintf(temp_num_str, "%0*u", digits, nums); // Format with leading zeros

  for (unsigned int i = 0; i < digits; i++) {
      if ((col + i) < TILE_COLS) { // Ensure writing within bounds
          write_number(temp_num_str[i] - '0', row, col + i);
      } else {
          break; // Stop if we go out of column bounds
      }
  }
}


// update displayed score at a pre-determined location, max digit length of 4
// only work with positive integers
void write_score(int new_score)
{
  if (new_score < 0) new_score = 0; // Handle negative scores if necessary
  write_numbers((unsigned int) new_score, SCORE_MAX_LENGTH, SCORE_COORD_R, SCORE_COORD_C);
}


// write a string at a specific tile, only works with lower case letters, make sure to keep the string short
void write_text(unsigned char *text, unsigned int length, unsigned int row, unsigned int col)
{
  if (!text) return;
  if ((col + length) > TILE_COLS) {
    // printf("string too long or out of bounds!\n");
    // Optionally truncate or skip drawing
    length = TILE_COLS - col; // Truncate if it goes out of bounds
  }
  
  for (unsigned int i = 0; i < length; i++) {
    if ((col + i) < TILE_COLS) { // Double check bounds per character
        write_letter(*(text + i), row, col + i);
    } else {
        break;
    }
  }
}

// NEW: Implementation of fill_sky_and_grass
// This function will now benefit from the shadow tilemap optimization
// if called repeatedly with the same sky/grass pattern.
void fill_sky_and_grass(void) {
    if (!shadow_map_initialized) {
        init_vga_interface(); // Ensure shadow map is ready
    }
    int r, c;
    // Draw sky
    for (r = 0; r < GRASS_ROW_START; ++r) {
        for (c = 0; c < TILE_COLS; ++c) {
            write_tile_to_kernel(r, c, SKY_TILE_IDX);
        }
    }
    // Draw grass
    for (r = GRASS_ROW_START; r < TILE_ROWS; ++r) {
        for (c = 0; c < TILE_COLS; ++c) {
            write_tile_to_kernel(r, c, GRASS_TILE_IDX);
        }
    }
}
