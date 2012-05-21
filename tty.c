#include "tty.h"


#include <fcntl.h>
#include <linux/kd.h>
#include <malloc.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/vt.h>
#include <termios.h>
#include <unistd.h>


struct tty {
	int fd, vt, starting_vt, kb_mode, has_vt;
	struct termios terminal_attributes;
};

static int 
tty_open(struct tty* tty) {
	int tty0, fd;
	char filename[16];
	
	tty0 = open("/dev/tty0", O_WRONLY | O_CLOEXEC);
	if (tty0 < 0) {
		fprintf(stderr, "could not open tty0: %m\n");
		return -1;
	}
	
	if (ioctl(tty0, VT_OPENQRY, &tty->vt) < 0 || tty->vt == -1) {
		fprintf(stderr, "could not open tty0: %m\n");
		close(tty0);
		return -1;
	}
	
	close(tty0);
	snprintf(filename, sizeof filename, "/dev/tty%d", tty->vt);
	fprintf(stderr, "compositor: using new vt %s\n", filename);
	fd = open(filename, O_RDWR | O_NOCTTY | O_CLOEXEC);
	if (fd < 0)
		return fd;
	
	return fd;
}

struct tty* 
tty_create() {
	struct vt_stat vts;
	struct termios raw_attributes;
	struct vt_mode mode = { 0 };
	struct tty* tty;
	int ret;
	
	tty = (struct tty*)malloc(sizeof *tty);
	
	if (tty == NULL)
		return NULL;
	
	memset(tty, 0, sizeof *tty);
	
	tty->fd = tty_open(tty);
	
	if (ioctl(tty->fd, VT_GETSTATE, &vts) == 0)
		tty->starting_vt = vts.v_active;
	else
		tty->starting_vt = tty->vt;
	
	if (tty->starting_vt != tty->vt) {
		if (ioctl(tty->fd, VT_ACTIVATE, tty->vt) < 0 ||
			ioctl(tty->fd, VT_WAITACTIVE, tty->vt) < 0) {
			fprintf(stderr, "failed to swtich to new vt\n");
		return NULL;
			}
	}
	
	if (tcgetattr(tty->fd, &tty->terminal_attributes) < 0) {
		fprintf(stderr, "could not get terminal attributes: %m\n");
		goto err;
	}
	
	/* Ignore control characters and disable echo */
	raw_attributes = tty->terminal_attributes;
	cfmakeraw(&raw_attributes);
	
	/* Fix up line endings to be normal (cfmakeraw hoses them) */
	raw_attributes.c_oflag |= OPOST | OCRNL;
	
	if (tcsetattr(tty->fd, TCSANOW, &raw_attributes) < 0)
		fprintf(stderr, "could not put terminal into raw mode: %m\n");
	
	ioctl(tty->fd, KDGKBMODE, &tty->kb_mode);
	ret = ioctl(tty->fd, KDSKBMODE, K_OFF);
	if (ret) {
		ret = ioctl(tty->fd, KDSKBMODE, K_RAW);
		if (ret)
			goto err_attr;
	}
	
	if (ret) {
		fprintf(stderr, "failed to set K_OFF keyboard mode on tty: %m\n");
		goto err_attr;
	}
	
	ret = ioctl(tty->fd, KDSETMODE, KD_GRAPHICS);
	if (ret) {
		fprintf(stderr, "failed to set KD_GRAPHICS mode on tty: %m\n");
		goto err_kdkbmode;
	}
	
	tty->has_vt = 1;
	mode.mode = VT_PROCESS;
	mode.relsig = SIGUSR1;
	mode.acqsig = SIGUSR1;
	if (ioctl(tty->fd, VT_SETMODE, &mode) < 0) {
		fprintf(stderr, "failed to take control of vt handling\n");
		goto err_kdmode;
	}
	
	
	return tty;
	
	err_vtmode:
	mode.mode = VT_AUTO;
	ioctl(tty->fd, VT_SETMODE, &mode);
	
	err_kdmode:
	ioctl(tty->fd, KDSETMODE, KD_TEXT);
	
	err_kdkbmode:
	ioctl(tty->fd, KDSKBMODE, tty->kb_mode);
	
	err_attr:
	tcsetattr(tty->fd, TCSANOW, &tty->terminal_attributes);
	
	err:
	if (tty->has_vt && tty->vt != tty->starting_vt) {
		ioctl(tty->fd, VT_ACTIVATE, tty->starting_vt);
		ioctl(tty->fd, VT_WAITACTIVE, tty->starting_vt);
	}
	close(tty->fd);
	free(tty);
	return NULL;
}

void 
tty_destroy(struct tty* tty) {
	struct vt_mode mode = { 0 };
	
	if (ioctl(tty->fd, KDSKBMODE, tty->kb_mode))
		fprintf(stderr, "failed to restore keyboard mode: %m\n");
	
	if (ioctl(tty->fd, KDSETMODE, KD_TEXT))
		fprintf(stderr,
				"failed to set KD_TEXT mode on tty: %m\n");
		
	if (tcsetattr(tty->fd, TCSANOW, &tty->terminal_attributes) < 0)
		fprintf(stderr,
				"could not restore terminal to canonical mode\n");
		
		mode.mode = VT_AUTO;
	if (ioctl(tty->fd, VT_SETMODE, &mode) < 0)
		fprintf(stderr, "could not reset vt handling\n");
	
	if (tty->has_vt && tty->vt != tty->starting_vt) {
		ioctl(tty->fd, VT_ACTIVATE, tty->starting_vt);
		ioctl(tty->fd, VT_WAITACTIVE, tty->starting_vt);
	}
	
	close(tty->fd);
	
	free(tty);
}