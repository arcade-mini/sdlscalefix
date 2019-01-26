#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
int force = 1;

#define SDL_HWSURFACE 0x00000001

typedef struct SDLPixelFormat
{
  void *palette;
  uint8_t BitsPerPixel;
  uint8_t BytesPerPixel;
  uint8_t Rloss, Gloss, Bloss, Aloss;
  uint8_t Rshift, Gshift, Bshift, Ashift;
  uint32_t Rmask, Gmask, Bmask, Amask;
  uint32_t colorkey;
  uint8_t alpha;
} SDLPixelFormat;

typedef struct
{
  uint32_t hw_available : 1;
  uint32_t wm_available : 1;
  uint32_t blit_hw : 1;
  uint32_t blit_hw_CC : 1;
  uint32_t blit_hw_A : 1;
  uint32_t blit_sw : 1;
  uint32_t blit_sw_CC : 1;
  uint32_t blit_sw_A : 1;
  uint32_t blit_fill : 1;
  uint32_t video_mem;
  void *vfmt;
  int current_w;
  int current_h;
} SDLVideoInfo;

typedef struct SDLSurface
{
  uint32_t flags;
  SDLPixelFormat *format;
  int w, h;
  int pitch;
  void *pixels;
  void *userdata;
  int locked;
  void *lock_data;

} SDLSurface;

SDLSurface *screen = NULL;
SDLSurface *realScreen = NULL;

#define AVERAGEHI(AB) ((((AB) & 0xF7DE0000) >> 1) + (((AB) & 0xF7DE) << 15))
#define AVERAGELO(CD) ((((CD) & 0xF7DE) >> 1) + (((CD) & 0xF7DE0000) >> 17))

void upscale_320xXXX_to_800x480(uint32_t *dst, uint32_t *src, uint32_t height)
{
    uint32_t Eh = 0;
    uint32_t source = 0;
    uint32_t dh = 0;
    uint32_t y, x;
    for (y = 0; y < 480; y++)
    {
        source = dh * 320/2;

        for (x = 0; x < 800/10; x++)
        {
			register uint32_t ab, cd;
			
			__builtin_prefetch(dst + 4, 1);
			__builtin_prefetch(src + source + 4, 0);

            ab = src[source] & 0xF7DEF7DE;
            cd = src[source + 1] & 0xF7DEF7DE;

            *dst++ = (ab & 0xFFFF) | (ab << 16);
            *dst++ = (((ab & 0xFFFF) >> 1) + ((ab & 0xFFFF0000) >> 17)) | (ab & 0xFFFF0000);
            *dst++ = (ab >> 16) | (cd << 16);
            *dst++ = (cd & 0xFFFF) | (((cd & 0xFFFF) << 15) + ((cd & 0xFFFF0000) >> 1));
            *dst++ = (cd >> 16) | (cd & 0xFFFF0000);

            source += 2;
        }
        Eh += height; if(Eh >= 480) { Eh -= 480; dh++; }
    }
}

void bitmap_scale(uint32_t startx, uint32_t starty, uint32_t viswidth, uint32_t visheight, uint32_t newwidth, uint32_t newheight,uint32_t pitchsrc,uint32_t pitchdest, uint16_t *src, uint16_t *dst)
{
    uint32_t W,H,ix,iy,x,y;
    
    x=startx<<16;
    y=starty<<16;
    W=newwidth;
    H=newheight;
    ix=(viswidth<<16)/W;
    iy=(visheight<<16)/H;

    do 
    {
        uint16_t *buffer_mem = &src[(y>>16)*pitchsrc];
        W=newwidth; x=startx<<16;
        do 
        {
            *dst++=buffer_mem[x>>16];
			x+=ix;
        } while (--W);
        dst+=pitchdest;
        y+=iy;
    } while (--H);
}

void *SDL_GetVideoInfo2()
{
	static void *(*info)() = NULL;

	if (info == NULL)
	{
	info = dlsym(RTLD_NEXT, "SDL_GetVideoInfo");
	assert(info != NULL);
	}

	SDLVideoInfo *inf = (SDLVideoInfo *)info(); //<-- calls SDL_GetVideoInfo();
	int screenWidth = inf->current_w;
	int screenHeight = inf->current_h;

	if (screenWidth == 800 && screenHeight == 480)
	{
		inf->current_h = 480;
	}
	
	return inf;
}

void *SDL_SetVideoMode(int width, int height, int bitsperpixel, uint32_t flags)
{
	static void *(*real_func)(int, int, int, uint32_t) = NULL;
	static void *(*create)(uint32_t, int, int, int, uint32_t, uint32_t, uint32_t, uint32_t) = NULL;

	printf("Call to SDL_SetVideoMode intercepted %d %d %d\n", width, height, flags);

	if (real_func == NULL)
	{
		real_func = dlsym(RTLD_NEXT, "SDL_SetVideoMode");
		assert(real_func != NULL);
	}

	if (create == NULL)
	{
		create = dlsym(RTLD_NEXT, "SDL_CreateRGBSurface");
		assert(create != NULL);
	}
	
	screen = (SDLSurface *)create(0, width, height, 16, 0, 0, 0, 0);
	realScreen = (SDLSurface *)real_func(800, 480, 16, SDL_HWSURFACE);
	return screen;
	return realScreen;
}

static void *(*flip)() = NULL;

void *SDL_Flip(void *surface)
{
	if (flip == NULL)
	{
		flip = dlsym(RTLD_NEXT, "SDL_Flip");
		assert(flip != NULL);
	}

	if (surface == realScreen)
	{
		return flip(surface);
	}
	else if (surface == screen)
	{
		switch(screen->w)
		{
			case 320:
				upscale_320xXXX_to_800x480((uint32_t *)realScreen->pixels, (uint32_t *)screen->pixels, screen->h);
			break;
			case 800:
				memmove(realScreen->pixels, screen->pixels, (realScreen->w * realScreen->h)*2);
			break;
			default:
				bitmap_scale(0, 0, screen->w, screen->h, realScreen->w, realScreen->h, screen->w, 0, (uint16_t*)screen->pixels, (uint16_t*)realScreen->pixels);
			break;
		}
		return flip(realScreen);
	}
	else
	{
		return flip(surface);
	}
}
