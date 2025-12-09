/**
 * No Net Dino Game
 * by David Dinnyes
 */

#undef F_CPU
#define F_CPU 16000000
#include "avr_mcu_section.h"
AVR_MCU(F_CPU, "atmega128");

#define	__AVR_ATmega128__	1
#include <avr/io.h>


// GENERAL INIT - USED BY ALMOST EVERYTHING ----------------------------------

static void port_init() {
	PORTA = 0b00011111;	DDRA = 0b01000000; // buttons & led
	PORTB = 0b00000000;	DDRB = 0b00000000;
	PORTC = 0b00000000;	DDRC = 0b11110111; // lcd
	PORTD = 0b11000000;	DDRD = 0b00001000;
	PORTE = 0b00100000;	DDRE = 0b00110000; // buzzer
	PORTF = 0b00000000;	DDRF = 0b00000000;
	PORTG = 0b00000000;	DDRG = 0b00000000;
}

// TIMER-BASED RANDOM NUMBER GENERATOR ---------------------------------------

static void rnd_init() {
	TCCR0 |= (1  << CS00);	// Timer 0 no prescaling (@FCPU)
	TCNT0 = 0; 				// init counter
}

// generate a value between 0 and max
static int rnd_gen(int max) {
	return TCNT0 % max;
}

// SOUND GENERATOR -----------------------------------------------------------

typedef struct {
	int freq;
	int length;
} tune_t;

static tune_t TUNE_START[] = { { 2000, 40 }, { 0, 0 } };
static tune_t TUNE_LEVELUP[] = { { 3000, 20 }, { 0, 0 } };
static tune_t TUNE_GAMEOVER[] = { { 1000, 200 }, { 1500, 200 }, { 2000, 400 }, { 0, 0 } };

static void play_note(int freq, int len) {
	for (int l = 0; l < len; ++l) {
		int i;
		PORTE = (PORTE & 0b11011111) | 0b00010000;	//set bit4 = 1; set bit5 = 0
		for (i=freq; i; i--);
		PORTE = (PORTE | 0b00100000) & 0b11101111;	//set bit4 = 0; set bit5 = 1
		for (i=freq; i; i--);
	}
}

static void play_tune(tune_t *tune) {
	while (tune->freq != 0) {
		play_note(tune->freq, tune->length);
		++tune;
	}
}

// BUTTON HANDLING -----------------------------------------------------------

#define BUTTON_NONE		0
#define BUTTON_CENTER	1
#define BUTTON_LEFT		4
#define BUTTON_RIGHT	5
#define BUTTON_UP		2
#define BUTTON_DOWN		3
static int button_accept = 1;

static int button_pressed() {
	// right
	if (!(PINA & 0b00001000) & button_accept) { // check state of button 1 and value of button_accept
		button_accept = 0; // button is pressed
		return BUTTON_RIGHT;
	}

	// up
	if (!(PINA & 0b00000001) & button_accept) { // check state of button 2 and value of button_accept
		button_accept = 0; // button is pressed
		return BUTTON_UP;
	}

	// down
	if (!(PINA & 0b00010000) & button_accept) { // check state of button 2 and value of button_accept
		button_accept = 0; // button is pressed
		return BUTTON_DOWN;
	}

	// center
	if (!(PINA & 0b00000100) & button_accept) { // check state of button 3 and value of button_accept
		button_accept = 0; // button is pressed
		return BUTTON_CENTER;
	}

	// left
	if (!(PINA & 0b00000010) & button_accept) { // check state of button 5 and value of button_accept
		button_accept = 0; // button is pressed
		return BUTTON_LEFT;
	}

	return BUTTON_NONE;
}

static void button_unlock() {
	//check state of all buttons
	if (
		((PINA & 0b00000001)
		|(PINA & 0b00000010)
		|(PINA & 0b00000100)
		|(PINA & 0b00001000)
		|(PINA & 0b00010000)) == 31)
	button_accept = 1; //if all buttons are released button_accept gets value 1
}

// LCD HELPERS ---------------------------------------------------------------

