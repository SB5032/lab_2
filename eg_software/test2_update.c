/*
 * Userspace program that communicates with the aud and vga_bird device driver
 * through ioctls
 * Columbia University
 */

#include <stdio.h>
#include "interfaces.h"
#include "vga_bird.h"
#include "aud.h"
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

#define X_MAX 639 
#define Y_MAX 479
#define NEXT 14
#define CONFIRM 13
#define ARROW_INDEX 22
#define BIRD_ID 28

int vga_bird_fd;
int aud_fd;
FILE *log_file;

enum difficulty {EASY, MEDIUM, HARD};


int check_bird_pass_pipe(sprite *sprites, vga_pipe_position_t *pipe_info_first, vga_pipe_position_t *pipe_info_second) {
    int bird_x = sprites[15].x;
	int first_pipe_position_x = sprites[16].x;
	int second_pipe_position_x = sprites[29].x;
    int bird_passed = 1;

    // Check if the bird has passed the first pipe
    if ((pipe_info_first->pipe_num != 0 && bird_x == sprites[pipe_info_first->top_pipe_max_index].x + 32 )||(pipe_info_second->pipe_num != 0 && bird_x == sprites[pipe_info_second->top_pipe_max_index].x + 32)) {
        return bird_passed;
	}
    return 0;
}
void updateBall(sprite *obj) {
	obj->x += obj->dx;
	obj->y += obj->dy;
}

int check_bird_score(sprite *sprites, vga_pipe_position_t *pipe_info_second, int score){
	int first_pipe_position_x = sprites[16].x;
	int second_pipe_position_x = sprites[29].x;
	if (first_pipe_position_x == 200) {
		score++;
	} else if (second_pipe_position_x == 200) {
		score++;
	}
	return score;
}

int check_bird_position(sprite *sprites, vga_pipe_position_t *pipe_info_first, vga_pipe_position_t *pipe_info_second) {
	int bird_position_x = sprites[15].x;
	int first_pipe_position_x = sprites[16].x;
	int second_pipe_position_x = sprites[29].x;
	int second_top_pipe_position_y;
	int second_bottom_pipe_position_y;
	if (pipe_info_second->pipe_num != 0) {
		second_top_pipe_position_y = sprites[pipe_info_second->top_pipe_max_index].y;
		second_bottom_pipe_position_y = sprites[pipe_info_second->bottom_pipe_max_index].y;
	}
	int first_top_pipe_position_y = sprites[pipe_info_first->top_pipe_max_index].y;
	int first_bottom_pipe_position_y = sprites[pipe_info_first->bottom_pipe_max_index].y;

	int bird_position_y = sprites[15].y;
	if (bird_position_y >= 480 || bird_position_y <= 0) { 
		// if this bird hits the floor.
		printf("bird hits the floor.\n");
		return 1;
	}
	//top 4, bottom 5

	if (first_pipe_position_x > (bird_position_x - 32) && first_pipe_position_x < (bird_position_x + 32)) {
		if ((bird_position_y + 4) < (first_top_pipe_position_y + 32) || (bird_position_y + 32 - 5) > first_bottom_pipe_position_y) {
			printf("bird hits the first pipe.\n");
			printf("pipe info first top pipe max index: %d, bottom pipe max index: %d\n", pipe_info_first->top_pipe_max_index, pipe_info_first->bottom_pipe_max_index);
			printf("bird position x: %d, y: %d\n", bird_position_x, bird_position_y);
			printf("first pipe position x: %d, top y: %d, bottom y: %d\n", first_pipe_position_x, first_top_pipe_position_y, first_bottom_pipe_position_y);
			aud_mem_t c;
			c.sound = 2;
			printf("send sound\n");
			send_sound(&c, aud_fd);
			return 1;
		}
	}

	if (pipe_info_second->pipe_num != 0) {
		if (second_pipe_position_x > (bird_position_x - 32) && second_pipe_position_x < (bird_position_x + 32)) {
			if ((bird_position_y + 4) < (second_top_pipe_position_y + 32) || (bird_position_y + 32 - 5) > second_bottom_pipe_position_y) {
				printf("bird hits the second pipe.\n");
				printf("pipe info second top pipe max index: %d, bottom pipe max index: %d\n", pipe_info_second->top_pipe_max_index, pipe_info_second->bottom_pipe_max_index);
				printf("bird position x: %d, y: %d\n", bird_position_x, bird_position_y);
				printf("second pipe position x: %d, top y: %d, bottom y: %d\n", second_pipe_position_x, second_top_pipe_position_y, second_bottom_pipe_position_y);
				aud_mem_t c;
				c.sound = 2;
				printf("send sound\n");
				send_sound(&c, aud_fd);
				return 1;
			}
		}
	}



	return 0;
}


