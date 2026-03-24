CC      = gcc
CFLAGS  = -Wall -Wextra -g
 
all: controller runner
 
controller: controller.c common.h
    $(CC) $(CFLAGS) -o controller controller.c
 
runner: runner.c common.h
    $(CC) $(CFLAGS) -o runner runner.c
 
clean:
    rm -f controller runner
    rm -f $(FIFO_PATH)
 
.PHONY: all clean