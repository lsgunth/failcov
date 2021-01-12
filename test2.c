
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void)
{
	char *x;

	x = strdup("OK");
	if (!x) {
		return 1;
	}

	free(x);
	close(88);
	return 0;
}