// The idea is that every pipe will be at most 13 long, so the sprites will be from 16 to 28. Another will be from 29 to 41.
void create_pipe(sprite *sprites, vga_pipe_position_t *pipe_info, int pipe_index_start, int difficulty_level) {
	// head 30, tail 31.

	// use the sprites starts from 16
	// first do the down pipe.
	int dx = -1;
	if (difficulty_level == EASY) {
		dx = -1;
	} else if (difficulty_level == MEDIUM) {
		dx = -2;
	} else if (difficulty_level == HARD) {
		dx = -3;
	}
	int pipe_num;
	int top_pipe_num;
	int bottom_pipe_num;
	pipe_num = 6 + rand() % (10 - 6 + 1);
	top_pipe_num = 2 + rand() % (pipe_num - 6 + 1);
	//every pipe has at least 3 with at least one top and one bottom.
	bottom_pipe_num = pipe_num - top_pipe_num;
	if (difficulty_level == EASY) {
		pipe_num = 6 + rand() % (10 - 6 + 1);
		top_pipe_num = 2 + rand() % (pipe_num - 6 + 1);
		//every pipe has at least 3 with at least one top and one bottom.
		bottom_pipe_num = pipe_num - top_pipe_num;
		dx = -1;
	} else if (difficulty_level == MEDIUM) {
		pipe_num = 7 + rand() % (11 - 7 + 1);
		top_pipe_num = 2 + rand() % (pipe_num - 6 + 1);
		bottom_pipe_num = pipe_num - top_pipe_num;
		dx = -2;
	} else if (difficulty_level == HARD) {
		pipe_num = 8 + rand() % (10 - 6 + 1);
		top_pipe_num = 2 + rand() % (pipe_num - 8 + 1);
		//every pipe has at least 3 with at least one top and one bottom.
		bottom_pipe_num = pipe_num - top_pipe_num;
		dx=-4;
	}

	// for (int i = 0; i < pipe_num; i++){
	// 	sprites[16+i].id = 30;
	// }
	// add the refresh part.
	for (int i = pipe_index_start; i < pipe_index_start+13; i++) {
		sprites[i].x = 630; 
		sprites[i].y = 470;
		sprites[i].dx = 0;  
		sprites[i].dy = 0; 
		sprites[i].id = 0;
		sprites[i].hit = 1;
		sprites[i].index = i;
	}

	for (int i = 0; i < top_pipe_num - 1; i++) {
		sprites[pipe_index_start+i].id = 30;
		sprites[pipe_index_start+i].x = 480 - 32;
		sprites[pipe_index_start+i].y = 32 * i;
		sprites[pipe_index_start+i].dx = dx;
		sprites[pipe_index_start+i].dy = 0;
		sprites[pipe_index_start+i].hit = 1;
		sprites[pipe_index_start+i].index = pipe_index_start+i;
	}

	//head.
	sprites[pipe_index_start+top_pipe_num-1].id = 29;
	sprites[pipe_index_start+top_pipe_num-1].x = 480 - 32;
	sprites[pipe_index_start+top_pipe_num-1].y = 32 * (top_pipe_num - 1);
	sprites[pipe_index_start+top_pipe_num-1].dx = dx;
	sprites[pipe_index_start+top_pipe_num-1].dy = 0;
	sprites[pipe_index_start+top_pipe_num-1].hit = 1;
	sprites[pipe_index_start+top_pipe_num-1].index = pipe_index_start+top_pipe_num-1;
	printf("top pipe index: %d\n", pipe_index_start+top_pipe_num-1);
	printf("top pipe y is: %d\n", sprites[pipe_index_start+top_pipe_num-1].y);

	for (int i = 0; i < bottom_pipe_num-1; i++) {
		sprites[pipe_index_start+top_pipe_num+i].id = 30;
		sprites[pipe_index_start+top_pipe_num+i].x = 480 - 32;
		sprites[pipe_index_start+top_pipe_num+i].y = 480 - 32 - 32 * i;
		sprites[pipe_index_start+top_pipe_num+i].dx = dx;
		sprites[pipe_index_start+top_pipe_num+i].dy = 0;
		sprites[pipe_index_start+top_pipe_num+i].hit = 1;
		sprites[pipe_index_start+top_pipe_num+i].index = pipe_index_start+top_pipe_num+i;
	}

	sprites[pipe_index_start+top_pipe_num+bottom_pipe_num-1].id = 29;
	sprites[pipe_index_start+top_pipe_num+bottom_pipe_num-1].x = 480 - 32;
	sprites[pipe_index_start+top_pipe_num+bottom_pipe_num-1].y = 480 - 32 - 32 * (bottom_pipe_num - 1);
	sprites[pipe_index_start+top_pipe_num+bottom_pipe_num-1].dx = dx;
	sprites[pipe_index_start+top_pipe_num+bottom_pipe_num-1].dy = 0;
	sprites[pipe_index_start+top_pipe_num+bottom_pipe_num-1].hit = 1;
	sprites[pipe_index_start+top_pipe_num+bottom_pipe_num-1].index = pipe_index_start+top_pipe_num+bottom_pipe_num-1;
	printf("bottom pipe index: %d\n", pipe_index_start+top_pipe_num+bottom_pipe_num-1);
	printf("bottom pipe y is: %d\n", sprites[pipe_index_start+top_pipe_num+bottom_pipe_num-1].y);
	pipe_info->pipe_num = pipe_num;
	pipe_info->top_pipe_max_index = pipe_index_start+top_pipe_num-1;
	pipe_info->bottom_pipe_max_index = pipe_index_start+top_pipe_num+bottom_pipe_num-1;
}


