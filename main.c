#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>

#include <SDL/SDL.h>

// 
// Input Worker
// 

#define KEY_UP      0x01
#define KEY_DOWN    0x02
#define KEY_LEFT    0x04
#define KEY_RIGHT   0x08
#define KEY_DROP    0x10
#define KEY_CW      0x20
#define KEY_CCW     0x40
#define KEY_RESET   0x80
#define KEY_EXIT    0x8000

static uint16_t keystate;

void *input_loop(void *_) {
	static uint16_t local_keystate;

	while (1) {
		int c = getchar();

		switch (c) {
		case 'a': local_keystate &= ~KEY_UP;    break;
		case 'A': local_keystate |=  KEY_UP;    break;
		case 'b': local_keystate &= ~KEY_DOWN;  break;
		case 'B': local_keystate |=  KEY_DOWN;  break;
		case 'c': local_keystate &= ~KEY_LEFT;  break;
		case 'C': local_keystate |=  KEY_LEFT;  break;
		case 'd': local_keystate &= ~KEY_RIGHT; break;
		case 'D': local_keystate |=  KEY_RIGHT; break;
		case 'e': local_keystate &= ~KEY_DROP;  break;
		case 'E': local_keystate |=  KEY_DROP;  break;
		case 'f': local_keystate &= ~KEY_CW;    break;
		case 'F': local_keystate |=  KEY_CW;    break;
		case 'g': local_keystate &= ~KEY_CCW;   break;
		case 'G': local_keystate |=  KEY_CCW;   break;
		case 'h': local_keystate &= ~KEY_RESET; break;
		case 'H': local_keystate |=  KEY_RESET; break;
		case EOF: local_keystate |=  KEY_EXIT;  break;
		}

		keystate = local_keystate;

		if (local_keystate & KEY_EXIT) break;
	}

	pthread_exit(NULL);
}

//
// Game Logic
//

// Tetrominoes

#define TET_O 0
#define TET_T 1
#define TET_I 2
#define TET_S 3
#define TET_Z 4
#define TET_J 5
#define TET_L 6

int __tetromino[7][4] = {
	{ 0x0660, 0x0660, 0x0660, 0x0660 }, // O
	{ 0x4E00, 0x4640, 0x0E40, 0x4C40 }, // T
	{ 0x4444, 0x0F00, 0x4444, 0x0F00 }, // I
	{ 0xC600, 0x4C80, 0xC600, 0x4C80 }, // S
	{ 0x6C00, 0x8C40, 0x6C00, 0x8C40 }, // Z
	{ 0x4460, 0x0E80, 0xC440, 0x2E00 }, // J
	{ 0x44C0, 0x8E00, 0x6440, 0x0E20 }  // L
};

#define TET_HAS_BLOCK(piece, rot, i, j)\
	(__tetromino[piece][rot] & (1 << ((j) + (i) * 4)))

// Game State

#define WELL_H 18
#define WELL_W 10

static int well[WELL_H][WELL_W];

static int piece_x;
static int piece_y;
static int piece_rot;
static int piece;
static int level;
static int speed;
static int score;
static int tick;

int gameover = 0;

static void draw_block(int r, int c, int piece);

// Images and Drawing

static SDL_Surface *screen;

static SDL_Surface *well_image;
static SDL_Surface *block_image[7];

static void draw_block(int r, int c, int piece) {

	struct SDL_Rect rect = {
		.x = c * 20,
		.y = r * 20,
		.w = 20,
		.h = 20,
	};

	SDL_BlitSurface(block_image[piece], NULL, screen, &rect);
}

static void draw_bar() {

	struct SDL_Rect rect = {
		.x = 11,
		.y = 371,
		.w = 19 + score * 20,
		.h = 19,
	};

	if (gameover) {
		rect.w = 179;
		SDL_FillRect(screen, &rect, 0xCC0000);
	}
	else {
		SDL_FillRect(screen, &rect, 0x00CC00);
	}
}

static void redraw(void) {
	
	// draw well
	SDL_BlitSurface(well_image, NULL, screen, NULL);

	// draw well contents
	for (int r = 0; r < WELL_H; r++) {
		for (int c = 0; c < WELL_W; c++) {
			if (well[r][c] != -1) {
				draw_block(r, c, well[r][c]);
			}
		}
	}

	// draw piece	
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			if (TET_HAS_BLOCK(piece, piece_rot, i, j)) {
				if (i + piece_y >= 0 && i + piece_y < WELL_H && j + piece_x >= 0 && j + piece_x < WELL_W) {
					draw_block(i + piece_y, j + piece_x, piece);
				}
			}
		}
	}

	// draw progress bar
	draw_bar();

	SDL_Flip(screen);
}

/*
 * is_valid - determine whether a given next state is valid, i.e. has no 
 * overlapping blocks.
 *
 * well  - state of the well
 * piece - type of the current piece
 * rot   - rotation of the current piece
 * y     - y offset of the current piece
 * x     - x offset of the current piece
 *
 * Returns nonzero iff the state is valid.
 */
