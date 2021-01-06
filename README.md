![CI]
[![Coverage Status]][Coverage Details]

## Introduction

This project is a shared library to deterministically inject failures
into libc calls made by programs for testing coverage purposes. The
library typically is used with LD_PRELOAD to instrument a program under
test without needing to modify it.

The idea for this library was inspired by the [mallocfail] project.
However, this project had the following additional goals that mallocfail
did not meet::

  1. Fewer dependencies. The goal is for this library to consist of onl<y
     a single C file that can be easily integrated into an existing testing
     framework. The only dependency is `libunwind` (which is packaged by
     most distributions).

  2. Support more than just malloc. This library injects failures into
     standard Unix interfaces like `open()` and `close()` as well as
     stdio routines such as `fopen()` and `fclose()`.

  3. Try to check for correctness. Mallocfail only injects the error,
     it doesn't do much to ensure that the program under test cleans
     up correctly. The original hope was to pair this library with
     valgrind to ensure no memory is leaked, however valgrind does not
     play well with LD_PRELOAD so this was not possible. Instead, this
     library does some rudimentary checking to ensure all `malloc()`
     calls have a corresponding free() call and all open() calls have
     a corresponding close() call.

## Theory of Operation

When this library is pre-loaded with a program it overrides a number of
standard libc functions. Every time one of thes functions is called, the
library hashes the symbolic execution stack and checks whether the hash
is in a database. If it is not, it returns an appropriate failure from
the call and stores the hash in the database. In this way, subsequent
runs of the program will fail each libc call until all calls have seen
a failure. This technique can be used to approach 100% coverage in a C
program.

If the library itself hits an error (ie. failure to allocate memory or
read/write the database) it will return error code 32. If it detects that
there was a file or memory leak it will return 33. Otherwise, it returns
the same error code the program would have reported. These error codes can
be changed with environment variables (see below) in case they
conflict with an the test program's return code..

## Usage

   1. Install the necessary libunwind headers for your system:

     apt install libunwind-dev      or
     yum install libunwind-devel    or
     dnf install libundind-devel

   2. Build the library from the C file either using the included
      `Makefile` or a call to gcc such as:

     cc -shared -fPIC -Werror -Wall -g -O2  libfailinj.c -ldl -lunwind -o libfailinj.so

   3. Run the program under test with the library, repeatedly, until it
      succeeds permanently:

     LD_PRELOAD=./libfailinj.so ./program_to_test <args>

   4. To restart the test from the beginning, delete `failinj.db`

## Configuration

Environment variables can be used to change the exit codes, ignore
specific leaks under specific functions or skip failure injections for
calls under a specific function.

  * `FAILINJ_DATABASE` - The filename to use as the database. If not
     specified uses `failinj.db` in the current working directory.

  * `FAILINJ_EXIT_ERROR` - Error code to use if libfailinj encounters an
     error during its own execution (eg. failure to allocate memory or
     read/write the database).

  * `FAILINJ_BUG_FOUND` - Error code to use if libfailinj detects a bug
    in the running program such as leaked memory or un-closed files.

  * `FAILINJ_SKIP_INJECTION` - Skip injections that have a specific
     function in their execution stack. If a function in the execution
     stack is found within this variable, the injection will not occur.
     This is fairly simplistic at this point so care may need to be
     taken with overly generally function names.

The following environment variables can be used to ignore specific types
of errors in specific functions. The all take a space separated list of
function names which, if seen in the back trace, cause libfailinj to
ignore errors under those functions.

  * `FAILINJ_IGNORE_MEM_LEAKS` - Ignore memory that was allocated and
    never freed. The functions specified should be where the memory was
    allocated.

  * `FAILINJ_IGNORE_FD_LEAKS` - Ignore file descriptors that were opened
    and never closed. The functions specified should be where the
    descriptor was opened.

  * `FAILINJ_IGNORE_FILE_LEAKS` - Ignore FILEs that were fopened but
    never fclosed.

  * `FAILINJ_IGNORE_UNTRACKED_FREES` - Ignore free() calls that did not
    have a corresponding malloc() call.

  * `FAILINJ_IGNORE_UNTRACKED_CLOSES` - Ignore close() calls that did not
    have a corresponding open() call.

  * `FAILINJ_IGNORE_UNTRACKED_FCLOSES` - Ignore fclose() calls that did not
    have a corresponding fopen() call.

The following environment variables, if set at all, cause libfailinj to
ignore entire classes of errors.

  * `FAILINJ_IGNORE_ALL_MEM_LEAKS` - Ignore all memory leaks.

  * `FAILINJ_IGNORE_ALL_FD_LEAKS` - Ignore all file descriptor leaks.

  * `FAILINJ_IGNORE_ALL_FILE_LEAKS` - Ignore all FILE* leaks.

  * `FAILINJ_IGNORE_ALL_UNTRACKED_FREES` - Ignore all untracked frees

  * `FAILINJ_IGNORE_ALL_UNTRACKED_CLOSES` - Ignore all untracked closes

  * `FAILINJ_IGNORE_ALL_UNTRACKED_FCLOSES` - Ignore all untracked fcloses

## Performance

`libfailinj.so` adds some overhead to every system call to decide whether
to inject a failure and track resource. It alsos requires a small amount
of memory and disk space for every call. It maintains a number of hash tables
for each call-site, allocation, fd and file. Complicated programs under test
may also take a large number of runs to fully test every branch so
this technique may not be suitable for all cases. Your mileage may vary.

## Threading

The library holds a mutex while accessing the hash tables, so
it should, in theory, work with threads. However, this has not been
tested at this time and other issues may exist. Patches welcome if
bugs are found.


[mallocfail]: https://github.com/ralight/mallocfail

[CI]: https://github.com/lsgunth/libfailinj/workflows/CI/badge.svg
[Coverage Status]: https://coveralls.io/repos/github/lsgunth/failcov/badge.svg?branch=main
[Coverage Details]: https://coveralls.io/github/lsgunth/failcov?branch=main
