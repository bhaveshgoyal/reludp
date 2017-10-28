
CC = gcc

LIBS = -lresolv -lnsl -lpthread\
		/home/bhavesh/Desktop/CSE533/hw2/libs/unpv13e/libunp.a\

FLAGS = -g -O2

CFLAGS = ${FLAGS} -I/home/bhavesh/Desktop/CSE533/hw2/libs/unpv13e/lib

all: cli srv

srv:
	rm -rf ./bin/srv
	${CC} ${CFLAGS} -o bin/srv src/srv.c ${LIBS}

cli:
	rm -rf ./bin/cli
	${CC} ${CFLAGS}  -o bin/cli src/cli.c ${LIBS}

clean:
	rm -rv bin/*