static int is_valid(int piece, int rot, int y, int x) {

	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			if (TET_HAS_BLOCK(piece, rot, i, j)) {
				if (i + y < 0 || i + y >= WELL_H) return 0; // check horizontal bounds
				if (j + x < 0 || j + x >= WELL_W) return 0; // check vertical bounds
				if (well[i + y][j + x] != -1) return 0;     // check for overlaps
			}
		}
	}

	return 1;
}

void move_down() {
	
	if (is_valid(piece, piece_rot, piece_y + 1, piece_x)) {
		piece_y++;
	}
	else {

		// copy piece into well
		for (int i = 0; i < 4; i++) {
			for (int j = 0; j < 4; j++) {
				if (TET_HAS_BLOCK(piece, piece_rot, i, j)) {
					well[i + piece_y][j + piece_x] = piece;
				}
			}
		}

		// check for completed lines
		for (int i = WELL_H - 1; i >= 0; i--) {
			int row = 1;
			for (int j = 0; j < WELL_W; j++) {
				if (well[i][j] == -1) {
					row = 0;
				}
			}
			if (row) {
				for (int i1 = i - 1; i1 >= 0; i1--) {
					for (int j = 0; j < WELL_W; j++) {
						well[i1+1][j] = well[i1][j];
					}
				}
				i++;

				score++;
				speed--;

				if (score == 8) {
					printf("!\n");
				}

				if (score > 8) score = 8;
			}
		}

		// generate new piece
		piece_y = 0;
		piece_x = (WELL_W / 2) - 2;
		piece_rot = rand() % 4;
		piece = rand() % 7;

		// check for game over
		if (!is_valid(piece, piece_rot, piece_y, piece_x)) {
			gameover = 1;
		}
	}
}

void move_left() {

	if (is_valid(piece, piece_rot, piece_y, piece_x - 1)) {
		piece_x--;
	}
}

void move_right() {
	
	if (is_valid(piece, piece_rot, piece_y, piece_x + 1)) {
		piece_x++;
	}
}

void move_cw() {
	
	if (is_valid(piece, (piece_rot - 1) & 3, piece_y, piece_x)) {
		piece_rot = (piece_rot - 1) & 3;
	}
}

void move_ccw() {
	
	if (is_valid(piece, (piece_rot + 1) & 3, piece_y, piece_x)) {
		piece_rot = (piece_rot + 1) & 3;
	}
}

void do_tick() {

	tick++;

	if (keystate & KEY_LEFT)  move_left();
	if (keystate & KEY_RIGHT) move_right();
	if (keystate & KEY_CCW)   move_ccw();
	if (keystate & KEY_CW)    move_cw();

	keystate &= ~(KEY_LEFT | KEY_RIGHT | KEY_CCW | KEY_CW);

	if ((tick % speed == 0) || (keystate & KEY_DROP)) {
		move_down();
	}
}

void *game_loop(void *_) {
	
	// initialize screen
	SDL_Init(SDL_INIT_VIDEO);
	screen = SDL_SetVideoMode(200, 400, 32, SDL_SWSURFACE);

	// load images
	well_image = SDL_LoadBMP("well.bmp");
	block_image[0] = SDL_LoadBMP("block0.bmp");
	block_image[1] = SDL_LoadBMP("block1.bmp");
	block_image[2] = SDL_LoadBMP("block2.bmp");
	block_image[3] = SDL_LoadBMP("block3.bmp");
	block_image[4] = SDL_LoadBMP("block4.bmp");
	block_image[5] = SDL_LoadBMP("block5.bmp");
	block_image[6] = SDL_LoadBMP("block6.bmp");

	reset:

	// initialize game state
	for (int r = 0; r < WELL_H; r++) {
		for (int c = 0; c < WELL_W; c++) {
			well[r][c] = -1;
		}
	}

	srand(time(NULL));

	piece_y = -1;
	piece_x = (WELL_W / 2) - 2;
	piece_rot = rand() % 4;
	piece = rand() % 7;

	level = 1;
	speed = 15;
	score = 0;

	while (1) {

		SDL_Delay(25);

		if (gameover) {
			if (keystate & KEY_RESET) {
				gameover = 0;
				goto reset;
			}

			if (keystate & KEY_EXIT) {
				break;
			}

			continue;
		}

		if (score == 8) {
			if (keystate & KEY_RESET) {
				goto reset;
			}

			if (keystate & KEY_EXIT) {
				break;
			}

			continue;
		}

		if (keystate & KEY_RESET) {
			goto reset;
		}

		if (keystate & KEY_EXIT) {
			break;
		}

		do_tick();

		redraw();
	}

	// free images
	SDL_FreeSurface(well_image);	

	// exit
	SDL_Quit();
	pthread_exit(NULL);
}

// 
// Entry
//

int main(int argc, char **argv) {

	pthread_t input_thread;
	pthread_t game_thread;

	pthread_create(&input_thread, NULL, input_loop, NULL);
	pthread_create(&game_thread, NULL, game_loop, NULL);

	pthread_exit(NULL);

	return 0;
}
