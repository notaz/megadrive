/*
 * TeensyTAS, TAS input player for MegaDrive
 * Copyright (c) 2014 notaz
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

/* return 1 if founf, 0 if not, < 0 on error */
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

/* ?0SA 00DU, ?1CB RLDU */
#define STATE_BYTES 2

static uint8_t fixed_input_state[STATE_BYTES] = { 0x33, 0x3f };

enum mdbtn {
  MDBTN_UP = 1,
  MDBTN_DOWN,
  MDBTN_LEFT,
  MDBTN_RIGHT,
  MDBTN_A,
  MDBTN_B,
  MDBTN_C,
  MDBTN_START,
};

static const enum mdbtn evdev_md_map[KEY_CNT] = {
  [KEY_UP]       = MDBTN_UP,
  [KEY_DOWN]     = MDBTN_DOWN,
  [KEY_LEFT]     = MDBTN_LEFT,
  [KEY_RIGHT]    = MDBTN_RIGHT,
  [KEY_HOME]     = MDBTN_A,
  [KEY_PAGEDOWN] = MDBTN_B,
  [KEY_END]      = MDBTN_C,
  [KEY_LEFTALT]  = MDBTN_START,
};

int do_evdev_input(int fd)
{
  uint8_t old_state[STATE_BYTES];
  uint8_t changed_bits[STATE_BYTES] = { 0, };
  struct input_event ev;
  enum mdbtn mdbtn;
  int i, ret;

  ret = read(fd, &ev, sizeof(ev));
  if (ret != sizeof(ev)) {
    fprintf(stderr, "evdev read %d/%zd: ", ret, sizeof(ev));
    perror("");
    return 0;
  }

  if (ev.type != EV_KEY)
    return 0;

  if (ev.value != 0 && ev.value != 1)
    return 0;

  if ((uint32_t)ev.code >= ARRAY_SIZE(evdev_md_map)) {
    fprintf(stderr, "evdev read bad key: %u\n", ev.code);
    return 0;
  }

  mdbtn = evdev_md_map[ev.code];
  if (mdbtn == 0)
    return 0;

  memcpy(old_state, fixed_input_state, STATE_BYTES);

  /* ?0SA 00DU, ?1CB RLDU */
  switch (mdbtn) {
  case MDBTN_UP:
    changed_bits[0] = 0x01;
    changed_bits[1] = 0x01;
    break;
  case MDBTN_DOWN:
    changed_bits[0] = 0x02;
    changed_bits[1] = 0x02;
    break;
  case MDBTN_LEFT:
    changed_bits[0] = 0x00;
    changed_bits[1] = 0x04;
    break;
  case MDBTN_RIGHT:
    changed_bits[0] = 0x00;
    changed_bits[1] = 0x08;
    break;
  case MDBTN_A:
    changed_bits[0] = 0x10;
    changed_bits[1] = 0x00;
    break;
  case MDBTN_B:
    changed_bits[0] = 0x00;
    changed_bits[1] = 0x10;
    break;
  case MDBTN_C:
    changed_bits[0] = 0x00;
    changed_bits[1] = 0x20;
    break;
  case MDBTN_START:
    changed_bits[0] = 0x20;
    changed_bits[1] = 0x00;
    break;
  }

  if (ev.value) {
    // key press
    for (i = 0; i < STATE_BYTES; i++)
      fixed_input_state[i] &= ~changed_bits[i];
  }
  else {
    // key release
    for (i = 0; i < STATE_BYTES; i++)
      fixed_input_state[i] |=  changed_bits[i];
  }

  return memcmp(old_state, fixed_input_state, STATE_BYTES) ? 1 : 0;
}

#define MAX_INPUT_BYTES 2

