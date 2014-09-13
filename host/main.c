#include <stdio.h>
//#include <stdlib.h>
#include <string.h>
#include <stdint.h>
//#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <linux/usbdevice_fs.h>
#include <linux/usb/ch9.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

#if 0
#include "rawhid/hid.h"

static int g_quit;

static void *dbg_reader(void *arg)
{
	char buf[65];
	int ret;

	buf[64] = 0;

	while (!g_quit) {
		ret = rawhid_recv(0, buf, 64, 1000);
		if (ret == 0)
			continue;

		if (ret != 64) {
			printf("\nrawhid_recv(dbg): %d\n", ret);
			return NULL;
		}
		printf("%s", buf);
	}

	// rawhid_close(0);
	return NULL;
}
#endif

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

enum my_urbs {
  URB_DATA_IN,
  URB_DATA_OUT,
  URB_DBG_IN,
  URB_CNT
};

int main(int argc, char *argv[])
{
  struct teensy_dev dev;
  struct usbdevfs_urb urb[URB_CNT];
  struct usbdevfs_urb *reaped_urb;
  char buf_dbg[64 + 1], buf_in[64];
  int wait_device = 0;
  int dbg_in_sent = 0;
  int data_in_sent = 0;
  fd_set wfds;
  int ret;

  dev.fd = -1;

  while (1)
  {
    if (dev.fd == -1) {
      ret = find_device(&dev, 0x16C0, 0x0486);
      if (ret < 0)
        return ret;

      if (ret == 0) {
        if (!wait_device) {
          printf("waiting for device..\n");
          wait_device = 1;
        }
        usleep(250000);
        continue;
      }

      wait_device = 0;
      data_in_sent = 0;
      dbg_in_sent = 0;
    }

    if (!data_in_sent) {
      memset(&urb[URB_DATA_IN], 0, sizeof(urb[URB_DATA_IN]));
      urb[URB_DATA_IN].type = USBDEVFS_URB_TYPE_INTERRUPT;
      urb[URB_DATA_IN].endpoint = dev.ifaces[0].ep_in;
      urb[URB_DATA_IN].buffer = buf_in;
      urb[URB_DATA_IN].buffer_length = sizeof(buf_in);

      ret = ioctl(dev.fd, USBDEVFS_SUBMITURB, &urb[URB_DATA_IN]);
      if (ret != 0) {
        perror("USBDEVFS_SUBMITURB URB_DATA_IN");
        return 1;
      }
      data_in_sent = 1;
    }
    if (!dbg_in_sent) {
      memset(&urb[URB_DBG_IN], 0, sizeof(urb[URB_DBG_IN]));
      urb[URB_DBG_IN].type = USBDEVFS_URB_TYPE_INTERRUPT;
      urb[URB_DBG_IN].endpoint = dev.ifaces[1].ep_in;
      urb[URB_DBG_IN].buffer = buf_dbg;
      urb[URB_DBG_IN].buffer_length = sizeof(buf_dbg) - 1;

      ret = ioctl(dev.fd, USBDEVFS_SUBMITURB, &urb[URB_DBG_IN]);
      if (ret != 0) {
        perror("USBDEVFS_SUBMITURB URB_DBG_IN");
        return 1;
      }
      dbg_in_sent = 1;
    }

    FD_ZERO(&wfds);
    FD_SET(dev.fd, &wfds);

    ret = select(dev.fd + 1, NULL, &wfds, NULL, NULL);
    if (ret < 0) {
      perror("select");
      return 1;
    }

    if (FD_ISSET(dev.fd, &wfds)) {
      reaped_urb = NULL;
      ret = ioctl(dev.fd, USBDEVFS_REAPURB, &reaped_urb);
      if (ret != 0) {
        if (errno == ENODEV)
          goto dev_close;
        perror("USBDEVFS_REAPURB");
        return 1;
      }

      if (reaped_urb != NULL && reaped_urb->status != 0) {
        errno = -reaped_urb->status;
        perror("urb status");
        if (reaped_urb->status == -EILSEQ) {
          /* this is usually a sign of disconnect.. */
          usleep(250000);
          goto dev_close;
        }
      }

      if (reaped_urb == &urb[URB_DATA_IN]) {
        printf("*data*\n");
        data_in_sent = 0;
      }
      else if (reaped_urb == &urb[URB_DBG_IN]) {
        /* debug text */
        buf_dbg[reaped_urb->actual_length] = 0;
        printf("%s", buf_dbg);
        dbg_in_sent = 0;
      }
      else {
        fprintf(stderr, "reaped unknown urb? %p\n", reaped_urb);
      }
    }
    continue;

dev_close:
    close(dev.fd);
    dev.fd = -1;
  }

#if 0
  ret = rawhid_open(1, 0x16C0, 0x0486, 0xFFC9, 0x0004);
  if (ret <= 0) {
    fprintf(stderr, "dbg rawhid is missing\n");
    return 1;
  }

  ret = rawhid_open(1, 0x16C0, 0x0486, 0xFFAB, 0x0200);
  if (ret <= 0) {
    fprintf(stderr, "data rawhid is missing\n");
    return 1;
  }

  pthread_t thread;
  ret = pthread_create(&thread, NULL, dbg_reader, NULL);
  if (ret) {
    fprintf(stderr, "pthread_create failed: %d\n", ret);
    return 1;
  }

  memset(buf, 0, sizeof(buf));
  snprintf(buf, sizeof(buf), "hi");

  ret = rawhid_send(1, buf, 64, 220);
  if (ret != 64) {
    fprintf(stderr, "send failed: %d\n", ret);
  }

  while (1) {
    // check if any Raw HID packet has arrived
    ret = rawhid_recv(1, buf, 64, 220);
    if (ret < 0) {
      fprintf(stderr, "\nrawhid_recv(1): %d\n", ret);
      break;
    }
    if (ret > 0) {
      printf("\nrecv %d bytes:\n", ret);
      for (i=0; i<ret; i++) {
        printf("%02X ", buf[i] & 255);
        if (i % 16 == 15 && i < ret-1) printf("\n");
      }
      printf("\n");
    }
  }

  // rawhid_close(1);

  g_quit = 1;
  pthread_join(thread, NULL);
#endif

  return 0;
}

// vim: ts=2:sw=2:expandtab
