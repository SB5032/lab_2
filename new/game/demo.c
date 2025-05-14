// screamjump.c
// Main game logic for ScreamJump.
// Handles player movement, level progression, obstacles, scoring, and VGA output.
// Authored by: Ananya Mann Singh (am6542), Kamala Vennela Vasireddy (kv2446), Sharwari Bhosale (sb5032)

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <fcntl.h>
#include <string.h>
#include <math.h> // For ceil and floor

#include "usbcontroller.h"
#include "vga_interface.h"

// Screen and physics
#define SCR_W              640   // VGA width
#define SCR_H              480   // VGA height
#define MARGIN             8     // Top/bottom margin (px)
#define GRAVITY            +1

// Sprite dimensions
#define CKN_W              32
#define CKN_H              32
#define COIN_W             32
#define COIN_H             32

// MIF indices
#define CKN_STAND_IDX      8
#define CKN_JUMP_IDX       9
#define TOWER_TILE_IDX     40
#define SUN_TILE_IDX       20
#define MOON_TILE_IDX      21
#define COIN_SPRITE_IDX    22

// Tower
#define TOWER_TOP_ROW      21
#define CKN_TOWER_Y        ((TOWER_TOP_ROW * TILE_SIZE) - CKN_H)

// Game settings
#define INIT_LIVES         5
#define JUMP_VEL           -20   // Initial jump velocity Y
#define BASE_JUMP_DELAY    2000  // us
#define LONG_JUMP_DELAY    4000  // us
#define PTS_PER_LVL        10
#define MAX_LVL            5
#define MAX_SCORE_DIGITS   3
#define MAX_COIN_DIGITS    2

// Bar properties
#define MAX_BARS           10    // Size of bar arrays
#define WAVE_SWITCH_OFF    70    // Offset px to trigger next wave spawn
#define BAR_H_ROWS         2     // Bar height in tiles
#define BAR_TILE_IDX       39
#define BAR_OFFSCREEN_X    -1000 // X pos for inactive bars
#define BAR_X_STAGGER_B    96    // Initial X stagger for bar group B

// Bar Y clamping
#define BAR_MIN_Y          180   // Min Y for bar top
#define BAR_MAX_Y          400   // Max Y for bar top

// Fixed Y for L1/L2 bars
#define L12_BAR_Y_A        240
#define L12_BAR_Y_B        200

// Relative Y offset for random bar groups (L3+)
#define BAR_Y_REL_OFF      150

// Coin properties
#define MAX_COINS          5     // Max on screen
#define COIN_POINTS        2
#define COIN_SPAWN_LVL     3     // Level when coins start spawning
#define COIN_SPAWN_CHANCE  100   // Percent chance to spawn coin on eligible bar
#define COIN_COLLECT_DELAY (500) // Microseconds on bar to collect
#define FIRST_COIN_SPR_REG 2     // First sprite hardware register for coins

// Global variables
int vga_fd;
//int g_audio_fd;
struct controller_output_packet g_ctrl_state;
bool g_tower_on = true;
int  g_coins_total = 0;
bool g_do_restart = true;
int  g_level; // Current game level

// Structures
typedef struct {
    int x, y, vy;
    bool jumping;
    int coin_idx; // Index of coin being collected, or -1
    int coin_timer_us; // Timer for coin collection
} Chicken;

typedef struct {
    int x, y;     // Top-left pixel position
    int len;      // Length in tiles
    bool has_coin;
    int coin_idx; // Index in active_coins array, or -1
} Bar;

typedef struct {
    int bar_idx;    // Index of bar it's on
    int bar_grp;    // 0 for group A, 1 for group B
    bool active;
    int spr_reg;    // Sprite hardware register
} Coin;

Coin g_coins[MAX_COINS]; // Active coins on screen

