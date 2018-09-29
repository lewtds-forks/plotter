/*
===============================================================================
 Name        : main.c
 Author      : $(author)
 Version     :
 Copyright   : $(copyright)
 Description : main definition
===============================================================================
*/

#if defined (__USE_LPCOPEN)
#if defined(NO_BOARD_LIB)
#include "chip.h"
#else
#include "board.h"
#endif
#endif

#include <cr_section_macros.h>

// TODO: insert other include files here
#include "FreeRTOS.h"
#include "task.h"
#include "ITM_write.h"

#include <mutex>
#include "Fmutex.h"
#include "user_vcom.h"
#include "instruction.h"
#include "DigitalIoPin.h"

struct coord{
	int x;
	int y;
};

DigitalIoPin *xmax;
DigitalIoPin *xmin;
DigitalIoPin *ymax;
DigitalIoPin *ymin;
DigitalIoPin *lim1pin;
DigitalIoPin *lim2pin;
DigitalIoPin *lim3pin;
DigitalIoPin *lim4pin;
DigitalIoPin *xdirpin;
DigitalIoPin *ydirpin;
DigitalIoPin *xsteppin;
DigitalIoPin *ysteppin;

bool xstepbool = true;
bool ystepbool = true;
bool xdir_cw = false;
bool ydir_cw = false;

bool overridebool = false; // Used to override limit switch lockout in calibration stage
bool runxaxis = true; // Set X or Y axis for interrupt
bool motorcalibrating = true; // Used when limit switches' identity unknown

volatile uint32_t RIT_count;
xSemaphoreHandle sbRIT;
QueueHandle_t coordQueue;

coord getDistance(coord from, coord to);
void RIT_start(int count, int us);

// TODO: insert other definitions and declarations here

/* the following is required if runtime statistics are to be collected */
extern "C" {

void vConfigureTimerForRunTimeStats( void ) {
	Chip_SCT_Init(LPC_SCTSMALL1);
	LPC_SCTSMALL1->CONFIG = SCT_CONFIG_32BIT_COUNTER;
	LPC_SCTSMALL1->CTRL_U = SCT_CTRL_PRE_L(255) | SCT_CTRL_CLRCTR_L; // set prescaler to 256 (255 + 1), and start timer
}

}
/* end runtime statistics collection */


/* Sets up system hardware */
static void prvSetupHardware(void)
{
	SystemCoreClockUpdate();
	Board_Init();

	/* Initial LED0 state is off */
	Board_LED_Set(0, false);

}

//inline void limAssign(DigitalIoPin *ptr) {
//	// Assigns name of ptr to whichever limit switch is closed
//		if (lim1pin->read()) {
//			ptr = lim1pin;
//		} else if (lim2pin->read()) {
//			ptr = lim2pin;
//		} else if (lim3pin->read()) {
//			ptr = lim3pin;
//		} else if (lim4pin->read()) {
//			ptr = lim4pin;
//		}
//}