#define		CLR_DISP	    0x00000001
#define		DISP_ON		    0x0000000C
#define		DISP_OFF	    0x00000008
#define		CUR_HOME      0x00000002
#define		CUR_OFF 	    0x0000000C
#define   CUR_ON_UNDER  0x0000000E
#define   CUR_ON_BLINK  0x0000000F
#define   CUR_LEFT      0x00000010
#define   CUR_RIGHT     0x00000014
#define   CG_RAM_ADDR		0x00000040
#define		DD_RAM_ADDR	  0x00000080
#define		DD_RAM_ADDR2	0x000000C0

//#define		ENTRY_INC	    0x00000007	//LCD increment
//#define		ENTRY_DEC	    0x00000005	//LCD decrement
//#define		SH_LCD_LEFT	  0x00000010	//LCD shift left
//#define		SH_LCD_RIGHT	0x00000014	//LCD shift right
//#define		MV_LCD_LEFT	  0x00000018	//LCD move left
//#define		MV_LCD_RIGHT	0x0000001C	//LCD move right

static void lcd_delay(unsigned int b) {
	volatile unsigned int a = b;
	while (a)
		a--;
}

static void lcd_pulse() {
	PORTC = PORTC | 0b00000100;	//set E to high
	lcd_delay(1400); 			//delay ~110ms
	PORTC = PORTC & 0b11111011;	//set E to low
}

static void lcd_send(int command, unsigned char a) {
	unsigned char data;

	data = 0b00001111 | a;					//get high 4 bits
	PORTC = (PORTC | 0b11110000) & data;	//set D4-D7
	if (command)
		PORTC = PORTC & 0b11111110;			//set RS port to 0 -> display set to command mode
	else
		PORTC = PORTC | 0b00000001;			//set RS port to 1 -> display set to data mode
	lcd_pulse();							//pulse to set D4-D7 bits

	data = a<<4;							//get low 4 bits
	PORTC = (PORTC & 0b00001111) | data;	//set D4-D7
	if (command)
		PORTC = PORTC & 0b11111110;			//set RS port to 0 -> display set to command mode
	else
		PORTC = PORTC | 0b00000001;			//set RS port to 1 -> display set to data mode
	lcd_pulse();							//pulse to set d4-d7 bits
}

static void lcd_send_command(unsigned char a) {
	lcd_send(1, a);
}

static void lcd_send_data(unsigned char a) {
	lcd_send(0, a);
}

static void lcd_init() {
	//LCD initialization
	//step by step (from Gosho) - from DATASHEET

	PORTC = PORTC & 0b11111110;

	lcd_delay(10000);

	PORTC = 0b00110000;				//set D4, D5 port to 1
	lcd_pulse();					//high->low to E port (pulse)
	lcd_delay(1000);

	PORTC = 0b00110000;				//set D4, D5 port to 1
	lcd_pulse();					//high->low to E port (pulse)
	lcd_delay(1000);

	PORTC = 0b00110000;				//set D4, D5 port to 1
	lcd_pulse();					//high->low to E port (pulse)
	lcd_delay(1000);

	PORTC = 0b00100000;				//set D4 to 0, D5 port to 1
	lcd_pulse();					//high->low to E port (pulse)

	lcd_send_command(0x28); // function set: 4 bits interface, 2 display lines, 5x8 font
	lcd_send_command(DISP_OFF); // display off, cursor off, blinking off
	lcd_send_command(CLR_DISP); // clear display
	lcd_send_command(0x06); // entry mode set: cursor increments, display does not shift

	lcd_send_command(DISP_ON);		// Turn ON Display
	lcd_send_command(CLR_DISP);		// Clear Display
}

static void lcd_send_text(char *str) {
	while (*str)
		lcd_send_data(*str++);
}

static void lcd_send_line1(char *str) {
	lcd_send_command(DD_RAM_ADDR);
	lcd_send_text(str);
}

static void lcd_send_line2(char *str) {
	lcd_send_command(DD_RAM_ADDR2);
	lcd_send_text(str);
}

// SPEED LEVELS --------------------------------------------------------------

typedef struct {
	int	delay;
	int rows;
} level_t;

