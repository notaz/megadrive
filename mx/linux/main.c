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
#define CMD_SEC_GET_NAME	'G'	/* filename r/w */
#define CMD_SEC_PUT_NAME	'P'
#define CMD_SEC_DEVID		'L'	/* read flash device ID */
#define CMD_SEC_ERASE		'E'
#define CMD_SEC_READY		'C'	/* is flash ready? */

/* bus controllers */
#define CTL_DATA_BUS	0x55
#define CTL_ADDR_BUS	0xAA

#define W_COUNTER	0xA0
#define W_CNT_WRITE	0x01
#define W_CNT_READ	0x00

#define FILENAME_ROM0	0
#define FILENAME_ROM1	1
#define FILENAME_RAM	2

typedef struct {
	u8 magic[4];
	u8 reserved[8];
	u8 write_flag;
	u8 reserved2[2];
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
			u8 cmd;
			u8 action;
			u8 b0;
			u8 b1;
			u8 b2;
			u8 b3;
		} write_cnt;
		struct {
			u8 which;
			u8 dev_id;
		} rom_id;
	};
	u8 pad[8];
} dev_cmd_t;

typedef struct {
	u8 firmware_ver[4];
	u8 bootloader_ver[4];
	char names[56];
} dev_info_t;

typedef struct {
	u32 start_addr;
	u32 end_addr;
	u32 page_size;
} page_table_t;

static const page_table_t p_AM29LV320DB[] =
{
	{ 0x000000, 0x00ffff, 0x002000 },
	{ 0x010000, 0x3fffff, 0x010000 },
	{ 0x000000, 0x000000, 0x000000 },
};

static const page_table_t p_AM29LV320DT[] =
{
	{ 0x000000, 0x3effff, 0x010000 },
	{ 0x3f0000, 0x3fffff, 0x002000 },
	{ 0x000000, 0x000000, 0x000000 },
};

/*****************************************************************************/

static void prepare_cmd(dev_cmd_t *dev_cmd, u8 cmd)
{
	memset(dev_cmd, 0, sizeof(*dev_cmd));

	memcpy(dev_cmd->magic, "USBC", 4);
	dev_cmd->magic2 = 0x67; /* MySCSICommand, EXCOMMAND */
	dev_cmd->mx_cmd = cmd;
}

static int write_data(struct usb_dev_handle *dev, void *data, int len)
{
	int ret = usb_bulk_write(dev, 0x03, data, len, 2000);
	if (ret < 0) {
		fprintf(stderr, "failed to write:\n");
		fprintf(stderr, "%s (%d)\n", usb_strerror(), ret);
	} else if (ret != len)
		printf("write_cmd: wrote only %d of %d bytes\n", ret, len);
	
	return ret;
}

static int write_cmd(struct usb_dev_handle *dev, dev_cmd_t *cmd)
{
	return write_data(dev, cmd, sizeof(*cmd));
}

static int read_data(struct usb_dev_handle *dev, void *buff, int size)
{
	int ret = usb_bulk_read(dev, 0x82, buff, size, 2000);
	if (ret < 0) {
		fprintf(stderr, "failed to read:\n");
		fprintf(stderr, "%s (%d)\n", usb_strerror(), ret);
	} else if (ret != size)
		printf("read_data: read only %d of %d bytes\n", ret, size);

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

	ret = read_data(device, &info, sizeof(info));
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

static int read_filename(struct usb_dev_handle *dev, char *dst, int len, u8 which)
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

	ret = read_data(dev, buff, 64);
	if (ret < 0)
		return ret;

	strncpy(dst, buff, len);
	dst[len - 1] = 0;

	return 0;
}

static int write_filename(struct usb_dev_handle *dev, const char *fname, u8 which)
{
	dev_cmd_t cmd;
	char buff[64];
	int ret, len;

	len = strlen(fname);
	if (len > 63)
		len = 63;
	strncpy(buff, fname, len);
	buff[len] = 0;

	prepare_cmd(&cmd, CMD_SEC_PUT_NAME);
	cmd.filename.which = which;

	ret = write_cmd(dev, &cmd);
	if (ret < 0)
		return ret;

	return write_data(dev, buff, len + 1);
}

