/*
 * $Id: blueping.c,v 1.8 2005/12/18 03:31:46 jcs Exp $
 *
 * blueping
 * a bluetooth monitoring utility
 *
 * Copyright (c) 2005 joshua stein <jcs@jcs.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define MAC_OS_X_VERSION_MIN_REQUIRED MAC_OS_X_VERSION_10_2

#include <CoreFoundation/CoreFoundation.h>
#include <IOBluetooth/IOBluetoothUserLib.h>
#include <IOBluetooth/IOBluetoothUtilities.h>
#include <err.h>
#include <signal.h>
#include <time.h>

void bail(int);
void dolog(char *);
void launchapp(char *);
void pingloop(void);
void sig_handler(int);
void usage(void);

IOBluetoothDeviceRef device;
CFRunLoopTimerRef looptimer;
char enterprog[1024], exitprog[1024];
int connected = -1;
int verbose = 0;

/* default poll interval in seconds */
long pollint = 15;

extern char *__progname;

int
main(int argc, char *argv[])
{
	BluetoothDeviceAddress device_address;
	CFStringRef t;
	char macbuf[255], logstr[255];
	char *inval = NULL, *self = NULL, *p = NULL;
	u_char devname[50];
	int ch;

	CFRunLoopTimerContext context = {0, self, NULL, NULL, NULL};

	bzero(enterprog, sizeof(enterprog));
	bzero(exitprog, sizeof(exitprog));
	bzero(macbuf, sizeof(macbuf));

	while ((ch = getopt(argc, argv, "d:e:i:vx:")) != -1) {
		switch (ch) {
		case 'd':
			strncpy(macbuf, optarg, sizeof(macbuf));
			break;
		case 'e':
			strncpy(enterprog, optarg, sizeof(enterprog));
			break;
		case 'i':
			if ((!(pollint = strtol((p = optarg), &inval, 10))) ||
			    (pollint < 1))
				errx(1, "invalid poll interval -- %s", optarg);
			break;
		case 'v':
			verbose++;
			break;
		case 'x':
			strncpy(exitprog, optarg, sizeof(exitprog));
			break;
		default:
			usage();
		}
	}

	if (!strlen(macbuf))
		usage();

	if (!IOBluetoothLocalDeviceAvailable()) {
		fprintf(stderr, "%s: no bluetooth available\n", __progname);
		exit(1);
	}

	signal(SIGCHLD, sig_handler);
	signal(SIGINT, sig_handler);

	/* convert the mac address and establish our object */
	t = CFStringCreateWithCString(NULL, macbuf, kCFStringEncodingASCII);
	int status = IOBluetoothNSStringToDeviceAddress(t, &device_address);
	if (status != kIOReturnSuccess) {
		fprintf(stderr, "%s: error creating device address: %s\n",
			__progname, macbuf);
		exit(1);
	}
	CFRelease(t);

	device = IOBluetoothDeviceCreateWithAddress(&device_address);

	if (verbose) {
		status = IOBluetoothDeviceRemoteNameRequest(device, NULL, NULL,
			devname);
		if (status == kIOReturnSuccess) {
			snprintf(logstr, sizeof(logstr),
				"found device named \"%s\"", devname);
			dolog(logstr);
		}
	}

	looptimer = CFRunLoopTimerCreate(
		NULL,
		CFAbsoluteTimeGetCurrent(),
		pollint,
		0,
		0,
		(void *)pingloop,
		&context);

	CFRunLoopAddTimer(CFRunLoopGetCurrent(), looptimer,
		kCFRunLoopCommonModes);

	CFRunLoopRun();

	IOBluetoothDeviceCloseConnection(device);
	IOBluetoothObjectRelease(device);

	return(0);
}

void
usage()
{
	fprintf(stderr, "usage: %s -d macaddr [-e enterprog] [-i pollint] [-v]"
		" [-x exitprog]\n", __progname);
	exit(1);
}

void
dolog(char *entry)
{
	struct timeval tv[2];
	time_t now;
	struct tm *tm;

	gettimeofday(&tv[0], NULL);
	now = tv[0].tv_sec;
	tm = localtime(&now);

	printf("[%02d:%02d:%02d] %s\n", tm->tm_hour, tm->tm_min, tm->tm_sec,
		entry);
}

void
pingloop()
{
	int status;
	char logstr[512];

	status = IOBluetoothDeviceOpenConnection(device, NULL, NULL);
	if (status == kIOReturnSuccess) {
		if (connected == 0 && verbose) {
			bzero(logstr, sizeof(logstr));
			snprintf(logstr, sizeof(logstr),
				"device entered range");

			if (strlen(enterprog))
				snprintf(logstr, sizeof(logstr),
					"%s, executing \"%s\"", logstr,
					enterprog);

			dolog(logstr);
		}

		if (connected == 0 && strlen(enterprog))
			launchapp(enterprog);

		connected = 1;
	} else {
		if (connected == 1 && verbose) {
			bzero(logstr, sizeof(logstr));
			snprintf(logstr, sizeof(logstr), "device left range");

			if (strlen(exitprog))
				snprintf(logstr, sizeof(logstr),
					"%s, executing \"%s\"", logstr,
					exitprog);

			dolog(logstr);
		}

		if (connected == 1 && strlen(exitprog))
			launchapp(exitprog);

		connected = 0;
	}

	IOBluetoothDeviceCloseConnection(device);

	if (verbose > 1) {
		bzero(logstr, sizeof(logstr));
		snprintf(logstr, sizeof(logstr), "device is %sin range, "
			"sleeping for %ld second%s",
			(connected == 1 ? "" : "not "), pollint,
			(pollint == 1 ? "" : "s"));
		dolog(logstr);
	}
}

void
launchapp(char *progname)
{
	char *argp[] = {"sh", "-c", NULL, NULL};
	pid_t pid;

	argp[2] = progname;
	pid = fork();
	if (pid == 0) {
		execv("/bin/sh", argp);
		_exit(1);
	}
}

void
sig_handler(int sig)
{
	switch (sig) {
	case SIGCHLD:
		printf("got sigchld\n");
		wait(NULL);
		break;
	case SIGINT:
		printf("\nshutting down\n");
		IOBluetoothDeviceCloseConnection(device);
		IOBluetoothObjectRelease(device);
		_exit(0);
		break;
	}
}
