/*
 * ECE312FinalProject.c
 *
 * Created: 11/21/2019 2:09:37 PM
 * Author : hlin2
 */ 

#define F_CPU 1000000 //The clock speed of our AVR
#include <stdlib.h>
#include <stdio.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <math.h>
#include "lcd.h" 

#define MetronomeSound 440 //Original Metronome Sound
#define MetronomeSound2 1320 //The end of a measure metronome sound
#define CalibrationSlope 143656 //slope for the counter-to-tempo calibration
#define CalibrationPower -0.742	//power for the counter-to-tempo calibration

#define SAMPLE_TOTAL 5	//indicates total number of sampled taps for tempo tap

/**********************************************************************/
/*Initializing variables required for our code					      */
/**********************************************************************/
int tempo = 100; //variable for tempo
int samples = 0; //Variable for counting our number of tempo tap samples
long counter = 0; //Variable to be incremented as a pseudo-clock for our tempo tap
long counterStore[SAMPLE_TOTAL]; //initializes the counter storage array
long counterAverage; //Our eventual counter average after multiple taps
int measureDivisor = 4;//Variable for counting beats in a bar to find the end of a measure in order to play a different tone
int needlePosition = 1;	//Determines which side LED lights up (1 is left, 2 is right)
uint8_t beatLength = 2;	//Determines the speed at which the metronome will play (quarter note, eighth note)


/**********************************************************************/
/*All the variables for creating our metronome sound			      */
/**********************************************************************/
uint16_t interruptCounter = 0; //Counter that acts within the metronome interrupt
uint8_t buzzerState = 0; //Determines the state of the buzzer (0 is off, 1 is on)
uint16_t interruptCounterMax = MetronomeSound; //Determines top value for interrupt counter based on desired tempo (default is for tempo = 100)
uint8_t tickPeriod = 9; //The length of the tone, tickPeriod = MetronomeSound * 20milliseconds
uint16_t toneFrequency = MetronomeSound; //The tone frequency of our metronome to produce our metronome sound;
uint8_t beatCounter = 0; //This counts the beats


/**********************************************************************/
/*Initializing our two states for the state machine					  */
/**********************************************************************/
enum //state 0 is the playing state, state 1 is the tempo changing state
{
	STATE0,STATE1
};
volatile  int state =STATE0; //Initializing our metronome to start in the tempo playing state

enum //All the different states for our different time signatures
{
	FOUR_FOUR ,THREE_FOUR,TWO_FOUR,THREE_EIGHT,SIX_EIGHT
};
volatile int TimeSignature=FOUR_FOUR; //Initializing our metronome to start in the 4/4 state


/**********************************************************************/
/*Code for initializing the LCD display code (Received from ECE312Lab)*/
/**********************************************************************/
int   outlcd(char c, FILE *stream);
FILE mystdout = FDEV_SETUP_STREAM(outlcd, NULL, _FDEV_SETUP_WRITE);

void lcdini(void)	//initialize LCD
{
	lcd_init(LCD_DISP_ON);
	stdout = &mystdout;
}

int outlcd(char c, FILE *stream) {
	lcd_putc(c);
	return 0;
}



/**********************************************************************/
/*Code for printing the time signature							      */
/**********************************************************************/
void TimeSignaturePrint()	//prints the currently chosen time signature on the LCD screen
{
	printf("\n");
	switch(TimeSignature)
	{
		case FOUR_FOUR:
		printf("TimeSign:4/4");
		break;
		
		case THREE_FOUR:
		printf("TimeSign:3/4");
		break;
		
		case TWO_FOUR:
		printf("TimeSign:2/4");
		break;
		
		case THREE_EIGHT:
		printf("TimeSign:3/8");
		break;
		
		case SIX_EIGHT:
		printf("TimeSign:6/8");
		break;
	}
	
}



/**********************************************************************/
/*Code for printing the tempo										  */
/**********************************************************************/
void TempoPrint()	//prints the current tempo on the LCD screen
{
	printf("Tempo: %d",tempo);
}



/**********************************************************************/
/*Code for changing the time signature with a button on PIN PB0       */
/**********************************************************************/
void ChangeTimeSignature ()
{
	_delay_ms(150); //this delay is for the bounce back
	switch(TimeSignature)	//when a time signature is chosen, the program will store the next state for when the button is pressed again
	{
		case FOUR_FOUR:
		TimeSignature=THREE_FOUR;	//Stores next state
		measureDivisor=3;	//Stores the measure divisor needed to calculate the end of a measure for the next state
		beatLength = 2;	//Stores the length of the beat for the next state
		break;
		
		case THREE_FOUR:
		TimeSignature=TWO_FOUR;
		measureDivisor=2;
		beatLength = 2;
		break;
		
		case TWO_FOUR:
		TimeSignature=THREE_EIGHT;
		measureDivisor=3;
		beatLength = 1;
		break;
		
		case THREE_EIGHT:
		TimeSignature=SIX_EIGHT;
		measureDivisor=6;
		beatLength = 1;
		break;
		
		case SIX_EIGHT:
		TimeSignature=FOUR_FOUR;
		measureDivisor=4;
		beatLength = 2;
		break;
	}
	lcd_clrscr();	//Prints the current tempo and the newly chosen time signature
	TempoPrint();
	TimeSignaturePrint();
}

