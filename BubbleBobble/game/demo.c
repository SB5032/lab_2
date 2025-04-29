#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <fcntl.h>
#include "usbcontroller.h"
#include "vga_interface.h"
#include "audio_interface.h"
#define LENGTH 640
#define WIDTH 480
#define MAX_ENEMIES 4
#define MAX_BUBBLES 7
#define BUBBLE_SPEED 8
#define BUBBLE_RADIUS 16
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define WALL 16
#define HVEC 8
int level = 0;
int numEnemy = MAX_ENEMIES;
int numOfReward = 0;
int grade = 0;
int life = 3;
bool restart = true;
int vga_fd;
int audio_fd;

typedef struct
{
    int x, y;
    int width, height;
    int vx, vy;
    bool jumping;
    bool facingRight;
    bool active;
    bool canFire;
} Character;

typedef struct
{
    int x, y;
    int width, height;
    int vx, vy;
    bool jumping;
    bool surrounded;
    bool active;
    bool facingRight;
    int type;
    int reg;
    int enemyARight;
    int enemyALeft;
    int enemyBRight;
    int enemyBLeft;
} Enemy;

typedef struct
{
    int x, y;
    int radius;
    int dx;
    int dy;
    bool active;
    int reg;
} Bubble;

typedef struct
{
    int x, y;
    int width, height;
    int vy;
    bool onTheFloor;
    bool active;
    int reg;
    int seq;
} Reward;

typedef struct
{
    int x, y;
    int width, height;
} Wall;

// 初始化角色
void initCharacter(Character *character)
{
    character->width = 32;
    character->height = 32;
    character->x = 64;
    character->y = WIDTH - character->height - 16;
    character->vx = 0;
    character->vy = 0;
    character->jumping = false;
    character->facingRight = true;
    character->active = true;
    character->canFire = false;
}

void initEnemy(Enemy *enemy, int reg, Wall wall[])
{
    enemy->width = 32;
    enemy->height = 32;
    int h = rand() % 5 + 3;
    if (h == 3)
    {
        enemy->x = 160 + rand() % 14 * 32;
    }
    else
    {
        enemy->x = wall[h].x + 16 + rand() % (wall[h].width - 16);
    }
    enemy->y = wall[h].y - enemy->height;
    enemy->vx = HVEC;
    enemy->vy = 0;
    enemy->jumping = false;
    enemy->surrounded = false;
    enemy->active = true;
    enemy->facingRight = true;
    enemy->type = rand() % 2;
    enemy->reg = reg;
    enemy->enemyARight = 14;
    enemy->enemyALeft = 16;
    enemy->enemyBRight = 21;
    enemy->enemyBLeft = 23;
}
void initReward(Reward *reward, int x, int y, int reg)
{
    reward->x = x;
    reward->y = y + 10;
    reward->width = 32;
    reward->height = 32;
    reward->vy = 20;
    reward->active = true;
    reward->onTheFloor = false;
    reward->reg = reg;
    reward->seq = rand() % 4 + 26;
}

void initBubble(Bubble *bubble, int x, int y, int bubbleSequence)
{
    bubble->x = x;
    bubble->y = y;
    bubble->radius = BUBBLE_RADIUS;
    bubble->dx = BUBBLE_SPEED;
    bubble->active = false;
    bubble->reg = bubbleSequence;
}

void shootBubble(Bubble *bubbles, int maxBubbles, const Character *character)
{
    for (int i = 0; i < maxBubbles; ++i)
    {
        if (!bubbles[i].active)
        {
            if (character->facingRight)
            {
                bubbles[i].x = character->x + (character->facingRight ? character->width : 0);
            }
            else
            {
                bubbles[i].x = character->x - character->width;
            }
            bubbles[i].y = character->y;
            bubbles[i].active = true;
            bubbles[i].dx = character->facingRight ? BUBBLE_SPEED : -BUBBLE_SPEED;
            break;
        }
    }
}

