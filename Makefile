
CC = $(CROSS_COMPILE)gcc
CFLAGS += -I. -O3 -g -O0

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

toasc: toasc.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f toasc *.o

