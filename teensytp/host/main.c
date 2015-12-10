/*
 * TeensyTP, Team Player/4-Player Adaptor implementation for Teensy3
 * host part
 * Copyright (c) 2015 notaz
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <termios.h>
#include <errno.h>
#include <linux/usbdevice_fs.h>
#include <linux/usb/ch9.h>
#include <linux/input.h>
#include "../pkts.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

struct teensy_dev {
  int fd;
  struct {
    int ep_in;
    int ep_out;
  } ifaces[2];
};

/* return 1 if found, 0 if not, < 0 on error */
static int find_device(struct teensy_dev *dev,
  uint16_t vendor, uint16_t product)
{
  const char path_root[] = "/dev/bus/usb";
  union {
    struct usb_descriptor_header hdr;
    struct usb_device_descriptor d;
    struct usb_config_descriptor c;
    struct usb_interface_descriptor i;
    struct usb_endpoint_descriptor e;
    char space[0x100]; /* enough? */
  } desc;
  char path_bus[256], path_dev[256];
  struct dirent *ent, *ent_bus;
  DIR *dir = NULL, *dir_bus = NULL;
  int num, fd = -1;
  int iface = -1;
  int retval = -1;
  int ret;

  memset(dev, 0xff, sizeof(*dev));

  dir = opendir(path_root);
  if (dir == NULL) {
    perror("opendir");
    return -1;
  }

  for (ent = readdir(dir); ent != NULL; ent = readdir(dir)) {
    /* should be a number like 000 */
    if (sscanf(ent->d_name, "%03d", &num) != 1)
      continue;

    snprintf(path_bus, sizeof(path_bus), "%s/%s",
        path_root, ent->d_name);

    dir_bus = opendir(path_bus);
    if (dir_bus == NULL)
      continue;

    ent_bus = readdir(dir_bus);
    for (; ent_bus != NULL; ent_bus = readdir(dir_bus)) {
      if (sscanf(ent->d_name, "%03d", &num) != 1)
        continue;

      snprintf(path_dev, sizeof(path_dev), "%s/%s/%s",
          path_root, ent->d_name, ent_bus->d_name);

      fd = open(path_dev, O_RDWR);
      if (fd == -1)
        continue;

      ret = read(fd, &desc.d, sizeof(desc.d));
      if (ret != sizeof(desc.d)) {
        fprintf(stderr, "desc read: %d/%zd: ", ret, sizeof(desc.d));
        perror("");
        goto next;
      }

      if (desc.d.bDescriptorType != USB_DT_DEVICE) {
        fprintf(stderr, "%s: bad DT: 0x%02x\n",
            path_dev, desc.d.bDescriptorType);
        goto next;
      }

      if (desc.d.idVendor == vendor && desc.d.idProduct == product)
        goto found;

next:
      close(fd);
      fd = -1;
    }

    closedir(dir_bus);
    dir_bus = NULL;
  }

  /* not found */
  retval = 0;
  goto out;

found:
  if (desc.d.bNumConfigurations != 1) {
    fprintf(stderr, "unexpected bNumConfigurations: %u\n",
        desc.d.bNumConfigurations);
    goto out;
  }

  /* walk through all descriptors */
  while (1)
  {
    ret = read(fd, &desc.hdr, sizeof(desc.hdr));
    if (ret == 0)
      break;
    if (ret != sizeof(desc.hdr)) {
      fprintf(stderr, "desc.hdr read: %d/%zd: ", ret, sizeof(desc.hdr));
      perror("");
      break;
    }

    ret = (int)lseek(fd, -sizeof(desc.hdr), SEEK_CUR);
    if (ret == -1) {
      perror("lseek");
      break;
    }

    ret = read(fd, &desc, desc.hdr.bLength);
    if (ret != desc.hdr.bLength) {
      fprintf(stderr, "desc read: %d/%u: ", ret, desc.hdr.bLength);
      perror("");
      break;
    }

    switch (desc.hdr.bDescriptorType) {
      case USB_DT_CONFIG:
        if (desc.c.bNumInterfaces != 2) {
          fprintf(stderr, "unexpected bNumInterfaces: %u\n",
              desc.c.bNumInterfaces);
          goto out;
        }
        break;

      case USB_DT_INTERFACE:
        if (desc.i.bInterfaceClass != USB_CLASS_HID
            || desc.i.bInterfaceSubClass != 0
            || desc.i.bInterfaceProtocol != 0) {
          fprintf(stderr, "unexpected interface %x:%x:%x\n",
            desc.i.bInterfaceClass, desc.i.bInterfaceSubClass,
            desc.i.bInterfaceProtocol);
          goto out;
        }
        if (desc.i.bNumEndpoints != 2) {
          fprintf(stderr, "unexpected bNumEndpoints: %u\n",
            desc.i.bNumEndpoints);
          goto out;
        }
        iface++;
        break;

      case USB_DT_ENDPOINT:
        if (iface < 0 || iface >= ARRAY_SIZE(dev->ifaces)) {
          fprintf(stderr, "bad iface: %d\n", iface);
          goto out;
        }
        if (desc.e.wMaxPacketSize != 64 && desc.e.wMaxPacketSize != 32) {
          fprintf(stderr, "iface %d, EP %02x: "
            "unexpected wMaxPacketSize: %u\n",
            iface, desc.e.bEndpointAddress, desc.e.wMaxPacketSize);
          goto out;
        }
        if (desc.e.bEndpointAddress & 0x80)
          dev->ifaces[iface].ep_in = desc.e.bEndpointAddress; // & 0x7F;
        else
          dev->ifaces[iface].ep_out = desc.e.bEndpointAddress;
        break;

      case 0x21:
        /* ignore */
        break;

      default:
        fprintf(stderr, "skipping desc 0x%02x\n",
          desc.hdr.bDescriptorType);
        break;
    }
  }

  /* claim interfaces */
  for (iface = 0; iface < ARRAY_SIZE(dev->ifaces); iface++) {
    struct usbdevfs_ioctl usbio;

    if (dev->ifaces[iface].ep_in == -1) {
      fprintf(stderr, "missing ep_in, iface: %d\n", iface);
      goto out;
    }
    if (dev->ifaces[iface].ep_out == -1) {
      fprintf(stderr, "missing ep_out, iface: %d\n", iface);
      goto out;
    }

    /* disconnect default driver */
    memset(&usbio, 0, sizeof(usbio));
    usbio.ifno = iface;
    usbio.ioctl_code = USBDEVFS_DISCONNECT;
    ret = ioctl(fd, USBDEVFS_IOCTL, &usbio);
    if (ret != 0 && errno != ENODATA)
      perror("USBDEVFS_DISCONNECT");

    ret = ioctl(fd, USBDEVFS_CLAIMINTERFACE, &iface);
    if (ret != 0)
      perror("USBDEVFS_CLAIMINTERFACE");
  }

  dev->fd = fd;
  fd = -1;
  retval = 1;

out:
  if (fd != -1)
    close(fd);
  if (dir_bus != NULL)
    closedir(dir_bus);
  if (dir != NULL)
    closedir(dir);

  return retval;
}