void moveCharacter(Character *character, int dx, int dy, const Wall *walls, int numWalls)
{
    if (character->active == false)
    {
        // return;
    }
    int newX = character->x + dx;
    int newY = character->y + dy;
    if (newX <= WALL && dx < 0)
    {
        newX = WALL;
    }
    if (newX >= LENGTH - character->width - WALL && dx > 0)
    {
        newX = LENGTH - character->width - WALL;
    }

    if (newY >= WIDTH - character->height - WALL)
    {
        newY = WIDTH - character->height - WALL;
        character->jumping = false;
    }

    if (newY <= WALL)
    {
        newY = WALL;
    }

    for (int i = 4; i < numWalls; ++i)
    {
        if (newX < walls[i].x + walls[i].width &&
            newX + character->width > walls[i].x &&
            newY < walls[i].y + walls[i].height &&
            newY + character->height > walls[i].y)
        {

            if (dy > 0)
            {

                newY = walls[i].y - character->height;
                character->vy = 0;
                character->jumping = false;
            }
            else if (dy < 0)
            {
            }
            else if (dx > 0)
            {

                newX = MIN(newX, LENGTH - WALL - character->width);
                character->vx = 0;
            }
            else if (dx < 0)
            {

                newX = WALL;
                character->vx = 0;
            }
        }
    }

    character->x = newX;
    character->y = newY;
}
void moveReward(Reward *reward, const Wall *walls, int numWalls)
{
    int newY = reward->y + reward->vy;
    for (int i = 0; i < numWalls; ++i)
    {
        if (reward->x < walls[i].x + walls[i].width &&
            reward->x + reward->width > walls[i].x &&
            newY < walls[i].y + walls[i].height &&
            newY + reward->height > walls[i].y)
        {
            newY = walls[i].y - reward->height;
            reward->vy = 0;
            reward->onTheFloor = true;
        }
    }
    reward->y = newY;
}

void moveEnemy(Enemy *enemy, int dx, int dy, const Wall *walls, int numWalls)
{
    int newX = enemy->x + enemy->vx;
    int newY = enemy->y + enemy->vy;
    if (enemy->surrounded)
    {
        enemy->vy = -1;
        for (int i = 4; i < numWalls; ++i)
        {
            if (newX < walls[i].x + walls[i].width &&
                newX + enemy->width > walls[i].x &&
                newY < walls[i].y + walls[i].height &&
                newY + enemy->height > walls[i].y)
            {

                if (newY >= walls[i].y)
                {
                    newY = walls[i].y + WALL;
                    enemy->vy = 0;
                }
            }
        }
        enemy->y = newY;
        return;
    }
    if (rand() % 70 == 0)
    {
        enemy->vx *= -1;
        enemy->facingRight = !enemy->facingRight;
    }
    if (rand() % 200 == 0)
    {
        if (!enemy->jumping)
        {

            enemy->vy = -13;
            enemy->jumping = true;
        }
    }

    if (newX <= WALL && dx < 0)
    {
        newX = WALL;
        enemy->vx *= -1;
        enemy->facingRight = true;
    }
    if (newX >= LENGTH - enemy->width - WALL && dx > 0)
    {
        newX = LENGTH - enemy->width - WALL;
        enemy->vx *= -1;
        enemy->facingRight = false;
    }

    if (newY >= WIDTH - enemy->height - WALL)
    {
        newY = WIDTH - enemy->height - WALL;
        enemy->jumping = false;
    }

    if (newY <= WALL)
    {
        newY = WALL;
    }

    for (int i = 4; i < numWalls; ++i)
    {
        if (newX < walls[i].x + walls[i].width &&
            newX + enemy->width > walls[i].x &&
            newY < walls[i].y + walls[i].height &&
            newY + enemy->height > walls[i].y)
        {

            if (dy > 0)
            {

                newY = walls[i].y - enemy->height;
                enemy->vy = 0;
                enemy->jumping = false;
            }
            else if (dy < 0)
            {
            }
            else if (dx > 0)
            {

                newX = MIN(newX, LENGTH - WALL - enemy->width);
                enemy->vx *= -1;
                enemy->facingRight = false;
            }
            else if (dx < 0)
            {

                newX = WALL;
                enemy->vx *= -1;
                enemy->facingRight = true;
            }
        }
    }

    enemy->x = newX;
    enemy->y = newY;
}