static void motor_task(void *pvParameters) {
	int totalsteps = 500; // HARDCODED VALUE TO BE REPLACED WITH ACTUAL COUNTED STEPS.
	coord rcv; // Received coordinates from MDraw instruction
	coord prev; // Previous coordinates received
	coord dist; // Distance from prev to rcv
	coord mid; // Changing midpoints used by algorithm
	coord drawdist; // Small distance to draw during each iteration of algorithm
	int d = 0; // Used by algorithm
	int pps = 1000;
	bool startmode = true;
	bool countingmode = false;
	bool drawingmode = false;

	while (startmode) {
		// Set direction to clockwise for both motors
		xdirpin->write(0);
		xdir_cw = true;
		ydirpin->write(0);
		ydir_cw = true;

		// For each motor, go enough steps to ensure max limit switch will be found, then assign the max pointer to the switch.
		// Reverse direction and repeat.
		runxaxis = true;
		RIT_start(4000 * 2, 500000 / pps); // Steps * 2 to account for high and low pulse.
		//limAssign(xmax);
		if (lim1pin->read()) {
			xmax = lim1pin;
		} else if (lim2pin->read()) {
			xmax = lim2pin;
		} else if (lim3pin->read()) {
			xmax = lim3pin;
		} else if (lim4pin->read()) {
			xmax = lim4pin;
		}

		xdirpin->write(1);
		xdir_cw = false;
		overridebool = true; // Temporarily override the switch stopping the motor
		RIT_start(4000 * 2, 500000 / pps); // Steps * 2 to account for high and low pulse.
		overridebool = false;
		//limAssign(xmin);
		if (lim1pin->read()) {
			xmin = lim1pin;
		} else if (lim2pin->read()) {
			xmin = lim2pin;
		} else if (lim3pin->read()) {
			xmin = lim3pin;
		} else if (lim4pin->read()) {
			xmin = lim4pin;
		}

		runxaxis = false;
		overridebool = true; // Temporarily override the switch stopping the motor
		RIT_start(4000 * 2, 500000 / pps); // Steps * 2 to account for high and low pulse.
		overridebool = false;
		//limAssign(ymax);
		if (lim1pin->read() && lim1pin != xmin) {
			ymax = lim1pin;
		} else if (lim2pin->read() && lim2pin != xmin) {
			ymax = lim2pin;
		} else if (lim3pin->read() && lim3pin != xmin) {
			ymax = lim3pin;
		} else if (lim4pin->read() && lim4pin != xmin) {
			ymax = lim4pin;
		}

		ydirpin->write(1);
		ydir_cw = false;
		overridebool = true; // Temporarily override the switch stopping the motor
		RIT_start(500 * 2, 500000 / pps); // Steps * 2 to account for high and low pulse.
		overridebool = false;
		RIT_start(3500 * 2, 500000 / pps); // Steps * 2 to account for high and low pulse.
	//	limAssign(ymin);
		if (lim1pin->read() && lim1pin != xmin) {
			ymin = lim1pin;
		} else if (lim2pin->read() && lim2pin != xmin) {
			ymin = lim2pin;
		} else if (lim3pin->read() && lim3pin != xmin) {
			ymin = lim3pin;
		} else if (lim4pin->read() && lim4pin != xmin) {
			ymin = lim4pin;
		}

		startmode = false;
		countingmode = true;
	}
	while (countingmode) {
		//count steps
		countingmode = false;
		motorcalibrating = false; // Switch limit-switch-reading mode in interrupt handler
		drawingmode = true;
	}
	while (drawingmode) {

		if (xQueueReceive(coordQueue, &rcv, portMAX_DELAY) == pdPASS) {
			// Calculate distance
			dist = getDistance(prev, rcv);

			// Convert distance to steps
			dist.x = (dist.x * totalsteps) / 50000; // 38000
			dist.y = (dist.y * totalsteps) / 50000; // 31000

			// Set direction for each motor based on sign of distance.
			// Then, if negative, multiply by -1 (get absolute value).
			if (dist.x < 0) {
				xdirpin->write(1);
				xdir_cw = false;
				dist.x = dist.x * -1;
			} else {
				xdirpin->write(0);
				xdir_cw = true;
			}

			if (dist.y < 0) {
				ydirpin->write(1);
				ydir_cw = false;
				dist.y = dist.y * -1;
			} else {
				ydirpin->write(0);
				ydir_cw = true;
			}

			// Drive with Bresenham's algorithm

			    d = (2 * dist.y) - dist.x;
			    mid.x = prev.x;
			    mid.y = prev.y;

			    while (mid.x <= rcv.x) {
			    	//  Draw the distance to mid from prev
			    	drawdist = getDistance(prev, mid);
			    	if (drawdist.x > 0) {
			    		runxaxis = true;
			    		RIT_start(drawdist.x * 2, 500000 / pps); // Steps * 2 to account for high and low pulse.
			    	}
			    	if (drawdist.y > 0) {
			    		runxaxis = false;
			    		RIT_start(drawdist.y * 2, 500000 / pps); // Steps * 2 to account for high and low pulse.
			    	}

			    	// Old mid becomes new prev
			    	prev.x = mid.x;
			    	prev.y = mid.y;

			    	// Get next mid
			    	mid.x++;
			    	if ( d < 0 ) {
			    		d += dist.y + dist.y;
			    	} else {
			    		d += 2 * (dist.y - dist.x);
			    		mid.y++;
			    	}
			    }
		}

		vTaskDelay((TickType_t) 10); // 10ms delay
	}
}


