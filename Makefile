
CPPFLAGS=-Werror -Wall
CFLAGS=-g -O2
LDLIBS=-ldl -lunwind

failcov.so: failcov.c
	$(CC) -shared -fPIC $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

clean:
	-rm -f failcov.so failcov.db