#define LEVEL_NUM 3
static level_t LEVELS[] = { { 5, 5 }, { 3, 10 }, { 1, 30 } };
static int level_current = 0;
static int delay_cycle;

static void row_removed() {
	// do nothing if already at top speed
	if (level_current == LEVEL_NUM-1)
		return;

	// if enough rows removed, increase speed
	if (--LEVELS[level_current].rows == 0) {
		++level_current;
		play_tune(TUNE_LEVELUP);
	}
}

// PATTERNS AND PLAYFIELD ----------------------------------------------------
/* Vertical axis: 0 is top row, increments downwards
 * Horizontal axis: 0 is right column, increments leftwards */

#define PATTERN_NUM 2
#define PATTERN_SIZE 1
static unsigned char PATTERNS[PATTERN_NUM][PATTERN_SIZE] = {
    { 1 },   // cactus
    { 2 }   // bird
};

/* Obstacle system: multiple obstacles */
#define OBSTACLE_MAX 4   // tetszés szerint növelhető (figyelj a kijelzőre!)

static unsigned char obstacle_pattern[OBSTACLE_MAX]; // 1 vagy 2
static int obstacle_col[OBSTACLE_MAX];
static int obstacle_row[OBSTACLE_MAX];
static unsigned char obstacle_active[OBSTACLE_MAX];

static unsigned char CACTUS[8] = {
    0b00100,
    0b01110,
    0b00100,
    0b00101,
    0b11111,
    0b10100,
    0b00100,
    0b00100
};

static unsigned char BIRD[8] ={
    0b00000,
    0b01110,
    0b10101,
    0b10001,
    0b10001,
    0b00000,
    0b00000,
    0b00000
};

static unsigned char DINO[8] = {
    0b00100,
    0b01110,
    0b01111,
    0b00110,
    0b01111,
    0b11100,
    0b00100,
    0b00000
};

// Actually, 1 row taller and 2 columns wider, which extras are filled with ones to help collision detection
#define PLAYFIELD_ROWS	16
#define PLAYFIELD_COLS	4
static unsigned char playfield[PLAYFIELD_ROWS + 1];

static void playfield_clear() {
	for (int r = 0; r < PLAYFIELD_ROWS; ++r)
		playfield[r] = 0b100001;
	playfield[PLAYFIELD_ROWS] = 0b111111;
}

int dino_col_position = 2;
int dino_row_position = 1;
int dino_jump = 0;
int air_time = 0;

static void obstacles_init() {
	for (int i = 0; i < OBSTACLE_MAX; ++i) {
		obstacle_active[i] = 0;
		obstacle_col[i] = 0;
		obstacle_row[i] = 0;
		obstacle_pattern[i] = 0;
	}
}

// spawn első szabad helyre
static void spawn_obstacle() {
	for (int i = 0; i < OBSTACLE_MAX; ++i) {
		if (!obstacle_active[i]) {
			int p = rnd_gen(100) > 50 ? 1 : 0; // 0=cactus,1=bird
			obstacle_pattern[i] = PATTERNS[p][0];
			obstacle_col[i] = PLAYFIELD_ROWS - 1; // 15 (jobb oldalon)
			obstacle_row[i] = (p == 1) ? 0 : 1; // bird -> top (0), cactus -> bottom (1)
			obstacle_active[i] = 1;
			break;
		}
	}
}

int dino_collision() {
	for (int i = 0; i < OBSTACLE_MAX; ++i) {
		if (!obstacle_active[i]) continue;
		if (dino_col_position == obstacle_col[i] && dino_row_position == obstacle_row[i])
			return 1;
	}
	return 0;
}

// GRAPHICS ------------------------------------------------------------------

#define CHAR_EMPTY_PATTERN			0
#define CHAR_EMPTY_PLAYGROUND		1
#define CHAR_PATTERN_EMPTY			2
#define CHAR_PATTERN_PATTERN		3
#define CHAR_PATTERN_PLAYGROUND		4
#define CHAR_PLAYGROUND_EMPTY		5
#define CHAR_PLAYGROUND_PATTERN		6
#define CHAR_PLAYGROUND_PLAYGROUND	7
#define CHAR_EMPTY_EMPTY			' '
#define CHAR_ERROR					'X'

