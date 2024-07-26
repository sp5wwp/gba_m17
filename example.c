#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <m17.h>
#include "fonts.h"

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

//M17 stuff
char msg[64];

uint8_t dst_raw[10]={'A', 'L', 'L', '\0'};                  //raw, unencoded destination address
uint8_t src_raw[10]={'N', '0', 'C', 'A', 'L', 'L', '\0'};
uint8_t can=0;

lsf_t lsf;

float full_packet[6912+88];
uint8_t full_packet_data[32*25];
uint32_t pkt_sym_cnt=0;
uint16_t num_bytes=0;

uint8_t enc_bits[SYM_PER_PLD*2];
uint8_t rf_bits[SYM_PER_PLD*2];

//audio playback
uint32_t samples[600]; //int8_t samples, fs=24kHz

//key press detection
volatile uint32_t key_states=0;

//Functions
//interrupt handler
void irqh(void)
{
	//no more samples, stop timer 0 and dma
	REG_TM0CNT_H=0; //disable timer 0
	REG_DMA1CNT_H=0; //stop DMA

	//clear the interrupt(s)
	REG_IF |= REG_IF;
}

//form a 16-bit BGR GBA colour from three component values
uint16_t color(uint8_t r, uint8_t g, uint8_t b)
{
	r>>=3;
	g>>=3;
	b>>=3;

	return ((uint16_t)b<<10)|((uint16_t)g<<5)|(uint16_t)r;
}

void set_pixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b)
{
	MEM_VRAM[y*240+x] = color(r, g, b);
}

void put_letter(const char c, uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b)
{
	for(uint8_t i=0; i<8; i++)
	{
		for(uint8_t j=0; j<5; j++)
		{
			if(font_5_8[(uint8_t)c-0x20][j]&(1<<i))
				set_pixel(x+j, y+i, r, g, b);
			//else
				//set_pixel(x+j, y+i, 0, 0, 0);
		}
	}
}

void put_string(const char *str, uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b)
{
	for(uint8_t i=0; i<strlen(str); i++)
	{
		put_letter(str[i], x, y, r, g, b);
		x+=6;
	}
}

void str_print(const uint16_t x, const uint16_t y, const uint8_t r, const uint8_t g, const uint8_t b, const char* fmt, ...)
{
	char str[50];
	va_list ap;

	va_start(ap, fmt);
	vsprintf(str, fmt, ap);
	va_end(ap);

	put_string(str, x, y, r, g, b);
}

