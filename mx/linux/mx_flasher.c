/*
 * Copyright (c) 2009, Gražvydas Ignotas
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the organization nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <copyright holder> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
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

#define VERSION			"0.9"

#define IO_BLK_SIZE		0x2000	/* 8K */
#define IO_RAM_BLK_SIZE		256

#define CMD_ATM_READY		0x22
#define CMD_SEC_GET_NAME	'G'	/* filename r/w */
#define CMD_SEC_PUT_NAME	'P'
#define CMD_SEC_DEVID		'L'	/* read flash device ID */
#define CMD_SEC_ERASE		'E'
#define CMD_SEC_READY		'C'	/* is flash ready? */
#define CMD_SEC_READ		'R'
#define CMD_SEC_WRITE		'W'
#define CMD_SEC_RAM_READ	'D'	/* not implemented? */
#define CMD_SEC_RAM_WRITE	'U'
#define CMD_SEC_COMPAT		'$'	/* set RAM mode */

/* bus controllers */
#define CTL_DATA_BUS	0x55
#define CTL_ADDR_BUS	0xAA

#define W_COUNTER	0xA0
#define W_CNT_WRITE	0x01
#define W_CNT_READ	0x00

#define FILENAME_ROM0	0
#define FILENAME_ROM1	1
#define FILENAME_RAM	2

/* windows app sets to 0x80 on init
 * checkboxes use 0x41 0x42 0x43 (?)
 * r,w Ram/ROM uses 0x23/0x21
 */
#define C_MODE_4M_NORAM	0x41	/* RAM always off */
#define C_MODE_4M_RAM	0x42	/* RAM switched by game */
#define C_MODE_2M_RAM	0x43
#define C_RAM_TMP_OFF	0x21
#define C_RAM_TMP_ON	0x23

