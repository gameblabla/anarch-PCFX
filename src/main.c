/**
  @file main_32x.c

  This is an 32X implementation of the game front end. It can be used to
  compile a native executable or a transpiled JS browser version with
  emscripten.

  This frontend is not strictly minimal, it could be reduced a lot. If you want
  a learning example of frontend, look at another, simpler one, e.g. terminal.

  To compile with emscripten run:

  emcc ./main_sdl.c -s USE_SDL=2 -O3 --shell-file HTMLshell.html -o game.html

  by Miloslav Ciz (drummyfish), 2019

  Released under CC0 1.0 (https://creativecommons.org/publicdomain/zero/1.0/)
  plus a waiver of all other intellectual property. The goal of this work is to
  be and remain completely in the public domain forever, available for any use
  whatsoever.
*/

#if defined(_WIN32) || defined(WIN32) || defined(__WIN32__) || defined(__NT__) || defined(__APPLE__)
  #define SFG_OS_IS_MALWARE 1
#endif

// #define SFG_START_LEVEL 1
// #define SFG_QUICK_WIN 1
// #define SFG_IMMORTAL 1
// #define SFG_ALL_LEVELS 1
// #define SFG_UNLOCK_DOOR 1
// #define SFG_REVEAL_MAP 1
// #define SFG_INFINITE_AMMO 1
// #define SFG_TIME_MULTIPLIER 512
// #define SFG_CPU_LOAD(percent) printf("CPU load: %d%\n",percent);
// #define GAME_LQ


// lower quality
#define SFG_FPS 20
#define SFG_RAYCASTING_SUBSAMPLE 3
#define SFG_DIMINISH_SPRITES 0
#define SFG_DITHERED_SHADOW 0
#define SFG_BACKGROUND_BLUR 0
#define SFG_RAYCASTING_MAX_STEPS 15
#define SFG_RAYCASTING_MAX_HITS 5
#define SFG_RAYCASTING_VISIBILITY_MAX_HITS 6
#define SFG_CAN_EXIT 0
#define SFG_DRAW_LEVEL_BACKGROUND 1

#define SFG_PLAYER_DAMAGE_MULTIPLIER 1024
#define SDL_MUSIC_VOLUME 16
#define SDL_ANALOG_DIVIDER 1024

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "types.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

#include "game_constants.h"
#include "shared_objects.h"

uint32_t ticks = 0;
extern uint32_t nframe;

//#define DEBUGFPS 1

#if 0
#define SFG_SCREEN_RESOLUTION_X 256
#define SFG_SCREEN_RESOLUTION_Y 240
#define SFG_RESOLUTION_SCALEDOWN 2
uint8_t framebuffer[(256*240)];
#define SFG_setPixel( x,  y,  colorIndex) framebuffer[y * SFG_SCREEN_RESOLUTION_X + x] = colorIndex;
#endif

#if 0
// SLOW
#define SFG_SCREEN_RESOLUTION_X 256
#define SFG_SCREEN_RESOLUTION_Y 240
#define SFG_RESOLUTION_SCALEDOWN 1
uint32_t framebuffer[(256*240)/4];
static inline void SFG_setPixel(uint32_t x, uint32_t y, uint32_t colorIndex)
{
    // Calculate the pixel index within a 32-bit value
    uint32_t pixelIndex = (x % 4);

    // Calculate the index of the 32-bit value in the buffer
    uint32_t bufferIndex = (y * (SFG_SCREEN_RESOLUTION_X >> 2)) + (x >> 2);

	uint32_t shift = (pixelIndex) << 3;

    // Set the color index in the appropriate pixel within the 32-bit value
    framebuffer[bufferIndex] &= ~(0xFF << shift); // Clear the bits for the pixel
    framebuffer[bufferIndex] |= (colorIndex << shift); // Set the bits for the pixel
}

#endif


#if 0
#define SFG_SCREEN_RESOLUTION_X 128
#define SFG_SCREEN_RESOLUTION_Y 120
#define SFG_RESOLUTION_SCALEDOWN 1
uint32_t framebuffer[(256*240)/4];
//#define SFG_setPixel(x, y, colorIndex) ((uint16_t*)framebuffer)[(y << 1) * SFG_SCREEN_RESOLUTION_X + x] = (uint16_t)(colorIndex << 8) | colorIndex;
#define SFG_setPixel(x, y, colorIndex) ((uint16_t*)framebuffer)[(y << 8) + x] = (uint16_t)(colorIndex << 8) | colorIndex;
#endif

