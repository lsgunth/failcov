
CPPFLAGS=-Werror -Wall
CFLAGS=-g -O2
LDLIBS=-ldl -lunwind
LCOVFLAGS=--no-external

ifeq ($(COVERAGE),1)
  CFLAGS += -fprofile-arcs -ftest-coverage -Og
  LDFLAGS += -fprofile-arcs
endif

all: failcov.so test

failcov.so: failcov.c
	$(CC) -shared -fPIC $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

test: test.c

coverage.info:
	geninfo $(LCOVFLAGS) . -o $@

clean:
	-rm -f failcov.so failcov.db test *.gcno *.gcda *.info