typedef struct {
	u8 magic[4];
	u8 reserved[8];
	u8 write_flag;
	u8 reserved2[2];
	u8 magic2;
	u8 mx_cmd;
	union {				/* 0x11 */
		struct {
			u8 which_device;
		} dev_info;
		struct {
			u8 addrb2;	/* most significant (BE) */
			u8 addrb1;
			u8 addrb0;
			u8 param;	/* 64 byte usb packets for i/o */
			u8 param2;
		} rom_rw;
		struct {
			u8 which;
		} filename, mode;
		struct {
			u8 cmd;
			u8 action;
			u8 b0;		/* LE */
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

static const page_table_t p_2x_16[] =
{
	{ 0x000000, 0x003fff, 0x004000 },
	{ 0x004000, 0x007fff, 0x002000 },
	{ 0x008000, 0x00ffff, 0x008000 },
	{ 0x010000, 0x1fffff, 0x010000 },
	{ 0x200000, 0x203fff, 0x004000 },
	{ 0x204000, 0x207fff, 0x002000 },
	{ 0x208000, 0x20ffff, 0x008000 },
	{ 0x210000, 0x3fffff, 0x010000 },
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
	}
/*
	else if (ret != size)
		printf("read_data: read only %d of %d bytes\n", ret, size);
*/
	return ret;
}

static int read_info(struct usb_dev_handle *device, u8 ctl_id, dev_info_t *info)
{
	dev_cmd_t cmd;
	int ret;

	prepare_cmd(&cmd, CMD_ATM_READY);
	cmd.dev_info.which_device = ctl_id;
	memset(info, 0, sizeof(*info));

	ret = write_cmd(device, &cmd);
	if (ret < 0)
		return ret;

	ret = read_data(device, info, sizeof(*info));
	if (ret < 0)
		return ret;
	
	return 0;
}

static void printf_info(dev_info_t *info)
{
	printf(" firmware version:   %X.%X.%X%c\n", info->firmware_ver[0],
		info->firmware_ver[1], info->firmware_ver[2], info->firmware_ver[3]);
	printf(" bootloader version: %X.%X.%X%c\n", info->bootloader_ver[0],
		info->bootloader_ver[1], info->bootloader_ver[2], info->bootloader_ver[3]);
	info->names[sizeof(info->names) - 1] = 0;
	printf(" device name:        %s\n", info->names);
}

static void print_progress(u32 done, u32 total)
{
	int i, step;

	printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
	printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b"); /* 20 */
	printf("\b\b\b\b\b\b");
	printf("%06x/%06x |", done, total);

	step = total / 20;
	for (i = step; i <= total; i += step)
		printf("%c", done >= i ? '=' : '-');
	printf("| %3d%%", done * 100 / total);
	fflush(stdout);
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

static int read_erase_counter(struct usb_dev_handle *dev, u32 *val)
{
	dev_info_t dummy_info;
	dev_cmd_t cmd;
	u8 buff[4];
	int ret;

	/* must perform dummy info read here,
	 * or else device hangs after close (firmware bug?) */
	ret = read_info(dev, CTL_DATA_BUS, &dummy_info);
	if (ret < 0)
		return ret;

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

static int read_flash_rom_id(struct usb_dev_handle *dev, int is_second, u32 *val)
{
	dev_cmd_t cmd;
	u8 buff[2];
	int ret;

	prepare_cmd(&cmd, CMD_SEC_DEVID);
	cmd.rom_id.which = is_second ? 0x10 : 0;
	cmd.rom_id.dev_id = 0;

	ret = write_cmd(dev, &cmd);
	if (ret < 0)
		return ret;

	ret = read_data(dev, buff, sizeof(buff));
	if (ret < 0)
		return ret;

	*val = *(u16 *)buff << 16;

	cmd.rom_id.dev_id = 1;
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
	case 0x01004922:
	case 0xC2004922:
		return p_2x_16;
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
		return 1;	/* no more */
	
	fprintf(stderr, "get_page_size: failed on addr %06x\n", addr);
	return -1;
}

static int set_ram_mode(struct usb_dev_handle *dev, u8 mode)
{
	dev_cmd_t cmd;
	u8 buff[2];
	int ret;

	prepare_cmd(&cmd, CMD_SEC_COMPAT);
	cmd.write_flag = 1;
	cmd.mode.which = mode;

	ret = write_cmd(dev, &cmd);
	if (ret < 0)
		goto end;

	ret = read_data(dev, buff, sizeof(buff));

end:
	if (ret < 0)
		fprintf(stderr, "warning: failed to set RAM mode\n");
	return ret;
}

/* limitations:
 * - bytes must be multiple of 64
 * - bytes must be less than 16k
 * - must perform even number of reads, or dev hangs on exit (firmware bug?) */
static int rw_dev_block(struct usb_dev_handle *dev, u32 addr, void *buffer, int bytes, int mx_cmd)
{
	dev_cmd_t cmd;
	int ret;

	prepare_cmd(&cmd, mx_cmd);
	if (mx_cmd == CMD_SEC_WRITE || mx_cmd == CMD_SEC_RAM_WRITE)
		cmd.write_flag = 1;
	cmd.rom_rw.addrb2 = addr >> (16 + 1);
	cmd.rom_rw.addrb1 = addr >> (8 + 1);
	cmd.rom_rw.addrb0 = addr >> 1;
	cmd.rom_rw.param = bytes / 64;
	if (mx_cmd == CMD_SEC_WRITE || mx_cmd == CMD_SEC_RAM_WRITE)
		cmd.rom_rw.param2 = 1; /* ? */

	ret = write_cmd(dev, &cmd);
	if (ret < 0)
		return ret;

	bytes &= ~63;

	if (mx_cmd == CMD_SEC_WRITE || mx_cmd == CMD_SEC_RAM_WRITE)
		ret = write_data(dev, buffer, bytes);
	else
		ret = read_data(dev, buffer, bytes);
	if (ret < 0)
		return ret;

	if (ret != bytes)
		fprintf(stderr, "rw_dev_block warning: done only %d/%d bytes\n", ret, bytes);

	return ret;
}

static int read_write_rom(struct usb_dev_handle *dev, u32 addr, void *buffer, int bytes, int is_write)
{
	int mx_cmd = is_write ? CMD_SEC_WRITE : CMD_SEC_READ;
	int total_bytes = bytes;
	u8 *buff = buffer;
	u8 dummy[64 * 4];
	int count, ret;

	if (addr & 1)
		fprintf(stderr, "read_write_rom: can't handle odd address %06x, "
				"LSb will be ignored\n", addr);
	if (bytes & 63)
		fprintf(stderr, "read_write_rom: byte count must be multiple of 64, "
				"last %d bytes will not be handled\n", bytes & 63);

	set_ram_mode(dev, C_RAM_TMP_OFF);

	printf("%s flash ROM...\n", is_write ? "writing to" : "reading");

	/* do i/o in blocks */
	for (count = 0; bytes >= IO_BLK_SIZE; count++) {
		print_progress(buff - (u8 *)buffer, total_bytes);

		ret = rw_dev_block(dev, addr, buff, IO_BLK_SIZE, mx_cmd);
		if (ret < 0)
			return ret;
		buff += IO_BLK_SIZE;
		addr += IO_BLK_SIZE;
		bytes -= IO_BLK_SIZE;
	}
	print_progress(buff - (u8 *)buffer, total_bytes);

	ret = 0;
	if (bytes != 0) {
		ret = rw_dev_block(dev, addr, buff, bytes, mx_cmd);
		count++;
		print_progress(total_bytes, total_bytes);
	}

	if (count & 1)
		/* work around rw_dev_block() limitation 3 (works for reads only?) */
		rw_dev_block(dev, 0, dummy, sizeof(dummy), 0);

	printf("\n");
	return ret;
}

static int read_write_ram(struct usb_dev_handle *dev, void *buffer, int bytes, int is_write)
{
	int mx_cmd = is_write ? CMD_SEC_RAM_WRITE : CMD_SEC_READ;
	int total_bytes = bytes;
	u8 *buff = buffer;
	u32 addr = 0x200000;
	int i, ret = 0;

	if (bytes % IO_RAM_BLK_SIZE)
		fprintf(stderr, "read_write_ram: byte count must be multiple of %d, "
				"last %d bytes will not be handled\n", IO_RAM_BLK_SIZE,
				bytes % IO_RAM_BLK_SIZE);

	set_ram_mode(dev, C_RAM_TMP_ON);

	printf("%s RAM...\n", is_write ? "writing to" : "reading");

	/* do i/o in blocks */
	while (bytes >= IO_RAM_BLK_SIZE) {
		print_progress(buff - (u8 *)buffer, total_bytes);

		ret = rw_dev_block(dev, addr, buff, IO_RAM_BLK_SIZE, mx_cmd);
		if (ret < 0)
			return ret;
		buff += IO_RAM_BLK_SIZE;
		addr += IO_RAM_BLK_SIZE;
		bytes -= IO_RAM_BLK_SIZE;
	}
	print_progress(buff - (u8 *)buffer, total_bytes);

	/* only D0-D7 connected.. */
	for (i = 0; i < total_bytes; i += 2)
		((u8 *)buffer)[i] = 0;

	printf("\n");
	return ret;

}

static int increment_erase_cnt(struct usb_dev_handle *dev)
{
	dev_cmd_t cmd;
	u8 buff[4];
	u32 cnt;
	int ret;

	ret = read_erase_counter(dev, &cnt);
	if (ret != 0)
		return ret;

	if (cnt == (u32)-1) {
		fprintf(stderr, "flash erase counter maxed out!\n");
		fprintf(stderr, "(wow, did you really erase so many times?)\n");
		return -1;
	}

	cnt++;

	prepare_cmd(&cmd, CMD_ATM_READY);
	cmd.write_cnt.cmd = W_COUNTER;
	cmd.write_cnt.action = W_CNT_WRITE;
	cmd.write_cnt.b3 = cnt >> 24;
	cmd.write_cnt.b2 = cnt >> 16;
	cmd.write_cnt.b1 = cnt >> 8;
	cmd.write_cnt.b0 = cnt;

	ret = write_cmd(dev, &cmd);
	if (ret < 0)
		return ret;

	ret = read_data(dev, buff, sizeof(buff));
	if (ret < 0)
		return ret;

	return cnt;
}

static int erase_page(struct usb_dev_handle *dev, u32 addr, int whole)
{
	dev_cmd_t cmd;
	u8 buff[5];
	int i, ret;

	prepare_cmd(&cmd, CMD_SEC_ERASE);
	cmd.write_flag = 1;
	cmd.rom_rw.addrb2 = addr >> (16 + 1);
	cmd.rom_rw.addrb1 = addr >> (8 + 1);
	cmd.rom_rw.addrb0 = addr >> 1;
	cmd.rom_rw.param = whole ? 0x10 : 0;

	ret = write_cmd(dev, &cmd);
	if (ret < 0)
		return ret;

	ret = read_data(dev, buff, sizeof(buff));
	if (ret < 0)
		return ret;
	
	prepare_cmd(&cmd, CMD_SEC_READY);
	cmd.rom_rw.addrb2 = addr >> (16 + 1);
	cmd.rom_rw.addrb1 = addr >> (8 + 1);
	cmd.rom_rw.addrb0 = addr >> 1;

	for (i = 0; i < 100; i++) {
		ret = write_cmd(dev, &cmd);
		if (ret < 0)
			return ret;

		ret = read_data(dev, buff, sizeof(buff));
		if (ret < 0)
			return ret;

		if (ret > 4 && buff[4] == 1)
			break;

		usleep((whole ? 600 : 20) * 1000);
	}

	if (i == 100) {
		fprintf(stderr, "\ntimeout waiting for erase to complete\n");
		return -1;
	}

	return 0;
}

static int erase_seq(struct usb_dev_handle *dev, u32 size)
{
	const page_table_t *table;
	u32 addr, page_size = 0;
	u32 rom0_id, rom1_id;
	int count, ret;

	ret = read_flash_rom_id(dev, 0, &rom0_id);
	if (ret < 0)
		return ret;

	ret = read_flash_rom_id(dev, 1, &rom1_id);
	if (ret < 0)
		return ret;

	if (rom0_id != rom1_id)
		fprintf(stderr, "Warning: flash ROM ids differ: %08x %08x\n",
			rom0_id, rom1_id);

 	table = get_page_table(rom0_id);
	if (table == NULL)
		return -1;

	ret = increment_erase_cnt(dev);
	if (ret < 0)
		fprintf(stderr, "warning: coun't increase erase counter\n");

	printf("erasing flash... (erase count=%u)\n", ret);

	for (addr = 0, count = 0; addr < size; addr += page_size, count++) {
		print_progress(addr, size);

		ret = erase_page(dev, addr, 0);
		if (ret < 0)
			return ret;

		ret = get_page_size(table, addr, &page_size);
		if (ret != 0)
			break;
	}

	if (count & 1)
		/* ??? */
		/* must submit even number of erase commands (fw bug?) */
		erase_page(dev, 0, 0);

	print_progress(addr, size);
	printf("\n");

	return ret;
}

static int erase_all(struct usb_dev_handle *dev, u32 size)
{
	int ret;

	ret = increment_erase_cnt(dev);
	if (ret < 0)
		fprintf(stderr, "warning: couldn't increase erase counter\n");

	printf("erasing flash0, count=%u ...", ret);
	fflush(stdout);

	ret = erase_page(dev, 0xaaa, 1);
	if (ret != 0)
		return ret;

	if (size > 0x200000) {
		printf(" done.\n");
		printf("erasing flash1...");
		fflush(stdout);

		ret = erase_page(dev, 0x200aaa, 1);
	}

	printf(" done.\n");
	return ret;
}

static int print_device_info(struct usb_dev_handle *dev)
{
	u32 counter, rom0_id, rom1_id;
	dev_info_t info;
	int ret;

	printf("data bus controller:\n");
	ret = read_info(dev, CTL_DATA_BUS, &info);
	if (ret < 0)
		return ret;
	printf_info(&info);

	printf("address bus controller:\n");
	ret = read_info(dev, CTL_ADDR_BUS, &info);
	if (ret < 0)
		return ret;
	printf_info(&info);

	ret = read_erase_counter(dev, &counter);
	if (ret < 0)
		return ret;
	printf("flash erase count:   %u\n", counter);

	ret = read_flash_rom_id(dev, 0, &rom0_id);
	if (ret < 0)
		return ret;
	printf("flash rom0 id:       %08x\n", rom0_id);

	ret = read_flash_rom_id(dev, 1, &rom1_id);
	if (ret < 0)
		return ret;
	printf("flash rom1 id:       %08x\n", rom1_id);

	return 0;
}

static int print_game_info(struct usb_dev_handle *dev)
{
	char fname[65];
	int ret;

	ret = read_filename(dev, fname, sizeof(fname), FILENAME_ROM0);
	if (ret < 0)
		return ret;
	printf("ROM filename:  %s\n", fname);

	ret = read_filename(dev, fname, sizeof(fname), FILENAME_RAM);
	if (ret < 0)
		return ret;
	printf("SRAM filename: %s\n", fname);

	return 0;
}

static int read_file(const char *fname, void **buff_out, int *size, int limit)
{
	int file_size, ret;
	void *data;
	FILE *file;

	file = fopen(fname, "rb");
	if (file == NULL) {
		fprintf(stderr, "can't open file: %s\n", fname);
		return -1;
	}

	fseek(file, 0, SEEK_END);
	file_size = ftell(file);
	fseek(file, 0, SEEK_SET);
	if (file_size > limit)
		fprintf(stderr, "warning: input file \"%s\" too large\n", fname);
	if (file_size < 0) {
		fprintf(stderr, "bad/empty file: %s\n", fname);
		goto fail;
	}

	data = malloc(file_size);
	if (data == NULL) {
		fprintf(stderr, "low memory\n");
		goto fail;
	}

	ret = fread(data, 1, file_size, file);
	if (ret != file_size) {
		fprintf(stderr, "failed to read file: %s", fname);
		perror("");
		goto fail;
	}

	*buff_out = data;
	*size = file_size;
	fclose(file);
	return 0;

fail:
	fclose(file);
	return -1;
}

static int write_file(const char *fname, void *buff, int size)
{
	FILE *file;
	int ret;

	file = fopen(fname, "wb");
	if (file == NULL) {
		fprintf(stderr, "can't open for writing: %s\n", fname);
		return -1;
	}

	ret = fwrite(buff, 1, size, file);
	if (ret != size) {
		fprintf(stderr, "write failed to %s", fname);
		perror("");
	} else
		printf("saved to \"%s\".\n", fname);
	fclose(file);
	
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

static void usage(const char *app_name)
{
	printf("Flasher tool for MX game devices\n"
		"written by Grazvydas \"notaz\" Ignotas\n");
	printf("v" VERSION " (" __DATE__ ")\n\n");
	printf("Usage:\n"
		"%s [-i] [-g] [-e] [-r [file]] [-w <file>] ...\n"
		"  -i         print some info about connected device\n"
		"  -g         print some info about game ROM inside device\n"
		"  -e[1]      erase whole flash ROM in device, '1' uses different erase method\n"
		"  -m[1-3]    set MX mode: 2M+RAM, 4M no RAM, 4M+RAM\n"
		"  -f         skip file check\n"
		"  -r [file]  copy game image from device to file; can autodetect filename\n"
		"  -w <file>  write file to device; also does erase\n"
		"  -sr [file] read save RAM to file\n"
		"  -sw <file> write save RAM file to device\n"
		"  -sc        clear save RAM\n"
		"  -v         with -w or -sw: verify written file\n",
		app_name);
}

int main(int argc, char *argv[])
{
	char *r_fname = NULL, *w_fname = NULL, *sr_fname = NULL, *sw_fname = NULL;
	void *r_fdata = NULL, *w_fdata = NULL, *sr_fdata = NULL, *sw_fdata = NULL;
	int do_read_ram = 0, do_clear_ram = 0, do_verify = 0, do_check = 1;
	int pr_dev_info = 0, pr_rom_info = 0, do_read = 0, mx_mode = 0;
	int erase_method = 0, do_erase_size = 0;
	int w_fsize = 0, sw_fsize = 0;
	struct usb_dev_handle *device;
	char fname_buff[65];
	int i, ret = 0;

	for (i = 1; i < argc; i++)
	{
		if (argv[i][0] != '-')
			break;

		switch (argv[i][1]) {
		case 'i':
			pr_dev_info = 1;
			break;
		case 'g':
			pr_rom_info = 1;
			break;
		case 'e':
			do_erase_size = 0x400000;
			if (argv[i][2] == '1')
				erase_method = 1;
			break;
		case 'f':
			do_check = 0;
			break;
		case 'v':
			do_verify = 1;
			break;
		case 'm':
			mx_mode = argv[i][2];
			break;
		case 'r':
			do_read = 1;
			if (argv[i+1] && argv[i+1][0] != '-')
				r_fname = argv[++i];
			break;
		case 'w':
			if (argv[i+1] && argv[i+1][0] != '-')
				w_fname = argv[++i];
			else
				goto breakloop;
			break;
		case 's':
			switch (argv[i][2]) {
			case 'r':
				do_read_ram = 1;
				if (argv[i+1] && argv[i+1][0] != '-')
					sr_fname = argv[++i];
				break;
			case 'w':
				if (argv[i+1] && argv[i+1][0] != '-')
					sw_fname = argv[++i];
				else
					goto breakloop;
				break;
			case 'c':
				do_clear_ram = 1;
				break;
			default:
				goto breakloop;
			}
			break;
		default:
			goto breakloop;
		}
	}

breakloop:
	if (i <= 1 || i < argc) {
		usage(argv[0]);
		return 1;
	}

	/* preparations */
	if (w_fname != NULL) {
		/* check extension */
		ret = strlen(w_fname);
		if (do_check && (w_fname[ret - 4] == '.' || w_fname[ret - 3] == '.' ||
				w_fname[ret - 2] == '.') &&
				strcasecmp(&w_fname[ret - 4], ".gen") != 0 &&
				strcasecmp(&w_fname[ret - 4], ".bin") != 0) {
			fprintf(stderr, "\"%s\" doesn't look like a game ROM, aborting "
					"(use -f to disable this check)\n", w_fname);
			return 1;
		}

		ret = read_file(w_fname, &w_fdata, &w_fsize, 0x400000);
		if (ret < 0)
			return 1;

		if (do_erase_size < w_fsize)
			do_erase_size = w_fsize;
	}
	if (sw_fname != NULL) {
		ret = read_file(sw_fname, &sw_fdata, &sw_fsize, 0x8000*2);
		if (ret < 0)
			return 1;
	}
	if (sw_fdata != NULL || do_clear_ram) {
		if (sw_fsize < 0x8000*2) {
			sw_fdata = realloc(sw_fdata, 0x8000*2);
			if (sw_fdata == NULL) {
				fprintf(stderr, "low mem\n");
				return 1;
			}
			memset((u8 *)sw_fdata + sw_fsize, 0, 0x8000*2 - sw_fsize);
		}
		sw_fsize = 0x8000*2;
	}
	if (w_fname == NULL && sw_fname == NULL && do_verify) {
		fprintf(stderr, "warning: -w or -sw not specified, -v ignored.\n");
		do_verify = 0;
	}

	/* init */
	usb_init();

	device = get_device();
	if (device == NULL)
		return 1;

	/* info */
	if (pr_dev_info) {
		ret = print_device_info(device);
		if (ret < 0)
			goto end;
	}

	if (pr_rom_info) {
		ret = print_game_info(device);
		if (ret < 0)
			goto end;
	}

	/* set mode */
	if (mx_mode || w_fsize > 0x200000) {
		if (mx_mode == 0)
			mx_mode = '3';
		printf("MX mode set to ");
		switch (mx_mode) {
		case '1':
			printf("2M with RAM.\n");
			mx_mode = C_MODE_2M_RAM;
			break;
		case '2':
			printf("4M, no RAM.\n");
			mx_mode = C_MODE_4M_NORAM;
			break;
		default:
			printf("4M with RAM.\n");
			mx_mode = C_MODE_4M_RAM;
			break;
		}
		set_ram_mode(device, mx_mode);
	}

	/* erase */
	if (do_erase_size != 0) {
		if (erase_method)
			ret = erase_all(device, do_erase_size);
		else
			ret = erase_seq(device, do_erase_size);
		if (ret < 0)
			goto end;
	}

	/* write flash */
	if (w_fdata != NULL) {
		char *p;

		ret = read_write_rom(device, 0, w_fdata, w_fsize, 1);
		if (ret < 0)
			goto end;

		p = strrchr(w_fname, '/');
		p = (p == NULL) ? w_fname : p + 1;

		ret = write_filename(device, p, FILENAME_ROM0);
		if (ret < 0)
			fprintf(stderr, "warning: failed to save ROM filename\n");
	}

	/* write ram */
	if (sw_fdata != NULL) {
		char *p, *t;

		ret = read_write_ram(device, sw_fdata, sw_fsize, 1);
		if (ret < 0)
			goto end;

		memset(fname_buff, 0, sizeof(fname_buff));
		p = fname_buff;
		if (sw_fname != NULL) {
			p = strrchr(sw_fname, '/');
			p = (p == NULL) ? sw_fname : p + 1;
		} else if (w_fname != NULL) {
			t = strrchr(w_fname, '/');
			t = (t == NULL) ? w_fname : t + 1;

			strncpy(fname_buff, t, sizeof(fname_buff));
			fname_buff[sizeof(fname_buff) - 1] = 0;
			ret = strlen(fname_buff);
			if (ret > 4 && fname_buff[ret - 4] == '.')
				strcpy(&fname_buff[ret - 4], ".srm");
		}

		ret = write_filename(device, p, FILENAME_RAM);
		if (ret < 0)
			fprintf(stderr, "warning: failed to save RAM filename\n");
	}

	/* read flash */
	if (do_read && r_fname == NULL) {
		ret = read_filename(device, fname_buff, sizeof(fname_buff), FILENAME_ROM0);
		if (ret < 0)
			return ret;
		r_fname = fname_buff;
		if (r_fname[0] == 0)
			r_fname = "rom.gen";
	}

	if (r_fname != NULL || do_verify) {
		r_fdata = malloc(0x400000);
		if (r_fdata == NULL) {
			fprintf(stderr, "low mem\n");
			goto end;
		}

		ret = read_write_rom(device, 0, r_fdata, 0x400000, 0);
		if (ret < 0)
			goto end;
	}

	if (r_fname != NULL)
		write_file(r_fname, r_fdata, 0x400000);

	/* read ram */
	if (do_read_ram && sr_fname == NULL) {
		ret = read_filename(device, fname_buff, sizeof(fname_buff), FILENAME_RAM);
		if (ret < 0)
			return ret;
		sr_fname = fname_buff;
		if (sr_fname[0] == 0)
			sr_fname = "rom.srm";
	}

	if (sr_fname != NULL || do_verify) {
		sr_fdata = malloc(0x8000*2);
		if (sr_fdata == NULL) {
			fprintf(stderr, "low mem\n");
			goto end;
		}

		ret = read_write_ram(device, sr_fdata, 0x8000*2, 0);
		if (ret < 0)
			goto end;
	}

	if (sr_fname != NULL)
		write_file(sr_fname, sr_fdata, 0x8000*2);

	/* verify */
	if (do_verify && w_fdata != NULL && r_fdata != NULL) {
		ret = memcmp(w_fdata, r_fdata, w_fsize);
		if (ret == 0)
			printf("flash verification passed.\n");
		else
			printf("flash verification FAILED!\n");
	}

	if (do_verify && sw_fdata != NULL && sr_fdata != NULL) {
		ret = memcmp(sw_fdata, sr_fdata, 0x8000*2);
		if (ret == 0)
			printf("RAM verification passed.\n");
		else
			printf("RAM verification FAILED!\n");
	}

	printf("all done.\n");
	ret = 0;

end:
	if (w_fdata != NULL)
		free(w_fdata);
	if (r_fdata != NULL)
		free(r_fdata);

	release_device(device);

	return ret;
}