// TODO: 6btn
static int tas_data_to_teensy(uint16_t b, uint8_t *data, FILE *logf)
{
  uint8_t t;

  /* SCBA RLDU */
  /*     v     */
  /* ?0SA 00DU, ?1CB RLDU */
  data[0] = (b & 0x13) | ((b >> 2) & 0x20);
  data[1] = (b & 0x0f) | ((b >> 1) & 0x30);

  if (logf != NULL) {
    fwrite(&data[0], 1, 1, logf);
    t = data[1] | 0x40; // expected TH
    fwrite(&t, 1, 1, logf);
  }

  return 2;
}

struct gmv_tas {
  char sig[15];
  char ver;
  uint32_t rerecord_count;
  char ctrl1;
  char ctrl2;
  uint16_t flags;
  char name[40];
  uint8_t data[0][3];
};

static int import_gmv(FILE *f, long size,
  uint8_t *out[2], int out_byte_count[2], FILE *logf)
{
  struct gmv_tas *gmv;
  int frame_count;
  int count = 0;
  uint16_t val;
  int ret;
  int i;

  out_byte_count[0] = out_byte_count[1] = 0;

  if (size < (long)sizeof(*gmv)) {
    fprintf(stderr, "bad gmv size: %ld\n", size);
    return -1;
  }

  gmv = malloc(size);
  if (gmv == NULL) {
    fprintf(stderr, "OOM?\n");
    return -1;
  }
  ret = fread(gmv, 1, size, f);
  if (ret != size) {
    fprintf(stderr, "fread %d/%ld: ", ret, size);
    perror("");
    return -1;
  }

  frame_count = (size - sizeof(*gmv)) / sizeof(gmv->data[0]);

  /* check the GMV.. */
  if (frame_count <= 0 || size != sizeof(*gmv) + frame_count * 3) {
    fprintf(stderr, "broken gmv? frames=%d\n", frame_count);
    return -1;
  }

  if (strncmp(gmv->sig, "Gens Movie TEST", 15) != 0) {
    fprintf(stderr, "bad GMV sig\n");
    return -1;
  }
  if (gmv->ctrl1 != '3') {
    fprintf(stderr, "unhandled controlled config: '%c'\n", gmv->ctrl1);
    //return -1;
  }
  if (gmv->ver >= 'A') {
    if (gmv->flags & 0x40) {
      fprintf(stderr, "unhandled flag: movie requires a savestate\n");
      return -1;
    }
    if (gmv->flags & 0x20) {
      fprintf(stderr, "unhandled flag: 3-player movie\n");
      return -1;
    }
    if (gmv->flags & ~0x80) {
      //fprintf(stderr, "unhandled flag(s): %04x\n", gmv->flags);
      //return 1;
    }
  }
  gmv->name[39] = 0;
  printf("loaded GMV: %s\n", gmv->name);
  printf("%d frames, %u rerecords\n",
         frame_count, gmv->rerecord_count);

  out[0] = malloc(frame_count * MAX_INPUT_BYTES);
  if (out[0] == NULL) {
    fprintf(stderr, "OOM?\n");
    return -1;
  }

  for (i = 0; i < frame_count; i++) {
    val = gmv->data[i][0] | ((gmv->data[i][2] & 0x0f) << 8);
    count += tas_data_to_teensy(val, out[0] + count, logf);

    if (gmv->data[i][1] != 0xff || gmv->data[i][2] != 0xff)
    {
      fprintf(stderr, "f %d: unhandled byte(s) %02x %02x\n",
        i, gmv->data[i][1], gmv->data[i][2]);
    }
  }

  out_byte_count[0] = count;
  return 0;
}

static int do_bkm_char(char c, char expect, uint16_t *val, int bit)
{
  if (c == expect) {
    *val &= ~(1 << bit);
    return 0;
  }
  if (c == '.')
    return 0;

  fprintf(stderr, "unexpected bkm char: '%c' instead of '%c'\n",
          c, expect);
  return 1;
}

