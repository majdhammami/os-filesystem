C      := gcc
FLAGS  := -Wall -g
LFLAGS := -L. -lmfs

LIB_SRC := mfs.c
DEPS_SRC := udp.c

TARGETS := server client
OBJECTS := server.o client.o mfs.o

LIBRARY := libmfs.so

.PHONY: all
all: ${TARGETS} ${LIBRARY}

server: server.o ${DEPS_SRC}
	${C} ${FLAGS} -o server server.o ${DEPS_SRC}

client: client.o ${LIBRARY}
	${C} ${FLAGS} -o client client.o ${LFLAGS}

${LIBRARY} : mfs.o ${DEPS_SRC}
	${C} ${FLAGS} -fPIC -shared -Wl,-soname,libmfs.so -o ${LIBRARY} mfs.o udp.c -lc

clean:
	rm -f ./${TARGETS} *.o ${LIBRARY} fs.img

mfs.o : ${LIB_SRC} Makefile
	${C} ${FLAGS} -c -fPIC ${LIB_SRC}

%.o: %.c Makefile
	${C} ${FLAGS} -c $<
