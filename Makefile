all: sdlfix.so

sdlfix.so: sdl_scaler_arcademini.o
	$(CC) -shared -fpic -fPIE -o $@ $<

CFLAGS = -fpic -fPIE -fPIC -g -O0

.PHONY: clean

clean:
	$(RM) sdlfix.so sdlfix.o
