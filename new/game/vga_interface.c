#include "vga_top.h"      
#include "vga_interface.h" 
#include <stdio.h>         
#include <sys/ioctl.h>     
#include <sys/types.h>     
#include <sys/stat.h>      
#include <fcntl.h>         
#include <string.h>        
#include <unistd.h>        
#include <stdlib.h>        // For rand()
#include <stdbool.h>       

// --- Software Double Buffering for Tiles ---
static unsigned char display_buffer_A[TILE_ROWS][TILE_COLS];
static unsigned char display_buffer_B[TILE_ROWS][TILE_COLS];
static unsigned char (*current_back_buffer)[TILE_COLS] = display_buffer_A;
static unsigned char (*current_front_buffer)[TILE_COLS] = display_buffer_B;

// Shadow map for actual hardware tile writes
static unsigned char shadow_hardware_map[TILE_ROWS][TILE_COLS];
static bool vga_initialized = false; 

// --- Sprite State Buffering ---
static SpriteHWState desired_sprite_states[MAX_HARDWARE_SPRITES];
static SpriteHWState actual_hw_sprites[MAX_HARDWARE_SPRITES];   

// MODIFICATION: Variables for scrolling grass effect
static int grass_pixel_scroll_accumulator = 0;
static int grass_current_tile_shift = 0; // How many full tiles the grass pattern has shifted


static void vga_hardware_write_tile(unsigned char r, unsigned char c, unsigned char n) {
    if (r >= TILE_ROWS || c >= TILE_COLS) return;
    if (shadow_hardware_map[r][c] == n) return; 

    vga_top_arg_t vla;
    vla.r = r; vla.c = c; vla.n = n;
    if (ioctl(vga_fd, VGA_TOP_WRITE_TILE, &vla)) {
        perror("ioctl(VGA_TOP_WRITE_TILE) failed in vga_hardware_write_tile");
        return;
    }
    shadow_hardware_map[r][c] = n;
}

static void vga_hardware_write_sprite(unsigned char active, unsigned short r, unsigned short c, unsigned char n, unsigned short register_n) {
  if (register_n >= MAX_HARDWARE_SPRITES) return;
  
  vga_top_arg_s vla;
  vla.active = active; vla.r = r; vla.c = c; vla.n = n; vla.register_n = register_n;
  if (ioctl(vga_fd, VGA_TOP_WRITE_SPRITE, &vla)) {
    // perror("ioctl(VGA_TOP_WRITE_SPRITE) failed in vga_hardware_write_sprite"); 
  }
}


void init_vga_interface(void) {
    if (vga_initialized) return;

    for (int r = 0; r < TILE_ROWS; r++) {
        for (int c = 0; c < TILE_COLS; c++) {
            display_buffer_A[r][c] = BLANKTILE; 
            display_buffer_B[r][c] = BLANKTILE; 
            shadow_hardware_map[r][c] = 0xFF; 
        }
    }
    current_back_buffer = display_buffer_A;
    current_front_buffer = display_buffer_B; 

    for (int r = 0; r < TILE_ROWS; r++) {
        for (int c = 0; c < TILE_COLS; c++) {
            vga_hardware_write_tile(r, c, BLANKTILE);
        }
    }

    for (int i = 0; i < MAX_HARDWARE_SPRITES; i++) {
        desired_sprite_states[i] = (SpriteHWState){.active = false, .r = 0, .c = 0, .n = 0};
        actual_hw_sprites[i]   = (SpriteHWState){.active = false, .r = 0, .c = 0, .n = 0};
        vga_hardware_write_sprite(0, 0, 0, 0, i); 
    }
    grass_pixel_scroll_accumulator = 0; // Initialize grass scroll variables
    grass_current_tile_shift = 0;
    vga_initialized = true;
}

void write_tile_to_kernel(unsigned char r, unsigned char c, unsigned char n) {
  if (!vga_initialized) return; 
  if (r >= TILE_ROWS || c >= TILE_COLS) return;
  current_back_buffer[r][c] = n;
}

void vga_present_frame(void) {
    if (!vga_initialized) return;
    for (int r = 0; r < TILE_ROWS; r++) {
        for (int c = 0; c < TILE_COLS; c++) {
            if (current_back_buffer[r][c] != shadow_hardware_map[r][c]) {
                 vga_hardware_write_tile(r, c, current_back_buffer[r][c]);
            }
        }
    }
    unsigned char (*temp_buffer)[TILE_COLS] = current_front_buffer;
    current_front_buffer = current_back_buffer;
    current_back_buffer = temp_buffer;
}