void create_pipe_condition(sprite *sprites, vga_pipe_position_t *pipe_info_first, vga_pipe_position_t *pipe_info_second, int difficulty_level) {
	int first_pipe_position_x = sprites[16].x;
	int second_pipe_position_x = sprites[29].x;
	if (first_pipe_position_x == 224) {
		printf("second pipe position x is created.\n");
		create_pipe(sprites, pipe_info_second, 29, difficulty_level);
	} else if (second_pipe_position_x == 224) {
		printf("first pipe position x is created.\n");
		create_pipe(sprites, pipe_info_first, 16, difficulty_level);
	}
}
void menu_setup(sprite *sprites){
	sprites[1].id = 20; //M 20
	sprites[2].id = 18; //E 18
	sprites[3].id = 25; //N  
	sprites[4].id = 19; //U  

	for (int i = 1; i < 5; i++) {
		sprites[i].x = 108 + 32*(i-1); 
		sprites[i].y = 120;
		sprites[i].dx = 0;  
		sprites[i].dy = 0; 
		sprites[i].hit = 1;
		sprites[i].index = i;
	}

	// START
	sprites[5].id = 22; //S
	sprites[6].id = 23; //T 
	sprites[7].id = 33; //A
	sprites[8].id = 34; //R
	sprites[9].id = 23; //T  

	for (int i = 5; i < 10; i++) {
		sprites[i].x = 140 + 32*(i-5); 
		sprites[i].y = 180;
		sprites[i].dx = 0;  
		sprites[i].dy = 0; 
		sprites[i].hit = 1;
		sprites[i].index = i;
	}
	
	// SETTING
	sprites[10].id = 22; //S
	sprites[11].id = 18; //E
	sprites[12].id = 23; //T  
	sprites[13].id = 23; //T 
	sprites[14].id = 24; //I 
	sprites[15].id = 25; //N 
	sprites[16].id = 31; //G  

	for (int i = 10; i < 17; i++) {
		sprites[i].x = 140 + 32*(i-10); 
		sprites[i].y = 240;
		sprites[i].dx = 0;  
		sprites[i].dy = 0; 
		sprites[i].hit = 1;
		sprites[i].index = i;
	}

	//add a arrow
	// T I N G U
	//arrow index will always be 22
	// sprites[ARROW_INDEX].id = 28; // Arrow is bird now
	// sprites[ARROW_INDEX].x = 370;
	// sprites[ARROW_INDEX].y = 180;
	// sprites[ARROW_INDEX].dx = 0;  
	// sprites[ARROW_INDEX].dy = 0; 
	// sprites[ARROW_INDEX].hit = 1;
	// sprites[ARROW_INDEX].index = ARROW_INDEX;
}

