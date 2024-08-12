#pragma once

#define SCREEN_WIDTH			240
#define SCREEN_HEIGHT			160

#define MEM_IO					0x04000000
#define MEM_VRAM				((volatile uint16_t *)0x06000000)

#define REG_DISPLAY				(*((volatile uint32_t *)(MEM_IO)))
#define REG_DISPLAY_VCOUNT		(*((volatile uint32_t *)(MEM_IO + 0x0006)))
#define REG_KEY_INPUT			(*((volatile uint32_t *)(MEM_IO + 0x0130)))

#define FRAME_SEL_BIT			0x0010
#define BG2_ENABLE				0x0400

#define BUTTON_A				0x0001      // A Button
#define BUTTON_B				0x0002      // B Button
#define BUTTON_SELECT			0x0004      // select button
#define BUTTON_START			0x0008      // START button
#define KEYPAD_RIGHT			0x0010      // Right key
#define KEYPAD_LEFT				0x0020      // Left key
#define KEYPAD_UP				0x0040      // Up key
#define KEYPAD_DOWN				0x0080      // Down key
#define BUTTON_RIGHT			0x0100      // R shoulder Button
#define BUTTON_LEFT				0x0200		// L shoulder Button
#define KEY_ANY					0x03FF

#define SAMP_RATE				(0xFFFF-699+1)			// 2^24/699 = 24,000 Hz
#define	REG_SOUNDCNT_L			(*((uint16_t volatile *)(MEM_IO + 0x080)))
#define	REG_SOUNDCNT_H			(*((uint16_t volatile *)(MEM_IO + 0x082)))
#define	REG_SOUNDCNT_X			(*((uint16_t volatile *)(MEM_IO + 0x084)))
#define SND_ENABLED				0x0080
#define SND_OUTPUT_RATIO_25		0x0000
#define SND_OUTPUT_RATIO_50		0x0001
#define SND_OUTPUT_RATIO_100	0x0002
#define DSA_OUTPUT_RATIO_50		0x0000
#define DSA_OUTPUT_RATIO_100	0x0004
#define DSA_OUTPUT_TO_RIGHT		0x0100
#define DSA_OUTPUT_TO_LEFT		0x0200
#define DSA_OUTPUT_TO_BOTH		0x0300
#define DSA_TIMER0				0x0000
#define DSA_TIMER1				0x0400
#define DSA_FIFO_RESET			0x0800
#define DSB_OUTPUT_RATIO_50		0x0000
#define DSB_OUTPUT_RATIO_100	0x0008
#define DSB_OUTPUT_TO_RIGHT		0x1000
#define DSB_OUTPUT_TO_LEFT		0x2000
#define DSB_OUTPUT_TO_BOTH		0x3000
#define DSB_TIMER0				0x0000
#define DSB_TIMER1				0x4000
#define DSB_FIFO_RESET			0x8000
//DMA channel 1 register definitions
#define REG_DMA1SAD				*(volatile uint32_t*)(MEM_IO + 0x0bc)
#define REG_DMA1DAD				*(volatile uint32_t*)(MEM_IO + 0x0c0)
#define REG_DMA1CNT				*(volatile uint32_t*)(MEM_IO + 0x0c4)
#define REG_DMA1CNT_L			*(volatile uint16_t*)0x040000C4			// control register
#define REG_DMA1CNT_H			*(volatile uint16_t*)0x040000C6
//DMA flags
#define WORD_DMA				0x04000000
#define HALF_WORD_DMA			0x00000000
#define ENABLE_DMA				0x80000000
#define START_ON_FIFO_EMPTY		0x30000000
#define DMA_REPEAT				0x02000000
#define DEST_REG_SAME			0x00400000
//Timer0 register definitions
#define REG_TM0CNT				*(volatile uint32_t*)(MEM_IO + 0x100)
#define REG_TM0CNT_L			*(volatile uint16_t*)(MEM_IO + 0x100)
#define REG_TM0CNT_H			*(volatile uint16_t*)(MEM_IO + 0x102)
#define REG_TM1CNT				*(volatile uint32_t*)(MEM_IO + 0x104)
#define REG_TM1CNT_L			*(volatile uint16_t*)(MEM_IO + 0x104)
#define REG_TM1CNT_H			*(volatile uint16_t*)(MEM_IO + 0x106)
//Timer flags
#define TIMER_ENABLED			0x0080
//FIFO address defines
#define REG_FIFO_A				0x040000A0
#define REG_FIFO_B				0x040000A4
//INT
#define	REG_IE					*(volatile uint16_t*)(MEM_IO + 0x200)	// Interrupt Enable
#define REG_IF					*(volatile uint16_t*)(MEM_IO + 0x202)	// Interrupt Flag
#define	REG_IME					*(volatile uint16_t*)(MEM_IO + 0x208)	// Interrupt Master Enable
#define REG_INTR_HANDLER		*(volatile uint32_t*)(0x03007FFC)		// Interrupt Handler
#define TIM1                    (1<<4)

#define DS_BUSY                 (REG_TM0CNT_H & TIMER_ENABLED)          // Direct Sound busy?
