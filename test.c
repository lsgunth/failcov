// SPDX-License-Identifier: MIT
/* Copyright (C) 2020, Logan Gunthorpe */

#define _GNU_SOURCE

#include <asm/unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

static int test_fd(void *x)
{
	ssize_t rd;
	int fd;

	fd = open("/dev/zero", O_RDWR);
	if (fd == -1) {
		perror("Unable to open /dev/zero");
		return 1;
	}

	rd = read(fd, x, 50);
	if (rd < 0) {
		perror("Failed to read /dev/zero");
		return 1;
	}

	rd = write(fd, x, 50);
	if (rd < 0) {
		perror("Failed to write /dev/zero");
		close(fd);
		close(819);
		return 1;
	}

	close(fd);

	return 0;
}

static int test_openat(void)
{
	int val, fd, ret;
	ssize_t rd;

	fd = openat(AT_FDCWD, "/dev/urandom", O_RDONLY);
	if (fd == -1) {
		perror("Unable to open /dev/urandom");
		return 1;
	}

	rd = read(fd, &val, sizeof(val));
	if (rd != sizeof(val)) {
		perror("Could not read /dev/urandom");
		return 1;
	}

	srand(val);

	ret = close(fd);
	if (ret) {
		perror("Error closing /dev/urandom");
		return 1;
	}

	return 0;
}

static int test_stdio(void *x)
{
	char *line = NULL;
	size_t len;
	ssize_t cnt;
	FILE *f;
	int ret;

	f = fopen("/dev/null", "w+");
	if (!f) {
		perror("Unable to open /dev/null");
		return 1;
	}

	cnt = fwrite(x, 1, 50, f);
	if (cnt != 50) {
		perror("Unable to write to /dev/null");
		return 1;
	}

	cnt = fread(x, 1, 50, f);
	if (cnt != 50 && ferror(f)) {
		perror("Unable to read from /dev/null");
		clearerr(f);
		return 1;
	}

	cnt = fscanf(f, "abc\n");
	if (cnt == EOF && ferror(f)) {
		perror("Unable to fscan from /dev/null");
		clearerr(f);
		return 1;
	}

	errno = 0;
	cnt = getline(&line, &len, f);
	if (cnt < 0 && errno) {
		perror("getline failure");
		free(line);
		return 1;
	}

	free(line);
	line = NULL;

	errno = 0;
	cnt = getdelim(&line, &len, ';', f);
	if (cnt < 0 && errno) {
		perror("getdelim failure");
		free(line);
		return 1;
	}

	free(line);

	ret = fflush(f);
	if (ret == EOF) {
		perror("Error while flushing to /dev/null");
		return 1;
	}

	ret = fclose(f);
	if (ret == EOF) {
		ret = 1;
		perror("Error while to closing /dev/null");
	}

	return ret;
}

static volatile int test_scanf(void)
{
	const char test[] = "Hello: 12345\n";
	int rc, x;

	rc = sscanf(test, "Hello: %d", &x);
	if (rc != 1) {
		perror("sscanf failed");
		return 1;
	}

	return 0;
}

static int test_fcloseall(void)
{
	FILE *a, *b;
	int ret;

	a = fopen("/dev/urandom", "rb");
	if (!a) {
		perror("Unable to open /dev/urandom");
		return 1;
	}

	b = fopen("/dev/random", "rb");
	if (!b) {
		perror("Unable to open /dev/random");
		return 1;
	}

	(void)a;
	(void)b;

	ret = fcloseall();
	if (ret) {
		perror("Error while closing all files");
		return 1;
	}

	return 0;
}

static int test_fmemopen(void)
{
	char buf[4096];
	FILE *f;
	int ret;

	f = fmemopen(buf, sizeof(buf), "w");
	if (!f) {
		perror("Unable to open memory FILE");
		return 1;
	}

	ret = fclose(f);
	if (ret == EOF) {
		perror("Failure closing memory FILE");
		return 1;
	}

	return 0;
}

static int test_tmpfile(void)
{
	FILE *f;
	int ret;

	f = tmpfile();
	if (!f) {
		perror("Unable to open temporary FILE");
		return 1;
	}

	ret = fclose(f);
	if (ret == EOF) {
		perror("Failure closing temporary FILE");
		return 1;
	}

	return 0;
}