int main(void)
{
	//config display
	REG_DISPLAY = 3 | BG2_ENABLE;
	REG_DISPLAY &= ~FRAME_SEL_BIT;

	//config sound
	uint8_t cycle=6*4;
	for(uint8_t i=0; i<cycle; i++)
		*((int8_t*)samples+i)=floorf(127.0f * sinf((float)i/cycle * 2.0f * M_PI));
	for(uint16_t i=1; i<sizeof(samples)/4/(cycle/4); i++)
		memcpy((int8_t*)(&samples[i*(cycle/4)]), (int8_t*)samples, cycle);
	//for(uint16_t i=0; i<sizeof(samples)/4; i++)
		//samples[i]=0;
	
	//are our samples ok? plot a pretty sinewave
	//for(uint8_t i=0; i<80; i++)
		//set_pixel(i*3, 90-20.0f*(float)*((int8_t*)samples+i)/127.0f, 0, 255, 0);
	
	//REG_SOUNDCNT_L = 0;
	REG_SOUNDCNT_H = 0x0B0E; //SND_OUTPUT_RATIO_100 | DSA_OUTPUT_RATIO_100 | DSA_OUTPUT_TO_BOTH | DSA_TIMER0 | DSA_FIFO_RESET;	//Channel A, full volume, TIM0
	REG_SOUNDCNT_X = SND_ENABLED;
	// DMA channel 1
	REG_DMA1SAD = (uint32_t)samples;
	REG_DMA1DAD = REG_FIFO_A;
	//REG_DMA1CNT_H = 0xB600; //REG_DMA1CNT = ENABLE_DMA | START_ON_FIFO_EMPTY | WORD_DMA | DMA_REPEAT | DEST_REG_SAME;

	//Timer1
	REG_TM1CNT_L=0xFFFF-sizeof(samples)+1; //0xffff-the number of samples to play
	REG_TM1CNT_H=0xC4; //enable timer 1 + irq and cascade from timer 0
	//IRQ
	REG_INTR_HANDLER=(uint32_t)&irqh; //pointer to the interrupt handler function
	REG_IE=0x10; //enable irq for timer 1
	REG_IME=1; //master enable interrupts
	//Timer0 - sample rate
	REG_TM0CNT_L = SAMP_RATE;

	//M17 stuff
	sprintf(msg, "Test message.");
	sprintf((char*)src_raw, "SP5WWP");
	str_print(0, 0*9, 255, 255, 255, "GBA"); str_print(20, 0*9, 255, 255, 255, "M"); str_print(25, 0*9, 255, 0, 0, "17"); str_print(35, 0*9, 255, 255, 255, " Packet Encoder by SP5WWP");
	str_print(0, 2*9, 255, 255, 255, "DST: %s", dst_raw); //doesn't work without "-specs=nosys.specs"
	str_print(0, 3*9, 255, 255, 255, "SRC: %s", src_raw);
	str_print(0, 4*9, 255, 255, 255, "Message: %s", msg);
	//obtain data and append with CRC
	memset(full_packet_data, 0, 32*25);
	full_packet_data[0]=0x05;
	num_bytes=sprintf((char*)&full_packet_data[1], msg)+2; //0x05 and 0x00
	uint16_t packet_crc=CRC_M17(full_packet_data, num_bytes);
	full_packet_data[num_bytes]  =packet_crc>>8;
	full_packet_data[num_bytes+1]=packet_crc&0xFF;
	num_bytes+=2; //count 2-byte CRC too

	//encode dst, src for the lsf struct
	uint64_t dst_encoded=0, src_encoded=0;
	uint16_t type=0;
	encode_callsign_value(&dst_encoded, dst_raw);
	encode_callsign_value(&src_encoded, src_raw);
	for(int8_t i=5; i>=0; i--)
	{
		lsf.dst[5-i]=(dst_encoded>>(i*8))&0xFF;
		lsf.src[5-i]=(src_encoded>>(i*8))&0xFF;
	}

	//fprintf(stderr, "DST: %s\t%012lX\nSRC: %s\t%012lX\n", dst_raw, dst_encoded, src_raw, src_encoded);
	//fprintf(stderr, "Data CRC:\t%04hX\n", packet_crc);
	type=((uint16_t)0x01<<1)|((uint16_t)can<<7); //packet mode, content: data
	lsf.type[0]=(uint16_t)type>>8;
	lsf.type[1]=(uint16_t)type&0xFF;
	memset(&lsf.meta, 0, 112/8);

	//calculate LSF CRC
	uint16_t lsf_crc=LSF_CRC(&lsf);
	lsf.crc[0]=lsf_crc>>8;
	lsf.crc[1]=lsf_crc&0xFF;
	//fprintf(stderr, "LSF CRC:\t%04hX\n", lsf_crc);

	//encode LSF data
	conv_encode_LSF(enc_bits, &lsf);

	//fill preamble
	memset((uint8_t*)full_packet, 0, sizeof(float)*(6912+88));
	send_preamble(full_packet, &pkt_sym_cnt, PREAM_LSF);

	//send LSF syncword
	send_syncword(full_packet, &pkt_sym_cnt, SYNC_LSF);

	//reorder bits
	for(uint16_t i=0; i<SYM_PER_PLD*2; i++)
		rf_bits[i]=enc_bits[intrl_seq[i]];

	//randomize
	for(uint16_t i=0; i<SYM_PER_PLD*2; i++)
	{
		if((rand_seq[i/8]>>(7-(i%8)))&1) //flip bit if '1'
		{
			if(rf_bits[i])
				rf_bits[i]=0;
			else
				rf_bits[i]=1;
		}
	}

	//fill packet with LSF
	send_data(full_packet, &pkt_sym_cnt, rf_bits);

	//generate frames
	;

	//send EOT
	for(uint8_t i=0; i<SYM_PER_FRA/SYM_PER_SWD; i++) //192/8=24
		send_syncword(full_packet, &pkt_sym_cnt, EOT_MRKR);

	str_print(0, 6*9, 255, 255, 255, "LSF_CRC=%04X PKT_CRC=%04X", lsf_crc, packet_crc);
	str_print(0, SCREEN_HEIGHT-1*9+1, 255, 255, 255, "Press A to transmit.");

	while(1)
	{
		//skip past the rest of any current V-blank, then skip past the V-draw
		//while(REG_DISPLAY_VCOUNT >= SCREEN_HEIGHT);
		//while(REG_DISPLAY_VCOUNT <  SCREEN_HEIGHT);

		//get current key states (REG_KEY_INPUT stores the states inverted)
		key_states = ~REG_KEY_INPUT & KEY_ANY;

		/*if(key_states & KEYPAD_LEFT)
			put_letter('<', 100, 100, 0xFF, 0xFF, 0xFF);
		else
			put_letter('<', 100, 100, 0, 0, 0); //clear

		if(key_states & KEYPAD_RIGHT)
			put_letter('>', 100, 100, 0xFF, 0xFF, 0xFF);
		else
			put_letter('>', 100, 100, 0, 0, 0); //clear
		*/
		if(key_states & BUTTON_A) //start DMA - start playing samples
		{
			REG_TM0CNT_H = TIMER_ENABLED; //enable timer 0 - sample rate generator
			REG_DMA1CNT_H = 0xB600; //start DMA transfers
		}
	}

	return 0;
}