#define CHARMAP_SIZE 8
static unsigned char CHARMAP[CHARMAP_SIZE][8] = {
	{ 0b10101, 0b01010, 0b10101, 0b01010, 0, 0, 0, 0 },							// CHAR_EMPTY_PATTERN
	{ 0b11111, 0b11111, 0b11111, 0b11111, 0, 0, 0, 0 },							// CHAR_EMPTY_PLAYGROUND
	{ 0, 0, 0, 0, 0b10101, 0b01010, 0b10101, 0b01010 },							// CHAR_PATTERN_EMPTY
	{ 0b10101, 0b01010, 0b10101, 0b01010, 0b10101, 0b01010, 0b10101, 0b01010 },	// CHAR_PATTERN_PATTERN
	{ 0b11111, 0b11111, 0b11111, 0b11111, 0b10101, 0b01010, 0b10101, 0b01010 },	// CHAR_PATTERN_PLAYGROUND
	{ 0, 0, 0, 0, 0b11111, 0b11111, 0b11111, 0b11111 },							// CHAR_PLAYGROUND_EMPTY
	{ 0b10101, 0b01010, 0b10101, 0b01010, 0b11111, 0b11111, 0b11111, 0b11111 },	// CHAR_PLAYGROUND_PATTERN
	{ 0b11111, 0b11111, 0b11111, 0b11111, 0b11111, 0b11111, 0b11111, 0b11111 }	// CHAR_PLAYGROUND_PLAYGROUND
};

static const unsigned char XLAT_PATTERN[] = { 0b0000, 0b0001, 0b0100, 0b0101 };
static const unsigned char XLAT_PLAYGROUND[] = { 0b0000, 0b0010, 0b1000, 0b1010 };
static const char XLAT_CHAR[] = {
	CHAR_EMPTY_EMPTY,			// 0b0000
	CHAR_EMPTY_PATTERN,			// 0b0001
	CHAR_EMPTY_PLAYGROUND,		// 0b0010
	CHAR_ERROR,					// 0b0011
	CHAR_PATTERN_EMPTY,			// 0b0100
	CHAR_PATTERN_PATTERN,		// 0b0101
	CHAR_PATTERN_PLAYGROUND,	// 0b0110
	CHAR_ERROR,					// 0b0111
	CHAR_PLAYGROUND_EMPTY,		// 0b1000
	CHAR_PLAYGROUND_PATTERN,	// 0b1001
	CHAR_PLAYGROUND_PLAYGROUND,	// 0b1010
	CHAR_ERROR,					// 0b1011
	CHAR_ERROR,					// 0b1100
	CHAR_ERROR,					// 0b1101
	CHAR_ERROR,					// 0b1110
	CHAR_ERROR					// 0b1111
};

static void chars_init() {

    lcd_send_command(CG_RAM_ADDR + 0*8);
    for (int i = 0; i < 8; i++)
        lcd_send_data(DINO[i]);

    lcd_send_command(CG_RAM_ADDR + 1*8);
    for (int i = 0; i < 8; i++)
        lcd_send_data(CACTUS[i]);

    lcd_send_command(CG_RAM_ADDR + 2*8);
    for (int i = 0; i < 8; i++)
        lcd_send_data(BIRD[i]);

    lcd_send_command(DD_RAM_ADDR);
}

static int is_obstacle_here(int col, int row) {
	for (int i = 0; i < OBSTACLE_MAX; ++i) {
		if (!obstacle_active[i]) continue;
		if (obstacle_col[i] == col && obstacle_row[i] == row)
			return obstacle_pattern[i]; // 1 vagy 2
	}
	return 0; // nincs akadály
}

