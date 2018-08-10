
CC = $(CROSS_COMPILE)gcc
CFLAGS += -std=gnu99 -I. -O3 -g -O0
LDFLAGS += -lm -lpthread

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

toasc: toasc.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f toasc *.o