void write_sprite_to_kernel_buffered(unsigned char active, unsigned short r, unsigned short c, unsigned char n, unsigned short register_n) {
    if (!vga_initialized || register_n >= MAX_HARDWARE_SPRITES) return;
    desired_sprite_states[register_n].active = active;
    desired_sprite_states[register_n].r = r;
    desired_sprite_states[register_n].c = c;
    desired_sprite_states[register_n].n = n;
}

void present_sprites(void) {
    if (!vga_initialized) return;
    for (int i = 0; i < MAX_HARDWARE_SPRITES; i++) {
        if (desired_sprite_states[i].active != actual_hw_sprites[i].active ||
            (desired_sprite_states[i].active && 
             (desired_sprite_states[i].r != actual_hw_sprites[i].r ||
              desired_sprite_states[i].c != actual_hw_sprites[i].c ||
              desired_sprite_states[i].n != actual_hw_sprites[i].n))) {
            
            vga_hardware_write_sprite(
                desired_sprite_states[i].active,
                desired_sprite_states[i].r,
                desired_sprite_states[i].c,
                desired_sprite_states[i].n,
                i
            );
            actual_hw_sprites[i] = desired_sprite_states[i]; 
        }
    }
}

void cleartiles() {
  if (!vga_initialized) return;
  for(int i=0; i < TILE_ROWS; i++) {
     for(int j=0; j < TILE_COLS; j++) {
        current_back_buffer[i][j] = BLANKTILE; 
     }
  }
}

void clearSprites_buffered(){
  if (!vga_initialized) return;
  for(int i = 0; i < MAX_HARDWARE_SPRITES; i++){ 
    desired_sprite_states[i].active = false;
  }
}

void write_number(unsigned int num, unsigned int row, unsigned int col) {
  if (num > 9) num = 9; 
  write_tile_to_kernel((unsigned char) row, (unsigned char) col, NUMBERTILE(num));
}

void write_letter(unsigned char letter, unsigned int row, unsigned int col) {
  if (letter >= 'a' && letter <= 'z') {
    letter = letter - 'a'; 
    write_tile_to_kernel(row, col, LETTERTILE(letter));
  } else {
    write_tile_to_kernel(row, col, BLANKTILE); 
  }
}

void write_numbers(unsigned int nums, unsigned int digits, unsigned int row, unsigned int col) {
  if ((col + digits) > TILE_COLS) return;
  if (digits == 0 || digits > 10) digits = 1;
  char temp_num_str[11]; 
  sprintf(temp_num_str, "%0*u", digits, nums);
  for (unsigned int i = 0; i < digits; i++) {
      if ((col + i) < TILE_COLS) {
          write_number(temp_num_str[i] - '0', row, col + i);
      } else { break; }
  }
}

void write_score(int new_score) {
  if (new_score < 0) new_score = 0;
  write_numbers((unsigned int) new_score, SCORE_MAX_LENGTH, SCORE_COORD_R, SCORE_COORD_C);
}

void write_text(unsigned char *text, unsigned int length, unsigned int row, unsigned int col) {
  if (!text) return;
  unsigned int write_len = length;
  if ((col + length) > TILE_COLS) {
    write_len = TILE_COLS - col; 
  }
  for (unsigned int i = 0; i < write_len; i++) {
    if ((col + i) < TILE_COLS) {
        write_letter(*(text + i), row, col + i);
    } else { break; }
  }
}

// NEW: Updates the grass scroll offset based on speed.
// This should be called once per game frame from main.c
void update_grass_scroll(int scroll_speed_px) {
    if (!vga_initialized) return; // Ensure interface is ready

    grass_pixel_scroll_accumulator += scroll_speed_px;
    
    // Calculate how many full tiles we need to shift the pattern
    int tiles_to_shift_this_frame = grass_pixel_scroll_accumulator / TILE_SIZE;
    
    if (tiles_to_shift_this_frame != 0) { // Check if it's non-zero to handle negative speeds correctly too
        grass_current_tile_shift += tiles_to_shift_this_frame;
        // Keep the remainder of pixels for the next frame's calculation
        grass_pixel_scroll_accumulator %= TILE_SIZE; 

        // Wrap grass_current_tile_shift around TILE_COLS to make the pattern repeat
        // Handle potential negative results from modulo with negative numbers if speed can be negative
        grass_current_tile_shift %= TILE_COLS;
        if (grass_current_tile_shift < 0) {
            grass_current_tile_shift += TILE_COLS;
        }
    }
}


