CC = gcc
SDL_CFLAGS = $(shell sdl2-config --cflags)
SDL_LDFLAGS = $(shell sdl2-config --libs)
CFLAGS = -std=c99 -D_POSIX_SOURCE -g $(SDL_CFLAGS)
LINK = -lm  $(SDL_LDFLAGS)
VPATH = src/
OUT = bin/
OBJ = obj/
SRCS = pong.c

.PHONY: all clean
all: pong

include $(SRCS:.c=.d)

pong : $(SRCS:.c=.o)
	$(CC) $(CFLAGS) $(LINK) -o $(OUT)$@ $^ 

%.o : %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.d: %.c
	@set -e; rm -f $@; \
	$(CC) -MM $(CFLAGS) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

clean :
	rm -f ./*.o; \
	rm -f ./bin/pong