#if 1
#define SFG_SCREEN_RESOLUTION_X 128
#define SFG_SCREEN_RESOLUTION_Y 120
#define SFG_RESOLUTION_SCALEDOWN 1
uint32_t framebuffer[(256*240)/4];
static inline void SFG_setPixel(uint32_t x, uint32_t y, uint32_t colorIndex)
{
    // Calculate the index into the framebuffer, accounting for the spacing
    //uint32_t index = ((y << 1) * SFG_SCREEN_RESOLUTION_X) + x;
    uint32_t index = (y << 8) + x;
    uint32_t c = (uint32_t)((colorIndex << 8) | colorIndex);
    
    // Write the color index to the framebuffer for two consecutive lines
    ((uint16_t*)framebuffer)[index] = c;
    ((uint16_t*)framebuffer)[index + SFG_SCREEN_RESOLUTION_X] = c;
}
#endif


#include "game.h"
#include "sounds.h"
#include "fastking.h"
#include "pcfx.h"
#include "snd.h"


#define KRAM_PAGE0 0x00000000
#define KRAM_PAGE1 0x80000000

#define ADPCM_OFFSET 0x00000000 | KRAM_PAGE1
#define CHANNEL_0_ADPCM 0x51
#define CHANNEL_1_ADPCM 0x52

#define CDDA_SILENT 0
#define CDDA_NORMAL 0x03
#define CDDA_LOOP 0x04


#define WAIT_CD 0x800

static void cd_start_track(u8 start)
{	
	/*
	 * 
	 * To play a CD-DA track, you need to use both 0xD8 and 0xD9.
	 * 0xD8 is for the starting track and 0xD9 is for the ending track as well and controlling whenever
	 * or not the track should loop (after it's done playing).
	*/
	int r10;
	u8 scsicmd10[10];
	memset(scsicmd10, 0, sizeof(scsicmd10));
	
	scsicmd10[0] = 0xD8;
	scsicmd10[1] = 0x00;
	scsicmd10[2] = start;
	scsicmd10[9] = 0x80; // 0x80, 0x40 LBA, 0x00 MSB, Other : Illegal
	eris_low_scsi_command(scsicmd10,10);

	/* Same here. Without this, it will freeze the whole application. */
	r10 = WAIT_CD; 
    while (r10 != 0) {
        r10--;
		__asm__ (
        "nop\n"
        "nop\n"
        "nop\n"
        "nop"
        :
        :
        :
		);
    }
	eris_low_scsi_status();
}

static void cd_end_track(u8 end, u8 loop)
{	
	/*
	 * 
	 * To play a CD-DA track, you need to use both 0xD8 and 0xD9.
	 * 0xD8 is for the starting track and 0xD9 is for the ending track as well and controlling whenever
	 * or not the track should loop (after it's done playing).
	*/
	int r10;
	u8 scsicmd10[10];
	
	memset(scsicmd10, 0, sizeof(scsicmd10));
	scsicmd10[0] = 0xD9;
	scsicmd10[1] = loop; // 0 : Silent, 4: Loop, Other: Normal
	scsicmd10[2] = end;
	scsicmd10[9] = 0x80; // 0x80, 0x40 LBA, 0x00 MSB, Other : Illegal

	eris_low_scsi_command(scsicmd10,10);
	
	/* Same here. Without this, it will freeze the whole application. */
	r10 = WAIT_CD; 
    while (r10 != 0) {
        r10--;
		__asm__ (
        "nop\n"
        "nop\n"
        "nop\n"
        "nop"
        :
        :
        :
		);
    }
	eris_low_scsi_status();

}


// now implement the Anarch API functions (SFG_*)

/*
void SFG_setPixel(uint16_t x, uint16_t y, uint8_t colorIndex)
{
    // Calculate the index into the framebuffer, accounting for the spacing
    uint32_t index = (y * 2) * SFG_SCREEN_RESOLUTION_X + x;
    // Write the color index to the framebuffer
    ((uint16_t*)framebuffer)[index] = (uint16_t)(colorIndex << 8) | colorIndex;


    //((uint16_t*)framebuffer)[y * SFG_SCREEN_RESOLUTION_X + x] = (uint16_t)(colorIndex << 8) | colorIndex;
    
    
   //framebuffer[y * SFG_SCREEN_RESOLUTION_X * 2 + x * 2] = colorIndex;
   // framebuffer[y * SFG_SCREEN_RESOLUTION_X * 2 + x * 2 + 1] = colorIndex;
}*/