/**********************************************************************/
/*Code for the push button that increases our tempo on pin PD6        */
/**********************************************************************/
void ButtonTempoIncrease()
{
	_delay_ms(150); //software de-bouncing for button bounce back
	if (tempo<200) //sets a cap for the tempo at 200
	{
		tempo++; //increments tempo if not at the cap
	}
	lcd_clrscr();	//Prints the current tempo and the newly chosen time signature
	TempoPrint();
	TimeSignaturePrint();
}

/**********************************************************************/
/*Code for the push button that decreases our tempo on pin PD4        */
/**********************************************************************/
void ButtonTempoDecrease()
{
	_delay_ms(150); //software de-bouncing for button bounce back
	if (tempo>0) //setting a cap for the lowest tempo at 0 so that it does not display negative tempo
	{
		tempo--; //decrements tempo if not at cap
	}
	lcd_clrscr();	//Prints the current tempo and the newly chosen time signature
	TempoPrint();
	TimeSignaturePrint();
}

/**********************************************************************/
/*Code that sets push button for tapping your own tempo on pin PD7    */
/**********************************************************************/

void ButtonTempoSet()
{
	_delay_ms(150);	//software de-bouncing for button bounce back
	lcd_clrscr();	//Prints initial prompt for tempo tap setting
	lcd_puts("Tempo w/ 5 taps\n");
	lcd_puts("Tap 2 start");
	
	while(!((PIND&(1<<PD7)) == 0));//hold program until tempo tap is initiated
	
	for(samples = 1; samples < SAMPLE_TOTAL+1; samples++)	//repeats for 5 taps
	{
		counter = 0;	//resets the counter timer
		while(!((PIND&(1<<PD7))== 0))//checking for when the tempo tap button is pushed again
		{
			++counter; //constantly incrementing counter as a pseudo-clock to record time between taps
		}
		_delay_ms(150); //software de-bouncing for pushbutton after it is pressed again
		counterStore[samples-1] = counter;	//stores the value of the counter time interval for the given tap
		lcd_clrscr();
		printf("Tap: %d",samples);	//Prints the current tap number
		
	}
	
	for(samples = 0; samples < SAMPLE_TOTAL; samples++)
	{
		counterAverage += counterStore[samples];	//calculates the average time interval between taps
	}
	counterAverage = counterAverage/(SAMPLE_TOTAL);
	
	tempo = CalibrationSlope*(pow(counterAverage,CalibrationPower)); //calculates tempo from the average counter value based on calibrated curve
	if (tempo>200)
	{
		tempo=200;	//floors the tempo value if it is greater than 200
	}
	
	lcd_clrscr();	//Prints the current tempo and the newly chosen time signature
	TempoPrint();
	TimeSignaturePrint();
}

/**********************************************************************/
/*Code for our global interrupt to alternate between states           */
/**********************************************************************/
ISR(INT1_vect,ISR_BLOCK)	//interrupt occurs when the start/stop button is pushed (external interrupt request)
{
	switch(state)
	{
		case STATE0: //Changing to muted state so you can change the tempo
		state=STATE1;	//Prepares next state to be the tempo playing state
		TIMSK1 &= ~(1<<OCIE1A); //disables interrupts for 16 bit timer to disable ticking
		lcd_clrscr();	//Prints the current tempo and the newly chosen time signature
		TempoPrint();
		TimeSignaturePrint();
		break;
		
		case STATE1: //Playing metronome state
		state=STATE0; //Prepares the next state to be the tempo set state
		TIMSK1 |= (1<<OCIE1A); //enables interrupts for 16 bit timer to enable ticking
		TCNT1=0; //this resets the 16 bit counter to 0 after every run to prevent glitches
		lcd_clrscr();	
		printf("Playing!");
		break;
	}
}