static int import_bkm(FILE *f, uint8_t *out[2], int out_byte_count[2],
  FILE *logf)
{
  int teensy_bytes = 0;
  int have_pl2 = 0;
  int have_xyz = 0;
  int frames = 0;
  int count = 0;
  int alloc = 0;
  int line = 0;
  char buf[256];
  const char *r;
  uint16_t val;
  char *p;
  int pl, i;

  while ((p = fgets(buf, sizeof(buf), f)) != NULL)
  {
    line++;
    if (p[0] != '|')
      continue;

    if (strlen(p) < 30)
      goto unhandled_line;
    if (p[30] != '\r' && p[30] != '\n')
      goto unhandled_line;
    p[30] = 0;

    if (count >= alloc - MAX_INPUT_BYTES) {
      alloc = alloc * 2 + 64;
      for (pl = 0; pl < 2; pl++) {
        out[pl] = realloc(out[pl], alloc * sizeof(out[0][0]));
        if (out[pl] == NULL) {
          fprintf(stderr, "OOM?\n");
          return -1;
        }
      }
    }

    if (strncmp(p, "|.|", 3) != 0)
      goto unhandled_line;
    p += 3;

    for (pl = 0; pl < 2; pl++) {
      static const char ref[] = "UDLRABCSXYZM";

      val = 0xfff;
      for (r = ref, i = 0; *r != 0; p++, r++, i++) {
        if (do_bkm_char(*p, *r, &val, i))
          goto unhandled_line;
      }

      if (*p++ != '|')
        goto unhandled_line;

      teensy_bytes = tas_data_to_teensy(val, out[pl] + count, logf);

      if ((val & 0xf00) != 0xf00)
        have_xyz = 1;
      if (pl == 1)
        have_pl2 |= (val != 0xfff);
    }
    count += teensy_bytes;

    if (strcmp(p, "|") != 0)
      goto unhandled_line;

    frames++;
    continue;

unhandled_line:
    fprintf(stderr, "unhandled bkm line %d: '%s'\n", line, buf);
    return -1;
  }

  printf("loaded bkm, %d players, %d frames, %d bytes, have_xyz=%d\n",
    have_pl2 ? 2 : 1, frames, count, have_xyz);
  out_byte_count[0] = count;
  if (have_pl2)
    out_byte_count[1] = count;
  else {
    free(out[1]);
    out[1] = NULL;
  }

  return 0;
}

static int import_raw(FILE *f, uint8_t **out, int *out_byte_count,
  FILE *logf)
{
  int count = 0;
  int alloc = 0;
  int line = 0;
  int first = 1;
  char buf[256];
  uint8_t val;
  char *p;
  int i;

  *out_byte_count = 0;

  while ((p = fgets(buf, sizeof(buf), f)) != NULL)
  {
    line++;
    if (p[0] == '#')
      continue;
    if (p[0] != 'e') {
      printf("skipping: %s", p);
      continue;
    }

    val = 0;
    p++;
    for (i = 6; i >= 0; i--, p++) {
      if (*p != '0' && *p != '1')
        goto bad;
      if (*p == '1')
        val |= 1 << i;
    }
    if (*p != ' ')
      goto bad;

    if (first && (val & 0x40))
      continue; // XXX..
    first = 0;

    if (count >= alloc) {
      alloc = alloc * 2 + 64;
      *out = realloc(*out, alloc * sizeof((*out)[0]));
      if (*out == NULL) {
        fprintf(stderr, "OOM?\n");
        return -1;
      }
    }

    if (logf)
      fwrite(&val, 1, 1, logf);

    (*out)[count++] = val & 0x3f;
    continue;

bad:
    fprintf(stderr, "bad raw line %d: '%s'\n", line, buf);
    return -1;
  }

  printf("loaded raw, %d bytes\n", count);
  *out_byte_count = count;
  return 0;
}

