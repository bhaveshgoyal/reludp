
CC = gcc

LIBS = -lresolv -lnsl -lm -lpthread\
		/home/courses/cse533/Stevens/unpv13e_solaris2.10/libunp.a\

FLAGS = -g -O2

CFLAGS = ${FLAGS} -I/home/courses/cse533/Stevens/unpv13e_solaris2.10/lib

all: cli srv

srv:
	rm -rf ./bin/srv
	${CC} ${CFLAGS} -o bin/srv src/srv.c ${LIBS}

cli:
	rm -rf ./bin/cli
	${CC} ${CFLAGS}  -o bin/cli src/cli.c ${LIBS}

clean:
	rm -rv bin/*
