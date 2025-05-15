LOGIN=revanth
SUBMITPATH=/home/cs537-1/handin/revanth/

CC=gcc
CFLAGS=-Wall -Wextra -Werror -pedantic -std=gnu18
OPTFLAGS=-O2
DBGFLAGS=-Og -ggdb
SOURCES=wsh.c

all: wsh wsh-dbg

wsh: $(SOURCES) 
	$(CC) -o $@ $^ $(CFLAGS) $(OPTFLAGS)

wsh-dbg: $(SOURCES) 
	$(CC) -o $@ $^ $(CFLAGS) $(DBGFLAGS)

clean:
	rm -f *.o wsh wsh-dbg
	rm -rf wsh-dbg.dSYM

submit: 
	cp -rf ../p3 $(SUBMITPATH)