static int enable_echo(int enable)
{
  const char *portname = "/dev/tty";
  struct termios tty;
  int retval = -1;
  int ret;
  int fd;

  memset(&tty, 0, sizeof(tty));

  fd = open(portname, O_RDWR | O_NOCTTY | O_SYNC);
  if (fd < 0) {
    fprintf(stderr, "open %s: ", portname);
    perror("");
    return 1;
  }

  ret = tcgetattr(fd, &tty);
  if (ret != 0) {
    perror("tcgetattr");
    goto out;
  }

  // printf("lflag: 0%o\n", tty.c_lflag);
  if (enable)
    tty.c_lflag |= ECHO | ICANON;
  else {
    tty.c_lflag &= ~(ECHO | ICANON);
    tty.c_cc[VMIN] = tty.c_cc[VTIME] = 0;
  }

  ret = tcsetattr(fd, TCSANOW, &tty);
  if (ret != 0) {
    perror("tcsetattr");
    goto out;
  }

  retval = 0;
out:
  close(fd);

  return retval;
}

static int g_exit;

static void signal_handler(int sig)
{
  g_exit = 1;
  signal(sig, SIG_DFL);
}

/* MXYZ SACB RLDU */
enum mdbtn {
  MDBTN_UP    = (1 <<  0),
  MDBTN_DOWN  = (1 <<  1),
  MDBTN_LEFT  = (1 <<  2),
  MDBTN_RIGHT = (1 <<  3),
  MDBTN_A     = (1 <<  6),
  MDBTN_B     = (1 <<  4),
  MDBTN_C     = (1 <<  5),
  MDBTN_START = (1 <<  7),
  MDBTN_X     = (1 << 10),
  MDBTN_Y     = (1 <<  9),
  MDBTN_Z     = (1 <<  8),
  MDBTN_MODE  = (1 << 11),
};

