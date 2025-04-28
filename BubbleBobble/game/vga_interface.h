
#define BLANKTILE 0 // img number of blank tile
// TODO: Change the offset according to the actual mapping
// which tile corresponding to which number, ideally the tile img containing the numbers
// are 0 to 9 starting from a offset from the base by a specific number
#define NUMBEROFFSET 1
#define NUMBERTILE(x) (unsigned char) (x+NUMBEROFFSET) 

// same principle as the number tiles, a to z offset from a specific number
#define LETTEROFFSET 11
#define LETTERTILE(x) (unsigned char) (x+LETTEROFFSET)

// assume score is displayed at a specific location, coordinate is using the tile grid rather than the pixels
// tile grid 0, 0 at the top left corner
#define SCORE_COORD_R 1
#define SCORE_COORD_C 20
#define SCORE_MAX_LENGTH 4

#define SKY_TILE  37 
#define GRASS_TILE 38
#define ROWS       30    // your screen is 30 tiles tall
#define COLS       40    // your screen is 40 tiles wide

extern int vga_fd; // vga file descriptor, define in main file


void write_tile_to_kernel(unsigned char r, unsigned char c, unsigned char n);

void write_sprite_to_kernel(unsigned char active, unsigned short r, unsigned short c, unsigned char n, unsigned short register_n);

void write_number(unsigned int num, unsigned int row, unsigned int col); // for writing numbers, input corresponding tile row and column

void write_letter(unsigned char letter, unsigned int row, unsigned int col); // for writing letters at specific coordinates

// write multiple numbers given the number of digits
void write_numbers(unsigned int nums, unsigned int digits, unsigned int row, unsigned int col); 

void write_score(int new_score); // for writing new scores

void write_text(unsigned char *text, unsigned int length, unsigned int row, unsigned int col);
// for writinng text at a specific coordinate, the coordinate of the text is the start of the text on the left

void cleartiles(); // for setting all tiles to empty

void fill_sky_and_grass();

void clearSprites();





