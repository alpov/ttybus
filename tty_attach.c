#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "configure.h"
#include "loragw_gps.h"

#define MAX_TTY        256
#define BUFFER_SIZE    4096
#define POLL_R_TIMEOUT 100
#define POLL_W_TIMEOUT 50

static char *tty_bus_path;
static char *devname;
static char *init_string;


static void usage(char *app) {
  fprintf(stderr, "%s, Ver %s.%s.%s\n", basename(app), MAJORV, MINORV, SVNVERSION);
  fprintf(stderr, "Usage: %s [-h] [-g] [-s bus_path] tty_device\n", app);
  fprintf(stderr, "-h: shows this help\n");
  fprintf(stderr, "-g: gps mode (sniffer)\n");
  fprintf(stderr, "-d: detach from terminal and run as daemon\n");
  fprintf(stderr, "-s bus_path: uses bus_path as bus path name (default: /tmp/ttybus)\n");
  fprintf(stderr, "-i init_string: send init string to device\n\n");
  fprintf(stderr, "Please also see: tty_bus, tty_fake, tty_plug, dpipe\n");
  fprintf(stderr, "Example of usage:\n");
  fprintf(stderr, "  Create a new bus called /tmp/ttyS0mux\n");
  fprintf(stderr, "    tty_bus -d -s /tmp/ttyS0mux\n");
  fprintf(stderr, "  Connect a real device to the bus /tmp/ttyS0mux\n");
  fprintf(stderr, "    tty_attach -d -s /tmp/ttyS0mux /dev/ttyS0\n");
  fprintf(stderr, "  Create two fake ttyS0 devices, attached to the bus /tmp/ttyS0mux\n");
  fprintf(stderr, "    tty_fake -d -s /tmp/ttyS0mux /dev/ttyS0.0\n");
  fprintf(stderr, "    tty_fake -d -s /tmp/ttyS0mux /dev/ttyS0.1\n");
  exit(2);
}


int tty_connect(char *path) {
  struct sockaddr_un sun;
  int connect_fd = socket(PF_UNIX, SOCK_STREAM, 0);
  memset(&sun, 0, sizeof(struct sockaddr_un));
  sun.sun_family = AF_UNIX;
  strncpy(sun.sun_path, path, strlen(path));
  if (connect(connect_fd, (struct sockaddr *) &sun, sizeof(sun)) < 0) {
    perror("Cannot connect to socket");
    syslog(LOG_ERR, "Cannot connect to socket");
    exit(-1);
  }
  return connect_fd;
}


int main(int argc, char *argv[]) {
  int fd;
  struct pollfd pfd[2];
  int pollret, r;
  char buffer[BUFFER_SIZE];
  int realdev;
  int daemonize = 0;
  int gps = 0;

  while (1) {
    int c;
    c = getopt(argc, argv, "dghi:s:");
    if (c == -1)
      break;

    switch (c) {
      case 'd':
        daemonize = 1;
        break;
      case 'g':
        gps = 1;
        break;
      case 'h':
        usage(argv[0]);  // implies exit
        break;
      case 's':
        tty_bus_path = strdup(optarg);
        break;
      case 'i':
        init_string = strdup(optarg);
        break;
      default:
        usage(argv[0]);  // implies exit
    }
  }
  if (optind != (argc - 1))
    usage(argv[0]);  // implies exit

  if (daemonize)
    daemon(0, 0);

  devname = strdup(argv[optind]);

  if (!tty_bus_path)
    tty_bus_path = strdup("/tmp/ttybus");

  fprintf(stderr, "Connecting to bus: %s\n", tty_bus_path);
  syslog(LOG_INFO, "Connecting to bus: %s\n", tty_bus_path);
  fd = tty_connect(tty_bus_path);

  if (gps) {
    if (lgw_gps_enable(devname, "ubx7", 0, &realdev) != LGW_GPS_SUCCESS) {
      fprintf(stderr, "WARNING: [main] impossible to open %s for GPS sync (check permissions)\n", devname);
    } else {
      fprintf(stderr, "INFO: [main] TTY port %s open for GPS synchronization\n", devname);
    }
  } else {
    realdev = open(devname, O_RDWR);
  }
  if (realdev < 0) {
    perror("opening device");
    exit(3);
  }

  if (init_string) {
    pfd[0].fd = realdev;
    pfd[0].events = POLLOUT;
    pollret = poll(&pfd[0], 1, 50);
    if (pollret < 0) {
      fprintf(stderr, "Poll error: %s\n", strerror(errno));
      syslog(LOG_ERR, "Poll error: %s\n", strerror(errno));
      exit(1);
    }
    if (pfd[0].revents & POLLOUT) {
      write(realdev, init_string, strlen(init_string));
      write(realdev, "\n", 1);
    } else {
      fprintf(stderr, "Device is busy, cannot send init string.\n");
      syslog(LOG_WARNING, "Device is busy, cannot send init string.\n");
    }
  }
  for (;;) {
    pfd[0].fd = realdev;
    pfd[0].events = POLLIN;
    pfd[1].fd = fd;
    pfd[1].events = POLLIN;
    pollret = poll(pfd, 2, 1000);
    if (pollret < 0) {
      fprintf(stderr, "Poll error: %s\n", strerror(errno));
      syslog(LOG_ERR, "Poll error: %s\n", strerror(errno));
      exit(1);
    }
    if (pollret == 0)
      continue;

    if ((pfd[0].revents & POLLHUP || pfd[0].revents & POLLERR || pfd[0].revents & POLLNVAL) ||
        (pfd[1].revents & POLLHUP || pfd[1].revents & POLLERR || pfd[1].revents & POLLNVAL))
      exit(1);
    if (pfd[0].revents & POLLIN) {
      r = read(realdev, buffer, BUFFER_SIZE);
      pfd[1].events = POLLOUT;
      pollret = poll(&pfd[1], 1, 50);
      if (pollret < 0) {
        fprintf(stderr, "Poll error: %s\n", strerror(errno));
        syslog(LOG_ERR, "Poll error: %s\n", strerror(errno));
        exit(1);
      }
      if (pfd[1].revents & POLLOUT)
        write(fd, buffer, r);
    }
    if (pfd[1].revents & POLLIN) {
      r = read(fd, buffer, BUFFER_SIZE);
      pfd[0].fd = realdev;
      pfd[0].events = POLLOUT;
      pollret = poll(&pfd[0], 1, 50);
      if (pollret < 0) {
        fprintf(stderr, "Poll error: %s\n", strerror(errno));
        syslog(LOG_ERR, "Poll error: %s\n", strerror(errno));
       exit(1);
      }
      if (pfd[0].revents & POLLOUT)
        write(realdev, buffer, r);
    }
  }
}