void scorecombosetup(sprite *sprites) {
	//index 0 acts strangly
	//'SCORE'
	sprites[1].id = 17; //S
	sprites[2].id = 12; //C
	sprites[3].id = 15; //O
	sprites[4].id = 16; //R
	sprites[5].id = 13; //E
	for (int i = 1; i < 6; i++) {
		sprites[i].x = 480+32*(i-1); 
		sprites[i].y = 40;
		sprites[i].dx = 0;  
		sprites[i].dy = 0; 
		sprites[i].hit = 1;
		sprites[i].index = i;
	}
	sprites[6].id = 10; //0
	sprites[7].id = 10; //0
	sprites[8].id = 10; //0
	for (int i = 6; i < 9; i++) {
		sprites[i].x = 480+32+32*(i-6); 
		sprites[i].y = 90;
		sprites[i].dx = 0;  
		sprites[i].dy = 0; 
		sprites[i].hit = 1;
		sprites[i].index = i;
	}

	
	//'MAX'
	sprites[9].id = 14; //M
	sprites[10].id = 26; //A
	sprites[11].id = 27; //X
	for (int i = 9; i < 12; i++) {
		sprites[i].x = 480+32+32*(i-9); 
		sprites[i].y = 240;
		sprites[i].dx = 0;  
		sprites[i].dy = 0; 
		sprites[i].hit = 1;
		sprites[i].index = i;
	}
	int max = read_max_from_file();
	if(max>0){
		update_max(sprites, max);
	}
	else{
	sprites[12].id = 10; //0
	sprites[13].id = 10; //0
	sprites[14].id = 10; //0
	}
	for (int i = 12; i < 15; i++) {
		sprites[i].x = 480+32+32*(i-12); 
		sprites[i].y = 290;
		sprites[i].dx = 0;  
		sprites[i].dy = 0; 
		sprites[i].hit = 1;
		sprites[i].index = i;
	}
	sprites[15].id = BIRD_ID; 
	sprites[15].x = 224; 
	sprites[15].y = 240;
	sprites[15].dx = 0;  
	sprites[15].dy = 0; 
	sprites[15].hit = 1;
	sprites[15].index = 15;
}

// void create_bird(sprite *sprites){
// 	sprites[15].id = BIRD_ID; 
// 	sprites[15].x = 240; 
// 	sprites[15].y = 240;
// 	sprites[15].dx = 0;  
// 	sprites[15].dy = 0; 
// 	sprites[15].hit = 1;
// 	sprites[15].index = 15;
// }