// Function Prototypes
void draw_bars_buffered(Bar bars_a[], Bar bars_b[], int size);
void move_bars(Bar bars_a[], Bar bars_b[], int size, int speed);
bool check_bar_collision(Bar bars[], int grp_id, int size, int prev_y_ckn, Chicken *ckn, int *score, bool *landed);
void *ctrl_thread(void *arg);
void init_ckn(Chicken *c);
void move_ckn(Chicken *c);
void update_sun_moon_sprite(int current_level);
void reset_bars(Bar bars[], int size);
void init_coins(void);
void draw_coins_buffered(Bar bars_a[], Bar bars_b[]);
void reset_for_death(Chicken *c, Bar bA[], Bar bB[], bool *tower_on, bool *grpA_act, bool *needs_A, bool *needs_B, int *wA_idx, int *wB_idx, int *next_sA, int *next_sB, int *last_y_A, int *last_y_B, bool *first_rand_wave);

void draw_bars_buffered(Bar bars_a[], Bar bars_b[], int size) {
    Bar* cur_grp;
    for (int grp = 0; grp < 2; grp++) {
        cur_grp = (grp == 0) ? bars_a : bars_b;
        for (int b = 0; b < size; b++) {
            if (cur_grp[b].x == BAR_OFFSCREEN_X || cur_grp[b].len == 0) continue;
            int bar_px_w = cur_grp[b].len * TILE_SIZE;
            // Only draw if bar is somewhat on screen
            if (cur_grp[b].x < SCR_W && cur_grp[b].x + bar_px_w > 0) {
                int col0 = cur_grp[b].x / TILE_SIZE;
                int row0 = cur_grp[b].y / TILE_SIZE;
                int row1 = row0 + BAR_H_ROWS - 1;
                for (int r_tile = row0; r_tile <= row1; r_tile++) {
                    if (r_tile < 0 || r_tile >= TILE_ROWS) continue; // Bounds check row
                    for (int i = 0; i < cur_grp[b].len; i++) {
                        int c_tile = col0 + i;
                        if (c_tile >= 0 && c_tile < TILE_COLS) // Bounds check col
                            write_tile_to_kernel(r_tile, c_tile, BAR_TILE_IDX);
                    }
                }
            }
        }
    }
}

void move_bars(Bar bars_a[], Bar bars_b[], int size, int speed) {
    Bar* cur_grp;
    for (int grp = 0; grp < 2; grp++) {
        cur_grp = (grp == 0) ? bars_a : bars_b;
        for (int b = 0; b < size; b++) {
            if (cur_grp[b].x == BAR_OFFSCREEN_X) continue;
            cur_grp[b].x -= speed;
            int bar_px_w = cur_grp[b].len * TILE_SIZE;
            if (cur_grp[b].x + bar_px_w <= 0) { // Bar off-screen
                cur_grp[b].x = BAR_OFFSCREEN_X;
                if (cur_grp[b].has_coin && cur_grp[b].coin_idx != -1) {
                    int c_idx = cur_grp[b].coin_idx;
                    if (c_idx >= 0 && c_idx < MAX_COINS) { // Safe access
                        g_coins[c_idx].active = false;
                    }
                    cur_grp[b].has_coin = false;
                    cur_grp[b].coin_idx = -1;
                }
            }
        }
    }
}

bool check_bar_collision(Bar bars[], int grp_id, int size, int prev_y_ckn, Chicken *ckn, int *score, bool *landed) {
    if (ckn->vy <= 0) return false; // Not falling or moving up

    for (int b = 0; b < size; b++) {
        if (bars[b].x == BAR_OFFSCREEN_X) continue;

        int bar_top = bars[b].y;
        int bar_bottom = bars[b].y + BAR_H_ROWS * TILE_SIZE;
        int bar_left = bars[b].x;
        int bar_right = bars[b].x + bars[b].len * TILE_SIZE;

        int ckn_bottom_prev = prev_y_ckn + CKN_H;
        int ckn_bottom_curr = ckn->y + CKN_H;
        int ckn_right = ckn->x + CKN_W;

        // Collision: passed through bar top, within horizontal and vertical bounds
        if (ckn_bottom_prev <= bar_top && ckn_bottom_curr >= bar_top &&
            ckn_bottom_curr <= bar_bottom &&
            ckn_right > bar_left && ckn->x < bar_right) {
            ckn->y = bar_top - CKN_H; // Land on bar
            ckn->vy = 0;
            ckn->jumping = false;
            if (!(*landed)) {
                (*score)++;
                *landed = true;
            }
            // Check for coin on this bar
            if (bars[b].has_coin && bars[b].coin_idx != -1 && g_coins[bars[b].coin_idx].active) {
                ckn->coin_idx = bars[b].coin_idx;
                ckn->coin_timer_us = 0;
            } else {
                ckn->coin_idx = -1;
            }
            return true; // Collision detected
        }
    }
    return false; // No collision
}

