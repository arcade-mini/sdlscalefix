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

void upscale_256xXXX_to_480x272(uint32_t *dst, uint32_t *src, uint32_t height)
{
    uint32_t Eh = 0;
    uint32_t source = 0;
    uint32_t dh = 0;
    uint32_t y, x;

    for (y = 0; y < 272; y++)
    {
        source = dh * 256 / 2;

        for (x = 0; x < 480/30; x++)
        {
            register uint32_t ab, cd, ef, gh, ij, kl, mn, op;

            __builtin_prefetch(dst + 4, 1);
            __builtin_prefetch(src + source + 4, 0);

            ab = src[source] & 0xF7DEF7DE;
            cd = src[source + 1] & 0xF7DEF7DE;
            ef = src[source + 2] & 0xF7DEF7DE;
            gh = src[source + 3] & 0xF7DEF7DE;
            ij = src[source + 4] & 0xF7DEF7DE;
            kl = src[source + 5] & 0xF7DEF7DE;
            mn = src[source + 6] & 0xF7DEF7DE;
            op = src[source + 7] & 0xF7DEF7DE;

            *dst++ = (ab & 0xFFFF) + (ab << 16);            // [aa]
            *dst++ = (ab >> 16) + (ab & 0xFFFF0000);        // [bb]
            *dst++ = (cd & 0xFFFF) + (cd << 16);            // [cc]
            *dst++ = (cd >> 16) + (((cd & 0xF7DE0000) >> 1) + ((ef & 0xF7DE) << 15)); // [d(de)]
            *dst++ = ef;                                    // [ef]
            *dst++ = (ef >> 16) + (gh << 16);               // [fg]
            *dst++ = gh;                                    // [gh]
            *dst++ = (gh >> 16) + (ij << 16);               // [hi]
            *dst++ = ij;                                    // [ij]
            *dst++ = (ij >> 16) + (kl << 16);               // [jk]
            *dst++ = kl;                                    // [kl]
            *dst++ = (((kl & 0xF7DE0000) >> 17) + ((mn & 0xF7DE) >> 1)) + (mn << 16); // [(lm)m]
            *dst++ = (mn >> 16) + (mn & 0xFFFF0000);        // [nn]
            *dst++ = (op & 0xFFFF) + (op << 16);            // [oo]
            *dst++ = (op >> 16) + (op & 0xFFFF0000);        // [pp]

            source += 8;
        }
        Eh += height; if(Eh >= 272) { Eh -= 272; dh++; }
	}
}

void upscale_320xXXX_to_480x272(uint32_t *dst, uint32_t *src, uint32_t height)
{
	uint32_t Eh = 0;
	uint32_t source = 0;
	uint32_t dh = 0;
	uint32_t y, x;

	for (y = 0; y < 272; y++)
	{
		source = dh * 320/2;

		for (x = 0; x < 480/6; x++)
		{
			register uint32_t ab, cd;

			__builtin_prefetch(dst + 4, 1);
			__builtin_prefetch(src + source + 4, 0);

			ab = src[source] & 0xF7DEF7DE;
			cd = src[source + 1] & 0xF7DEF7DE;
			
			*dst++ = (ab & 0xFFFF) + AVERAGEHI(ab);
			*dst++ = (ab >> 16) + ((cd & 0xFFFF) << 16);
			*dst++ = (cd & 0xFFFF0000) + AVERAGELO(cd);

			source += 2;
		}
		Eh += height;
		if(Eh >= 272)
		{
			Eh -= 272;
			dh++; 
		}
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

	if (screenWidth == 480 && screenHeight == 272)
	{
		inf->current_h = 272;
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
	realScreen = (SDLSurface *)real_func(480, 272, 16, SDL_HWSURFACE);
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
			case 256:
				upscale_256xXXX_to_480x272((uint32_t *)realScreen->pixels, (uint32_t *)screen->pixels, screen->h);
			break;
			case 320:
				upscale_320xXXX_to_480x272((uint32_t *)realScreen->pixels, (uint32_t *)screen->pixels, screen->h);
			break;
			/* Assume height is also 272 for speed and size reasons */
			case 480:
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