#define BTN_JOY BTN_JOYSTICK

static const uint32_t evdev_md_default_map[KEY_CNT] = {
  [KEY_UP]       = MDBTN_UP,
  [KEY_DOWN]     = MDBTN_DOWN,
  [KEY_LEFT]     = MDBTN_LEFT,
  [KEY_RIGHT]    = MDBTN_RIGHT,
  [KEY_Z]        = MDBTN_A,
  [KEY_X]        = MDBTN_B,
  [KEY_C]        = MDBTN_C,
  [KEY_A]        = MDBTN_X,
  [KEY_S]        = MDBTN_Y,
  [KEY_D]        = MDBTN_Z,
  [KEY_F]        = MDBTN_MODE,
  [KEY_ENTER]    = MDBTN_START,
  // gamepad
  [BTN_JOY + 0]  = MDBTN_A,
  [BTN_JOY + 1]  = MDBTN_B,
  [BTN_JOY + 2]  = MDBTN_C,
  [BTN_JOY + 3]  = MDBTN_START,
  // pandora
  [KEY_HOME]     = MDBTN_A,
  [KEY_PAGEDOWN] = MDBTN_B,
  [KEY_END]      = MDBTN_C,
  [KEY_LEFTALT]  = MDBTN_START,
};

static struct player_state {
  uint32_t kc_map[KEY_CNT];
  uint32_t state;
  int dirty;
} players[4];

static int do_evdev_input(int fd, unsigned int player_i)
{
  struct player_state *player;
  struct input_event ev;
  uint32_t mask_clear = 0;
  uint32_t mask_set = 0;
  uint32_t old_state;
  int ret;

  ret = read(fd, &ev, sizeof(ev));
  if (ret != sizeof(ev)) {
    fprintf(stderr, "evdev read %d/%zd: ", ret, sizeof(ev));
    perror("");
    return 0;
  }

  if (player_i >= ARRAY_SIZE(players)) {
    fprintf(stderr, "bad player: %u\n", player_i);
    return 0;
  }
  player = &players[player_i];
  old_state = player->state;

  if (ev.type == EV_ABS) {
    if (ev.code == 0) {
      mask_clear = MDBTN_LEFT | MDBTN_RIGHT;
      if (ev.value < 10)
        mask_set = MDBTN_LEFT;
      else if (ev.value > 210)
        mask_set = MDBTN_RIGHT;
    }
    else {
      mask_clear = MDBTN_UP | MDBTN_DOWN;
      if (ev.value < 10)
        mask_set = MDBTN_UP;
      else if (ev.value > 210)
        mask_set = MDBTN_DOWN;
    }
    printf("abs, s %x %x\n", mask_clear, mask_set);
  }
  else if (ev.type == EV_KEY) {
    if (ev.value != 0 && ev.value != 1)
      return 0;

    if ((uint32_t)ev.code >= ARRAY_SIZE(player->kc_map)) {
      fprintf(stderr, "evdev read bad key: %u\n", ev.code);
      return 0;
    }

    if (ev.value) // press?
      mask_set   = player->kc_map[ev.code];
    else
      mask_clear = player->kc_map[ev.code];
  }

  player->state &= ~mask_clear;
  player->state |=  mask_set;

  player->dirty |= old_state != player->state;
  return player->dirty;
}

