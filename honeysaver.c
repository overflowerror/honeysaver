
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xos.h>

#include <pthread.h>

#include <security/pam_appl.h>

#include <sys/ioctl.h>
#include <sys/mman.h>

#include <linux/videodev2.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>

Display* dis;
int screen;
Window terminal;
GC gc;

Colormap colormap;
unsigned long black, white;
XColor green;

int width;
int height;

int fontHeight = 10;
int fontWidth = 6;
struct position {
	int x;
	int y;
} position = {.x = 0, .y = 0};
struct position newPosition = {.x = 0, .y = 0};
int lines = 0;
char* text[110] = {""};

bool password = false;

#define charBufferLength 255

char input[charBufferLength] = {'\0'};

char inputBuffer[charBufferLength] = {'\0'};

bool cursorOn = true;

/* //////////////////////////////////
NOTMYCODE
////////////////////////////////// */

struct pam_response *reply;  

// //function used to get user input  
int function_conversation(int num_msg, const struct pam_message **msg, struct pam_response **resp, void *appdata_ptr) {  
	*resp = reply;  
	return PAM_SUCCESS;  
}  

int authenticate_system(const char *username, const char *password) {
	const struct pam_conv local_conversation = { function_conversation, NULL };  
	pam_handle_t *local_auth_handle = NULL; // this gets set by pam_start  

	int retval;  
	retval = pam_start("su", username, &local_conversation, &local_auth_handle);  

	if (retval != PAM_SUCCESS) {  
		printf("pam_start returned: %d\n ", retval);  
		return 0;  
	}  

	reply = (struct pam_response *)malloc(sizeof(struct pam_response));  

	reply[0].resp = strdup(password);  
	reply[0].resp_retcode = 0;  
	retval = pam_authenticate(local_auth_handle, 0);  

	if (retval != PAM_SUCCESS) {  
		if (retval == PAM_AUTH_ERR) {  
	 		printf("Authentication failure.\n");  
		} else {
			printf("pam_authenticate returned %d\n", retval);  
		}  
		return 0;  
	}  

	printf("Authenticated.\n");  
	retval = pam_end(local_auth_handle, retval);  

	if (retval != PAM_SUCCESS) {  
		printf("pam_end returned\n");  
		return 0;  
	}  

	return 1;  
}  

/* //////////////////////////////////
END NOTMYCODE
////////////////////////////////// */

void vputs(char* string) {
	if ((lines + 3) * fontHeight >= height) {
		for (int i = 0; i < lines; i++) {
			text[i] = "";
		}
		lines = 0;
	}

	text[lines] = string;
	lines++;
}

#define PROMPT "root@memex-mobilis:/etc/X11# "
#define PWPROMPT "Password: "

void drawCursor() {
	const char* prompt = (password) ? PWPROMPT : PROMPT;

	if (cursorOn) {
		XDrawString(dis, terminal, gc, fontWidth * (position.x + strlen(prompt)), 10 + fontHeight * lines, "_", 1);
	} else {
		XSetForeground(dis, gc, black);
		XFillRectangle(dis, terminal, gc, fontWidth * (strlen(prompt)), fontHeight * lines + 1, fontWidth * (position.x + strlen(input) + 1), (position.y + 1) * fontHeight + 3);
		XSetForeground(dis, gc, white);

		if (!password) {
			position = newPosition;
			strcpy(input, inputBuffer);
		}

		XDrawString(dis, terminal, gc, fontWidth * strlen(prompt), 10 + fontHeight * lines, input, strlen(input));
	}

	XFlush(dis);
}

void redraw() {
	const char* prompt = (password) ? PWPROMPT : PROMPT;

	//XClearWindow(dis, terminal);
	XSetForeground(dis, gc, black);
	XFillRectangle(dis, terminal, gc, 0, 0, width, height);
	XSetForeground(dis, gc, white);

	for (int i = 0; i < lines; i++) {
		XDrawString(dis, terminal, gc, 0, 10 + i * fontHeight, text[i], strlen(text[i]));
	}
	XDrawString(dis, terminal, gc, 0, 10 + lines*fontHeight, prompt, strlen(prompt));

	drawCursor();
}

void closeWindow() {
	XUngrabKeyboard(dis, CurrentTime);
	XFreeColormap(dis, colormap);
	XFreeGC(dis, gc);
	XDestroyWindow(dis, terminal);
	XCloseDisplay(dis);
	exit(1);
}


/* //////////////////////////////////
NOTMYCODE
////////////////////////////////// */

uint8_t *buffer;
int length;

static int xioctl(int fd, int request, void *arg) {
        int r;
 
        do r = ioctl (fd, request, arg);
        while (-1 == r && EINTR == errno);
 
        return r;
}

