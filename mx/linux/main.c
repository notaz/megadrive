#include <stdio.h>
#include <string.h>
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

#define CMD_ATM_READY		0x22
#define CMD_SEC_GET_NAME	'G'	/* read filename */

/* bus controllers */
#define CTL_DATA_BUS	0x55
#define CTL_ADDR_BUS	0xAA

#define FILENAME_ROM0	0
#define FILENAME_ROM1	1
#define FILENAME_RAM	2

typedef struct {
	u8 magic[4];
	u8 reserved[11];
	u8 magic2;
	u8 mx_cmd;
	union {
		struct {
			u8 which_device;
		} dev_info;
		struct {
			u8 addrb2;	/* most significant */
			u8 addrb1;
			u8 addrb0;
			u8 num_pages;
		} rom_rw;
		struct {
			u8 which;
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
} dev_cmd_t;

typedef struct {
	u8 firmware_ver[4];
	u8 bootloader_ver[4];
	char names[56];
} dev_info_t;

static void prepare_cmd(dev_cmd_t *dev_cmd, u8 cmd)
{
	memset(dev_cmd, 0, sizeof(*dev_cmd));

	memcpy(dev_cmd->magic, "USBC", 4);
	dev_cmd->magic2 = 0x67; /* MySCSICommand, EXCOMMAND */
	dev_cmd->mx_cmd = cmd;
}

static int write_cmd(struct usb_dev_handle *dev, dev_cmd_t *cmd)
{
	int ret = usb_bulk_write(dev, 0x03, (char *)cmd, sizeof(*cmd), 2000);
	if (ret < 0) {
		fprintf(stderr, "failed to write:\n");
		fprintf(stderr, "%s (%d)\n", usb_strerror(), ret);
	} else if (ret != sizeof(*cmd))
		printf("write_cmd: wrote only %d of %d bytes\n", ret, sizeof(*cmd));
	
	return ret;
}

static int read_response(struct usb_dev_handle *dev, void *buff, int size)
{
	int ret = usb_bulk_read(dev, 0x82, buff, size, 2000);
	if (ret < 0) {
		fprintf(stderr, "failed to read:\n");
		fprintf(stderr, "%s (%d)\n", usb_strerror(), ret);
	} else if (ret != size)
		printf("read_response: read only %d of %d bytes\n", ret, size);

	return ret;
}

static int read_info(struct usb_dev_handle *device, u8 ctl_id)
{
	dev_cmd_t cmd;
	dev_info_t info;
	int ret;

	prepare_cmd(&cmd, CMD_ATM_READY);
	cmd.dev_info.which_device = ctl_id;
	memset(&info, 0, sizeof(info));

	ret = write_cmd(device, &cmd);
	if (ret < 0)
		return ret;

	ret = read_response(device, &info, sizeof(info));
	if (ret < 0)
		return ret;
	
	printf(" firmware version:   %X.%X.%X%c\n", info.firmware_ver[0],
		info.firmware_ver[1], info.firmware_ver[2], info.firmware_ver[3]);
	printf(" bootloader version: %X.%X.%X%c\n", info.bootloader_ver[0],
		info.bootloader_ver[1], info.bootloader_ver[2], info.bootloader_ver[3]);
	info.names[sizeof(info.names) - 1] = 0;
	printf(" device name:        %s\n", info.names);

	return 0;
}

static int get_filename(struct usb_dev_handle *dev, char *dst, int len, u8 which)
{
	char buff[65];
	dev_cmd_t cmd;
	int ret;

	prepare_cmd(&cmd, CMD_SEC_GET_NAME);
	cmd.filename.which = which;
	memset(buff, 0, sizeof(buff));

	ret = write_cmd(dev, &cmd);
	if (ret < 0)
		return ret;

	ret = read_response(dev, buff, 64);
	if (ret < 0)
		return ret;

	strncpy(dst, buff, len);
	dst[len - 1] = 0;

	return 0;
}

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
		fprintf(stderr, "%s (%d)\n", usb_strerror(), ret);
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
	char fname[65];
	int ret;

	usb_init();

	device = get_device();
	if (device == NULL)
		return 1;

	printf("data bus controller:\n");
	ret = read_info(device, CTL_DATA_BUS);
	if (ret < 0)
		goto end;

	printf("address bus controller:\n");
	ret = read_info(device, CTL_ADDR_BUS);
	if (ret < 0)
		goto end;

	ret = get_filename(device, fname, sizeof(fname), FILENAME_ROM0);
	if (ret < 0)
		goto end;
	printf("ROM filename:  %s\n", fname);

	ret = get_filename(device, fname, sizeof(fname), FILENAME_RAM);
	if (ret < 0)
		goto end;
	printf("SRAM filename: %s\n", fname);



end:
	release_device(device);

	return ret;
}