/**********************************************************************/
/*Beginning of code for interrupt to create the metronome sound       */
/**********************************************************************/
ISR(TIMER1_COMPA_vect,ISR_BLOCK)
{
	interruptCounter++; //increment interrupt counter
	TCCR1A &= ~(1<<COM1A0); //disable PWM output on pin PB1
	
	if (interruptCounter==(interruptCounterMax/2)) //the half tick in between sides
	{
		PORTD |= (1<<PORTD5); //turning on led
		PORTB &= ~(1<<PORTB7); //turning off led
		PORTB &= ~(1<<PORTB6); //turning off led
	}
	
	if((interruptCounter == interruptCounterMax) || buzzerState == 1)
		{
		buzzerState = 1; //buzzer is playing
		TCCR1A |= (1<<COM1A0); //enable PWM output on pin PB1

		//if the buzzer has been on for the specified length of time
		if(interruptCounter == (interruptCounterMax + tickPeriod))
		{
			buzzerState = 0; //set buzzer state to off
			interruptCounter = 0; //reset increment counter
			PORTD &= ~(1<<PORTD5);
			if(beatCounter % measureDivisor == 0)
			{
				toneFrequency = MetronomeSound2; //Our sound to indicate the end of a measure
				tickPeriod = 18; //0.2ms * frequency
				beatCounter++;
				
				switch(needlePosition){ //code for changing the needle position
					case 1:
					PORTB |= (1<<PORTB6);	//turns on left LED
					PORTB &= ~(1<<PORTB7);
					needlePosition++;	
					break;
							
					case 2:
					PORTB |= (1<<PORTB7);	//turns on right LED
					PORTB &= ~(1<<PORTB6);
					needlePosition--;	//changes next needle position to the left LED
					break;
				}
			}
			else
			{
				toneFrequency = MetronomeSound;
				tickPeriod = 9; //0.2ms * frequency
				beatCounter++;
				switch(needlePosition){ //code for changing the needle position
					case 1:
					PORTB |= (1<<PORTB6);	
					PORTB &= ~(1<<PORTB7);
					needlePosition++;
					break;
							
					case 2:
					PORTB |= (1<<PORTB7);
					PORTB &= ~(1<<PORTB6);
					needlePosition--;
					break;
				}
			}
			interruptCounterMax = beatLength*toneFrequency*((float)60/((float)(tempo+1)*2)); //recalculate interrupt counter max which depends on the chosen tempo
			OCR1A = (F_CPU/8/toneFrequency)-1; //modify compare value so timer1 fires at proper rate
			
		}
	}
}

/**********************************************************************/
/*The main function												      */
/**********************************************************************/

int main(void)
{
	DDRD &= ~(1<<DDD3); //Setting this as input
	PORTD |= (1<<PORTD3); //Writing to port D3 to set it as our input (For stopping/starting metronome)
	
	DDRD &= ~(1<<DDD6); //Setting this as input
	PORTD |= (1<<PORTD6); //Writing to port D6 to set it as our input (For increasing tempo)

	DDRD &= ~(1<<DDD4); //Setting this as input
	PORTD |= (1<<PORTD4); //Writing to port D4 to set it as our input (For decreasing tempo)
	
	DDRD &= (1<<DDD7);	//Setting PD7 as input 
	PORTD |= (1<<PORTD7);	//Writing to port D7 to set it as our input (For setting tempo via tap)
	
	DDRB &= (1<<DDB0);  //Setting PB0 as input
	PORTB |= (1<<PORTB0); //Writing to port B0 to set it as our input (For time signature change)
	
	DDRB |= (1<<DDB6);  //Setting PB6 as output for LED
	
	DDRB |= (1<<DDB7);  //Setting PB7 as output for LED

	DDRD |= (1<<DDD5);  //Setting PD5 as output for LED

	
	
	//setting the 16 bit clock for the metronome	
	
	DDRB |= (1<<PB1); //Setting this as output 16 bit timer
	TCCR1A |= (1<<WGM11) | (1<<WGM10); //This sets it to fast PWM mode
	TCCR1B |= (1<<CS11) | (1<<WGM12) | (1<<WGM13); // this sets the clock pre-scaler to 8 and sets it to fast PWM mode
	SREG |=(1<<7); //enables global interrupts
	TIMSK1 |=(1<<OCIE1A); //enables interrupts for 16 bit timer
	OCR1A = (F_CPU/8/toneFrequency)-1; //This is the frequency used to produce the metronome sound

	EICRA  |= (1<<ISC11); //External Interrupt control Register A
	EICRA &= ~(1<<ISC10); //this is enabling interrupt sense control setting it to '1 0' falling edge of INT1 generates an interrupt request
	EIMSK |= (1<<INT1); //External Interrupt Mask Register, setting this bit so we can enable the external pin interrupt

	sei();
	
	lcdini(); //initializing the lcd display
	lcd_clrscr();
	printf("Playing!");//First state is the playing state
	
	while (1) 
	{	
		switch (state)
			{
				case STATE0:	//playing tempo state
				counter = 0;	//resets counter for tempo tapping timer
				
				break;
				
				case STATE1:	//tempo changing or time signature changing state  state
				if ((PIND &(1<<PD6)) == 0) //this checks for when the switch is pushed
				{
					ButtonTempoIncrease(); //This is the function for increasing the tempo
				}
				if ((PIND &(1<<PD4)) == 0) //this checks for when the switch is pushed
				{
					ButtonTempoDecrease(); //This is the function for decreasing the tempo
				}
				if((PIND &(1<<PD7))== 0) //this checks for when the switch is pushed
				{
					ButtonTempoSet();	//This is the function for tapping the tempo
				}
				if ((PINB &(1<<PB0))==0) //this checks for when the switch is pushed
				{
					ChangeTimeSignature();//This is the function for changing the time signature
					
				}
		}	
	}
}
//end of function