static int open_evdev(const char *name)
{
  int evdev_support = 0;
  int fd, ret;

  fd = open(name, O_RDONLY);
  if (fd == -1) {
    fprintf(stderr, "open %s: ", name);
    perror("");
    return -1;
  }
  ret = ioctl(fd, EVIOCGBIT(0, sizeof(evdev_support)),
              &evdev_support);
  if (ret < 0)
    perror("EVIOCGBIT");
  if (!(evdev_support & (1 << EV_KEY))) {
    fprintf(stderr, "%s doesn't have keys\n", name);
    close(fd);
    return -1;
  }

  return fd;
}

static void do_stdin_input(uint32_t *mode, int *changed)
{
  char c = 0;
  int ret;

  ret = read(STDIN_FILENO, &c, 1);
  if (ret <= 0) {
    perror("read stdin");
    return;
  }

  switch (c) {
  case '1':
    printf("3btn mode\n");
    *mode = OP_MODE_3BTN;
    *changed = 1;
    break;
  case '2':
    printf("6btn mode\n");
    *mode = OP_MODE_6BTN;
    *changed = 1;
    break;
  case '3':
    printf("teamplayer mode\n");
    *mode = OP_MODE_TEAMPLAYER;
    *changed = 1;
    break;
  }
}

static int parse_binds(unsigned int player_i, const char *binds)
{
  return 0;
}

static int submit_urb(int fd, struct usbdevfs_urb *urb, int ep,
  void *buf, size_t buf_size)
{
  memset(urb, 0, sizeof(*urb));
  urb->type = USBDEVFS_URB_TYPE_INTERRUPT;
  urb->endpoint = ep;
  urb->buffer = buf;
  urb->buffer_length = buf_size;

  return ioctl(fd, USBDEVFS_SUBMITURB, urb);
}

enum my_urbs {
  URB_DATA_IN,
  URB_DATA_OUT,
  URB_DBG_IN,
  URB_CNT
};

static void usage(const char *argv0)
{
  fprintf(stderr, "usage:\n%s <-e player /dev/input/node>*\n"
    "  [-b <player> <keycode=mdbtn>[,keycode=mdbtn]]\n"
    "  [-m <mode>]\n\n"
    "  mdbtn: int 0-11: UDLR ABCS XYZM)\n"
    "  mode:  int 0-2: 3btn 6btn teamplayer\n", argv0);
  exit(1);
}

static void bad_arg(char *argv[], int a)
{
  fprintf(stderr, "bad arg %d: '%s'\n", a, argv[a]);
  usage(argv[0]);
}

