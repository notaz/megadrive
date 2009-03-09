#include <stdio.h>
#include <usb.h>


typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
#define array_size(x) (sizeof(x) / sizeof(x[0]))

static const struct {
	unsigned short vendor;
	unsigned short product;
	const char *name;
} g_devices[] = {
	{ 0x03eb, 0x202a, "16MX+U Game Device" },
	{ 0x03eb, 0x202b, "32MX+U Game Device" },
	{ 0x03eb, 0x202c, "16MX+US Game Device" },
	{ 0x03eb, 0x202d, "32MX+UF Game Device" },
};

/*****************************************************************************/

struct {
	u8 magic[4];
	u8 reserved[11];
	u8 magic2;
	u8 mx_cmd;
	union {
		struct {
			u8 which_device;
			u8 boot_cmd;
		} dev_info;
		struct {
			u8 addrb2;	/* most significant */
			u8 addrb1;
			u8 addrb0;
			u8 num_pages;
		} rom_rw;
		struct {
			u8 position;
		} filename;
		struct {
			u8 times_cmd;
			u8 action;
			u8 cntb3;
			u8 cntb2;
			u8 cntb1;
			u8 cntb0;
		} times_write;
	};
	u8 pad[8];
} dev_cmd;


static usb_dev_handle *get_device(void)
{
	struct usb_dev_handle *handle;
	struct usb_device *dev;
	struct usb_bus *bus;
	int i, ret;

	ret = usb_find_busses();
	if (ret <= 0) {
		fprintf(stderr, "Can't find USB busses\n");
		return NULL;
	}

	ret = usb_find_devices();
	if (ret <= 0) {
		fprintf(stderr, "Can't find USB devices\n");
		return NULL;
	}

	bus = usb_get_busses();
	for (; bus; bus = bus->next)
	{
		for (dev = bus->devices; dev; dev = dev->next)
		{
			for (i = 0; i < array_size(g_devices); i++)
			{
				if (dev->descriptor.idVendor == g_devices[i].vendor &&
						dev->descriptor.idProduct == g_devices[i].product)
					goto found;
			}
		}
	}

	fprintf(stderr, "device not found.\n");
	return NULL;

found:
	printf("found %s.\n", g_devices[i].name);

	handle = usb_open(dev);
	if (handle == NULL) {
		fprintf(stderr, "failed to open device:\n");
		fprintf(stderr, "%s\n", usb_strerror());
		return NULL;
	}

	ret = usb_set_configuration(handle, 1);
	if (ret != 0) {
		fprintf(stderr, "couldn't set configuration for /*/bus/usb/%s/%s:\n",
			bus->dirname, dev->filename);
		fprintf(stderr, "%s\n", usb_strerror());
		return NULL;
	}

	ret = usb_claim_interface(handle, 0);
	if (ret != 0) {
		fprintf(stderr, "couldn't claim /*/bus/usb/%s/%s:\n",
			bus->dirname, dev->filename);
		fprintf(stderr, "%s (%d)\n", usb_strerror(), ret);
		return NULL;
	}

	return handle;
}

static void release_device(struct usb_dev_handle *device)
{
	usb_release_interface(device, 0);
	usb_close(device);
}

int main(int argc, char *argv[])
{
	struct usb_dev_handle *device;

	usb_init();

	device = get_device();
	if (device == NULL)
		return 1;



	release_device(device);

	return 0;
}