void *ctrl_thread(void *arg) {
    uint8_t endpt_addr;
    struct libusb_device_handle *ctrl_handle = opencontroller(&endpt_addr);
    if (!ctrl_handle) {
        fprintf(stderr, "Ctrl thread: opencontroller() failed.\n");
        pthread_exit(NULL);
    }

    unsigned char buf[GAMEPAD_READ_LENGTH];
    int actual_len;
    while (g_do_restart) { // Loop while game session may restart
        int status = libusb_interrupt_transfer(ctrl_handle, endpt_addr, buf, GAMEPAD_READ_LENGTH, &actual_len, 1000);
        if (status == LIBUSB_SUCCESS && actual_len == GAMEPAD_READ_LENGTH) {
            usb_to_output(&g_ctrl_state, buf);
        } else if (status == LIBUSB_ERROR_TIMEOUT) {
            continue; // Timeout is expected, just retry
        } else if (status == LIBUSB_ERROR_INTERRUPTED) {
            fprintf(stderr, "Ctrl thread: Transfer interrupted. Exiting.\n");
            break; // Interrupted, likely by shutdown
        } else {
            fprintf(stderr, "Ctrl thread: Read error: %s\n", libusb_error_name(status));
            if (status == LIBUSB_ERROR_NO_DEVICE) {
                fprintf(stderr, "Ctrl thread: Device disconnected.\n");
                break; // Device gone, critical error
            }
            usleep(100000); // Brief pause on other errors
        }
    }
    printf("Ctrl thread: Cleaning up...\n");
    libusb_release_interface(ctrl_handle, 0);
    libusb_close(ctrl_handle);
    pthread_exit(NULL);
}

void init_ckn(Chicken *c) {
    c->x = 32; c->y = CKN_TOWER_Y; c->vy = 0; c->jumping = false;
    c->coin_idx = -1; c->coin_timer_us = 0;
}

void move_ckn(Chicken *c) {
    if (!c->jumping && g_tower_on) return; // Don't move if on tower and not jumping
    c->y += c->vy;
    c->vy += GRAVITY;
}

// void update_sun_moon_sprite(int current_level) {
//     const int start_x = 32, end_x = 608, base_y = 64;
//     double frac = (current_level > 1) ? (double)(current_level - 1) / (MAX_LVL - 1) : 0.0;
//     if (current_level >= MAX_LVL) frac = 1.0;

//     int sprite_x = start_x + (int)((end_x - start_x) * frac + 0.5);
//     // Sprite reg 1 is for sun/moon
//     write_sprite_to_kernel_buffered(1, base_y, sprite_x, (g_level >=3 ? MOON_TILE_IDX : SUN_TILE_IDX), 1);
// }


