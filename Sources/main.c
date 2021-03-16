/* Header file with all the essential definitions for a given type of MCU */
#include "MK60D10.h"

/* Macros for bit-level registers manipulation */
#define GPIO_PIN_MASK	0x1Fu
#define GPIO_PIN(x)		(((1)<<(x & GPIO_PIN_MASK)))

// Display
#define DISPLAY_WIDTH 8
#define DISPLAY_HEIGHT 16

// Buttons
#define BTN_SW2 0x400     // Port E, bit 10, go right
#define BTN_SW3 0x1000    // Port E, bit 12, go down
#define BTN_SW4 0x8000000 // Port E, bit 27, go left
#define BTN_SW5 0x4000000 // Port E, bit 26, go up

// Snake size
#define SNAKE_SIZE 6

// Row constants
const unsigned ROWS[8] = {26, 24, 9, 25, 28, 7, 27, 29};
const unsigned ROWS_MASK = 0x3F000280;

// Point contains x and y position of snake part
typedef struct
{
	int x, y;
} point;

typedef struct
{
	point parts[SNAKE_SIZE];
} snake;

// Direction the snake is moving in
typedef enum direction {right, up, left, down} direction;

/* Configuration of the necessary MCU peripherals */
void systemInit(void)
{
	// Turn on all port clocks
	SIM->SCGC5 = SIM_SCGC5_PORTA_MASK | SIM_SCGC5_PORTE_MASK;

	/* Set corresponding PTA pins (column activators of 74HC154) for GPIO functionality */
	PORTA->PCR[8] = (0|PORT_PCR_MUX(0x01));  // A0
	PORTA->PCR[10] = (0|PORT_PCR_MUX(0x01)); // A1
	PORTA->PCR[6] = (0|PORT_PCR_MUX(0x01));  // A2
	PORTA->PCR[11] = (0|PORT_PCR_MUX(0x01)); // A3

	// Set corresponding PTA pins (rows selectors of 74HC154) for GPIO functionality
	PORTA->PCR[26] = (0|PORT_PCR_MUX(0x01));  // R0
	PORTA->PCR[24] = (0|PORT_PCR_MUX(0x01));  // R1
	PORTA->PCR[9] = (0|PORT_PCR_MUX(0x01));   // R2
	PORTA->PCR[25] = (0|PORT_PCR_MUX(0x01));  // R3
	PORTA->PCR[28] = (0|PORT_PCR_MUX(0x01));  // R4
	PORTA->PCR[7] = (0|PORT_PCR_MUX(0x01));   // R5
	PORTA->PCR[27] = (0|PORT_PCR_MUX(0x01));  // R6
	PORTA->PCR[29] = (0|PORT_PCR_MUX(0x01));  // R7

	// Set corresponding PTE pins (output enable of 74HC154) for GPIO functionality
	PORTE->PCR[28] = (0|PORT_PCR_MUX(0x01));

	// Change corresponding PTA port pins as outputs
	PTA->PDDR = GPIO_PDDR_PDD(0x3F000FC0);

	// Change corresponding PTE port pins as outputs
	PTE->PDDR = GPIO_PDDR_PDD(GPIO_PIN(28));

	// Enable buttons
    PORTE->PCR[10] = PORT_PCR_MUX(0x01); // SW2
    PORTE->PCR[12] = PORT_PCR_MUX(0x01); // SW3
    PORTE->PCR[27] = PORT_PCR_MUX(0x01); // SW4
    PORTE->PCR[26] = PORT_PCR_MUX(0x01); // SW5

    // PIT Settings
	SIM->SCGC6 = SIM_SCGC6_PIT_MASK; // Enable timer
    PIT_MCR = 0x00; // Enable PIT
}

void PIT0Init(void)
{
    NVIC_EnableIRQ(PIT0_IRQn); // Enable interrupt from PIT0

    PIT_LDVAL0 = 0x5B8D7F; // Set PIT to 0.25s interval

    PIT_TCTRL0 &= PIT_TCTRL_CHN(0);
    PIT_TCTRL0 |= PIT_TCTRL_TIE(1);
    PIT_TCTRL0 |= PIT_TCTRL_TEN(1);
    PIT_TFLG0 &= PIT_TFLG_TIF(0);
}

