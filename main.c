#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <m17.h>
#include "gba.h"
#include "fonts.h"

//input data
struct settings_t
{
	char dst_raw[10];						//raw, unencoded destination address
	char src_raw[10];						//raw, unencoded source address
	uint8_t can;							//Channel Access Number
	char msg[64];							//text message
	uint8_t phase;							//baseband phase 1-normal, 0-inverted
} settings;

//M17 stuff
lsf_t lsf;									//Link Setup Frame data

uint8_t full_packet_data[32*25]={0};		//packet payload
uint32_t pkt_sym_cnt=0;
uint16_t num_bytes=0;						//size of payload in bytes

//uint8_t enc_bits[SYM_PER_PLD*2];			//encoded bits
uint8_t rf_bits[SYM_PER_PLD*2];				//type-4 bits for transmission
int8_t symbols[SYM_PER_FRA];				//frame symbols

//audio playback
uint32_t samples[4][240];					//S8 samples, fs=24kHz, enough for 40ms plus 4 extra samples (as int8_t)

//key press detection
volatile uint32_t key_states=0;				//for key scanning

//baseband upsampling and filtering using fixed point arithmetic (floats are awefully slow on GBA)
//both set of taps are derived directly from `rrc_taps_5`
//the first one has the gain untouched
//const int32_t i_rrc_taps_5[41]={-75823, -46045, 36705, 112983, 114474, 22747, -100569, -145924, -40434, 171200, 318455, 200478, -254712, -865969, -1209552, -796138, 657141, 3005882, 5648794, 7735778, 8528542, 7735778, 5648794, 3005882, 657141, -796138, -1209552, -865969, -254712, 200478, 318455, 171200, -40434, -145924, -100569, 22747, 114474, 112983, 36705, -46045, -75823};
//the other has its gain multiplied by 64, to get rid of the additional shift right by 6
const int32_t i_rrc_taps_5[41]={-4852652, -2946890, 2349126, 7230909, 7326343, 1455796, -6436427, -9339120, -2587800, 10956799, 20381138, 12830587, -16301598, -55421996, -77411320, -50952844, 42057000, 192376416, 361522816, 495089760, 545826688, 495089760, 361522816, 192376416, 42057000, -50952844, -77411320, -55421996, -16301598, 12830587, 20381138, 10956799, -2587800, -9339120, -6436427, 1455796, 7326343, 7230909, 2349126, -2946890, -4852652};

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

//filter symbols, flt is assumed to be 41 taps long
void filter_symbols(int8_t* out, const int8_t* in, const int32_t* flt, uint8_t phase_inv)
{
	static int32_t last[41]; //memory for last samples

	for(uint8_t i=0; i<SYM_PER_FRA; i++)
	{
		for(uint8_t j=0; j<5; j++)
		{
			for(uint8_t k=0; k<40; k++)
				last[k]=last[k+1];

			if(j==0)
			{
				if(phase_inv) //normal phase - invert the symbol stream as GBA inverts the output
					last[40]=-in[i];
				else
					last[40]= in[i];
			}
			else
				last[40]=0;

			int32_t acc=0;
			for(uint8_t k=0; k<41; k++)
				acc+=last[k]*flt[k];
			
			if(out!=NULL) out[i*5+j]=acc>>24; //shr by 24 sets gain to unity (or whatever the gain of the tap set is), but we need to crank it up some more
		}
	}
}

//generate baseband samples - add args later
void generate_baseband(uint8_t phase_inv)
{
	//flush the RRC filter
	int8_t flush[SYM_PER_FRA]={0};
	filter_symbols(NULL, flush, i_rrc_taps_5, phase_inv);

	//generate preamble
	pkt_sym_cnt=0;
	gen_preamble_i8(symbols, &pkt_sym_cnt, PREAM_LSF);
	filter_symbols((int8_t*)&samples[0][0], symbols, i_rrc_taps_5, phase_inv);

	//are our samples ok? plot a pretty sinewave
	//for(uint8_t i=0; i<80; i++)
		//set_pixel(i*3, 90-20.0f*(float)*((int8_t*)&samples[0][0]+i/*+(sizeof(samples)/4-80)*/)/127.0f, 0, 255, 0);

	//generate LSF
	gen_frame_i8(symbols, NULL, FRAME_LSF, &lsf, 0, 0);
	filter_symbols((int8_t*)&samples[1][0], symbols, i_rrc_taps_5, phase_inv);

	//generate frames
	full_packet_data[25]=0x80|(num_bytes<<2); //fix this (hardcoded single frame of length<=25)
	gen_frame_i8(symbols, full_packet_data, FRAME_PKT, NULL, 0, 0); //no counter yet
	filter_symbols((int8_t*)&samples[2][0], symbols, i_rrc_taps_5, phase_inv);

	//generate EOT
	pkt_sym_cnt=0;
	gen_eot_i8(symbols, &pkt_sym_cnt);
	filter_symbols((int8_t*)&samples[3][0], symbols, i_rrc_taps_5, phase_inv);
}

//interrupt handler
void irqh(void)
{
	//no more samples, stop dma and timer 0, 1
	REG_DMA1CNT_H = 0; //stop DMA
	REG_TM0CNT_H = 0; //disable timer 0
	REG_TM1CNT_H = 0; //disable timer 1

	//clear the interrupt(s) by acknowledging them
	REG_IF |= REG_IF;
}