int print_caps(int fd) {
	struct v4l2_capability caps = {};
	if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &caps)) {
		perror("Querying Capabilities");
		return 1;
	}

	printf( "Driver Caps:\n"
		"  Driver: \"%s\"\n"
		"  Card: \"%s\"\n"
		"  Bus: \"%s\"\n"
		"  Version: %d.%d\n"
		"  Capabilities: %08x\n",
		caps.driver,
		caps.card,
		caps.bus_info,
		(caps.version>>16)&&0xff,
		(caps.version>>24)&&0xff,
		caps.capabilities);


	struct v4l2_cropcap cropcap = {0};
	cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (-1 == xioctl (fd, VIDIOC_CROPCAP, &cropcap)) {
		perror("Querying Cropping Capabilities");
		return 1;
	}

	printf( "Camera Cropping:\n"
		"  Bounds: %dx%d+%d+%d\n"
		"  Default: %dx%d+%d+%d\n"
		"  Aspect: %d/%d\n",
		cropcap.bounds.width, cropcap.bounds.height, cropcap.bounds.left, cropcap.bounds.top,
		cropcap.defrect.width, cropcap.defrect.height, cropcap.defrect.left, cropcap.defrect.top,
		cropcap.pixelaspect.numerator, cropcap.pixelaspect.denominator);

	int support_grbg10 = 0;

	struct v4l2_fmtdesc fmtdesc = {0};
	fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	char fourcc[5] = {0};
	char c, e;
	printf("  FMT : CE Desc\n--------------------\n");
	while (0 == xioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc)) {
		strncpy(fourcc, (char *)&fmtdesc.pixelformat, 4);
		if (fmtdesc.pixelformat == V4L2_PIX_FMT_SGRBG10)
			support_grbg10 = 1;
		c = fmtdesc.flags & 1? 'C' : ' ';
		e = fmtdesc.flags & 2? 'E' : ' ';
		printf("  %s: %c%c %s\n", fourcc, c, e, fmtdesc.description);
		fmtdesc.index++;
	}
	/*
	if (!support_grbg10)
	{
	printf("Doesn't support GRBG10.\n");
	return 1;
	}*/

	struct v4l2_format fmt = {0};
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	//fmt.fmt.pix.width = 640;
	//fmt.fmt.pix.height = 480;
	fmt.fmt.pix.width = 1280;
	fmt.fmt.pix.height = 720;
	//fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_BGR24;
	//fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_GREY;
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
	fmt.fmt.pix.field = V4L2_FIELD_NONE;

	if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt)) {
		perror("Setting Pixel Format");
		return 1;
	}

	strncpy(fourcc, (char *)&fmt.fmt.pix.pixelformat, 4);
	printf( "Selected Camera Mode:\n"
		"  Width: %d\n"
		"  Height: %d\n"
		"  PixFmt: %s\n"
		"  Field: %d\n",
		fmt.fmt.pix.width,
		fmt.fmt.pix.height,
		fourcc,
		fmt.fmt.pix.field);
	return 0;
}
 
int init_mmap(int fd)
{
	struct v4l2_requestbuffers req = {0};
	req.count = 1;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
		perror("Requesting Buffer");
		return 1;
	}

	struct v4l2_buffer buf = {0};
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.index = 0;
	if(-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf)) {
		perror("Querying Buffer");
		return 1;
	}

	buffer = mmap (NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
	length = buf.length;
	printf("Length: %d\nAddress: %p\n", buf.length, buffer);
	printf("Image Length: %d\n", buf.bytesused);

	return 0;
}
 
int capture_image(int fd, char* command) {
	struct v4l2_buffer buf = {0};
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.index = 0;
	if(-1 == xioctl(fd, VIDIOC_QBUF, &buf)) {
		perror("Query Buffer");
		return 1;
	}

	if(-1 == xioctl(fd, VIDIOC_STREAMON, &buf.type)) {
		perror("Start Capture");
		return 1;
	}

	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(fd, &fds);
	struct timeval tv = {0};
	tv.tv_sec = 2;
	int r = select(fd+1, &fds, NULL, NULL, &tv);
	if(-1 == r) {
		perror("Waiting for Frame");
		return 1;
	}

	if(-1 == xioctl(fd, VIDIOC_DQBUF, &buf)) {
		perror("Retrieving Frame");
		return 1;
	}
	printf ("saving image\n");

	for (int i = 0; i < strlen(command); i++) {
		if (command[i] == '/')
			command[i] == '%';
	}

	const char* path = "/home/overflow/.honeysaver/";
	const char* prefix = "pic-";
	const char* seperator = "-";
	const char* extension = ".jpg";

	int datelength = 100;

	time_t now = time(NULL);
	struct tm *t = localtime(&now);

	char* filename = malloc(strlen(path) + strlen(prefix) + strlen(seperator) + strlen(command) + strlen(extension) + datelength);
	strcpy(filename, path);
	strcat(filename, prefix);
	strftime(filename + strlen(filename), datelength - 1, "%Y-%m-%d-%H-%M", t);
	strcat(filename, seperator);
	strcat(filename, command);
	strcat(filename, extension);

	printf("Filename: %s\n", filename);

	int outfd = open(filename, O_RDWR | O_CREAT);
	if (outfd < 0) {
		perror("open file");
		return 1;
	}
	write(outfd, buffer, buf.bytesused);
	close(outfd);

	free(filename);

	printf("Saved %d bytes.\n", buf.bytesused);

	return 0;
}