void setting_setup(sprite *sprites){
	// we can change the index starting from 42
	// SETTING
		// SETTING
	sprites[1].id = 22; //S
	sprites[2].id = 18; //E
	sprites[3].id = 23; //T  
	sprites[4].id = 23; //T  
	sprites[5].id = 24; //I  
	sprites[6].id = 25; //N  
	sprites[7].id = 31; //G

	for (int i = 1; i < 8; i++) {
		sprites[i].x = 108 + 32*(i-1); 
		sprites[i].y = 120;
		sprites[i].dx = 0;  
		sprites[i].dy = 0; 
		sprites[i].hit = 1;
		sprites[i].index = i;
	}

	// EASY
	sprites[8].id = 18;  //E
	sprites[9].id = 33;  //A
	sprites[10].id = 22; //S
	sprites[11].id = 35; //Y No this one

	for (int i = 8; i < 12; i++) {
		sprites[i].x = 140 + 32*(i-8); 
		sprites[i].y = 180;
		sprites[i].dx = 0;  
		sprites[i].dy = 0; 
		sprites[i].hit = 1;
		sprites[i].index = i;
	}

	//MEDIUM
	sprites[12].id = 20; //M
	sprites[13].id = 18; //E
	sprites[14].id = 36; //D 
	sprites[15].id = 24; //I  
	sprites[16].id = 19; //U 
	sprites[17].id = 20; //M

	for (int i = 12; i < 18; i++) {
		sprites[i].x = 140 + 32*(i-12); 
		sprites[i].y = 240;
		sprites[i].dx = 0;  
		sprites[i].dy = 0; 
		sprites[i].hit = 1;
		sprites[i].index = i;
	}

	//HARD
	sprites[18].id = 37; //H 
	sprites[19].id = 33; //A
	sprites[20].id = 34; //R 
	sprites[21].id = 36; //D 

	for (int i = 18; i < 22; i++) {
		sprites[i].x = 140 + 32*(i-18); 
		sprites[i].y = 300;
		sprites[i].dx = 0;  
		sprites[i].dy = 0; 
		sprites[i].hit = 1;
		sprites[i].index = i;
	}

	// sprites[ARROW_INDEX].id = 28; // Arrow
	// sprites[ARROW_INDEX].x = 96;
	// sprites[ARROW_INDEX].y = 180;
	// sprites[ARROW_INDEX].dx = 0;  
	// sprites[ARROW_INDEX].dy = 0; 
	// sprites[ARROW_INDEX].hit = 1;
	// sprites[ARROW_INDEX].index = ARROW_INDEX;

}

void gameoversetup(sprite *sprites){
	//GAMEOVER
	sprites[1].id = 31; //G
	sprites[2].id = 33; //A
	sprites[3].id = 20; //M
	sprites[4].id = 18; //E
	sprites[5].id = 38; //O
	sprites[6].id = 21; //V
	sprites[7].id = 18; //E
	sprites[8].id = 34; //R
	for (int i = 1; i < 9; i++) {
		sprites[i].x = 120+32*(i-1); 
		sprites[i].y = 120;
		sprites[i].dx = 0;  
		sprites[i].dy = 0; 
		sprites[i].hit = 1;
		sprites[i].index = i;
	}

	sprites[9].id = 32; //dead bird
	sprites[9].x = 224;
	sprites[9].y = 240;
	sprites[9].dx = 0;
	sprites[9].dy = 0;
	sprites[9].hit = 1;
	sprites[9].index = 9;
}

void update_score(sprite *sprites, const int score) {
	int huds = (int)score/100;
	int tens = (int)(score - huds*100)/10;
	int ones = score - huds*100 - tens*10;
	if (huds == 0) huds = 10;
	if (tens == 0) tens = 10;
	if (ones == 0) ones = 10;
	sprites[6].id = huds; //100s	
	sprites[7].id = tens; //10s
	sprites[8].id = ones; //1s
	return;
}

void update_max(sprite *sprites, const int max) {
	int huds = (int)max/100;
	int tens = (int)(max - huds*100)/10;
	int ones = max - huds*100 - tens*10;
	if (huds == 0) huds = 10;
	if (tens == 0) tens = 10;
	if (ones == 0) ones = 10;
	sprites[12].id = huds; //100s	
	sprites[13].id = tens; //10s
	sprites[14].id = ones; //1s
	return;
}
// dedicate all sprites below 



void screen_refresh(sprite* sprites) {
   for (int i = 1; i < SIZE; i++) {
		sprites[i].x = 630; 
		sprites[i].y = 470;
		sprites[i].dx = 0;  
		sprites[i].dy = 0; 
		sprites[i].id = 0;
		sprites[i].hit = 1;
		sprites[i].index = i;
	}
	return;
}