static void screen_update() {
	lcd_send_command(DD_RAM_ADDR); // first line

    for (int col = 0; col < PLAYFIELD_ROWS; col++) {

        if (col == dino_col_position && dino_row_position == 0) {
            lcd_send_data(0);       // dino (CGRAM slot 0)
        }
        else {
            int p = is_obstacle_here(col, 0);
            if (p) {
                lcd_send_data(p); // p == 1 vagy 2 -> CGRAM slot 1/2
            } else {
                lcd_send_data(' ');
            }
        }
    }

    lcd_send_command(DD_RAM_ADDR2); // second line

    for (int col = 0; col < PLAYFIELD_ROWS; col++) {

        if (col == dino_col_position && dino_row_position == 1) {
            lcd_send_data(0);       // dino (CGRAM slot 0)
        }
        else {
            int p = is_obstacle_here(col, 1);
            if (p) {
                lcd_send_data(p);
            } else {
                lcd_send_data(' ');
            }
        }
    }
}

// THE GAME ==================================================================

int main() {
	port_init();
	lcd_init();
	chars_init();
	rnd_init();

	// "Splash screen"
	lcd_send_line1("    NoNetDino");
	lcd_send_line2("    by Dave");

	// loop of the whole program, always restarts game
	while (1) {
		int new_pattern;

		while (button_pressed() != BUTTON_CENTER) // wait till start signal
			button_unlock(); // keep on clearing button_accept

		lcd_send_line1("  Difficulity");
		lcd_send_line2("E-4   N-5   H-6");
		while (1) { // végtelen ciklus, amiből kilépünk gombnyomásra
	    	int btn = button_pressed();
    		if(btn == BUTTON_LEFT) {
        		level_current = 0;
        		break;
    		}
    		if(btn == BUTTON_CENTER) {
        		level_current = 1;
        		break;
    		}
    		if(btn == BUTTON_RIGHT) {
        		level_current = 2;
        		break;
    		}
    		button_unlock();
		}

		playfield_clear(); // set up new playfield
		delay_cycle = 0; // start the timer
		obstacles_init();
		// kezdő spawn
		spawn_obstacle();

		int spawn_cooldown = 0; // időzítő a spawnok között (megelőzi a túlzsúfoltságot)

		// loop of the game
		while (1) {

			// game over, if any obstacle collide with the dino
			if (dino_collision())
				break;

			if (++delay_cycle > LEVELS[level_current].delay) {
				delay_cycle = 0;

				for (int i = 0; i < OBSTACLE_MAX; ++i) {
					if (!obstacle_active[i]) continue;

					if (obstacle_col[i] == 0) {
						obstacle_active[i] = 0;
						row_removed();
						continue;
					}
					// különben léptetjük balra
					--obstacle_col[i];
				}

				// spawn logic
				if (spawn_cooldown > 0) spawn_cooldown--;
				else {
					int chance = 25;
					int delay = 10;
					if (level_current == 1) {
						chance = 50;
						delay = 7;
					}
					else if (level_current == 2) {
						chance = 70;
						delay = 5;
					}
					int r = rnd_gen(100);
					if (r < chance) {
						spawn_obstacle();
						spawn_cooldown = 5 + rnd_gen(delay); // kis késleltetés a következő spawnig
					}
				}
			}

			int button = button_pressed();
			if(button == BUTTON_UP && dino_jump == 0) {
       			dino_jump = 1;
			}
			if(button == BUTTON_DOWN) {
				dino_jump = -1;
			}

			if(dino_jump == 1) {            // if jump
    			if(dino_row_position > 0)
        			dino_row_position--;             // 1 block up
				else if(dino_row_position == 0 && air_time > 0) {
					air_time--;				// stays in the air for a short period
				}
    			else
        			dino_jump = -1;         // reached top and air time ran out
			}
			else if(dino_jump == -1) {      // falling
    			if(dino_row_position < 1)
        			dino_row_position++;             // 1 block down
    		else
        		dino_jump = 0;          // reached the floor, reset values
				air_time = 10 + 5 * (2 - level_current);
			}

			// once all movements are done, update the screen
			screen_update();

			// try to unlock the buttons (technicality but must be done)
			button_unlock();
		} // end of game-loop

		// playing some funeral tunes and displaying a game over screen
		play_tune(TUNE_GAMEOVER);
		lcd_send_line1("    GAME OVER   ");
		lcd_send_line2("   5 - restart");

	} // end of program-loop, we never quit
}