int photo(char* command) {
	int fd = open("/dev/video0", O_RDWR);
        if (fd == -1)
        {
                perror("Opening video device");
                return 1;
        }
        if(print_caps(fd))
            return 1;
        if(init_mmap(fd))
            return 1;
        int i;
        for(i=0; i < 1; i++) {
            if (capture_image(fd, command))
                return 1;
        }

	munmap(buffer, length);

	close(fd);

}

/* //////////////////////////////////
END NOTMYCODE
////////////////////////////////// */

void loop() {
	XEvent event;
	KeySym key;
	char text[charBufferLength];
	char username[charBufferLength];

	while (true) {
		XNextEvent(dis, &event);

		if (event.type == Expose && event.xexpose.count == 0) {
			redraw();
		}

		if (event.type == KeyPress) {
			int length = XLookupString(&event.xkey, text, charBufferLength, &key, 0);

			if (!password && event.xkey.state & ControlMask && key == XK_c) {
				vputs("root@memex-mobilis:/etc/X11# ^C");
				inputBuffer[0] = '\0';
				newPosition.x = 0;

				text[0] = '\0';

				redraw();
			}
			if (key == XK_Return) {

				if (password) {

					if (authenticate_system(username, inputBuffer) == 1) {
						// TODO check username
						closeWindow();
					} else {
						vputs(PWPROMPT);
						vputs("Login failed.");
						inputBuffer[0] = '\0';
						newPosition.x = 0;
						password = false;

						redraw();
					}
				} else {
					const char* token = strtok(inputBuffer, " ");
					if (token == NULL) {
						vputs(PROMPT);
						position.x = 0;
						inputBuffer[0] = '\0';

						redraw();
						continue;
					}
					if (strcmp(token, "su") == 0) {
						token = strtok(NULL, " ");

						if (token == NULL) {
							token = getenv("USER");
							if (token == NULL)
								token = "nobody";
						} else {
							// new session for user token
						}
						
						printf("Username %s\n", token);

						strcpy(username, token);

						vputs("root@memex-mobilis:/etc/X11# login");
						password = true;
						newPosition.x = 0;
						position.x = 0;
						inputBuffer[0] = '\0';
						input[0] = '\0';

						redraw();
					} else {
						
						photo(inputBuffer);

						vputs("root@memex-mobilis:/etc/X11# makepicture");
						vputs("Smile. : )");
						newPosition.x = 0;
						inputBuffer[0] = '\0';
						redraw();
					}
				}
			} else if (key == XK_Left) {
				if (newPosition.x > 0)
					newPosition.x--;
			} else if (key == XK_Right) {
				if (newPosition.x < strlen(inputBuffer))
					newPosition.x++;
			} else if (key == XK_BackSpace) {
				if (newPosition.x > 0) {
					memmove(inputBuffer + newPosition.x - 1, inputBuffer + newPosition.x, strlen(inputBuffer) - newPosition.x + 1);
					newPosition.x--;
				}
			} else {
				if (strlen(inputBuffer) + length >= charBufferLength)
					continue;
				text[length] = '\0';
				strcat(text, inputBuffer + newPosition.x);
				inputBuffer[newPosition.x] = '\0';
				strcat(inputBuffer, text);
				newPosition.x += length;
			}

			drawCursor();
		}

		if (event.type == ButtonPress) {
			printf("You pressed a button at (%i, %i)\n", event.xbutton.x, event.xbutton.y);
		}
	}
}

void fullscreen(Display* dis, Window win) {
	Atom atoms[2] = { XInternAtom(dis, "_NET_WM_STATE_FULLSCREEN", False), XInternAtom(dis, "_NET_WM_STATE_ABOVE", False)};
	XChangeProperty(
		dis,
		win,
		XInternAtom(dis, "_NET_WM_STATE", False),
		4, 32, PropModeReplace, (unsigned char *) atoms, 2
	);
}

