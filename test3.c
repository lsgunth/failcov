/*
 * test3 is for tests that require _GNU_SOURCE to be unset
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

int test_getline(void)
{
	char *line = NULL;
	ssize_t cnt;
	size_t len;
	FILE *f;

	f = fopen("/dev/null", "rb");
	if (!f) {
		perror("Could not open /dev/null");
		return 1;
	}

	errno = 0;
	cnt = getline(&line, &len, f);
	if (cnt < 0 && errno) {
		perror("getline failure");
		free(line);
		fclose(f);
		return 1;
	}

	free(line);
	fclose(f);
	return 0;
}

int main(void)
{
	const char test[] = "Hello: 12345\n";
	int rc, x;
	FILE *f;

	rc = sscanf(test, "Hello: %d", &x);
	if (rc != 1) {
		perror("sscanf failed");
		return 1;
	}

	f = fopen("/dev/zero", "rb");
	if (!f) {
		perror("Could not open /dev/zero");
		return 1;
	}

	rc = fscanf(f, "Hello: %d\n", &x);
	if (rc != 0) {
		perror("-fscanf failed");
		fclose(f);
		return 1;
	}

	fclose(f);

	return test_getline();
}