int check_receive_audio(int counter, float* sum_audio_data, int aud_fd, aud_mem_t* amt) {
	amt->data = get_aud_data(aud_fd);
	//printf("amt=%d\n",amt->data);
	if ((counter%10)==0) {
		if (*sum_audio_data / 10 > 50000000){		
			printf("after ten counter, with hit, the mean is: %f\n", (*sum_audio_data/10));
			*sum_audio_data = 0;
			return 1;
		}
		//printf("after ten counter, without hit, the mean is: %f\n", (*sum_audio_data/10));
		*sum_audio_data = 0;
		return 0;
	} else{
		*sum_audio_data += abs(amt->data);
		return 0;
	}
	return 0;
}

int return_operation(int aud_fd) {
	int button_value = get_button_value(aud_fd);
	// value it 15
	if (button_value == NEXT){
		return NEXT;
	} else if (button_value == CONFIRM){
		return CONFIRM;
	} else {
		return 0;
	}
	return 0;
}

int get_arrow_position_y(sprite *sprites){
	return sprites[ARROW_INDEX].y;
}

void add_arrow_position_y(sprite *sprites, int y){
	sprites[ARROW_INDEX].y += y;
	//printf("Arrow pos is: %d\n", sprites[ARROW_INDEX].y );
}

void send_to_hardware(sprite *sprites, int vga_bird_fd, vga_bird_data_t *vzdt) {
	for (int i = 0; i < SIZE; i++) {
		vzdt->data[i] = (sprites[i].index<<26) + (sprites[i].id<<20) + (sprites[i].y<<10) + (sprites[i].x<<0);
	}
	send_sprite_positions(&vzdt, vga_bird_fd);
}

int set_difficulty_level(sprite *sprites) {
	if (get_arrow_position_y(sprites) == 180) {
		
		return EASY;
	} else if (get_arrow_position_y(sprites) == 240) {
		return MEDIUM;
	} else if (get_arrow_position_y(sprites) == 300) {
		return HARD;
	}
	return EASY;
}
int read_max_from_file(enum difficulty difficulty) {
    int max = 0;
    FILE *file = NULL;

    // Determine the file name based on the difficulty level
    switch (difficulty) {
        case EASY:
            file = fopen("max_score_easy.txt", "r");
            break;
        case MEDIUM:
            file = fopen("max_score_medium.txt", "r");
            break;
        case HARD:
            file = fopen("max_score_hard.txt", "r");
            break;
        default:
            return -1;  // Return error if difficulty is not recognized
    }

    // Check if the file was opened successfully
    if (file == NULL) {
        return -1;  // Return -1 or another error code if file can't be opened
    }

    // Read the max score from the file
    fscanf(file, "%d", &max);
    fclose(file);  // Close the file
    return max;
}
void save_max_to_file(int max, enum difficulty difficulty) {
	FILE *file = NULL;
    // Select the appropriate file based on the difficulty level
    switch (difficulty) {
        case EASY:
            file = fopen("max_score_easy.txt", "w");
            break;
        case MEDIUM:
            file = fopen("max_score_medium.txt", "w");
            break;
        case HARD:
            file = fopen("max_score_hard.txt", "w");
            break;
        default:
            printf("Invalid difficulty level!\n");
            return;
    }

    // Check if the file was opened successfully
    if (file == NULL) {
        printf("Error opening file!\n");
        return;
    }

    // Write the max score to the file
    fprintf(file, "%d", max);
    fclose(file);  // Close the file
}

