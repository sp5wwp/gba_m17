#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <m17.h>
#include "gba.h"
#include "fonts.h"

//M17 stuff
char msg[64];

char dst_raw[10]="ALL";			//raw, unencoded destination address (default)
char src_raw[10]="N0CALL";		//raw, unencoded source address (default)
uint8_t can=0;

lsf_t lsf;

float full_packet[6800]={0.0f};
uint8_t full_packet_data[32*25]={0};
uint32_t pkt_sym_cnt=0;
uint16_t num_bytes=0;

uint8_t enc_bits[SYM_PER_PLD*2];
uint8_t rf_bits[SYM_PER_PLD*2];

//audio playback
uint32_t samples[241]; //int8_t samples, fs=24kHz

//key press detection
volatile uint32_t key_states=0;

//Functions
//low-level, hardware
void config_display(void)
{
	REG_DISPLAY = 3 | BG2_ENABLE;
	REG_DISPLAY &= ~FRAME_SEL_BIT;
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
			if(font_5_8[(uint8_t)c-' '][j]&(1<<i) || c==127)
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

//doesn't work without "-specs=nosys.specs"
void str_print(const uint16_t x, const uint16_t y, const uint8_t r, const uint8_t g, const uint8_t b, const char* fmt, ...)
{
	char str[50];
	va_list ap;

	va_start(ap, fmt);
	vsprintf(str, fmt, ap);
	va_end(ap);

	put_string(str, x, y, r, g, b);
}

void play_sample(uint32_t* sample, uint16_t len)
{
	REG_SOUNDCNT_H = 0; //clear the control register
	REG_SOUNDCNT_H = 0x0B0E; //clear FIFO A //SND_OUTPUT_RATIO_100 | DSA_OUTPUT_RATIO_100 | DSA_OUTPUT_TO_BOTH | DSA_TIMER0 | DSA_FIFO_RESET; //Channel A, full volume, TIM0
	REG_SOUNDCNT_X = SND_ENABLED; //enable direct sound
	REG_DMA1SAD = (uint32_t)sample; //DMA source
	REG_DMA1DAD = REG_FIFO_A; //DMA destination - FIFO A
	REG_DMA1CNT_H = 0xB600; //start DMA transfers //REG_DMA1CNT = ENABLE_DMA | START_ON_FIFO_EMPTY | WORD_DMA | DMA_REPEAT;
	REG_TM1CNT_L = 0xFFFF-len+1; //0xffff-(the number of samples to play)+1
	REG_TM1CNT_H = 0x00C4; //enable timer 1 + irq and cascade from timer 0
	REG_TM0CNT_L = SAMP_RATE; //set timer 0 to sample rate
	REG_TM0CNT_H = TIMER_ENABLED; //enable timer 0 - sample rate generator
}

//interrupt handler
void irqh(void)
{
	//no more samples, stop dma and timer 0, 1
	REG_DMA1CNT_H = 0; //stop DMA
	REG_TM0CNT_H = 0; //disable timer 0
	REG_TM1CNT_H = 0; //disable timer 1

	//clear the interrupt(s)
	REG_IF |= REG_IF;
}

int main(void)
{
	//config display
	config_display();

	//info header
	str_print(0, 0*9, 255, 255, 255, "GBA"); str_print(4*6, 0*9, 255, 255, 255, "M"); str_print(5*6, 0*9, 255, 0, 0, "17"); str_print(8*6, 0*9, 255, 255, 255, "Packet Encoder by SP5WWP");

	//generate sample
	uint8_t cycle=6*4; //at 24kHz sample rate, 24 -> 1kHz
	for(uint8_t i=0; i<cycle; i++)
		*((int8_t*)samples+i)=floorf(127.0f * sinf((float)i/cycle * 2.0f * M_PI));
	for(uint16_t i=1; i<sizeof(samples)/4/(cycle/4); i++)
		memcpy((int8_t*)(&samples[i*(cycle/4)]), (int8_t*)samples, cycle);
	//for(uint16_t i=0; i<sizeof(samples)/4; i++)
		//samples[i]=0;
	
	//are our samples ok? plot a pretty sinewave
	//for(uint8_t i=0; i<80; i++)
		//set_pixel(i*3, 90-20.0f*(float)*((int8_t*)samples+i/*+(sizeof(samples)/4-80)*/)/127.0f, 0, 255, 0);

	//IRQ
	REG_INTR_HANDLER=(uint32_t)&irqh; //pointer to the interrupt handler function
	REG_IME=1; //master enable interrupts
	REG_IE = TIM1; //enable irq for timer 1

	//M17 stuff
	sprintf(msg, "Test message.");
	sprintf(dst_raw, "ALL");
	sprintf(src_raw, "N0CALL");

	//obtain data and append with CRC
	//memset(full_packet_data, 0, 32*25);
	full_packet_data[0]=0x05;
	num_bytes=sprintf((char*)&full_packet_data[1], msg)+2; //0x05 and 0x00
	uint16_t packet_crc=CRC_M17(full_packet_data, num_bytes);
	full_packet_data[num_bytes]  =packet_crc>>8;
	full_packet_data[num_bytes+1]=packet_crc&0xFF;
	num_bytes+=2; //count 2-byte CRC too

	//encode dst, src for the lsf struct
	uint64_t dst_enc=0, src_enc=0;
	uint16_t type=0;
	encode_callsign_value(&dst_enc, (uint8_t*)dst_raw);
	encode_callsign_value(&src_enc, (uint8_t*)src_raw);
	for(int8_t i=5; i>=0; i--)
	{
		lsf.dst[5-i]=(dst_enc>>(i*8))&0xFF;
		lsf.src[5-i]=(src_enc>>(i*8))&0xFF;
	}
	
	type=((uint16_t)0x01<<1)|((uint16_t)can<<7); //packet mode, content: data
	lsf.type[0]=(uint16_t)type>>8;
	lsf.type[1]=(uint16_t)type&0xFF;
	memset(&lsf.meta, 0, 112/8);

	//calculate LSF CRC
	uint16_t lsf_crc=LSF_CRC(&lsf);
	lsf.crc[0]=lsf_crc>>8;
	lsf.crc[1]=lsf_crc&0xFF;

	//encode LSF data
	conv_encode_LSF(enc_bits, &lsf);

	//fill preamble
	memset((uint8_t*)full_packet, 0, sizeof(full_packet));
	send_preamble(full_packet, &pkt_sym_cnt, PREAM_LSF);

	//send LSF syncword
	send_syncword(full_packet, &pkt_sym_cnt, SYNC_LSF);

	//reorder bits
	reorder_bits(rf_bits, enc_bits);

	//randomize
	randomize_bits(rf_bits);

	//fill packet with LSF
	send_data(full_packet, &pkt_sym_cnt, rf_bits);

	//generate frames
	;

	//send EOT
	send_eot(full_packet, &pkt_sym_cnt);

	//display params
	str_print(0, 2*9, 255, 255, 255, "DST: %s", dst_raw); str_print(15*6, 2*9, 255, 255, 255, "(%04X", dst_enc>>32); str_print(15*6, 2*9, 255, 255, 255, "%13X)", dst_enc);
	str_print(0, 3*9, 255, 255, 255, "SRC: %s", src_raw); str_print(15*6, 3*9, 255, 255, 255, "(%04X", src_enc>>32); str_print(15*6, 3*9, 255, 255, 255, "%13X)", src_enc);
	str_print(0, 4*9, 255, 255, 255, "Message: %s", msg);
	str_print(0, 6*9, 255, 255, 255, "LSF_CRC=%04X PKT_CRC=%04X", lsf_crc, packet_crc);
	str_print(0, SCREEN_HEIGHT-1*9+1, 255, 255, 255, "Press A to transmit.");

	while(1)
	{
		//skip past the rest of any current V-blank, then skip past the V-draw
		while(REG_DISPLAY_VCOUNT >= SCREEN_HEIGHT);
		while(REG_DISPLAY_VCOUNT <  SCREEN_HEIGHT);

		//get current key states (REG_KEY_INPUT stores the states inverted)
		key_states = ~REG_KEY_INPUT & KEY_ANY;

		if(key_states & KEYPAD_LEFT)
			put_letter('<', 100, 100, 0xFF, 0xFF, 0xFF);
		else if(key_states & KEYPAD_RIGHT)
			put_letter('>', 100, 100, 0xFF, 0xFF, 0xFF);
		else
			put_letter(127, 100, 100, 0, 0, 0); //clear
		
		if(key_states & BUTTON_A) //start playing samples
		{
			if(!(REG_TM0CNT_H & TIMER_ENABLED)) //not playing samples?
			{
				for(uint8_t i=0; i<10-1; i++)
				{
					play_sample(samples, sizeof(samples)-4);
					while(REG_TM0CNT_H & TIMER_ENABLED);
				}
				play_sample(samples, sizeof(samples));
			}
		}
	}

	return 0;
}
