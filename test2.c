
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
	char *x;

	x = strdup("OK");
	if (!x)
		return 1;

	return 0;
}