static int read_w_counter(struct usb_dev_handle *dev, u32 *val)
{
	dev_cmd_t cmd;
	u8 buff[16];
	int ret;

	prepare_cmd(&cmd, CMD_ATM_READY);
	cmd.write_cnt.cmd = W_COUNTER;
	cmd.write_cnt.action = W_CNT_READ;

	ret = write_cmd(dev, &cmd);
	if (ret < 0)
		return ret;

	ret = read_data(dev, buff, sizeof(buff));
	if (ret < 0)
		return ret;
	
	*val = *(u32 *)buff;
	return 0;
}

static int read_flash_rom_id(struct usb_dev_handle *dev, u32 *val)
{
	dev_cmd_t cmd;
	u8 buff[2];
	int ret;

	prepare_cmd(&cmd, CMD_SEC_DEVID);
	cmd.write_flag = 1;			/* XXX why? */
	cmd.rom_id.dev_id = 1;
	cmd.rom_id.which = 0;

	ret = write_cmd(dev, &cmd);
	if (ret < 0)
		return ret;

	ret = read_data(dev, buff, sizeof(buff));
	if (ret < 0)
		return ret;

	*val = *(u16 *)buff << 16;

	cmd.rom_id.which = 0;
	ret = write_cmd(dev, &cmd);
	if (ret < 0)
		return ret;

	ret = read_data(dev, buff, sizeof(buff));
	if (ret < 0)
		return ret;
	
	*val |= *(u16 *)buff;
	return 0;
}

static const page_table_t *get_page_table(u32 rom_id)
{
	switch (rom_id) {
	case 0x0100F922:
		return p_AM29LV320DB;
	case 0x0100F422:
		return p_AM29LV320DT;
	default:
		fprintf(stderr, "unrecognized ROM id: %08x\n", rom_id);
	}

	return NULL;
}

static int get_page_size(const page_table_t *table, u32 addr, u32 *size)
{
	const page_table_t *t;
	
	for (t = table; t->end_addr != 0; t++) {
		if (addr >= t->start_addr && addr <= t->end_addr) {
			*size = t->page_size;
			return 0;
		}
	}

	if (addr == t[-1].end_addr + 1)
		return 1;	/* last */
	
	fprintf(stderr, "get_page_size: failed on addr %06x\n", addr);
	return -1;
}

static int erase_sector(struct usb_dev_handle *dev, u32 addr)
{
	dev_cmd_t cmd;
	u8 buff[10];
	int i, ret;

	prepare_cmd(&cmd, CMD_SEC_ERASE);
	cmd.write_flag = 1;
	cmd.rom_rw.addrb2 = addr >> 16;
	cmd.rom_rw.addrb1 = addr >> 8;
	cmd.rom_rw.addrb0 = addr;

	ret = write_cmd(dev, &cmd);
	if (ret < 0)
		return ret;

	ret = read_data(dev, buff, sizeof(buff));
	if (ret < 0)
		return ret;
	
	prepare_cmd(&cmd, CMD_SEC_READY);
	cmd.rom_rw.addrb2 = addr >> 16;
	cmd.rom_rw.addrb1 = addr >> 8;
	cmd.rom_rw.addrb0 = addr;

	for (i = 0; i < 100; i++) {
		ret = write_cmd(dev, &cmd);
		if (ret < 0)
			return ret;

		ret = read_data(dev, buff, sizeof(buff));
		if (ret < 0)
			return ret;

		if (ret > 4 && buff[4] == 1)
			break;

		usleep(50);
	}

	printf("i = %d\n", i);
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
	u32 counter, rom_id;
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

	ret = read_filename(device, fname, sizeof(fname), FILENAME_ROM0);
	if (ret < 0)
		goto end;
	printf("ROM filename:  %s\n", fname);

	ret = read_filename(device, fname, sizeof(fname), FILENAME_RAM);
	if (ret < 0)
		goto end;
	printf("SRAM filename: %s\n", fname);

	ret = read_w_counter(device, &counter);
	if (ret < 0)
		goto end;
	printf("flash writes:  %u\n", counter);

	ret = read_flash_rom_id(device, &rom_id);
	if (ret < 0)
		goto end;
	printf("flash rom id:  %08x\n", rom_id);

end:
	release_device(device);

	return ret;
}

