#include <time-simulator.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
	struct time_simulator *s;

	s = time_simulator_alloc();
	if (!s) {
		perror("Error allocating time simulator\n");
		return -1;
	}

	free(s);
	return 0;
}
