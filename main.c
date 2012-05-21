#include <unistd.h>
#include "tty.h"


int main(int argc, char **argv) {
	struct tty* tty = tty_create();
	
	sleep(2);
	
	tty_destroy(tty);
	
	return 0;
}