void moveBubble(Bubble *bubble, Enemy *enemies, int numEnemies)
{
    if (bubble->active)
    {
        static int distanceMoved = 0;
        bubble->x += bubble->dx;
        distanceMoved += abs(bubble->dx);
        if (bubble->x + bubble->radius <= WALL || bubble->x - bubble->radius >= LENGTH - WALL)
        {
            bubble->active = false;
            distanceMoved = 0;
        }

        for (int i = 0; i < numEnemies; ++i)
        {
            if (enemies[i].active && !enemies[i].surrounded &&
                enemies[i].x < bubble->x + bubble->radius &&
                enemies[i].x + enemies[i].width > bubble->x - bubble->radius &&
                enemies[i].y < bubble->y + bubble->radius &&
                enemies[i].y + enemies[i].height > bubble->y - bubble->radius)
            {

                distanceMoved = 0;
                bubble->active = false;
                enemies[i].active = false;
                enemies[i].surrounded = true;
                break;
            }
        }
        if (distanceMoved >= 350 || bubble->x <= WALL || bubble->x >= LENGTH - WALL - BUBBLE_RADIUS * 2)
        {
            bubble->active = false;
            distanceMoved = 0;
        }
        if (bubble->active == false)
        {
            write_sprite_to_kernel(0, 0, 0, 0, bubble->reg);
        }
    }
}
bool checkCollisionCharacterReward(const Character *character, const Reward *reward)
{
    if (reward->onTheFloor == false)
    {
        return false;
    }
    return (character->x < reward->x + reward->width &&
            character->x + character->width > reward->x &&
            character->y < reward->y + reward->height &&
            character->y + character->height > reward->y);
}

bool checkCollisionCharacterEnemy(const Character *character, const Enemy *enemy)
{
    return (character->x < enemy->x + enemy->width &&
            character->x + character->width > enemy->x &&
            character->y < enemy->y + enemy->height &&
            character->y + character->height > enemy->y);
}
void handleCollisionCharcterReward(Character *character, Reward *reward)
{
    for (int i = 0; i < MAX_ENEMIES; ++i)
    {
        if (checkCollisionCharacterReward(character, &reward[i]))
        {
            if (reward[i].active)
            {
                play_sfx(2);
                reward[i].active = false;
                write_sprite_to_kernel(0, 0, 0, 0, reward[i].reg);
                numOfReward--;
                grade += 10;
                write_score(grade);
                break;
            }
        }
    }
}

void handleCollisionCharcterEnemy(Character *character, Enemy *enemies, int numEnemies, Reward *reward)
{
    for (int i = 0; i < MAX_ENEMIES; ++i)
    {
        if (checkCollisionCharacterEnemy(character, &enemies[i]))
        {
            if (enemies[i].surrounded)
            {
                enemies[i].active = false;
                numOfReward++;
                initReward(&reward[numOfReward - 1], enemies[i].x, enemies[i].y, enemies[i].reg);
                for (int j = i; j < numEnemies - 1; ++j)
                {
                    enemies[j] = enemies[j + 1];
                }
                enemies[numEnemies - 1] = (Enemy){0};
                numEnemies--;
                numEnemy--;
            }
            else
            {
                if (character->active)
                {
                    play_sfx(0);
                    character->active = false;
                    life--;
                    if (life == 0)
                    {
                        bgm_startstop(0);
                        write_tile_to_kernel(1, 6, 1);
                        clearSprites();
                        write_sprite_to_kernel(1, character->y, character->x, 1, 11);
                        sleep(1);
                        write_sprite_to_kernel(1, character->y, character->x, 2, 11);
                        sleep(1);
                        return;
                    }
                    initCharacter(character);
                }
            }
            break;
        }
    }
}