void update_sun_moon_sprite(int current_level) {
    const int START_X = 32;
    const int END_X   = 608;
    const int BASE_Y  = 64;
    static bool seeded = false;
    if (!seeded) {
        srand(time(NULL));
        seeded = true;
    }

    // 1) compute sun/moon position
    double frac = (current_level > 1)
                  ? (double)(current_level - 1) / (MAX_LVL - 1)
                  : 0.0;
    if (current_level >= MAX_LVL) frac = 1.0;

    int sunX = START_X + (int)((END_X - START_X)*frac + 0.5);
    int sunTile = (current_level >= 4 ? MOON_TILE_IDX : SUN_TILE_IDX);
    write_sprite_to_kernel_buffered(
        1,               // sprite reg
        BASE_Y,          // y
        sunX,            // x
        sunTile,         // tile index
        1                // visible
    );

    // 2) set up cloud registers & tile indices
    const int cloud_regs[6]  = {7, 8, 9, 10, 11, 12};
    const int cloud_tiles[2] = {23, 24};

    // mirror sun’s motion to the opposite side
    int cloudBaseX = END_X - (sunX - START_X);

    // 3) spawn 6 clouds at 32px horizontal spacing
    for (int i = 0; i < 6; i++) {
        int reg = cloud_regs[i];
        int x = cloudBaseX + i * 32;

        // keep at least 32px away from sun
        if (abs(x - sunX) < 32) {
            x += (x < sunX) ? -32 : 32;
        }

        // first 3 at BASE_Y; last 3 randomly 32px above or below
        int y;
        if (i < 3) {
            y = BASE_Y;
        } else {
            y = (rand() & 1)
                ? BASE_Y + 32   // below
                : BASE_Y - 32;  // above
        }

        int tile = cloud_tiles[rand() % 2];
        write_sprite_to_kernel_buffered(reg, y, x, tile, 1);
    }
}


void reset_bars(Bar bars[], int size) {
    for (int i = 0; i < size; i++) {
        bars[i].x = BAR_OFFSCREEN_X; bars[i].len = 0;
        bars[i].has_coin = false; bars[i].coin_idx = -1;
    }
}

void init_coins(void) {
    for (int i = 0; i < MAX_COINS; i++) {
        g_coins[i].active = false; g_coins[i].bar_idx = -1;
        g_coins[i].bar_grp = -1;
        g_coins[i].spr_reg = FIRST_COIN_SPR_REG + i;
    }
}

void draw_coins_buffered(Bar bars_a[], Bar bars_b[]) {
    for (int i = 0; i < MAX_COINS; i++) {
        if (g_coins[i].active) {
            Bar *parent_bars = (g_coins[i].bar_grp == 0) ? bars_a : bars_b;
            int bar_idx = g_coins[i].bar_idx;

            // Check if the bar this coin is on is still active and valid
            if (bar_idx != -1 && bar_idx < MAX_BARS && parent_bars[bar_idx].x != BAR_OFFSCREEN_X && parent_bars[bar_idx].coin_idx == i) {
                int bar_center_x = parent_bars[bar_idx].x + (parent_bars[bar_idx].len * TILE_SIZE) / 2;
                int coin_x = bar_center_x - (COIN_W / 2);
                int coin_y = parent_bars[bar_idx].y - COIN_H - (TILE_SIZE / 4); // Position above bar

                bool on_screen_x = (coin_x + COIN_W > 0) && (coin_x < SCR_W);
                bool on_screen_y = (coin_y + COIN_H > 0) && (coin_y < SCR_H);

                if (on_screen_x && on_screen_y) {
                    write_sprite_to_kernel_buffered(1, coin_y, coin_x, COIN_SPRITE_IDX, g_coins[i].spr_reg);
                } else { // Active but off-screen
                    write_sprite_to_kernel_buffered(0, 0, 0, 0, g_coins[i].spr_reg); // Hide
                }
            } else { // Coin active, but its parent bar is gone/invalid. Deactivate.
                write_sprite_to_kernel_buffered(0, 0, 0, 0, g_coins[i].spr_reg); // Hide
                g_coins[i].active = false;
            }
        } else { // Coin not active, ensure sprite is hidden
            write_sprite_to_kernel_buffered(0, 0, 0, 0, g_coins[i].spr_reg);
        }
    }
}