static void USB_task(void *pvParameters) {
//	coord prev;
	coord next;

//	prev.x = 0;
//	prev.y = 0;

	bool LedState = false;
	//const char statusstr[] = "M10 XY 380 310 0.00 0.00 A0 B0 H0 S80 U160 D90\n";
	const char statusstr[] = "M10 XY 500 500 0.00 0.00 A0 B0 H0 S80 U160 D90\n";
	const char okstr[] = "OK\n";

	while (1) {
		char str[80];
		uint32_t len = USB_receive((uint8_t *)str, 79);
		str[len] = 0; /* make sure we have a zero at the end so that we can print the data */

		Instruction i = Instruction::parse(str);

		// DEBUG PRINT
		printf("\r%s\n", str);

		// If instruction is a move, send the coordinates to the queue.
		if (i.type == InstructionType::MOVE) {
			next.x = i.param1;
			next.y = i.param2;
			xQueueSendToBack(coordQueue, (void*) &next , portMAX_DELAY);
		} else if (i.type ==  InstructionType::MOVE_TO_ORIGIN) {
			next.x = 0;
			next.y = 0;
			xQueueSendToBack(coordQueue, (void*) &next , portMAX_DELAY);
		}

		// Reply to MDraw
		if (i.type == InstructionType::REPORT_STATUS) {
			USB_send( (uint8_t *) statusstr, 47);
		} else {
			USB_send( (uint8_t *) okstr, 3);
		}
//
//		// Set new previous coordinates for next iteration
//		if (i.type == InstructionType::MOVE) {
//			prev.x = i.param1;
//			prev.y = i.param2;
//		} else if (i.type == InstructionType::MOVE_TO_ORIGIN) {
//			prev.x = 0;
//			prev.y = 0;
//		}

		Board_LED_Set(1, LedState);
		LedState = (bool) !LedState;

		vTaskDelay((TickType_t) 10); // 10ms delay
	}
}

extern "C" {
void RIT_IRQHandler(void)
{
	// This used to check if a context switch is required
	portBASE_TYPE xHigherPriorityWoken = pdFALSE;
	// Tell timer that we have processed the interrupt.
	// Timer then removes the IRQ until next match occurs
	Chip_RIT_ClearIntStatus(LPC_RITIMER); // clear IRQ flag

	if(RIT_count > 0) {
		RIT_count--;

		if (motorcalibrating) {
			if ((!lim1pin->read() && !lim2pin->read() && !lim3pin->read() && !lim4pin->read()) || overridebool) {
				if (runxaxis) {
					xsteppin->write(xstepbool);
					xstepbool = !xstepbool;
				} else {
					ysteppin->write(ystepbool);
					ystepbool = !ystepbool;
				}
			}
		} else {
			if (runxaxis) {
				if ((xdir_cw && !xmax->read()) || (!xdir_cw && !xmin->read())) {
					xsteppin->write(xstepbool);
					xstepbool = !xstepbool;
				}
			} else {
				if ((ydir_cw && !ymax->read()) || (!ydir_cw && !ymin->read())) {
					ysteppin->write(ystepbool);
					ystepbool = !ystepbool;
				}
			}
		}
	}
	if (RIT_count == 0) {
		Chip_RIT_Disable(LPC_RITIMER); // disable timer
		// Give semaphore and set context switch flag if a higher priority task was woken up
		xSemaphoreGiveFromISR(sbRIT, &xHigherPriorityWoken);
	}
	// End the ISR and (possibly) do a context switch
	portEND_SWITCHING_ISR(xHigherPriorityWoken);
}
}

