// SPDX-License-Identifier: MIT
/* Copyright (C) 2020, Logan Gunthorpe */

#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

int main()
{
	FILE *f;
	void *x, *y;
	ssize_t rd, wr;
	int fd, ret = 0;

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

	fd = open("/dev/zero", O_RDONLY);
	if (fd == -1) {
		perror("Unable to open /dev/zero");
		ret = 1;
		goto out;
	}

	rd = read(fd, x, 50);
	if (rd < 0) {
		perror("Failed to read /dev/zero");
		ret = 1;
		goto out;
	}

	f = fopen("/dev/null", "w");
	if (!f) {
		ret = 1;
		perror("Unable to open /dev/null");
		goto close_out;
	}

	wr = fwrite(x, 1, rd, f);
	if (wr != rd) {
		ret = 1;
		perror("Unable to write to /dev/null");
		goto close_out;
	}

	ret = fflush(f);
	if (ret == EOF) {
		ret = 1;
		perror("Error while flushing to /dev/null");
		goto close_out;
	}

	ret = fclose(f);
	if (ret == EOF) {
		ret = 1;
		perror("Error while to closing /dev/null");
	}

close_out:
	close(fd);
out:
	free(y);
	free(x);
	if (!ret)
		printf("OK\n");
	return ret;
}