uint32_t SFG_getTimeMs()
{
  return ticks * 100;
}

void SFG_save(uint8_t data[SFG_SAVE_SIZE])
{
  /*FILE *f = fopen(SFG_SAVE_FILE_PATH,"wb");

  puts("SDL: opening and writing save file");

  if (f == NULL)
  {
    puts("SDL: could not open the file!");
    return;
  }

  fwrite(data,1,SFG_SAVE_SIZE,f);

  fclose(f);*/
}

uint8_t SFG_load(uint8_t data[SFG_SAVE_SIZE])
{/*
#ifndef __EMSCRIPTEN__
  FILE *f = fopen(SFG_SAVE_FILE_PATH,"rb");

  puts("SDL: opening and reading save file");

  if (f == NULL)
  {
    puts("SDL: no save file to open");
  }
  else
  {
    fread(data,1,SFG_SAVE_SIZE,f);
    fclose(f);
  }

  return 1;
#else
  // no saving for web version
  return 0;
#endif*/
	return 0;
}

void SFG_sleepMs(uint16_t timeMs)
{/*
#ifndef __EMSCRIPTEN__
  usleep(timeMs * 1000);
#endif*/
}



void SFG_getMouseOffset(int16_t *x, int16_t *y)
{
/*
#ifndef __EMSCRIPTEN__

#if !defined(__WIIU__)
  if (mouseMoved)
  {
    int mX, mY;

    SDL_GetMouseState(&mX,&mY);

    *x = mX - SFG_SCREEN_RESOLUTION_X / 2;
    *y = mY - SFG_SCREEN_RESOLUTION_Y / 2;

    SDL_WarpMouseInWindow(window,
      SFG_SCREEN_RESOLUTION_X / 2, SFG_SCREEN_RESOLUTION_Y / 2);
  }
#endif
  if (sdlController != NULL)
  {
    *x +=
      (SDL_GameControllerGetAxis(sdlController,SDL_CONTROLLER_AXIS_RIGHTX) + 
      SDL_GameControllerGetAxis(sdlController,SDL_CONTROLLER_AXIS_LEFTX)) /
      SDL_ANALOG_DIVIDER;

    *y +=
      (SDL_GameControllerGetAxis(sdlController,SDL_CONTROLLER_AXIS_RIGHTY) + 
      SDL_GameControllerGetAxis(sdlController,SDL_CONTROLLER_AXIS_LEFTY)) /
      SDL_ANALOG_DIVIDER;
  }
#endif
*/
}

void SFG_processEvent(uint8_t event, uint8_t data)
{
}


static uint32_t padtype, paddata;

int8_t SFG_keyPressed(uint8_t key)
{
	#define b(x) ((paddata & (1 << x) == 1))
	switch (key)
	{
		case SFG_KEY_UP: if (paddata & (1 << 8)) { return 1;} return 0; break;
		case SFG_KEY_DOWN: if (paddata & (1 << 10)) { return 1;} return 0; break;
		case SFG_KEY_LEFT: if (paddata & (1 << 11)) { return 1;} return 0; break;
		case SFG_KEY_RIGHT: if (paddata & (1 << 9)) { return 1;} return 0; break;

		
		case SFG_KEY_A: if (paddata & (1 << 0)) { return 1;} return 0; break;
		case SFG_KEY_B: if (paddata & (1 << 1)) { return 1;} return 0; break;
		case SFG_KEY_C: if (paddata & (1 << 2)) { return 1;} return 0; break;
		case SFG_KEY_JUMP: if (paddata & (1 << 3)) { return 1;} return 0; break;
		case SFG_KEY_STRAFE_LEFT: if (paddata & (1 << 4)) { return 1;} return 0; break;
		case SFG_KEY_STRAFE_RIGHT: if (paddata & (1 << 5)) { return 1;} return 0;break;
		
		//case SFG_KEY_MAP: if (paddata & (1 << 8)) { return 1;} return 0; break;
		
		case SFG_KEY_CYCLE_WEAPON: if (paddata & (1 << 6)) { return 1;} return 0; break;
		case SFG_KEY_MENU: if (paddata & (1 << 7)) { return 1;} return 0; break;
	   /* case SFG_KEY_NEXT_WEAPON:
		  if (b(ZR))
			return 1;
		  else
		  return 0;
		  break;*/

		default: return 0; break;
	}

  #undef b
}
  