void reset_for_death(Chicken *c, Bar bA[], Bar bB[], bool *tower_on,
                     bool *grpA_act, bool *needs_A, bool *needs_B,
                     int *watch_idx_A, int *watch_idx_B, int *next_slot_A, int *next_slot_B,
                     int *last_y_A, int *last_y_B, bool *first_rand_wave) {
    init_ckn(c);
    *tower_on = true;
    reset_bars(bA, MAX_BARS); reset_bars(bB, MAX_BARS);
    init_coins(); // Reset all coins

    *grpA_act = true; *needs_A = true; *needs_B = false;
    *watch_idx_A = -1; *watch_idx_B = -1;
    *next_slot_A = 0; *next_slot_B = 0;

    *last_y_A = L12_BAR_Y_A;
    *last_y_B = L12_BAR_Y_B;
    *first_rand_wave = true; 

    cleartiles();
    fill_sky_and_grass();
    clearSprites_buffered();
}


int main(void) {
    // Static vars for bar Y positions, persist across deaths, reset on new game.
    static int s_last_y_a = L12_BAR_Y_A;
    static int s_last_y_b = L12_BAR_Y_B;
    static bool s_first_rand_wave = true;

    int score, lives;

    if ((vga_fd = open("/dev/vga_top", O_RDWR)) < 0) { perror("VGA open"); return -1; }
    init_vga_interface();

    pthread_t ctrl_tid;
    g_do_restart = true; // Controller thread runs as long as game can restart
    if (pthread_create(&ctrl_tid, NULL, ctrl_thread, NULL) != 0) {
        perror("Ctrl thread create"); close(vga_fd); return -1;
    }

game_restart_point: // Label for full game restart
    score = 0;
    g_level = 1;
    lives = INIT_LIVES;
    g_coins_total = 0;
    init_coins(); // Clear any existing coins from previous game

    // Reset static Y tracking for new game
    s_last_y_a = L12_BAR_Y_A;
    s_last_y_b = L12_BAR_Y_B;
    s_first_rand_wave = true;
    g_tower_on = true; // Reset tower state for new game start

    cleartiles(); clearSprites_buffered();
    fill_sky_and_grass(); // Initial screen
    write_text("scream", 6, 13, 16); write_text("jump", 4, 13, 22);
    write_text("press", 5, 16, 12); write_text("x", 1, 16, 18);
    write_text("key", 3, 16, 20); write_text("to", 2, 16, 24);
    write_text("start", 5, 16, 27);
    vga_present_frame(); present_sprites();

    while (!g_ctrl_state.x) { usleep(10000); } // Wait for X press
    usleep(200000); // Debounce
    while (g_ctrl_state.x) { usleep(10000); } // Wait for X release

    cleartiles(); clearSprites_buffered();
    fill_sky_and_grass(); // Background for level 1 start

    srand(time(NULL));
    int jump_vy = JUMP_VEL;
    const int hud_col = TILE_COLS / 2; const int hud_off = 12;

    Bar bars_a[MAX_BARS], bars_b[MAX_BARS];
    reset_bars(bars_a, MAX_BARS); reset_bars(bars_b, MAX_BARS);

    int min_bar_len, max_bar_len, bar_count, bar_speed, bar_spacing_px;
    int fixed_y_a, fixed_y_b; // For L1 & L2
    int jump_delay;

    Chicken ckn; init_ckn(&ckn);
    bool landed_jump = false;
    bool grp_a_spawns = true; bool spawn_a = true; bool spawn_b = false;
    int next_slot_a = 0, next_slot_b = 0;
    int watch_idx_a = -1, watch_idx_b = -1;

    while (lives > 0) {
        // Update game level based on score
        int old_level = g_level;
        g_level = 1 + (score / PTS_PER_LVL);
        if (g_level > MAX_LVL) g_level = MAX_LVL;

                // If level changed, maybe update background (already handled by general draw)
        if (old_level != g_level) {
           if (g_level > 3 && old_level < 3) fill_nightsky_and_grass(); // Transition to night
               else if (g_level == 3 ) fill_evesky_and_grass(); //transition to eve
           else if (g_level < 3 && old_level >3) fill_sky_and_grass(); // Transition to day
        }

        // Level-specific settings
        switch (g_level) {
            case 1:
                min_bar_len = 7; max_bar_len = 8; bar_count = 4; bar_speed = 3;
                bar_spacing_px = 170; fixed_y_a = L12_BAR_Y_A; fixed_y_b = L12_BAR_Y_B;
                jump_delay = LONG_JUMP_DELAY;
                break;
            case 2:
                min_bar_len = 6; max_bar_len = 8; bar_count = 3; bar_speed = 3;
                bar_spacing_px = 180; fixed_y_a = L12_BAR_Y_A; fixed_y_b = L12_BAR_Y_B;
                jump_delay = LONG_JUMP_DELAY;
                break;
            case 3:
                min_bar_len = 6; max_bar_len = 8; bar_count = 3; bar_speed = 3;
                bar_spacing_px = 160; /* Y is random */ jump_delay = LONG_JUMP_DELAY;
                break;
            case 4:
                min_bar_len = 5; max_bar_len = 7; bar_count = 3; bar_speed = 4;
                bar_spacing_px = 190; /* Y is random */ jump_delay = BASE_JUMP_DELAY;
                break;
            case 5: default:
                min_bar_len = 5; max_bar_len = 6; bar_count = 2; bar_speed = 4;
                bar_spacing_px = 190; /* Y is random */ jump_delay = BASE_JUMP_DELAY;
                break;
        }

        update_grass_scroll(bar_speed); // Scroll grass based on effective bar speed

        // Handle jump input
        if (g_ctrl_state.b && !ckn.jumping) {
            ckn.vy = jump_vy; ckn.jumping = true;
            landed_jump = false; g_tower_on = false; // play_sfx(0);
            if(ckn.coin_idx != -1) { 
                ckn.coin_timer_us = 0; ckn.coin_idx = -1;
            }
            usleep(jump_delay);
        }

        int prev_y_ckn = ckn.y;
        move_ckn(&ckn);
        move_bars(bars_a, bars_b, MAX_BARS, bar_speed);

        // Spawn bar wave A
        if (grp_a_spawns && spawn_a) {
            int y_a;
            if (g_level >= 3) { // Random Y for L3+
                if (s_first_rand_wave) {
                    y_a = L12_BAR_Y_A; // First random wave starts at a known Y
                    s_first_rand_wave = false;
                } else {
                    y_a = s_last_y_b + (rand() % (2 * BAR_Y_REL_OFF + 1)) - BAR_Y_REL_OFF;
                }
            } else { // Fixed Y for L1/L2
                y_a = fixed_y_a;
            }
            y_a = (y_a < BAR_MIN_Y) ? BAR_MIN_Y : (y_a > BAR_MAX_Y) ? BAR_MAX_Y : y_a; // Clamp Y
            y_a = (y_a / TILE_SIZE) * TILE_SIZE; // Align to tile grid
            s_last_y_a = y_a;

            int spawned = 0, last_idx = -1;
            for (int i = 0; i < bar_count; i++) {
                int slot = -1; // Find available slot in bars_a
                for (int j = 0; j < MAX_BARS; j++) {
                    int cur = (next_slot_a + j) % MAX_BARS;
                    if (bars_a[cur].x == BAR_OFFSCREEN_X) { slot = cur; break; }
                }
                if (slot != -1) {
                    bars_a[slot].x = SCR_W + (i * bar_spacing_px);
                    bars_a[slot].y = y_a;
                    bars_a[slot].len = rand() % (max_bar_len - min_bar_len + 1) + min_bar_len;
                    bars_a[slot].has_coin = false; bars_a[slot].coin_idx = -1;
                    if (g_level >= COIN_SPAWN_LVL && (rand() % 100) < COIN_SPAWN_CHANCE) {
                        for (int c_idx = 0; c_idx < MAX_COINS; c_idx++) { // Find inactive coin
                            if (!g_coins[c_idx].active) {
                                g_coins[c_idx].active = true; g_coins[c_idx].bar_idx = slot;
                                g_coins[c_idx].bar_grp = 0; // Group A
                                bars_a[slot].has_coin = true; bars_a[slot].coin_idx = c_idx;
                                break;
                            }
                        }
                    }
                    last_idx = slot; spawned++;
                } else break; // No slot
            }
            if (spawned > 0) { watch_idx_a = last_idx; next_slot_a = (last_idx + 1) % MAX_BARS; }
            spawn_a = false;
        }
        // Spawn bar wave B
        else if (!grp_a_spawns && spawn_b) {
            int y_b;
            if (g_level >= 3) { // Random Y for L3+
                 // s_first_rand_wave only applies to group A's first random wave in a session/reset
                y_b = s_last_y_a + (rand() % (2 * BAR_Y_REL_OFF + 1)) - BAR_Y_REL_OFF;
            } else { // Fixed Y for L1/L2
                y_b = fixed_y_b;
            }
            y_b = (y_b < BAR_MIN_Y) ? BAR_MIN_Y : (y_b > BAR_MAX_Y) ? BAR_MAX_Y : y_b; // Clamp Y
            y_b = (y_b / TILE_SIZE) * TILE_SIZE; // Align to tile grid
            s_last_y_b = y_b;

            int spawned = 0, last_idx = -1;
            for (int i = 0; i < bar_count; i++) {
                int slot = -1;
                for (int j = 0; j < MAX_BARS; j++) {
                    int cur = (next_slot_b + j) % MAX_BARS;
                    if (bars_b[cur].x == BAR_OFFSCREEN_X) { slot = cur; break; }
                }
                if (slot != -1) {
                    bars_b[slot].x = SCR_W + BAR_X_STAGGER_B + (i * bar_spacing_px);
                    bars_b[slot].y = y_b;
                    bars_b[slot].len = rand() % (max_bar_len - min_bar_len + 1) + min_bar_len;
                    bars_b[slot].has_coin = false; bars_b[slot].coin_idx = -1;
                     if (g_level >= COIN_SPAWN_LVL && (rand() % 100) < COIN_SPAWN_CHANCE) {
                        for (int c_idx = 0; c_idx < MAX_COINS; c_idx++) { // Find inactive coin
                            if (!g_coins[c_idx].active) {
                                g_coins[c_idx].active = true; g_coins[c_idx].bar_idx = slot;
                                g_coins[c_idx].bar_grp = 1; // Group B
                                bars_b[slot].has_coin = true; bars_b[slot].coin_idx = c_idx;
                                break;
                            }
                        }
                    }
                    last_idx = slot; spawned++;
                } else break;
            }
            if (spawned > 0) { watch_idx_b = last_idx; next_slot_b = (last_idx + 1) % MAX_BARS; }
            spawn_b = false;
        }

        // Switch active spawner group
        if (grp_a_spawns && watch_idx_a != -1 && bars_a[watch_idx_a].x != BAR_OFFSCREEN_X) {
            if (bars_a[watch_idx_a].x < SCR_W - WAVE_SWITCH_OFF) {
                grp_a_spawns = false; spawn_b = true; watch_idx_a = -1;
            }
        } else if (!grp_a_spawns && watch_idx_b != -1 && bars_b[watch_idx_b].x != BAR_OFFSCREEN_X) {
            if (bars_b[watch_idx_b].x < SCR_W - WAVE_SWITCH_OFF) {
                grp_a_spawns = true; spawn_a = true; watch_idx_b = -1;
            }
        }

        // Coin collection logic
        if (ckn.coin_idx != -1 && !ckn.jumping) { 
            ckn.coin_timer_us += 16666; 
            if (ckn.coin_timer_us >= COIN_COLLECT_DELAY) {
                Coin* coin = &g_coins[ckn.coin_idx];
                if (coin->active) {
                    score += (COIN_POINTS -1);
                    g_coins_total++;
                    coin->active = false; // Deactivate coin itself

                    // Remove coin from bar
                    Bar* parent_bars = (coin->bar_grp == 0) ? bars_a : bars_b;
                    if(coin->bar_idx != -1 && coin->bar_idx < MAX_BARS && parent_bars[coin->bar_idx].coin_idx == ckn.coin_idx) {
                        parent_bars[coin->bar_idx].has_coin = false;
                        parent_bars[coin->bar_idx].coin_idx = -1;
                    }
                }
                ckn.coin_idx = -1; ckn.coin_timer_us = 0; 
            }
        } else if (ckn.coin_idx != -1 && ckn.jumping) { 
            ckn.coin_timer_us = 0; ckn.coin_idx = -1;
        }

        // Check collision with bars
        if (ckn.vy > 0) { // Only check if falling
            bool landed_a = check_bar_collision(bars_a, 0, MAX_BARS, prev_y_ckn, &ckn, &score, &landed_jump);
            if (!landed_a) check_bar_collision(bars_b, 1, MAX_BARS, prev_y_ckn, &ckn, &score, &landed_jump);
        }

        // Boundary checks for chicken
        if (ckn.y < MARGIN && ckn.jumping) { ckn.y = MARGIN; if (ckn.vy < 0) ckn.vy = 0; } // Hit ceiling
        if (ckn.y + CKN_H > SCR_H - MARGIN) { 
            lives--;
            if (lives > 0) {
                reset_for_death(&ckn, bars_a, bars_b, &g_tower_on,
                                &grp_a_spawns, &spawn_a, &spawn_b,
                                &watch_idx_a, &watch_idx_b, &next_slot_a, &next_slot_b,
                                &s_last_y_a, &s_last_y_b, &s_first_rand_wave);
                vga_present_frame(); present_sprites();
                usleep(1000000); // Pause after death (1 sec)
                continue; 
            }
        }
        if (g_level > 3) fill_nightsky_and_grass();
            else if (g_level == 3) fill_evesky_and_grass();
        else fill_sky_and_grass();
        //fill_sky_and_grass();
        draw_bars_buffered(bars_a, bars_b, MAX_BARS);

        write_text("lives", 5, 1, hud_col - hud_off);
        write_number(lives, 1, hud_col - hud_off + 6);
        write_text("score", 5, 1, hud_col - hud_off + 12);
        write_numbers(score, MAX_SCORE_DIGITS, 1, hud_col - hud_off + 18);
        write_text("level", 5, 1, hud_col - hud_off + 24);
        write_number(g_level, 1, hud_col - hud_off + 30);

        if (g_tower_on) {
            for (int r = TOWER_TOP_ROW; r < TOWER_TOP_ROW + 9; ++r) {
                 if (r >= TILE_ROWS ) break;
                for (int c_tower = 0; c_tower < 5; ++c_tower) { write_tile_to_kernel(r, c_tower, TOWER_TILE_IDX); }
            }
        }
        vga_present_frame(); // Push tilemap buffer to screen

        clearSprites_buffered(); // Prepare sprite buffer
        write_sprite_to_kernel_buffered(1, ckn.y, ckn.x, ckn.jumping ? CKN_JUMP_IDX : CKN_STAND_IDX, 0); // Chicken is sprite 0
        update_sun_moon_sprite(g_level); // Sun/Moon is sprite 1
        draw_coins_buffered(bars_a, bars_b); 
        present_sprites(); 

        usleep(16666); // ~60 FPS
    }


    cleartiles();
    fill_sky_and_grass(); 

    clearSprites_buffered();
    write_text("game", 4, 13, 16); write_text("over", 4, 13, 21);
    write_text("score", 5, 15, 16); write_numbers(score, MAX_SCORE_DIGITS, 15, 22);
    write_text("coins", 5, 17, 11); write_text("collected", 9, 17, 17); write_numbers(g_coins_total, MAX_COIN_DIGITS, 17, 27);
    write_text("press", 5, 19, 9); write_text("x", 1, 19, 15);
    write_text("key", 3, 19, 17); write_text("to", 2, 19, 21);
    write_text("restart", 7, 19, 24);
    vga_present_frame(); present_sprites();

    memset(&g_ctrl_state, 0, sizeof(g_ctrl_state));
    usleep(100000); // Debounce

    while(1) { // Wait for restart command
        if (g_ctrl_state.x) {
            goto game_restart_point; // Restart the game
        }
        usleep(50000);
    }


    close(vga_fd);
    return 0;
}