void PIT1Init(void)
{
    NVIC_EnableIRQ(PIT1_IRQn); // Enable interrupt from PIT1

    PIT_LDVAL1 = 0x1067F; // Set PIT to 1/360s interval (6 diodes, each 60 times per second)

    PIT_TCTRL1 &= PIT_TCTRL_CHN(0);
    PIT_TCTRL1 |= PIT_TCTRL_TIE(1);
    PIT_TCTRL1 |= PIT_TCTRL_TEN(1);
    PIT_TFLG1 &= PIT_TFLG_TIF(0);
}

/* Conversion of requested column number into the 4-to-16 decoder control.  */
void column_select(unsigned int col_num)
{
	unsigned result = 0,
			 col_sel[4];

	for (unsigned i = 0 ; i < 4; i++)
	{
		result = col_num / 2;	  // Whole-number division of the input number
		col_sel[i] = col_num % 2;
		col_num = result;

		switch (i)
		{
		// Selection signal A0
		case 0:
			((col_sel[i]) == 0) ? (PTA->PDOR &= ~GPIO_PDOR_PDO( GPIO_PIN(8))) : (PTA->PDOR |= GPIO_PDOR_PDO( GPIO_PIN(8)));
			break;

		// Selection signal A1
		case 1:
			((col_sel[i]) == 0) ? (PTA->PDOR &= ~GPIO_PDOR_PDO( GPIO_PIN(10))) : (PTA->PDOR |= GPIO_PDOR_PDO( GPIO_PIN(10)));
			break;

		// Selection signal A2
		case 2:
			((col_sel[i]) == 0) ? (PTA->PDOR &= ~GPIO_PDOR_PDO( GPIO_PIN(6))) : (PTA->PDOR |= GPIO_PDOR_PDO( GPIO_PIN(6)));
			break;

		// Selection signal A3
		case 3:
			((col_sel[i]) == 0) ? (PTA->PDOR &= ~GPIO_PDOR_PDO( GPIO_PIN(11))) : (PTA->PDOR |= GPIO_PDOR_PDO( GPIO_PIN(11)));
			break;

		default:
			break;
		}
	}
}

// Main snake declaration
snake s;

// Snake direction declaration
direction d = up;

void snakeInit(snake* const s)
{
	for (int i = 0; i < SNAKE_SIZE; i++)
	{
		s->parts[i].x = 4;
		s->parts[i].y = i + 8;
	}
}

// This interrupt handler handles snake movement
void PIT0_IRQHandler(void)
{
    PIT_TFLG0 |= PIT_TFLG_TIF_MASK; // Clear interrupt flag

	// Shift snake body
	for (int i = SNAKE_SIZE - 1; i > 0; i--)
	{
		s.parts[i] = s.parts[i - 1];
	}

	// Move head according to direction
	// and handle display overflow
	if (d == right)
	{
		if (--s.parts[0].x == -1) s.parts[0].x = 7;
	}
	else if (d == up)
	{
		if (--s.parts[0].y == -1) s.parts[0].y = 15;
	}
	else if (d == left)
	{
		if (++s.parts[0].x == DISPLAY_WIDTH) s.parts[0].x = 0;
	}
	else
	{
		if (++s.parts[0].y == DISPLAY_HEIGHT) s.parts[0].y = 0;
	}
}

// This interrupt handler handles snake rendering
void PIT1_IRQHandler(void)
{
    PIT_TFLG1 |= PIT_TFLG_TIF_MASK; // Clear interrupt flag

	static int i = 0; // Holds snake part index

	PTA->PDOR &= ~ROWS_MASK; // Disable all rows
	PTA->PDOR |= GPIO_PDOR_PDO(GPIO_PIN(ROWS[s.parts[i].x])); // Select row the snake part is in
	column_select(s.parts[i].y); // Select column the snake part is in

	// Handle overflow
	if (++i == SNAKE_SIZE) i = 0;
}

int main(void)
{
	systemInit();

	snakeInit(&s);

	PIT0Init();
	PIT1Init();

    while (1)
    {
    	// Handle buttons
    	if (!(GPIOE_PDIR & BTN_SW2) && d != left) // go right button pressed
    	{
    		d = right;
    	}
    	else if (!(GPIOE_PDIR & BTN_SW3) && d != up) // go down button pressed
    	{
    		d = down;
    	}
    	else if (!(GPIOE_PDIR & BTN_SW4) && d != right) // go left button pressed
    	{
    		d = left;
    	}
    	else if (!(GPIOE_PDIR & BTN_SW5) && d != down) // go up button pressed
    	{
    		d = up;
    	}
    }

    return 0;
}