int running = 1;

/*
void SFG_drawText(
  const char *text,
  uint16_t x,
  uint16_t y,
  uint8_t size,
  uint8_t color,
  uint16_t maxLength,
  uint16_t limitX)
*/

static inline char* myitoa(int val)
{
	
	static char buf[32] = {0};
	int base = 10;
	int i = 30;
	
	for(; val && i ; --i, val /= base)
	
		buf[i] = "0123456789abcdef"[val % base];
	
	return &buf[i+1];
	
}


void mainLoopIteration()
{
	padtype = eris_pad_type(0);
	paddata = eris_pad_read(0);

	if (!SFG_mainLoopBody())
		running = 0;

#ifdef DEBUGFPS
	SFG_drawText(myitoa(getFps()),8,8,
    SFG_FONT_SIZE_SMALL,4,0,0);
#endif

	eris_king_set_kram_write(1, 1);
	king_kram_write_buffer(framebuffer, (256*240));
	
	ticks++;
	++nframe;
}

uint8_t musicOn = 0;

int musicindex = 0;

void SFG_setMusic(uint8_t value)
{
  switch (value)
  {
    case SFG_MUSIC_TURN_ON: musicOn = 1; break;
    case SFG_MUSIC_TURN_OFF: musicOn = 0; break;
    case SFG_MUSIC_NEXT: 
		if (musicOn == 0) return;
		//SFG_nextMusicTrack(); 
		if (musicindex > 2) musicindex = 0;
		
		#ifndef FXUPLOADER
		switch (musicindex)
		{
			case 0:
				cd_start_track(5);
				cd_end_track(6, CDDA_LOOP);
			break;
			case 1:
				cd_start_track(3);
				cd_end_track(4, CDDA_LOOP);
			break;
			case 2:
				cd_start_track(6);
				cd_end_track(7, CDDA_LOOP);
			break;
		}
		#endif
		musicindex++;
		
	break;
    case SFG_MUSIC_TITLE:
		if (musicOn == 0) return;
		#ifndef FXUPLOADER
		cd_start_track(2);
		cd_end_track(3, CDDA_LOOP);
		#endif
    break;
    case SFG_MUSIC_WIN:
		if (musicOn == 0) return;
		#ifndef FXUPLOADER
		cd_start_track(4);
		cd_end_track(5, CDDA_NORMAL);
		#endif
    break;
    
    default: break;
  }
}

void SFG_playSound(uint8_t soundIndex, uint8_t volume)
{
	uint32_t addr;
	if (volume == 0) return;
	
  switch (soundIndex)
  {
    case 2: 
		addr = 4096*6 | KRAM_PAGE1;
		Play_ADPCM(1, addr, sizeof(shot),0, ADPCM_RATE_32000); // Monster death
      break;

    case 5:
		addr = 4096*3 | KRAM_PAGE1;
		Play_ADPCM(0, addr, sizeof(monster),0, ADPCM_RATE_32000); // Hurt
      break;

    default:
		addr = ADPCM_OFFSET;
		Play_ADPCM(0, addr, sizeof(click), 0, ADPCM_RATE_32000); // Click
      break;
  }	
}

void handleSignal(int signal)
{
  running = 0;
}




