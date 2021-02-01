/*
 * test3 is for tests that require _GNU_SOURCE to be unset
 */

#include <stdio.h>

int main(void)
{
	const char test[] = "Hello: 12345\n";
	int rc, x;
	FILE * f;

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

	return 0;
}