int main(int argc, char *argv[])
{
  struct teensy_dev dev;
  struct usbdevfs_urb urb[URB_CNT];
  struct usbdevfs_urb *reaped_urb;
  uint32_t evdev_players[16];
  int evdev_fds[16];
  int evdev_fd_cnt = 0;
  int wait_device = 0;
  int pending_urbs = 0;
  int had_input = 0;
  fd_set rfds, wfds;
  int mode_changed = 0;
  char buf_dbg[64 + 1];
  struct tp_pkt pkt_in;
  struct tp_pkt pkt_out;
  struct timeval *timeout = NULL;
  struct timeval tout;
  uint32_t mode = OP_MODE_3BTN;
  uint32_t player;
  int i, ret = -1;
  int fd;

  for (i = 0; i < ARRAY_SIZE(players); i++)
    memcpy(&players[i].kc_map, evdev_md_default_map,
           sizeof(players[i].kc_map));

  for (i = 1; i < argc; i++) {
    if (argv[i][0] == '-') {
      switch (argv[i][1] | (argv[i][2] << 8)) {
      case 'e':
        if (argv[++i] == NULL)
          bad_arg(argv, i);
        player = strtoul(argv[i], NULL, 0);
        if (player >= ARRAY_SIZE(players))
          bad_arg(argv, i);

        if (argv[++i] == NULL)
          bad_arg(argv, i);
        fd = open_evdev(argv[i]);
        if (fd == -1)
          bad_arg(argv, i);
        if (evdev_fd_cnt >= ARRAY_SIZE(evdev_fds)) {
          fprintf(stderr, "too many evdevs\n");
          break;
        }
        evdev_players[evdev_fd_cnt] = player;
        evdev_fds[evdev_fd_cnt] = fd;
        evdev_fd_cnt++;
        continue;
      case 'b':
        if (argv[++i] == NULL)
          bad_arg(argv, i);
        player = strtoul(argv[i], NULL, 0);
        if (player >= ARRAY_SIZE(players))
          bad_arg(argv, i);

        if (argv[++i] == NULL)
          bad_arg(argv, i);
        ret = parse_binds(player, argv[i]);
        if (ret != 0)
          bad_arg(argv, i);
        continue;
      case 'm':
        if (argv[++i] == NULL)
          bad_arg(argv, i);
        mode = strtoul(argv[i], NULL, 0);
        mode_changed = 1;
        continue;
      }
    }
    bad_arg(argv, i);
  }

  if (evdev_fd_cnt == 0)
    usage(argv[0]);

  enable_echo(0);
  signal(SIGINT, signal_handler);

  dev.fd = -1;

  while (!g_exit || (pending_urbs & (1 << URB_DATA_OUT)))
  {
    if (dev.fd == -1) {
      ret = find_device(&dev, 0x16C0, 0x0486);
      if (ret < 0)
        break;

      if (ret == 0) {
        if (!wait_device) {
          printf("waiting for device...\n");
          wait_device = 1;
        }
        usleep(250000);
        continue;
      }

      wait_device = 0;
      pending_urbs = 0;

      /* we wait first, then send commands, but if teensy
       * is started already, it won't send anything back */
      tout.tv_sec = 1;
      tout.tv_usec = 0;
      timeout = &tout;
    }

    if (!(pending_urbs & (1 << URB_DATA_IN))) {
      memset(&pkt_in, 0, sizeof(pkt_in));
      ret = submit_urb(dev.fd, &urb[URB_DATA_IN], dev.ifaces[0].ep_in,
                       &pkt_in, sizeof(pkt_in));
      if (ret != 0) {
        perror("USBDEVFS_SUBMITURB URB_DATA_IN");
        break;
      }

      pending_urbs |= 1 << URB_DATA_IN;
    }
    if (!(pending_urbs & (1 << URB_DBG_IN))) {
      ret = submit_urb(dev.fd, &urb[URB_DBG_IN], dev.ifaces[1].ep_in,
                       buf_dbg, sizeof(buf_dbg) - 1);
      if (ret != 0) {
        perror("USBDEVFS_SUBMITURB URB_DBG_IN");
        break;
      }

      pending_urbs |= 1 << URB_DBG_IN;
    }

    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    for (i = 0; i < evdev_fd_cnt; i++)
      FD_SET(evdev_fds[i], &rfds);

    FD_ZERO(&wfds);
    FD_SET(dev.fd, &wfds);

    ret = select(dev.fd + 1, &rfds, &wfds, NULL, timeout);
    if (ret < 0) {
      perror("select");
      break;
    }
    timeout = NULL;

    /* something form stdin? */
    if (FD_ISSET(STDIN_FILENO, &rfds))
      do_stdin_input(&mode, &mode_changed);

    /* something from input devices? */
    had_input = 0;
    for (i = 0; i < evdev_fd_cnt; i++) {
      if (FD_ISSET(evdev_fds[i], &rfds)) {
        do_evdev_input(evdev_fds[i], evdev_players[i]);
        had_input = 1;
      }
    }
    if (had_input) {
      /* collect any other input changes before starting
       * the slow USB transfer */
      tout.tv_sec = tout.tv_usec = 0;
      timeout = &tout;
      continue;
    }

    /* something from USB? */
    if (FD_ISSET(dev.fd, &wfds))
    {
      unsigned int which_urb;

      reaped_urb = NULL;
      ret = ioctl(dev.fd, USBDEVFS_REAPURB, &reaped_urb);
      if (ret != 0) {
        if (errno == ENODEV)
          goto dev_close;
        perror("USBDEVFS_REAPURB");
        break;
      }
      which_urb = reaped_urb - urb;
      if (which_urb < ARRAY_SIZE(urb))
        pending_urbs &= ~(1 << which_urb);
      else {
        fprintf(stderr, "reaped unknown urb: %p #%u",
                reaped_urb, which_urb);
      }

      if (reaped_urb != NULL && reaped_urb->status != 0) {
        errno = -reaped_urb->status;
        fprintf(stderr, "urb #%u: ", which_urb);
        perror("");
        if (reaped_urb->status == -EILSEQ) {
          /* this is usually a sign of disconnect.. */
          usleep(250000);
          goto dev_close;
        }
      }
      else if (reaped_urb == &urb[URB_DATA_IN])
      {
        /* some request from teensy */
        printf("rx data?\n");
      }
      else if (reaped_urb == &urb[URB_DATA_OUT])
      {
      }
      else if (reaped_urb == &urb[URB_DBG_IN])
      {
        /* debug text */
        buf_dbg[reaped_urb->actual_length] = 0;
        printf("%s", buf_dbg);

        // continue receiving debug before sending out stuff
        tout.tv_sec = 0;
        tout.tv_usec = 1000;
        timeout = &tout;
        continue;
      }
      else {
        fprintf(stderr, "reaped unknown urb? %p #%zu\n",
          reaped_urb, reaped_urb - urb);
      }
    }

    /* something to send? */
    if (pending_urbs & (1 << URB_DATA_OUT))
      // can't do that yet - out urb still busy
      continue;

    if (mode_changed) {
      memset(&pkt_out, 0, sizeof(pkt_out));
      pkt_out.type = PKT_UPD_MODE;
      pkt_out.mode = mode;

      ret = submit_urb(dev.fd, &urb[URB_DATA_OUT], dev.ifaces[0].ep_out,
                       &pkt_out, sizeof(pkt_out));
      if (ret != 0) {
        perror("USBDEVFS_SUBMITURB PKT_STREAM_ENABLE");
        continue;
      }
      pending_urbs |= 1 << URB_DATA_OUT;
      mode_changed = 0;
      continue;
    }

    /* send buttons if there were any changes */
    memset(&pkt_out, 0, sizeof(pkt_out));
    for (i = 0; i < ARRAY_SIZE(players); i++) {
      if (players[i].dirty)
        pkt_out.changed_players |= 1 << i;
      players[i].dirty = 0;

      pkt_out.bnts[i] = players[i].state;
    }
    if (pkt_out.changed_players != 0) {
      pkt_out.type = PKT_UPD_BTNS;

      ret = submit_urb(dev.fd, &urb[URB_DATA_OUT], dev.ifaces[0].ep_out,
                       &pkt_out, sizeof(pkt_out));
      if (ret != 0) {
        perror("USBDEVFS_SUBMITURB PKT_FIXED_STATE");
        break;
      }
      pending_urbs |= 1 << URB_DATA_OUT;
      continue;
    }

    continue;

dev_close:
    close(dev.fd);
    dev.fd = -1;
  }

  enable_echo(1);

  if (dev.fd != -1) {
    /* deal with pending URBs */
    if (pending_urbs & (1 << URB_DATA_IN))
      ioctl(dev.fd, USBDEVFS_DISCARDURB, &urb[URB_DATA_IN]);
    if (pending_urbs & (1 << URB_DBG_IN))
      ioctl(dev.fd, USBDEVFS_DISCARDURB, &urb[URB_DBG_IN]);
    for (i = 0; i < URB_CNT; i++) {
      if (pending_urbs & (1 << i)) {
        ret = ioctl(dev.fd, USBDEVFS_REAPURB, &reaped_urb);
        if (ret != 0)
          perror("USBDEVFS_REAPURB");
      }
    }

    close(dev.fd);
  }

  return ret;
}

// vim: ts=2:sw=2:expandtab
