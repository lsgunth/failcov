
CPPFLAGS=-Werror -Wall
CFLAGS=-g -O2
LDLIBS=-ldl -lunwind

all: failcov.so test

failcov.so: failcov.c
	$(CC) -shared -fPIC $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

test: test.c

clean:
	-rm -f failcov.so failcov.db test
