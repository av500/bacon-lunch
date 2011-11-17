CC     = gcc
CFLAGS = -Wall -g 

OBJS   = main.o

all: bacon

bacon: $(OBJS)
	$(CC) $(CFLAGS)  -lftdi -lpthread -lm -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS)  -c $<

clean:
	rm -fr bacon $(OBJS)
