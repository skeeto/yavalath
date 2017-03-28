.POSIX:
CC     = cc -std=c99
CFLAGS = -Wall -Wextra -Ofast -g3
LDLIBS = -lm

CLI_SOURCES = cli.c yavalath_ai.c

yavalath-cli : $(CLI_SOURCES) tables.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(CLI_SOURCES) $(LDLIBS)

tables.h : tablegen
	./tablegen > tables.h

tablegen : tablegen.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ tablegen.c

yavalath.c : yavalath_ai.c tables.h
	sed 's/^#include "tables.h"//' $< | cat tables.h - > $@

amalgamation : yavalath.c

clean :
	rm -f yavalath-cli tablegen tables.h yavalath.c