void init() {
	XInitThreads();

	dis = XOpenDisplay((char*) 0);
	screen = DefaultScreen(dis);
	black = BlackPixel(dis, screen);
	white = WhitePixel(dis, screen);

	width = DisplayWidth(dis, screen);
	height = DisplayHeight(dis, screen);

	terminal = XCreateSimpleWindow(dis, DefaultRootWindow(dis), 0, 0, height, width, 5, black, black);

	XWindowAttributes winatt;
	XGetWindowAttributes(dis, terminal, &winatt);

	colormap = DefaultColormap(dis, 0);
	XParseColor(dis, colormap, "#00ff00", &green);
	XAllocColor(dis, colormap, &green);

	XSetStandardProperties(dis, terminal, "Honeysaver", "Honeysaver", None, NULL, 0, NULL);

	gc = XCreateGC(dis, terminal, 0, 0);

	//XSetBackground(dis, gc, black);
	//XSetForeground(dis, gc, white);

	XSelectInput(dis, terminal, ExposureMask | ButtonPressMask | KeyPressMask);

	XClearWindow(dis, terminal);
	XMapRaised(dis, terminal);

	fullscreen(dis, terminal);

	do {
		XGetWindowAttributes(dis, terminal, &winatt);
		usleep(100);
	} while (winatt.map_state != IsViewable);  

	redraw();

	printf("grabkeyboard: %d\n", XGrabKeyboard(dis, terminal, False, GrabModeAsync, GrabModeAsync, CurrentTime));

	char data[1] = {0};
	Pixmap blank = XCreateBitmapFromData(dis, terminal, data, 1, 1);
	XColor dummy;
	Cursor cursor = XCreatePixmapCursor(dis, blank, blank, &dummy, &dummy, 0, 0);
	XFreePixmap(dis, blank);

	XDefineCursor(dis, terminal, cursor);
}

void* blink(void* arg) {
	while(true) {
		usleep(100000);
		cursorOn = !cursorOn;

		drawCursor();
	}
	return NULL;
}

int main(int argc, char** argv) {
	init();

	vputs("root@memex-mobilis:~# cd /ect");
	vputs("bash: cd: /ect: No such file or directory");
	vputs("root@memex-mobilis:~# cd /etc");
	vputs("root@memex-mobilis:/etc# cd X11");
	vputs("root@memex-mobilis:/etc/X11# ^C");
	vputs("root@memex-mobilis:/etc/X11# ls -la");
	vputs("total 108");
	vputs("drwxr-xr-x  13 root root  4096 May  3 21:24 .");
	vputs("drwxr-xr-x 150 root root 12288 Jun 20 00:54 ..");
	vputs("drwxr-xr-x   2 root root  4096 Mar 18 20:21 app-defaults");
	vputs("drwxr-xr-x   2 root root  4096 May  3 22:32 blackbox");
	vputs("-rw-r--r--   1 root root    15 Dec 20 21:35 default-display-manager");
	vputs("drwxr-xr-x   2 root root  4096 May  3 22:32 fluxbox");
	vputs("drwxr-xr-x   6 root root  4096 Dec 20 21:26 fonts");
	vputs("lrwxrwxrwx   1 root root    14 Nov 24  2016 openbox -> ../xdg/openbox");
	vputs("-rw-r--r--   1 root root 17394 Nov 23  2016 rgb.txt");
	vputs("drwxr-xr-x   2 root root  4096 Mar 13 20:03 xinit");
	vputs("drwxr-xr-x   2 root root  4096 Sep 24  2016 xkb");
	vputs("-rw-r--r--   1 root root   884 Oct  3  2016 Xloadimage");
	vputs("drwxr-xr-x   2 root root  4096 May  3 18:11 xrdp");
	vputs("-rwxr-xr-x   1 root root   709 Nov 23  2016 Xreset");
	vputs("drwxr-xr-x   2 root root  4096 Dec 20 21:14 Xreset.d");
	vputs("drwxr-xr-x   2 root root  4096 Dec 20 21:14 Xresources");
	vputs("-rwxr-xr-x   1 root root  3517 Nov 23  2016 Xsession");
	vputs("drwxr-xr-x   2 root root  4096 May  3 21:42 Xsession.d");
	vputs("-rw-r--r--   1 root root   265 Nov 23  2016 Xsession.options");
	vputs("drwxr-xr-x   2 root root  4096 Mar 13 20:03 xsm");
	vputs("-rw-r--r--   1 root root    13 Dec  5  2016 XvMCConfig");
	vputs("-rwxr-xr-x   1 root root   185 Apr  9 16:38 Xvnc-session");

	pthread_t blinkThread;

	pthread_create(&blinkThread, NULL, &blink, NULL);

	loop();

	return 0;
}
