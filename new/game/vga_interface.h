#ifndef VGA_INTERFACE_H
#define VGA_INTERFACE_H

#include <stdbool.h> // For bool type

// Define screen dimensions in tiles
#define TILE_ROWS 30
#define TILE_COLS 40

#define BLANKTILE 0 // img number of blank tile
#define NUMBEROFFSET 1
#define NUMBERTILE(x) (unsigned char) (x+NUMBEROFFSET) 
#define LETTEROFFSET 11
#define LETTERTILE(x) (unsigned char) (x+LETTEROFFSET)

#define SCORE_COORD_R 1
#define SCORE_COORD_C 20
#define SCORE_MAX_LENGTH 4

// MODIFICATION: Define maximum number of hardware sprites
#define MAX_HARDWARE_SPRITES 12 // Assuming 12 based on clearSprites

extern int vga_fd; // vga file descriptor, define in main file

// Initialization function for the VGA interface (shadow tilemap and software buffers)
void init_vga_interface(void);

// MODIFICATION: Sprite state structure for buffering
typedef struct {
    bool active;
    unsigned short r;
    unsigned short c;
    unsigned char n;
    // register_n is the index in the array
} SpriteHWState;


// MODIFIED: Now writes to the current software back buffer for tiles
void write_tile_to_kernel(unsigned char r, unsigned char c, unsigned char n);

// MODIFICATION: Now updates a desired sprite state buffer, doesn't write to HW directly
void write_sprite_to_kernel_buffered(unsigned char active, unsigned short r, unsigned short c, unsigned char n, unsigned short register_n);

// NEW: Function to present the current tile back buffer to the screen
void vga_present_frame(void);

// NEW: Function to present the desired sprite states to the hardware
void present_sprites(void);


void write_number(unsigned int num, unsigned int row, unsigned int col);
void write_letter(unsigned char letter, unsigned int row, unsigned int col);
void write_numbers(unsigned int nums, unsigned int digits, unsigned int row, unsigned int col); 
void write_score(int new_score);
void write_text(unsigned char *text, unsigned int length, unsigned int row, unsigned int col);

void cleartiles(); // Now clears the current software back buffer for tiles
// MODIFICATION: Now updates desired sprite states to inactive
void clearSprites_buffered();


#define SKY_TILE_IDX 50  // Example tile index for sky - USER MUST VERIFY/UPDATE
#define GRASS_TILE_IDX 51 // Example tile index for grass - USER MUST VERIFY/UPDATE
#define GRASS_ROW_START 25 // Example row where grass starts - USER MUST VERIFY/UPDATE

void fill_sky_and_grass(void); // Now fills the current software back buffer for tiles

#endif // VGA_INTERFACE_H