static int test_creat_fdopen(void)
{
	int fd, ret = 0;
	char tmpn[128];
	FILE *f;

	snprintf(tmpn, sizeof(tmpn), "/tmp/failinj%d", rand());

	fd = creat(tmpn, 0600);
	if (fd == -1) {
		ret = 1;
		perror("Unable to creat temporary file");
		goto out;
	}

	f = fdopen(fd, "wb");
	if (!f) {
		ret = 1;
		close(fd);
		perror("Unable to fdopen temporary file");
		goto out;
	}

	ret = fclose(f);
	if (ret == EOF) {
		ret = 1;
		perror("Failure closing temporary FILE");
		goto out;
	}

out:
	unlink(tmpn);
	return ret;
}

static int test_realloc(void)
{
	size_t sz = 1024;
	void *x, *y;

	x = calloc(sz, 1);
	if (!x) {
		perror("Unable to calloc memory");
		return 1;
	}
	memset(x, 0xAA, sz);

	y = realloc(x, 2048);
	if (!y) {
		perror("Unable to realloc memory");
		free(x);
		return 1;
	}
	x = y;

	y = reallocarray(x, sz, 4);
	if (!y) {
		perror("Unable to reallocarray memory");
		free(x);
		return 1;
	}
	x = y;

	free(x);
	return 0;
}

static int test_mmap(void)
{
	const size_t sz = 4096;
	void *x;
	int rc;

	x = mmap(NULL, sz, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (x == MAP_FAILED) {
		perror("Unable to mmap memory");
		return 1;
	}

	rc = mprotect(x, sz, PROT_READ | PROT_EXEC);
	if (rc) {
		perror("mprotect failed");
		return 1;
	}

	munmap(x, sz);
	return 0;
}

static int test_syscall(void)
{
	int rc;

	rc = syscall(__NR_sync);
	if (rc == -1) {
		perror("sync failed");
		return 1;
	}

	return 0;
}

__attribute__ ((noinline))
static int test_ignore_leak(void)
{
	void *x, *y;

	x = malloc(32);
	if (!x) {
		perror("Unable to allocate leaked memory");
		return 1;
	}

	y = malloc(32);
	if (!y) {
		perror("Unable to allocate ignored leak memory");
		return 1;
	}

	free(y);
	free(x);
	return 0;
}

__attribute__ ((noinline))
static int test_skip_failure(void)
{
	void *x;

	x = malloc(32);
	if (!x) {
		perror("Unable to allocate skipped malloc");
		return 1;
	}

	free(x);
	return 0;
}

/* Insert a large number of objects to ensure the hash table works */
static int test_hash_table(void)
{
	const int count = 16;
	void *memory[count];
	int i, ret = 0;

	for (i = 0; i < count; i++) {
		memory[i] = malloc(32);
		if (!memory[i]) {
			ret = 1;
			break;
		}
	}

	for (i--; i >= 0; i--)
		free(memory[i]);

	return ret;
}

int main(int argc, char *argv[])
{
	void *x, *y;
	int ret;

	x = malloc(50);
	if (!x) {
		/*
		 * Due to the intentional segfault, these lines will not appear
		 * to be covered (LCOV_EXCL_START)
		 */
		perror("x allocation failed");
		if (argc == 1)
			printf("This will segfault: %d\n", *((unsigned char *)x));
		return 1;
		/* LCOV_EXCL_STOP */
	}

	y = malloc(50);
	if (!y) {
		perror("y allocation failed");
		return 1;
	}

	ret = test_fd(x);
	if (ret)
		goto out;

	ret = test_openat();
	if (ret)
		goto out;

	ret = test_stdio(x);
	if (ret)
		goto out;

	ret = test_scanf();
	if (ret)
		goto out;

	ret = test_fmemopen();
	if (ret)
		goto out;

	ret = test_tmpfile();
	if (ret)
		goto out;

	ret = test_creat_fdopen();
	if (ret)
		goto out;

	ret = test_realloc();
	if (ret)
		goto out;

	ret = test_mmap();
	if (ret)
		goto out;

	ret = test_syscall();
	if (ret)
		goto out;

	ret = test_ignore_leak();
	if (ret)
		goto out;

	ret = test_skip_failure();
	if (ret)
		goto out;

	ret = test_hash_table();
	if (ret)
		goto out;

out:
	free(y);
	free(x);
	if (!ret) {
		printf("OK\n");
		ret = test_fcloseall();
	}
	return ret;
}