// MODIFICATION: Fills the sky and randomly distributes three types of grass tiles,
// taking into account the current scroll offset.
void fill_sky_and_grass(void) {
    if (!vga_initialized) {
        init_vga_interface(); 
        if (!vga_initialized) return; 
    }
    int r, c;
    unsigned char grass_tile_to_use;

    // Draw sky tiles to the back buffer.
    for (r = 0; r < GRASS_ROW_START; ++r) { 
        for (c = 0; c < TILE_COLS; ++c) {
            write_tile_to_kernel(r, c, SKY_TILE_IDX); 
        }
    }
    // Draw grass tiles randomly to the back buffer, considering the scroll.
    for (r = GRASS_ROW_START; r < TILE_ROWS; ++r) { 
        for (c = 0; c < TILE_COLS; ++c) {
            // Calculate the effective column in the "world" grass pattern
            int pattern_col = (c + grass_current_tile_shift) % TILE_COLS;
            if (pattern_col < 0) pattern_col += TILE_COLS; // Ensure positive for modulo-based pattern

            // Choose grass tile type based on the scrolled pattern column and row
            // This creates a diagonal-like repeating pattern that scrolls.
            int rand_choice = (pattern_col + r) % 3; // Deterministic based on scrolled position

            switch (rand_choice) {
                case 0:
                    grass_tile_to_use = GRASS_TILE_1_IDX;
                    break;
                case 1:
                    grass_tile_to_use = GRASS_TILE_2_IDX;
                    break;
                case 2:
                default: 
                    grass_tile_to_use = GRASS_TILE_3_IDX;
                    break;
            }
            write_tile_to_kernel(r, c, grass_tile_to_use); 
        }
    }
}


void fill_nightsky_and_grass(void) {
    if (!vga_initialized) {
        init_vga_interface(); 
        if (!vga_initialized) return; 
    }
    int r, c;
    int p, q;
    unsigned char grass_tile_to_use;
    unsigned char sky_tile_to_use;

    // Draw sky tiles to the back buffer.
    for (p = 0; p < GRASS_ROW_START; ++p) { 
        for (q = 0; q < TILE_COLS; ++q) {
            // Calculate the effective column in the "world" grass pattern
            int pattern_col = (q + grass_current_tile_shift) % TILE_COLS;
            if (pattern_col < 0) pattern_col += TILE_COLS; // Ensure positive for modulo-based pattern

            // Choose grass tile type based on the scrolled pattern column and row
            // This creates a diagonal-like repeating pattern that scrolls.
            int rand_choice = (pattern_col + p) % 3; // Deterministic based on scrolled position

            switch (rand_choice) {
                case 0:
                    sky_tile_to_use = NIGHTSKY_TILE_IDX;
                    break;
                case 1:
                default:
                    sky_tile_to_use = STAR_TILE_IDX;
                    break;
            }
            write_tile_to_kernel(p, q, sky_tile_to_use);  
        }
    }
    
    // Draw grass tiles randomly to the back buffer, considering the scroll.
    for (r = GRASS_ROW_START; r < TILE_ROWS; ++r) { 
        for (c = 0; c < TILE_COLS; ++c) {
            // Calculate the effective column in the "world" grass pattern
            int pattern_col = (c + grass_current_tile_shift) % TILE_COLS;
            if (pattern_col < 0) pattern_col += TILE_COLS; // Ensure positive for modulo-based pattern

            // Choose grass tile type based on the scrolled pattern column and row
            // This creates a diagonal-like repeating pattern that scrolls.
            int rand_choice = (pattern_col + r) % 3; // Deterministic based on scrolled position

            switch (rand_choice) {
                case 0:
                    grass_tile_to_use = GRASS_TILE_1_IDX;
                    break;
                case 1:
                    grass_tile_to_use = GRASS_TILE_2_IDX;
                    break;
                case 2:
                default: 
                    grass_tile_to_use = GRASS_TILE_3_IDX;
                    break;
            }
            write_tile_to_kernel(r, c, grass_tile_to_use); 
        }
    }
}
