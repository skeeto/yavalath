HOST_CC     = $(CC)
CFLAGS      = -std=c99 -Wall -Wextra -O3 -g3
HOST_CFLAGS = $(CFLAGS)
HOST_LDLIBS = -lm

yavalath : yavalath.c tables.h
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_LDFLAGS) -o $@ $< $(HOST_LDLIBS)

tables.h : tablegen
	./tablegen > tables.h

tablegen : tablegen.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

clean :
	$(RM) yavalath tablegen tables.h
