#ifndef _TTY_H_
#define _TTY_H_

struct tty;

struct tty* 
tty_create();

void 
tty_destroy(struct tty*);

#endif