CFLAGS = -std=c99 -Wall -Wextra -Ofast -g3
LDLIBS = -lm

CLI_SOURCES = cli.c yavalath.c

yavalath-cli : $(CLI_SOURCES) tables.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(CLI_SOURCES) $(LDLIBS)

tables.h : tablegen
	./tablegen > tables.h

tablegen : tablegen.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

yavalath_all.c : yavalath.c tables.h
	sed 's/^#include "tables.h"//' $< | cat tables.h - > $@

amalgamation : yavalath_all.c

clean :
	$(RM) yavalath-cli tablegen tables.h yavalath_all.c