void RIT_start(int count, int us) {
	uint64_t cmp_value;

	// Determine approximate compare value based on clock rate and passed interval
	cmp_value = (uint64_t) Chip_Clock_GetSystemClockRate() * (uint64_t) us / 1000000;

	// disable timer during configuration
	Chip_RIT_Disable(LPC_RITIMER);

	RIT_count = count;
	// enable automatic clear on when compare value==timer value
	// this makes interrupts trigger periodically
	Chip_RIT_EnableCompClear(LPC_RITIMER);
	// reset the counter
	Chip_RIT_SetCounter(LPC_RITIMER, 0);
	Chip_RIT_SetCompareValue(LPC_RITIMER, cmp_value);
	// start counting
	Chip_RIT_Enable(LPC_RITIMER);
	// Enable the interrupt signal in NVIC (the interrupt controller)
	NVIC_EnableIRQ(RITIMER_IRQn);

	// wait for ISR to tell that we're done
	if(xSemaphoreTake(sbRIT, portMAX_DELAY) == pdTRUE) {
		// Disable the interrupt signal in NVIC (the interrupt controller)
		NVIC_DisableIRQ(RITIMER_IRQn);
	}
	else {
		// unexpected error
	}
}

coord getDistance(coord from, coord to) {
	coord result;
	result.x = to.x - from.x;
	result.y = to.y - from.y;
	return result;
}

int main(void) {

	prvSetupHardware();

	coordQueue = xQueueCreate(10, sizeof(coord));
	vQueueAddToRegistry(coordQueue, "coordQueue");

	lim1pin = new DigitalIoPin (1, 3, true, true, true); // Limit switch 1
	lim2pin = new DigitalIoPin (0, 0, true, true, true); // Limit switch 2
	lim3pin = new DigitalIoPin (0, 9, true, true, true); // Limit switch 3
	lim4pin = new DigitalIoPin (0, 29, true, true, true); // Limit switch 4

//	// FOR TESTING WITH SINGLE MOTOR ONLY
//	lim1pin = new DigitalIoPin (0, 28, true, true, true); // Limit switch 1
//	lim2pin = new DigitalIoPin (0, 27, true, true, true); // Limit switch 2

	xdirpin = new DigitalIoPin (0, 28, false, false, false); // CCW 1, CW 0
	xsteppin = new DigitalIoPin (0, 27, false, false, false); // Step pin

	ydirpin = new DigitalIoPin (1, 0, false, false, false); // CCW 1, CW 0
	ysteppin = new DigitalIoPin (0, 24, false, false, false); // Step pin

	// initialize RIT (= enable clocking etc.)
	Chip_RIT_Init(LPC_RITIMER);

	// set the priority level of the interrupt
	// The level must be equal or lower than the maximum priority specified in FreeRTOS config
	// Note that in a Cortex-M3 a higher number indicates lower interrupt priority
	NVIC_SetPriority( RITIMER_IRQn, configMAX_SYSCALL_INTERRUPT_PRIORITY + 1 );

	sbRIT = xSemaphoreCreateBinary();

	xTaskCreate(motor_task, "Motor",
				configMINIMAL_STACK_SIZE + 128, NULL, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);

	xTaskCreate(USB_task, "USB",
			configMINIMAL_STACK_SIZE + 256, NULL, (tskIDLE_PRIORITY + 1UL),
			(TaskHandle_t *) NULL);

	xTaskCreate(cdc_task, "CDC",
			configMINIMAL_STACK_SIZE + 128, NULL, (tskIDLE_PRIORITY + 2UL), // Needs to be higher priority to empty receive queue
			(TaskHandle_t *) NULL);


	/* Start the scheduler */
	vTaskStartScheduler();

	/* Should never arrive here */
	return 1;
}
