// SPDX-License-Identifier: MIT
/* Copyright (C) 2020, Logan Gunthorpe */

#define _GNU_SOURCE

#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

static int test_fd(void *x)
{
	ssize_t rd;
	int fd;

	fd = open("/dev/zero", O_RDONLY);
	if (fd == -1) {
		perror("Unable to open /dev/zero");
		return 1;
	}

	rd = read(fd, x, 50);
	if (rd < 0) {
		perror("Failed to read /dev/zero");
		return 1;
	}

	close(fd);
	return 0;
}

static int test_stdio(void *x)
{
	ssize_t wr;
	FILE *f;
	int ret;

	f = fopen("/dev/null", "w");
	if (!f) {
		perror("Unable to open /dev/null");
		return 1;
	}

	wr = fwrite(x, 1, 50, f);
	if (wr != 50) {
		perror("Unable to write to /dev/null");
		return 1;
	}

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

int main()
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

	ret = test_stdio(x);
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
