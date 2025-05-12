#ifndef VGA_INTERFACE_H
#define VGA_INTERFACE_H

#include <stdbool.h> // For bool type

// Define screen dimensions in tiles
#define TILE_ROWS 30
#define TILE_COLS 40
#define TILE_SIZE 16

#define BLANKTILE 0 // img number of blank tile
#define NUMBEROFFSET 1
#define NUMBERTILE(x) (unsigned char) (x+NUMBEROFFSET) 
#define LETTEROFFSET 11
#define LETTERTILE(x) (unsigned char) (x+LETTEROFFSET)

#define SCORE_COORD_R 1
#define SCORE_COORD_C 20
#define SCORE_MAX_LENGTH 4

#define MAX_HARDWARE_SPRITES 12 

extern int vga_fd; 

void init_vga_interface(void);

typedef struct {
    bool active;
    unsigned short r;
    unsigned short c;
    unsigned char n;
} SpriteHWState;


void write_tile_to_kernel(unsigned char r, unsigned char c, unsigned char n);
void write_sprite_to_kernel_buffered(unsigned char active, unsigned short r, unsigned short c, unsigned char n, unsigned short register_n);
void vga_present_frame(void);
void present_sprites(void);


void write_number(unsigned int num, unsigned int row, unsigned int col);
void write_letter(unsigned char letter, unsigned int row, unsigned int col);
void write_numbers(unsigned int nums, unsigned int digits, unsigned int row, unsigned int col); 
void write_score(int new_score);
void write_text(unsigned char *text, unsigned int length, unsigned int row, unsigned int col);

void cleartiles(); 
void clearSprites_buffered();


#define SKY_TILE_IDX 37  
#define GRASS_TILE_1_IDX 41 // User-defined
#define GRASS_TILE_2_IDX 42 // User-defined
#define GRASS_ROW_START 25 

void fill_sky_and_grass(void); 
// NEW: Declaration for updating grass scroll offset
void update_grass_scroll(int scroll_speed_px);

#endif // VGA_INTERFACE_H