// simple game of hitting random falling notes when they reach the green zone
int main()
{
	log_file = fopen("log.txt", "w");
	if (log_file == NULL) {
        printf("Failed to open the file.\n");
        return 1;
    }
	vga_bird_arg_t vzat;

	aud_arg_t aat;
	aud_mem_t amt;

	vga_pipe_position_t pipe_info_first, pipe_info_second;

	int signal;
	int pipe_num;

	float sum_audio_data;

	srand(time(NULL));

	static const char filename1[] = "/dev/vga_bird";
	static const char filename2[] = "/dev/aud";

	printf("VGA Bird test Userspace program started\n");
	printf("%d\n", sizeof(int));	
	printf("%d\n", sizeof(short));

	if ((vga_bird_fd = open(filename1, O_RDWR)) == -1) {
		fprintf(stderr, "could not open %s\n", filename1);
		return -1;
	}
	if ((aud_fd = open(filename2, O_RDWR)) == -1) {
		fprintf(stderr, "could not open %s\n", filename2);
		return -1;
	}
 	//FILE *fp = fopen("test.txt", "w");
	//if (fp == NULL)	return -1;
	vga_bird_data_t vzdt;
	sprite *sprites = NULL;	
	sprites = calloc(SIZE, sizeof(*sprites));
	int max = 0;
	int arr_y=60;
	enum difficulty difficulty_level = EASY;
	int arrow_created;
program_start:
	// Just for testing.
	//  for (;;){
	// 	screen_refresh(sprites);
	//  	setting_setup(sprites);
		
	// 	if (return_operation(aud_fd) == CONFIRM){
	// 		break;
	// 	}

	// 	for (int i = 0; i < SIZE; i++) {
	// 		vzdt.data[i] = (sprites[i].index<<26) + (sprites[i].id<<20) + (sprites[i].y<<10) + (sprites[i].x<<0);
	// 	}
	// 	//send package to hardware
	// 	send_sprite_positions(&vzdt, vga_bird_fd);
	//  }
	arrow_created = 0;
	screen_refresh(sprites);
	menu_setup(sprites);
	for (;;){
		if (!arrow_created){
			sprites[ARROW_INDEX].id = 28; // Arrow is bird now
			sprites[ARROW_INDEX].x = 370;
			sprites[ARROW_INDEX].y = 180;
			sprites[ARROW_INDEX].dx = 0;  
			sprites[ARROW_INDEX].dy = 0; 
			sprites[ARROW_INDEX].hit = 1;
			sprites[ARROW_INDEX].index = ARROW_INDEX;
		}
		arrow_created = 1;
	 	if (return_operation(aud_fd) == CONFIRM && get_arrow_position_y(sprites) == 180) {
	 		printf("game start.\n");
	 		break;
	 	} else if (return_operation(aud_fd) == CONFIRM && get_arrow_position_y(sprites) == 240) {
	 		printf("setting start.\n");
	 		screen_refresh(sprites);
	 		setting_setup(sprites);
			int setting_arrow_created;
			setting_arrow_created = 0;
	 		for (;;){
				usleep(200000);
				if (!setting_arrow_created){
					sprites[ARROW_INDEX].id = 28; // Arrow is bird now
					sprites[ARROW_INDEX].x = 370;
					sprites[ARROW_INDEX].y = 180;
					sprites[ARROW_INDEX].dx = 0;  
					sprites[ARROW_INDEX].dy = 0; 
					sprites[ARROW_INDEX].hit = 1;
					sprites[ARROW_INDEX].index = ARROW_INDEX;
				}
				setting_arrow_created = 1;
	 			if (return_operation(aud_fd) == CONFIRM) {
	 				difficulty_level = set_difficulty_level(sprites);
					screen_refresh(sprites);
					menu_setup(sprites);
					arrow_created = 0;
	 				break;
	 			} else if (return_operation(aud_fd) == NEXT) {
	 				add_arrow_position_y(sprites, 60);
	 				if (get_arrow_position_y(sprites) == 360) {
	 					add_arrow_position_y(sprites, -180);
	 				}
	 			}
				for (int i = 0; i < SIZE; i++) {
					vzdt.data[i] = (sprites[i].index<<26) + (sprites[i].id<<20) + (sprites[i].y<<10) + (sprites[i].x<<0);
				}
				//send package to hardware
				send_sprite_positions(&vzdt, vga_bird_fd);
	 		}
	 	} else if (return_operation(aud_fd) == NEXT) {
	 		printf("next.\n");
	 		add_arrow_position_y(sprites, 60);
	 		if (get_arrow_position_y(sprites) == 300) {
	 			add_arrow_position_y(sprites, -120);
	 		}
	 	}
		usleep(200000);
	 	for (int i = 0; i < SIZE; i++) {
			vzdt.data[i] = (sprites[i].index<<26) + (sprites[i].id<<20) + (sprites[i].y<<10) + (sprites[i].x<<0);
		}
		//send package to hardware
		send_sprite_positions(&vzdt, vga_bird_fd);
	 }
	sum_audio_data = 0;
	screen_refresh(sprites);
	scorecombosetup(sprites);
	create_pipe(sprites, &pipe_info_first, 16, difficulty_level);

	//initialize pipe info second.
	pipe_info_second.pipe_num = 0;
	pipe_info_second.top_pipe_max_index = 0;
	pipe_info_second.bottom_pipe_max_index = 0;
	
	int score = 0;
	int combo = 0;
	//packet of sprite data to send
	int combo_flag = 1; 
	int counter = 0; 	
	int gamecounter = 0;
    int validleft, validright;
    int hitcount = 0;
	int noteCount = 0;  
	int MAX_NOTE_COUNT = 100;    
	for (;;) {
    	//printf("amt = %f\n", amt.data); 
		//printf("game difficulty level is: %d\n", difficulty_level);
		if (check_receive_audio(counter, &sum_audio_data, aud_fd, &amt)) {
			for (int j = 0; j < 20; j++){
				sprites[15].dy = -1;
				updateBall(&sprites[15]);
				
				vzdt.data[15] = (sprites[15].index<<26) + (sprites[15].id<<20) + (sprites[15].y<<10) + (sprites[15].x<<0);
				
				//send package to hardware
				send_sprite_positions(&vzdt, vga_bird_fd);
			}
			aud_mem_t c;
			c.sound = 1;
			printf("send bird flap sound\n");		
			send_sound(&c, aud_fd);
		} else{
			sprites[15].dy = 1;
			aud_mem_t c;
			c.sound = 0;
			send_sound(&c, aud_fd);
		}
 		
 		//update_score(sprites, amt.data);
		//update_combo(sprites, 1+(sprites[validleft].id-17)>>1);
		//update_score(sprites, score);
		//update_combo(sprites, combo);
		//update_max(sprites, max);
	
		//package the sprites together
		create_pipe_condition(sprites, &pipe_info_first, &pipe_info_second, difficulty_level);
		
		int bird_dead;
		bird_dead = check_bird_position(sprites, &pipe_info_first, &pipe_info_second);
		if (bird_dead) {
			printf("bird dead.\n");
			//reset the bird position.
			screen_refresh(sprites);
			gameoversetup(sprites);
			
			for (int i = 0; i < SIZE; i++) {
			vzdt.data[i] = (sprites[i].index<<26) + (sprites[i].id<<20) + (sprites[i].y<<10) + (sprites[i].x<<0);
			}
			//send package to hardware
			send_sprite_positions(&vzdt, vga_bird_fd);
			usleep(3000000);
			aud_mem_t c;
			c.sound = 0;
			printf("send sound\n");
			send_sound(&c, aud_fd);
			goto program_start;
		}
		int passed=check_bird_pass_pipe(sprites, &pipe_info_first, &pipe_info_second);
		if(passed){
			score += passed;
		}
		max=read_max_from_file(difficulty_level);
		if(score > max){
			max = score;
			save_max_to_file(max,difficulty_level);
		}
		update_score(sprites, score);
		update_max(sprites, max);
		for (int i = 0; i < SIZE; i++) {
			vzdt.data[i] = (sprites[i].index<<26) + (sprites[i].id<<20) + (sprites[i].y<<10) + (sprites[i].x<<0);
		}
		//send package to hardware
		send_sprite_positions(&vzdt, vga_bird_fd);

		updateBall(&sprites[15]);
		//update the pipe position;
		for (int i = 16; i < 42; i++) {
			updateBall(&sprites[i]);
		}
		combo_flag = 1;
		//pause to let hardware catch up
		counter++;
		//signal = 0;
		// aud_mem_t c;
		// c.sound = 0;
		// printf("send sound\n");
		// send_sound(&c, aud_fd);
		usleep(20000);
	}
	free (sprites);

	return 0;
}