unsigned short mypal[256] = {
0x88,0x288,0xc88,0x2288,0x4d88,0x8388,0xcf88,0xff88,
0x188,0x588,0xf89,0x1b89,0x337a,0x4e7b,0x776d,0x976d,
0x188,0x988,0x1588,0x2c79,0x4b69,0x716a,0xa85b,0xcf4a,
0x388,0xb88,0x1c78,0x3678,0x5968,0x9048,0xd038,0xef49,
0x288,0x988,0x1977,0x2d77,0x4c67,0x7556,0xaf44,0xcd55,
0x288,0x988,0x1787,0x2e77,0x4b76,0x7375,0xac63,0xcc74,
0x388,0xa88,0x1887,0x3187,0x4e96,0x7d95,0xbca3,0xd694,
0x188,0x588,0xd98,0x1b97,0x33a7,0x51b7,0x77c6,0xa1b6,
0x188,0x488,0xb98,0x1598,0x26a8,0x3eb8,0x5ad8,0x83c8,
0x188,0x588,0x1098,0x1e99,0x35a9,0x55ba,0x79bb,0xacbc,
0x188,0x688,0x1089,0x1e89,0x378a,0x548b,0x819d,0xa29c,
0x188,0x589,0xb89,0x197b,0x277c,0x396e,0x4f60,0x586f,
0x288,0x888,0x1379,0x277a,0x416b,0x605c,0x893d,0x9c3c,
0x488,0xe78,0x2078,0x3f67,0x6747,0x9537,0xcd17,0xd918,
0x488,0xb88,0x1977,0x3066,0x5055,0x7344,0x9e32,0xa433,
0x488,0xb87,0x1977,0x2f76,0x5064,0x7353,0x9c41,0xa451,
0x488,0xd87,0x1d97,0x3696,0x5894,0x83a2,0xb5b0,0xbaa1,
0x188,0x388,0x898,0x10a7,0x1db7,0x2ad6,0x3ef6,0x52e5,
0x88,0x298,0x598,0xaa8,0x11c7,0x1be7,0x2807,0x33f7,
0x188,0x588,0xc99,0x17a9,0x28ba,0x3bcb,0x54ec,0x64dd,
0x188,0x589,0xc89,0x1c8a,0x2d8c,0x418d,0x5b9f,0x669f,
0x188,0x589,0xb89,0x147a,0x237c,0x336d,0x475f,0x4c60,
0x288,0x688,0xf79,0x1b7a,0x306b,0x466c,0x634e,0x744e,
0x588,0xf78,0x2078,0x3a68,0x6258,0x9438,0xcc18,0xe109,
0x488,0xb88,0x1777,0x2c66,0x4855,0x6c44,0x9532,0x9f22,
0x488,0xb88,0x1677,0x2b66,0x4365,0x6543,0x9032,0x9531,
0x488,0xc87,0x1887,0x2d76,0x4775,0x6c73,0x9c61,0xa471,
0x188,0x688,0xd97,0x18a7,0x28b6,0x3db5,0x5ad4,0x6fd3,
0x88,0x198,0x498,0x8a8,0xec7,0x14d7,0x1cf7,0x2107,
0x88,0x298,0x498,0x8a8,0xfc7,0x15d7,0x1ff7,0x2307,
0x288,0x598,0xb99,0x17a9,0x26ba,0x3acb,0x52dd,0x63de,
0x188,0x589,0xb89,0x158a,0x257c,0x377d,0x4d7f,0x5480,

};

int main(int argc, char *argv[])
{
	uint32_t addr;
	eris_king_init();
	eris_tetsu_init();
	eris_pad_init(0);
	//eris_pad_init(1);
	
	
	Initialize_ADPCM(ADPCM_RATE_32000);
	eris_low_cdda_set_volume(63,63);
	
	addr = ADPCM_OFFSET;
	eris_king_set_kram_write(addr, 1);	
	king_kram_write_buffer(click, sizeof(click));
	
	/*addr = 4096 | KRAM_PAGE1;
	eris_king_set_kram_write(addr, 1);	
	king_kram_write_buffer(door, sizeof(door));
	
	addr = 4096*2 | KRAM_PAGE1;
	eris_king_set_kram_write(addr, 1);	
	king_kram_write_buffer(explosion, sizeof(explosion));*/
	
	addr = 4096*3 | KRAM_PAGE1;
	eris_king_set_kram_write(addr, 1);	
	king_kram_write_buffer(monster, sizeof(monster));
	
	/*addr = 4096*4 | KRAM_PAGE1;
	eris_king_set_kram_write(addr, 1);	
	king_kram_write_buffer(noise, sizeof(noise));*/
	
	/*addr = 4096*5 | KRAM_PAGE1;
	eris_king_set_kram_write(addr, 1);	
	king_kram_write_buffer(plasma, sizeof(plasma));*/
	
	addr = 4096*6 | KRAM_PAGE1;
	eris_king_set_kram_write(addr, 1);	
	king_kram_write_buffer(shot, sizeof(shot));
	
	Set_Video(KING_BGMODE_256_PAL);
	Upload_Palette(mypal, 256);
	initTimer(0, 1423);

	
	SFG_init();
  
	running = 1;
	
	
	while (running)
	{
		mainLoopIteration();
		
	}

  return 0;
}