int main(void)
{
	//config display
	config_display();

	//info header
	str_print(0, 0*9, 255, 255, 255, "GBA"); str_print(4*6, 0*9, 255, 255, 255, "M"); str_print(5*6, 0*9, 255, 0, 0, "17"); str_print(8*6, 0*9, 255, 255, 255, "Packet Encoder by SP5WWP");

	//IRQ
	REG_INTR_HANDLER=(uint32_t)&irqh; //pointer to the interrupt handler function
	REG_IME=1; //master enable interrupts
	REG_IE = TIM1; //enable irq for timer 1

	//input data
	sprintf(settings.msg, "Test message.");
	sprintf(settings.dst_raw, "@ALL");
	sprintf(settings.src_raw, "N0CALL");
	settings.phase=1; //normal

	//obtain data and append with CRC
	memset(full_packet_data, 0, 1*25); //replace with 32*25
	full_packet_data[0]=0x05;
	num_bytes=sprintf((char*)&full_packet_data[1], settings.msg)+2; //0x05 and 0x00
	uint16_t packet_crc=CRC_M17(full_packet_data, num_bytes);
	full_packet_data[num_bytes]  =packet_crc>>8;
	full_packet_data[num_bytes+1]=packet_crc&0xFF;
	num_bytes+=2; //count 2-byte CRC too

	//encode dst, src for the lsf struct
	uint64_t dst_enc=0, src_enc=0;
	uint16_t type=0;
	encode_callsign_value(&dst_enc, (uint8_t*)settings.dst_raw);
	encode_callsign_value(&src_enc, (uint8_t*)settings.src_raw);
	for(int8_t i=5; i>=0; i--)
	{
		lsf.dst[5-i]=(dst_enc>>(i*8))&0xFF;
		lsf.src[5-i]=(src_enc>>(i*8))&0xFF;
	}
	
	type=M17_TYPE_PACKET | M17_TYPE_CAN(settings.can); //packet mode
	lsf.type[0]=(uint16_t)type>>8;
	lsf.type[1]=(uint16_t)type&0xFF;
	memset(&lsf.meta, 0, 112/8);

	//calculate LSF CRC
	uint16_t lsf_crc=LSF_CRC(&lsf);
	lsf.crc[0]=lsf_crc>>8;
	lsf.crc[1]=lsf_crc&0xFF;

	//display params
	str_print(0, 2*9, 255, 255, 255, "DST: %s", settings.dst_raw); str_print(15*6, 2*9, 255, 255, 255, "(%04X", dst_enc>>32); str_print(15*6, 2*9, 255, 255, 255, "%13X)", dst_enc);
	str_print(0, 3*9, 255, 255, 255, "SRC: %s", settings.src_raw); str_print(15*6, 3*9, 255, 255, 255, "(%04X", src_enc>>32); str_print(15*6, 3*9, 255, 255, 255, "%13X)", src_enc);
	str_print(0, 4*9, 255, 255, 255, "Message: %s", settings.msg);

	str_print(0, 6*9, 255, 255, 255, "Phase: "); settings.phase==1 ? str_print(7*6, 6*9, 50, 255, 50, "normal") : str_print(7*6, 6*9, 255, 50, 50, "inverted");
	str_print(0, 7*9, 255, 255, 255, "LSF CRC: 0x%04X", lsf_crc);
	str_print(0, 8*9, 255, 255, 255, "PKT CRC: 0x%04X", packet_crc);

	str_print(0, SCREEN_HEIGHT-2*9+1, 255, 50, 50, "No baseband. Press START to generate.");
	str_print(0, SCREEN_HEIGHT-1*9+1, 255, 255, 255, "A: transmit, B: flip phase");

	while(1)
	{
		//skip past the rest of any current V-blank, then skip past the V-draw
		while(REG_DISPLAY_VCOUNT >= SCREEN_HEIGHT);
		while(REG_DISPLAY_VCOUNT <  SCREEN_HEIGHT);

		//get current key states (REG_KEY_INPUT stores the states inverted)
		key_states = ~REG_KEY_INPUT & KEY_ANY;
		
		if(key_states & BUTTON_A) //"A" button pressed - start playing samples
		{
			if(!DS_BUSY) //not playing samples?
			{
				for(uint8_t i=0; i<4; i++) //hardcoded length - 4 frames (preamble, LSF, data, EOT)
				{
					play_sample(&samples[i][0], SYM_PER_FRA*5);
					while(DS_BUSY);
				}
				uint32_t x=0;
				play_sample(&x, 4); //set the last 4 samples to 0 to set the idle voltage at 1/2 Vdd
			}
		}

		else if(key_states & BUTTON_B) //"B" pressed - flip the phase setting
		{
			settings.phase = !settings.phase;
			str_print(7*6, 6*9, 0, 0, 0, "\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F"); //clear
			settings.phase==1 ? str_print(7*6, 6*9, 50, 255, 50, "normal") : str_print(7*6, 6*9, 255, 255, 50, "inverted");
			for(uint16_t i=0; i<50000; i++) __asm("NOP"); //add a small delay
		}

		else if(key_states & BUTTON_START)
		{
			str_print(0, SCREEN_HEIGHT-2*9+1, 0, 0, 0, "\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F"); //clear
			str_print(0, SCREEN_HEIGHT-2*9+1, 255, 255, 50, "Generating baseband...");
			generate_baseband(settings.phase);
			str_print(0, SCREEN_HEIGHT-2*9+1, 0, 0, 0, "Generating baseband..."); //clear
			str_print(0, SCREEN_HEIGHT-2*9+1, 50, 255, 50, "Baseband ready.");
		}
	}

	return 0;
}