void loadNextLevel(Character *character, Enemy *enemies, Wall *walls)
{
    level++;
    play_sfx(1);
    clearSprites();
    write_text("next", 4, 14, 15);
    write_text("level", 5, 14, 20);
    character->x = 64;
    character->y = WIDTH - character->height - 16;
    sleep(2);
    fill_sky_and_grass();
    switch (level)
    {
    case 1:
        walls[4].x = 208;
        walls[4].y = 320;
        walls[4].width = 240;
        walls[4].height = WALL;

        walls[5].x = 208;
        walls[5].y = 192;
        walls[5].width = 240;
        walls[5].height = WALL;

        walls[6].x = 208;
        walls[6].y = 80;
        walls[6].width = 240;
        walls[6].height = WALL;

        walls[7].x = 208;
        walls[7].y = 80;
        walls[7].width = 240;
        walls[7].height = WALL;
        break;
    case 2:
        walls[4].x = 0;
        walls[4].y = 336;
        walls[4].width = 160;
        walls[4].height = WALL;

        walls[5].x = 160;
        walls[5].y = 224;
        walls[5].width = 160;
        walls[5].height = WALL;

        walls[6].x = 320;
        walls[6].y = 144;
        walls[6].width = 160;
        walls[6].height = WALL;

        walls[7].x = 480;
        walls[7].y = 64;
        walls[7].width = 144;
        walls[7].height = WALL;
        break;
    case 3:
        walls[4].x = 80;
        walls[4].y = 160;
        walls[4].width = 160;
        walls[4].height = WALL;

        walls[5].x = 80;
        walls[5].y = 320;
        walls[5].width = 160;
        walls[5].height = WALL;

        walls[6].x = 384;
        walls[6].y = 160;
        walls[6].width = 160;
        walls[6].height = WALL;

        walls[7].x = 384;
        walls[7].y = 320;
        walls[7].width = 160;
        walls[7].height = WALL;
        break;
    case 4:
        walls[4].x = 0;
        walls[4].y = 160;
        walls[4].width = 192;
        walls[4].height = WALL;

        walls[5].x = 0;
        walls[5].y = 320;
        walls[5].width = 192;
        walls[5].height = WALL;

        walls[6].x = 432;
        walls[6].y = 160;
        walls[6].width = 192;
        walls[6].height = WALL;

        walls[7].x = 432;
        walls[7].y = 320;
        walls[7].width = 192;
        walls[7].height = WALL;

        break;
    case 5:
        walls[4].x = 160;
        walls[4].y = 96;
        walls[4].width = 320;
        walls[4].height = WALL;

        walls[5].x = 160;
        walls[5].y = 352;
        walls[5].width = 320;
        walls[5].height = WALL;

        walls[6].x = 80;
        walls[6].y = 224;
        walls[6].width = 32;
        walls[6].height = WALL;

        walls[7].x = 508;
        walls[7].y = 224;
        walls[7].width = 32;
        walls[7].height = WALL;

        break;
    case 6:
        walls[4].x = 0;
        walls[4].y = 400;
        walls[4].width = 480;
        walls[4].height = WALL;

        walls[5].x = 144;
        walls[5].y = 288;
        walls[5].width = 480;
        walls[5].height = WALL;

        walls[6].x = 0;
        walls[6].y = 192;
        walls[6].width = 480;
        walls[6].height = WALL;

        walls[7].x = 144;
        walls[7].y = 80;
        walls[7].width = 480;
        walls[7].height = WALL;
        break;

    case 7:
        walls[4].x = 80;
        walls[4].y = 320;
        walls[4].width = 32;
        walls[4].height = WALL;

        walls[5].x = 240;
        walls[5].y = 240;
        walls[5].width = 32;
        walls[4].height = WALL;

        walls[6].x = 400;
        walls[6].y = 160;
        walls[6].width = 32;
        walls[6].height = WALL;

        walls[7].x = 96;
        walls[7].y = 80;
        walls[7].width = 546;
        walls[7].height = WALL;
        break;
    }

    for (int i = 0; i < MAX_ENEMIES; i++)
    {
        initEnemy(&enemies[i], MAX_BUBBLES + i, walls);
    }
    numEnemy = MAX_ENEMIES;
    write_score(grade);
}

struct controller_output_packet controller_state;