static int write_bkm_frame(FILE *f, const uint8_t *data)
{
  /* ?0SA 00DU, ?1CB RLDU */
  static const char ref[]  = "UDLRABCSXYZM";
  static const char bits[] = { 0,1,2,3, 12,4,5,13, 16,16,16,16 };
  uint32_t idata[2];
  int p, i;

  if (f == NULL) {
    fprintf(stderr, "%s called without outfile\n", __func__);
    goto out;
  }

  idata[0] = 0x10000 | (data[0] << 8) | data[1];
  idata[1] = ~0;

  fprintf(f, "|.|");
  for (p = 0; p < 2; p++) {
    for (i = 0; i < 12; i++)
      fprintf(f, "%c", (idata[p] & (1 << bits[i])) ? '.' : ref[i]);
    fprintf(f, "|");
  }
  fprintf(f, "|\n");

out:
  return 2;
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

static void missing_arg(int a)
{
  fprintf(stderr, "missing arg: %d\n", a);
  exit(1);
}

int main(int argc, char *argv[])
{
  struct teensy_dev dev;
  struct usbdevfs_urb urb[URB_CNT];
  struct usbdevfs_urb *reaped_urb;
  int fixed_input_changed = 0;
  int evdev_fds[16];
  int evdev_fd_cnt = 0;
  int evdev_support;
  int wait_device = 0;
  int pending_urbs = 0;
  fd_set rfds, wfds;
  const char *tasfn = NULL;
  const char *tasfn_p2 = NULL;
  const char *outfn = NULL;
  const char *logfn = NULL;
  uint8_t *tas_data[2] = { NULL, NULL };
  int tas_data_size[2] = { 0, 0 };
  int bytes_sent[2] = { 0, 0 };
  int use_vsync = 0; // frame increment on vsync
  int separate_2p = 0;
  int no_start_seq = 0;
  int enable_sent = 0;
  int abort_sent = 0;
  int frame_count = 0;
  char buf_dbg[64 + 1];
  struct tas_pkt pkt_in;
  struct tas_pkt pkt_out;
  struct timeval *timeout = NULL;
  struct timeval tout;
  FILE *outf = NULL;
  FILE *logf = NULL;
  int i, ret = -1;
  int fd;

  for (i = 1; i < argc; i++) {
    if (argv[i][0] == '-') {
      switch (argv[i][1] | (argv[i][2] << 8)) {
      case 'm':
        i++;
        if (argv[i] == NULL)
          missing_arg(i);
        tasfn = argv[i];
        continue;
      case '2':
        i++;
        if (argv[i] == NULL)
          missing_arg(i);
        tasfn_p2 = argv[i];
        continue;
      case 'w':
        i++;
        if (argv[i] == NULL)
          missing_arg(i);
        outfn = argv[i];
        continue;
      case 'l':
        i++;
        if (argv[i] == NULL)
          missing_arg(i);
        logfn = argv[i];
        continue;
      case 'v':
        use_vsync = 1;
        continue;
      case 'n':
        no_start_seq = 1;
        continue;
      default:
        fprintf(stderr, "bad arg: %s\n", argv[i]);
        return 1;
      }
    }

    /* remaining args are evdev filenames */
    if (evdev_fd_cnt >= ARRAY_SIZE(evdev_fds)) {
      fprintf(stderr, "too many evdevs\n");
      break;
    }
    fd = open(argv[i], O_RDONLY);
    if (fd == -1) {
      fprintf(stderr, "open %s: ", argv[i]);
      perror("");
      continue;
    }
    evdev_support = 0;
    ret = ioctl(fd, EVIOCGBIT(0, sizeof(evdev_support)),
                &evdev_support);
    if (ret < 0)
      perror("EVIOCGBIT");
    if (!(evdev_support & (1 << EV_KEY))) {
      fprintf(stderr, "%s doesn't have keys\n", argv[i]);
      close(fd);
      continue;
    }
    evdev_fds[evdev_fd_cnt++] = fd;
  }

  if (logfn != NULL) {
    logf = fopen(logfn, "wb");
    if (logf == NULL) {
      fprintf(stderr, "fopen %s: ", logfn);
      perror("");
      return 1;
    }
  }

  if (tasfn != NULL) {
    FILE *f, *f_p2 = NULL;
    const char *ext;
    long size;

    f = fopen(tasfn, "rb");
    if (f == NULL) {
      fprintf(stderr, "fopen %s: ", tasfn);
      perror("");
      return 1;
    }

    if (tasfn_p2 != NULL) {
      f_p2 = fopen(tasfn_p2, "rb");
      if (f_p2 == NULL) {
        fprintf(stderr, "fopen %s: ", tasfn_p2);
        perror("");
        return 1;
      }
    }

    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
      fprintf(stderr, "bad size: %ld\n", size);
      return 1;
    }

    ext = strrchr(tasfn, '.');
    if (ext == NULL)
      ext = tasfn;
    else
      ext++;

    if (strcasecmp(ext, "gmv") == 0)
      ret = import_gmv(f, size, tas_data, tas_data_size, logf);
    else if (strcasecmp(ext, "bkm") == 0)
      ret = import_bkm(f, tas_data, tas_data_size, logf);
    else if (strcasecmp(ext, "txt") == 0)
      ret = import_raw(f, &tas_data[0], &tas_data_size[0], logf);
    else {
      fprintf(stderr, "unknown movie type: '%s'\n", ext);
      return 1;
    }
    fclose(f);

    if (ret != 0 || tas_data[0] == NULL || tas_data_size[0] <= 0) {
      fprintf(stderr, "failed fo parse %s\n", tasfn);
      return 1;
    }

    // separate file with p2 input?
    if (f_p2 != NULL) {
      ret = import_raw(f_p2, &tas_data[1], &tas_data_size[1], NULL);
      if (ret != 0 || tas_data[1] == NULL || tas_data_size[1] <= 0) {
        fprintf(stderr, "failed fo parse %s\n", tasfn_p2);
        return 1;
      }
      fclose(f_p2);
      separate_2p = 1;
    }

    if (logf != NULL) {
      fclose(logf);
      logf = NULL;
    }

    if (tas_data_size[1] != 0 && tas_data[1] == NULL) {
      fprintf(stderr, "missing tas_data[1]\n");
      return 1;
    }
  }

  if (outfn != NULL) {
    outf = fopen(outfn, "w");
    if (outf == NULL) {
      fprintf(stderr, "fopen %s: ", outfn);
      perror("");
      return 1;
    }
  }

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
          printf("waiting for device..\n");
          wait_device = 1;
        }
        usleep(250000);
        continue;
      }

      wait_device = 0;
      pending_urbs = 0;
      enable_sent = 0;
      bytes_sent[0] = 0;
      bytes_sent[1] = 0;

      /* we wait first, then send commands, but if teensy
       * is started already, it won't send anything */
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

    /* sometihng form stdin? */
    if (FD_ISSET(STDIN_FILENO, &rfds)) {
      char c = 0;
      ret = read(STDIN_FILENO, &c, 1);
      if (ret <= 0) {
        perror("read stdin");
        break;
      }

      switch (c) {
      case 'r':
        enable_sent = 0;
        break;
      }
    }

    /* something from input devices? */
    for (i = 0; i < evdev_fd_cnt; i++) {
      if (FD_ISSET(evdev_fds[i], &rfds)) {
        fixed_input_changed |=
          do_evdev_input(evdev_fds[i]);
      }
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
        int p;

        /* some request from teensy */
        switch (pkt_in.type) {
        case PKT_STREAM_REQ:
          p = pkt_in.req.is_p2 ? 1 : 0;
          printf("req%d: %d/%d/%d\n", pkt_in.req.is_p2,
            pkt_in.req.frame * 2, bytes_sent[p], tas_data_size[p]);

          pkt_out.size = 0;
          if (bytes_sent[p] < tas_data_size[p]) {
            pkt_out.type = p ? PKT_STREAM_DATA_TO_P2
                             : PKT_STREAM_DATA_TO_P1;

            i = tas_data_size[p] - bytes_sent[p];
            if (i > sizeof(pkt_out.data))
              i = sizeof(pkt_out.data);
            memcpy(pkt_out.data, tas_data[p] + bytes_sent[p], i);
            bytes_sent[p] += i;
            pkt_out.size = i;
          }
          else {
            pkt_out.type = PKT_STREAM_END;
          }

          ret = submit_urb(dev.fd, &urb[URB_DATA_OUT],
                  dev.ifaces[0].ep_out, &pkt_out, sizeof(pkt_out));
          if (ret != 0)
            perror("USBDEVFS_SUBMITURB PKT_STREAM_DATA_TO");
          break;

        case PKT_STREAM_DATA_FROM:
          printf("f: %d\n", frame_count);
          if (pkt_in.size == 0 || pkt_in.size > sizeof(pkt_out.data)) {
            printf("host: got bad DATA_FROM size: %u\n", pkt_in.size);
            break;
          }
          for (i = 0; i < pkt_in.size; ) {
            i += write_bkm_frame(outf, pkt_in.data + i);
            frame_count++;
          }
          break;

        default:
          printf("host: got unknown pkt type: %04x\n", pkt_in.type);
          break;
        }
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
      // can't do that yet
      continue;

    if ((tas_data[0] != NULL || outf != NULL) && !enable_sent) {
      memset(&pkt_out, 0, sizeof(pkt_out));
      pkt_out.type = PKT_STREAM_ENABLE;
      pkt_out.enable.stream_to = (tas_data[0] != NULL);
      pkt_out.enable.stream_from = (outf != NULL);
      pkt_out.enable.no_start_seq = no_start_seq;
      if (use_vsync)
        pkt_out.enable.inc_mode = INC_MODE_VSYNC;
      else if (tas_data_size[1] != 0 && separate_2p)
        pkt_out.enable.inc_mode = INC_MODE_SEPARATE;
      else if (tas_data_size[1] != 0)
        pkt_out.enable.inc_mode = INC_MODE_SHARED_PL2;
      else
        pkt_out.enable.inc_mode = INC_MODE_SHARED_PL1;

      ret = submit_urb(dev.fd, &urb[URB_DATA_OUT], dev.ifaces[0].ep_out,
                       &pkt_out, sizeof(pkt_out));
      if (ret != 0) {
        perror("USBDEVFS_SUBMITURB PKT_STREAM_ENABLE");
        continue;
      }
      pending_urbs |= 1 << URB_DATA_OUT;
      enable_sent = 1;
      bytes_sent[0] = 0;
      bytes_sent[1] = 0;
      continue;
    }
    if (tas_data[0] == NULL && fixed_input_changed) {
      memset(&pkt_out, 0, sizeof(pkt_out));
      pkt_out.type = PKT_FIXED_STATE;
      memcpy(pkt_out.data, fixed_input_state, sizeof(fixed_input_state));

      ret = submit_urb(dev.fd, &urb[URB_DATA_OUT], dev.ifaces[0].ep_out,
                       &pkt_out, sizeof(pkt_out));
      if (ret != 0) {
        perror("USBDEVFS_SUBMITURB PKT_FIXED_STATE");
        break;
      }
      fixed_input_changed = 0;
      pending_urbs |= 1 << URB_DATA_OUT;
      continue;
    }
    if (g_exit && !abort_sent) {
      memset(&pkt_out, 0, sizeof(pkt_out));
      pkt_out.type = PKT_STREAM_ABORT;

      ret = submit_urb(dev.fd, &urb[URB_DATA_OUT], dev.ifaces[0].ep_out,
                       &pkt_out, sizeof(pkt_out));
      if (ret != 0) {
        perror("USBDEVFS_SUBMITURB PKT_STREAM_ABORT");
        break;
      }
      pending_urbs |= 1 << URB_DATA_OUT;
      abort_sent = 1;
      continue;
    }

    continue;

dev_close:
    close(dev.fd);
    dev.fd = -1;
  }

  enable_echo(1);

  if (outf != NULL)
    fclose(outf);

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