void *controller_input_thread(void *arg)
{
    uint8_t endpoint_address;
    struct libusb_device_handle *controller = opencontroller(&endpoint_address);
    if (controller == NULL)
    {
        fprintf(stderr, "Failed to open USB controller device.\n");
        pthread_exit(NULL);
    }

    while (1)
    {
        unsigned char output_buffer[GAMEPAD_READ_LENGTH];
        int bytes_transferred;

        int result = libusb_interrupt_transfer(controller, endpoint_address, output_buffer, GAMEPAD_READ_LENGTH, &bytes_transferred, 0);

        if (result == 0)
        {

            usb_to_output(&controller_state, output_buffer);
        }
        if (restart == false)
        {
            break;
        }
    }

    libusb_close(controller);
    libusb_exit(NULL);
    pthread_exit(NULL);
}
bool press()
{
    if (controller_state.updown != 0 || controller_state.leftright != 0 || controller_state.select || controller_state.left_rib || controller_state.right_rib || controller_state.x || controller_state.y || controller_state.a || controller_state.b)
    {
        return true;
    }
    return false;
}

int main(int argc, char *argv[])
{
    static const char filename[] = "/dev/vga_top";
    if ((vga_fd = open(filename, O_RDWR)) == -1)
    {
        fprintf(stderr, "could not open %s\n", filename);
        return -1;
    }
    static const char filename1[] = "/dev/fpga_audio";
    if ((audio_fd = open(filename1, O_RDWR)) == -1)
    {
        fprintf(stderr, "could not open %s\n", filename);
        return -1;
    }
    pthread_t controller_thread;
    if (pthread_create(&controller_thread, NULL, controller_input_thread, NULL) != 0)
    {
        fprintf(stderr, "Failed to create controller input thread.\n");
        return 1;
    }
    while (true)
    {
        play_sfx(5);
        fill_sky_and_grass();
        clearSprites();
        int index = 8;
        write_text("scream", 6, 13, 13);
        write_text("jump", 6, 13, 20);
        write_text("press", 5, 19, index);
        write_text("any", 3, 19, index + 6);
        write_text("key", 3, 19, index + 10);
        write_text("to", 2, 19, index + 14);
        write_text("start", 5, 19, index + 17);
        int characterLeftSequence = 3;
        int characterRightSequence = 7;
        int enemyS = 14;
        bool initialMove = true;
        while (true)
        {
            for (int i = 0; i < 648; i += 1)
            {
                write_sprite_to_kernel(1, 448, i, characterRightSequence, 11);
                characterRightSequence++;
                write_sprite_to_kernel(1, 448, i - 140, enemyS, 0); // 14-15
                enemyS = (enemyS == 14) ? 15 : 14;
                if (characterRightSequence == 11)
                {
                    characterRightSequence = 7;
                }

                if (press())
                {
                    initialMove = false;
                    break;
                }
                usleep(5000);
            }
            if (initialMove == false)
            {
                break;
            }
        }
        usleep(500);
        bgm_startstop(1);
        fill_sky_and_grass();
        clearSprites();
        srand(time(NULL));

        Character character;
        initCharacter(&character);

        Wall walls[] = {
            {0, 0, LENGTH, WALL},
            {0, 0, WALL, WIDTH},
            {LENGTH - WALL, 0, WALL, WIDTH},
            {0, WIDTH - WALL, LENGTH, WALL},
            {240, 352, 240, WALL},
            {560, WIDTH - WALL, 32, WALL},
            {400, WIDTH - WALL, 32, WALL},
            {352, WIDTH - WALL, 32, WALL}};

        Enemy enemies[MAX_ENEMIES];
        for (int i = 0; i < MAX_ENEMIES; ++i)
        {
            initEnemy(&enemies[i], 5 + i, walls);
        }

        Bubble bubbles[MAX_BUBBLES];
        int bubbleSequence = 0;
        for (int i = 0; i < MAX_BUBBLES; ++i)
        {
            initBubble(&bubbles[i], 0, 0, bubbleSequence++);
            if (bubbleSequence == 7)
            {
                bubbleSequence = 0;
            }
        }
        Reward reward[MAX_ENEMIES];
        // draw wall;   37-44
        int wallSequence = 37;
        for (int i = 0; i < sizeof(walls) / sizeof(walls[0]); ++i)
        {
            if (walls[i].width > walls[i].height)
            {
                for (int k = walls[i].x; k < walls[i].width + walls[i].x; k += 16)
                {
                    write_tile_to_kernel(walls[i].y / 16, k / 16, wallSequence);
                }
            }
            else
            {
                for (int k = walls[i].y; k < walls[i].height + walls[i].y; k += 16)
                {
                    write_tile_to_kernel(k / 16, walls[i].x / 16, wallSequence);
                }
            }
        }
        write_tile_to_kernel(1, 20, 1);
        while (true)
        {
            write_text("level", 5, 1, 30);
            write_tile_to_kernel(1, 36, level + 1);
            write_tile_to_kernel(1, 4, 45);
            write_tile_to_kernel(1, 5, 34);
            write_tile_to_kernel(1, 6, life + 1);
            write_tile_to_kernel(1, 11, 29);
            write_tile_to_kernel(1, 12, 13);
            write_tile_to_kernel(1, 13, 25);
            write_tile_to_kernel(1, 14, 28);
            write_tile_to_kernel(1, 15, 15);
            write_tile_to_kernel(1, 16, 29);
            if (numEnemy == 0 && numOfReward == 0)
            {
                // draw wall; 37-44
                fill_sky_and_grass();
                wallSequence++;
                if (wallSequence == 45)
                {
                    break;
                }
                loadNextLevel(&character, enemies, walls);
                for (int i = 0; i < sizeof(walls) / sizeof(walls[0]); ++i)
                {
                    if (walls[i].width > walls[i].height)
                    {
                        for (int k = walls[i].x; k < walls[i].width + walls[i].x; k += 16)
                        {
                            write_tile_to_kernel(walls[i].y / 16, k / 16, wallSequence);
                        }
                    }
                    else
                    {
                        for (int k = walls[i].y; k < walls[i].height + walls[i].y; k += 16)
                        {
                            write_tile_to_kernel(k / 16, walls[i].x / 16, wallSequence);
                        }
                    }
                }
            }

            if (controller_state.leftright == 1)
            {
                character.vx = -HVEC;
                character.facingRight = false;
            }
            else if (controller_state.leftright == -1)
            {
                character.vx = HVEC;
                character.facingRight = true;
            }
            else
            {
                character.vx = 0;
            }
            if (controller_state.a == 1)
            {
                character.canFire = true;
            }
            else
            {
                if (character.canFire == true)
                {
                    shootBubble(bubbles, MAX_BUBBLES, &character);
                    character.canFire = false;
                }
            }

            if (controller_state.b == 1 && !character.jumping)
            {
                character.vy = -13;
                character.jumping = true;
            }

            moveCharacter(&character, character.vx, character.vy, walls, sizeof(walls) / sizeof(walls[0]));
            character.y += character.vy;
            character.vy += 1;
            if (character.y >= WIDTH - character.height - WALL)
            {
                character.y = WIDTH - character.height - WALL;
                character.vy = 0;
                character.jumping = false;
            }

            if (character.y <= WALL)
            {
                character.y = WALL;
                character.vy = 1;
            }

            for (int i = 0; i < MAX_ENEMIES; ++i)
            {
                moveEnemy(&enemies[i], enemies[i].vx, enemies[i].vy, walls, sizeof(walls) / sizeof(walls[0]));
                enemies[i].y += enemies[i].vy;
                enemies[i].vy += 1;
                if (enemies[i].y >= WIDTH - enemies->height - WALL)
                {
                    enemies[i].y = WIDTH - enemies->height - WALL;
                    enemies[i].vy = 0;
                    enemies[i].jumping = false;
                }

                if (enemies[i].y <= WALL)
                {
                    enemies[i].y = WALL;
                    enemies[i].vy = 1;
                }
            }

            for (int i = 0; i < MAX_BUBBLES; ++i)
            {
                moveBubble(&bubbles[i], enemies, MAX_ENEMIES);
            }

            for (int i = 0; i < MAX_ENEMIES; i++)
            {
                moveReward(&reward[i], walls, sizeof(walls) / sizeof(walls[0]));
            }

            handleCollisionCharcterEnemy(&character, enemies, MAX_ENEMIES, reward);
            if (life == 0)
            {
                break;
                printf("GG\n");
            }
            handleCollisionCharcterReward(&character, reward);

            if (character.facingRight)
            {
                if (character.vx == 0)
                {
                    write_sprite_to_kernel(1, character.y, character.x, 7, 11);
                }
                else
                {
                    write_sprite_to_kernel(1, character.y, character.x, characterRightSequence, 11);
                    characterRightSequence++;
                    if (characterRightSequence == 11)
                    {
                        characterRightSequence = 7;
                    }
                }
            }
            else
            {
                if (character.vx == 0)
                {
                    write_sprite_to_kernel(1, character.y, character.x, 3, 11);
                }
                else
                {
                    write_sprite_to_kernel(1, character.y, character.x, characterLeftSequence, 11);
                    characterLeftSequence++;
                    if (characterLeftSequence == 7)
                    {
                        characterLeftSequence = 3;
                    }
                }
            }

            for (int i = 0; i < numEnemy; ++i)
            {
                if (enemies[i].surrounded)
                {
                    // draw surrounded bubble
                    write_sprite_to_kernel(1, enemies[i].y, enemies[i].x, 13, enemies[i].reg);
                }
                // draw enemy
                else
                {
                    if (enemies[i].type == 0)
                    {
                        if (enemies[i].facingRight)
                        {
                            write_sprite_to_kernel(1, enemies[i].y, enemies[i].x, enemies[i].enemyARight, enemies[i].reg);
                            enemies[i].enemyARight = (enemies[i].enemyARight == 14) ? 15 : 14;
                        }
                        else
                        {
                            write_sprite_to_kernel(1, enemies[i].y, enemies[i].x, enemies[i].enemyALeft, enemies[i].reg);
                            enemies[i].enemyALeft = (enemies[i].enemyALeft == 16) ? 17 : 16;
                        }
                    }
                    else
                    {
                        if (enemies[i].facingRight)
                        {
                            write_sprite_to_kernel(1, enemies[i].y, enemies[i].x, enemies[i].enemyBRight, enemies[i].reg);
                            enemies[i].enemyBRight = (enemies[i].enemyBRight == 21) ? 22 : 21;
                        }
                        else
                        {
                            write_sprite_to_kernel(1, enemies[i].y, enemies[i].x, enemies[i].enemyBLeft, enemies[i].reg);
                            enemies[i].enemyBLeft = (enemies[i].enemyBLeft == 23) ? 24 : 23;
                        }
                    }
                }
            }

            // draw bubble
            for (int i = 0; i < MAX_BUBBLES; ++i)
            {
                if (bubbles[i].active)
                {
                    write_sprite_to_kernel(1, bubbles[i].y, bubbles[i].x, 0, bubbles[i].reg);
                }
            }

            for (int i = 0; i < MAX_ENEMIES; ++i)
            {
                if (reward[i].active)
                {
                    // draw reward
                    write_sprite_to_kernel(1, reward[i].y, reward[i].x, reward[i].seq, reward[i].reg);
                }
            }
            usleep(50000);
        }
        fill_sky_and_grass();
        clearSprites();
        if (life >= 1)
        {
            play_sfx(1);
            write_text("you", 3, 14, 16);
            write_text("win", 3, 14, 20);
        }
        else
        {
            play_sfx(4);
            write_text("game", 4, 14, 15);
            write_text("over", 4, 14, 20);
        }
        int index1 = 11;
        write_text("press", 5, 21, index1);
        write_text("a", 1, 21, index1 + 6);
        write_text("to", 2, 21, index1 + 8);
        write_text("restart", 7, 21, index1 + 11);

        write_text("press", 5, 23, index1 + 1);
        write_text("b", 1, 23, index1 + 7);
        write_text("to", 2, 23, index1 + 9);
        write_text("exit", 4, 23, index1 + 12);
        while (true)
        {
            if (controller_state.a == 1 || controller_state.b == 1)
            {
                if (controller_state.a == 1)
                {
                    restart = true;
                }
                else
                {
                    restart = false;
                }
                break;
            }
        }
        if (!restart)
        {
            break;
        }
        level = 0;
        numEnemy = MAX_ENEMIES;
        numOfReward = 0;
        grade = 0;
        life = 3;
    }
    pthread_join(controller_thread, NULL);
    bgm_startstop(0);
    printf("You exit\n");
    return 0;
}
