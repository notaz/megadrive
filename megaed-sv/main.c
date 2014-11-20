#include <stdlib.h>
#include <stdarg.h>

#define u8      unsigned char
#define u16     unsigned short
#define u32     unsigned int

#define noinline __attribute__((noinline))
#define _packed __attribute__((packed))

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

#include "edos.h"
#include "asmtools.h"

extern u16 start_hvc;

#define GFX_DATA_PORT    0xC00000
#define GFX_CTRL_PORT    0xC00004

#define TILE_MEM_END     0xB000

#define FONT_LEN         128
#define TILE_FONT_BASE   (TILE_MEM_END / 32  - FONT_LEN)

/* note: using ED menu's layout here.. */
#define WPLANE           (TILE_MEM_END + 0x0000)
#define HSCRL            (TILE_MEM_END + 0x0800)
#define SLIST            (TILE_MEM_END + 0x0C00)
#define APLANE           (TILE_MEM_END + 0x1000)
#define BPLANE           (TILE_MEM_END + 0x3000)

#define read8(a) \
    *((volatile u8 *) (a))
#define read16(a) \
    *((volatile u16 *) (a))
#define read32(a) \
    *((volatile u32 *) (a))
#define write8(a, d) \
    *((volatile u8 *) (a)) = (d)
#define write16(a, d) \
    *((volatile u16 *) (a)) = (d)
#define write32(a, d) \
    *((volatile u32 *) (a)) = (d)

#define GFX_WRITE_VRAM_ADDR(adr) \
    (((0x4000 | ((adr) & 0x3FFF)) << 16) | ((adr) >> 14) | 0x00)
#define GFX_WRITE_VSRAM_ADDR(adr) \
    (((0x4000 | ((adr) & 0x3FFF)) << 16) | ((adr) >> 14) | 0x10)

enum {
    VDP_MODE1 = 0x00,
    VDP_MODE2 = 0x01,
    VDP_BACKDROP = 0x07,
    VDP_MODE3 = 0x0b,
    VDP_MODE4 = 0x0c,
    VDP_AUTOINC = 0x0f,
    VDP_SCROLLSZ = 0x10,
};

/* cell counts */
#define LEFT_BORDER 1   /* lame TV */
#define PLANE_W 64
#define PLANE_H 32
#define CSCREEN_H 28

static noinline void VDP_drawTextML(const char *str, u16 plane_base,
    u16 x, u16 y)
{
    const u8 *src = (const u8 *)str;
    u16 basetile = 0;
    int max_len = 40 - LEFT_BORDER;
    int len;
    u32 addr;

    x += LEFT_BORDER;

    for (len = 0; str[len] && len < max_len; len++)
        ;
    if (len > (PLANE_W - x))
        len = PLANE_W - x;

    addr = plane_base + ((x + (PLANE_W * y)) << 1);
    write32(GFX_CTRL_PORT, GFX_WRITE_VRAM_ADDR(addr));

    while (len-- > 0) {
        write16(GFX_DATA_PORT,
            basetile | ((*src++) - 32 + TILE_FONT_BASE));
    }
}

static int printf_ypos;

static void printf_line(int x, const char *buf)
{
    u32 addr;
    int i;

    VDP_drawTextML(buf, APLANE, x, printf_ypos++ & (PLANE_H - 1));

    if (printf_ypos >= CSCREEN_H) {
        /* clear next line */
        addr = APLANE;
        addr += (PLANE_W * (printf_ypos & (PLANE_H - 1))) << 1;
        write32(GFX_CTRL_PORT, GFX_WRITE_VRAM_ADDR(addr));
        for (i = 0; i < 40 / 2; i++)
            write32(GFX_DATA_PORT, 0);

        /* scroll plane */
        write32(GFX_CTRL_PORT, GFX_WRITE_VSRAM_ADDR(0));
        write16(GFX_DATA_PORT, (printf_ypos - CSCREEN_H + 1) * 8);
    }
}

#define PRINTF_LEN 40

static noinline int printf(const char *fmt, ...)
{
    static const char hexchars[] = "0123456789abcdef";
    static int printf_xpos;
    char c, buf[PRINTF_LEN + 11 + 1];
    const char *s;
    va_list ap;
    int ival;
    u32 uval;
    int d = 0;
    int i, j;

    va_start(ap, fmt);
    for (d = 0; *fmt; ) {
        int prefix0 = 0;
        int fwidth = 0;

        c = *fmt++;
        if (d < PRINTF_LEN)
            buf[d] = c;

        if (c != '%') {
            if (c == '\n') {
                buf[d] = 0;
                printf_line(printf_xpos, buf);
                d = 0;
                printf_xpos = 0;
                continue;
            }
            d++;
            continue;
        }
        if (d >= PRINTF_LEN)
            continue;

        if (*fmt == '0') {
            prefix0 = 1;
            fmt++;
        }

        while ('1' <= *fmt && *fmt <= '9') {
            fwidth = fwidth * 10 + *fmt - '0';
            fmt++;
        }

        switch (*fmt++) {
        case '%':
            d++;
            break;
        case 'd':
        case 'i':
            ival = va_arg(ap, int);
            if (ival < 0) {
                buf[d++] = '-';
                ival = -ival;
            }
            for (i = 1000000000; i >= 10; i /= 10)
                if (ival >= i)
                    break;
            for (; i >= 10; i /= 10) {
                buf[d++] = '0' + ival / i;
                ival %= i;
            }
            buf[d++] = '0' + ival;
            break;
        case 'x':
            uval = va_arg(ap, int);
            while (fwidth > 1 && uval < (1 << (fwidth - 1) * 4)) {
                buf[d++] = prefix0 ? '0' : ' ';
                fwidth--;
            }
            for (j = 1; j < 8 && uval >= (1 << j * 4); j++)
                ;
            for (j--; j >= 0; j--)
                buf[d++] = hexchars[(uval >> j * 4) & 0x0f];
            break;
        case 's':
            s = va_arg(ap, char *);
            while (*s && d < PRINTF_LEN)
                buf[d++] = *s++;
            break;
        default:
            // don't handle, for now
            d++;
            va_arg(ap, void *);
            break;
        }
    }
    buf[d] = 0;
    va_end(ap);

    if (d != 0) {
        // line without \n
        VDP_drawTextML(buf, APLANE, printf_xpos,
            printf_ypos & (PLANE_H - 1));
        printf_xpos += d;
    }

    return d; // wrong..
}

static u8 gethex(char c)
{
    if ('0' <= c && c <= '9')
        return c - '0';
    if ('a' <= c && c <= 'f')
        return c - 'a' + 10;
    if ('A' <= c && c <= 'F')
        return c - 'A' + 10;
    return 0;
}

static const char *exc_names[] = {
    NULL,
    NULL,
    "Bus Error",
    "Address Error",
    "Illegal Instruction",
    "Zero Divide",
    "CHK Instruction",
    "TRAPV Instruction",
    "Privilege Violation",  /*  8  8 */
    "Trace",
    "Line 1010 Emulator",
    "Line 1111 Emulator",
    NULL,
    NULL,
    NULL,
    "Uninitialized Interrupt",
    NULL,                   /* 10 16 */
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    "Spurious Interrupt",   /* 18 24 */
    "l1 irq",
    "l2 irq",
    "l3 irq",
    "l4 irq",
    "l5 irq",
    "l6 irq",
    "l7 irq",
};

struct exc_frame {
    u32 dr[8];
    u32 ar[8];
    u16 ecxnum; // from handler
    union {
        struct {
            u16 sr;
            u32 pc;
        } g _packed;
        struct {
            u16 fc;
            u32 addr;
            u16 ir;
            u16 sr;
            u32 pc;
        } bae _packed; // bus/address error frame
    };
} _packed;

int xtttt(void) { return sizeof(struct exc_frame); }

void exception(const struct exc_frame *f)
{
    int i;

    while (read16(GFX_CTRL_PORT) & 2)
        ;
    write16(GFX_CTRL_PORT, 0x8000 | (VDP_MODE1 << 8) | 0x04);
    write16(GFX_CTRL_PORT, 0x8000 | (VDP_MODE2 << 8) | 0x44);
    /* adjust scroll */
    write32(GFX_CTRL_PORT, GFX_WRITE_VSRAM_ADDR(0));
    write16(GFX_DATA_PORT,
      printf_ypos >= CSCREEN_H ?
        (printf_ypos - CSCREEN_H + 1) * 8 : 0);

    printf("exception %i ", f->ecxnum);
    if (f->ecxnum < ARRAY_SIZE(exc_names) && exc_names[f->ecxnum] != NULL)
        printf("(%s)", exc_names[f->ecxnum]);
    if (f->ecxnum < 4)
        printf(" (%s)", (f->bae.fc & 0x10) ? "r" : "w");
    printf("    \n");

    if (f->ecxnum < 4) {
        printf("  PC: %08x SR: %04x    \n", f->bae.pc, f->bae.sr);
        printf("addr: %08x IR: %04x FC: %02x   \n",
               f->bae.addr, f->bae.ir, f->bae.fc);
    }
    else {
        printf("  PC: %08x SR: %04x    \n", f->g.pc, f->g.sr);
    }
    for (i = 0; i < 8; i++)
        printf("  D%d: %08x A%d: %08x    \n", i, f->dr[i], i, f->ar[i]);
    printf("                       \n");
}

void vbl(void)
{
}

static int usb_read_while_ready(OsRoutine *ed,
    void *buf_, unsigned int maxlen)
{
    u8 *buf = buf_;
    unsigned int r = 0;

    while (ed->usbRdReady() && r < maxlen)
        buf[r++] = ed->usbReadByte();

    return r;
}

static int usb_read(OsRoutine *ed, void *buf_, unsigned int maxlen)
{
    u8 *buf = buf_;
    unsigned int r = 0;

    while (r < maxlen)
        buf[r++] = ed->usbReadByte();

    return r;
}

static int usb_write(OsRoutine *ed, const void *buf_, unsigned int maxlen)
{
    const u8 *buf = buf_;
    unsigned int r = 0;

    while (r < maxlen)
        ed->usbWriteByte(buf[r++]);

    return r;
}

/*
 * TH = 1 : ?1CBRLDU    3-button pad return value (not read)
 * TH = 0 : ?0SA00DU    3-button pad return value
 * TH = 1 : ?1CBRLDU    3-button pad return value
 * TH = 0 : ?0SA0000    D3-0 are forced to '0'
 * TH = 1 : ?1CBMXYZ    Extra buttons returned in D3-0
 * TH = 0 : ?0SA1111    D3-0 are forced to '1'
 */
static void test_joy_latency(int *min_out, int *max_out)
{
    u8 rbuf[8 * 6];
    int min = 8;
    int max = 0;
    int i, v, b, e;

    for (i = 0; i < 64; i++) {
        read_joy_responses(rbuf);

        for (b = 0; b < 8 * 4; b++) {
            v = b & 7;
            e = (b & 0x08) ? 0x0c : 0;
            if ((rbuf[b] & 0x0c) == e) {
                if (v < min)
                    min = v;
            }
            else if (v > max)
                max = v;
        }
    }

    /* print out the last test */
    for (b = 0; b < 8 * 5; b++) {
        printf(" %02x", rbuf[b]);
        if ((b & 7) == 7)
            printf("\n");
    }
    printf("\n");

    *min_out = min;
    *max_out = max;
}

static int do_test(OsRoutine *ed, u8 b3)
{
    int min = 0, max = 0;
    int i, t, len, seed;
    u8 *p, v;

    switch (b3)
    {
    case '0':
        printf("reading..\n");
        test_joy_read_log((void *)0x200000, 0x20000, 1);
        //test_joy_read_log((void *)0xff0200, 0x0f000, 1);
        printf("done\n");
        return 0;
    case '1':
        printf("reading w/vsync..\n");
        test_joy_read_log_vsync((void *)0x200000, 3600 * 2);
        printf("done\n");
        return 0;
    case '2':
    case '3':
        printf("3btn_idle test..\n");
        p = (void *)0x200000;
        len = 0x20000;
        test_joy_read_log(p, len, b3 == '3');
        for (i = 0; i < len; i++) {
            static const u8 e[2] = { 0x33, 0x7f };
            v = e[i & 1];
            if (p[i] != v)
                printf("%06x: bad: %02x %02x\n", &p[i], p[i], v);
        }
        printf("done\n");
        return 0;
    case '4':
        printf("3btn_idle data test..\n");
        p = (void *)0x200000;
        len = 0x20000;
        for (i = 0; i < len; i++) {
            static const u8 e[2] = { 0x33, 0x7f };
            v = e[i & 1];
            if (p[i] != v)
                printf("%06x: bad: %02x %02x\n", &p[i], p[i], v);
        }
        printf("done\n");
        return 0;
    case '5':
        seed = read8(0xC00009);
        printf("testing, seed=%02x\n", seed);
        p = (void *)0x200000;
        len = 0x100000;
        test_byte_write(p, len, seed);
        for (t = 0; t < 2; t++) {
            for (i = 0; i < len; i++) {
                v = (u8)(i + seed);
                if (p[i] != v)
                    printf("%06x: bad: %02x %02x\n", &p[i], p[i], v);
            }
            printf("done (%d)\n", t);
        }
        return 0;
    case 'j':
        test_joy_latency(&min, &max);
        printf("latency: %d - %d\n\n", min, max);
        return 0;
    default:
        break;
    }

    return -1;
}

static int do_custom(OsRoutine *ed, u8 b3)
{
    struct {
        unsigned int addr;
        unsigned int size;
    } d;

    switch (b3)
    {
    case 'd':
        usb_read(ed, &d, sizeof(d));
        ed->usbWriteByte('k');
        printf("sending %i bytes from %06x..\n", d.size, d.addr);
        usb_write(ed, (void *)d.addr, d.size);
        printf("done.\n");
        return 1;
    default:
        break;
    }

    return -1;
}

#define MTYPE_OS 0
#define MTYPE_MD 1
#define MTYPE_SSF 2
#define MTYPE_CD 3
#define MTYPE_SMS 4
#define MTYPE_10M 5
#define MTYPE_32X 6

static int do_run(OsRoutine *ed, u8 b3, int tas_sync)
{
    u8 mapper = 0;

    switch (b3)
    {
    case 's':
        mapper = MTYPE_SMS | (7 << 4);
        break;
    case 'm':
        mapper = MTYPE_MD;
        break;
    case 'o':
        mapper = MTYPE_OS;
        break;
    case 'c':
        mapper = MTYPE_CD;
        break;
    case '3':
        mapper = MTYPE_32X;
        break;
    case 'M':
        mapper = MTYPE_10M;
        break;
    case 'n':
        // raw numer: hex XX: mtype | x;
        // x: bits [4-7]: SRAM_ON, SRAM_3M_ON, SNAP_SAVE_ON, MKEY
        mapper  = gethex(ed->usbReadByte()) << 4;
        mapper |= gethex(ed->usbReadByte());
        break;
    default:
        return -1;
    }

    printf("syncing and starting mapper %x..\n", mapper);

    while (read16(GFX_CTRL_PORT) & 2)
        ;
    ed->VDP_setReg(VDP_MODE1, 0x04); 
    ed->VDP_setReg(VDP_MODE2, 0x44); 

    ed->usbWriteByte('k');

    run_game(mapper, tas_sync);
    /* should not get here.. */

    return -1;
}

void setup_z80(void)
{
    u8 *mem = (u8 *)0xa00000;
    int i;

    write8(0xa11100, 1);
    write8(0xa11200, 1);

    while (read8(0xa11100) & 1)
        ;

    /* must use byte access */
    for (i = 0x2000; i > 0; i--)
        *mem++ = 0;

    /* console starts with reset on, busreq off,
     * gens starts with busreq on, keep that for gmv.. */
}

int main()
{
    OsRoutine *ed;
    u8 buf[16];
    int len;
    int i, d, ret;

    ed = (OsRoutine *) *(u32 *)0x1A0;
    ed->memInitDmaCode(); 

    /* setup VDP */
    while (read16(GFX_CTRL_PORT) & 2)
        ;

    ed->VDP_setReg(VDP_MODE1, 0x04); 
    ed->VDP_setReg(VDP_MODE2, 0x64); 
    ed->VDP_setReg(VDP_AUTOINC, 2); 
    ed->VDP_setReg(VDP_SCROLLSZ, 0x01); 

    /* clear name tables */
    write32(GFX_CTRL_PORT, GFX_WRITE_VRAM_ADDR(APLANE));
    for (i = 0; i < PLANE_W * PLANE_H / 2; i++)
        write32(GFX_DATA_PORT, 0);

    write32(GFX_CTRL_PORT, GFX_WRITE_VRAM_ADDR(BPLANE));
    for (i = 0; i < PLANE_W * PLANE_H / 2; i++)
        write32(GFX_DATA_PORT, 0);

    /* scroll planes */
    write32(GFX_CTRL_PORT, GFX_WRITE_VSRAM_ADDR(0));
    write32(GFX_DATA_PORT, 0);

    /* note: relying on ED menu's font setup here.. */

    printf("\n");
    printf("version: %02x, hvc: %04x %04x\n",
           read8(0xa10001), start_hvc, read16(0xc00008));
    printf("ED os/fw: %d/%d\n\n", ed->osGetOsVersion(),
           ed->osGetFirmVersion());

    setup_z80();

    for (;;) {
        if (!ed->usbRdReady()) {
            /* note: stop corrupts SDRAM */
            //asm volatile("stop #0x2000");
            asm volatile(
                "move.l #1000/10, %0\n"
                "0: dbra %0, 0b\n" : "=r" (i) :: "cc");
            continue;
        }

        buf[0] = ed->usbReadByte();
        if (buf[0] == ' ')
            continue;
        if (buf[0] != '*') {
            d = 1;
            goto bad_input;
        }

        /* note: OS uses Twofgsr */
        buf[1] = ed->usbReadByte();
        switch (buf[1]) {
        case 'T':
            ed->usbWriteByte('k');
            break;
        case 'g':
            len = ed->usbReadByte() * 128;
            printf("loading %d bytes.. ", len * 512);
            ed->usbWriteByte('k');
            ed->usbReadDma((void *)0x200000, len);
            ed->usbWriteByte('d');
            printf("done\n");
            break;
        case 'r':
        case 'R':
            buf[2] = ed->usbReadByte();
            ret = do_run(ed, buf[2], buf[1] == 'R');
            if (ret != 0) {
                d = 3;
                goto bad_input;
            }
            printf("run returned??\n");
            break;

        /* custom */
        case 't':
            buf[2] = ed->usbReadByte();
            ret = do_test(ed, buf[2]);
            if (ret != 0) {
                d = 3;
                goto bad_input;
            }
            ed->usbWriteByte('k');
            break;
        case 'x':
            buf[2] = ed->usbReadByte();
            ret = do_custom(ed, buf[2]);
            if (ret == 1)
                break;
            if (ret != 0) {
                d = 3;
                goto bad_input;
            }
            ed->usbWriteByte('k');
            break;
        default:
            d = 2;
            goto bad_input;
        }

        continue;

bad_input:
        ret = usb_read_while_ready(ed, buf + d, sizeof(buf) - d);
        buf[d + ret] = 0;
        printf("bad cmd: %s\n", buf);
        /* consume all remaining data */
        while (ed->usbRdReady())
            usb_read_while_ready(ed, buf, sizeof(buf));

        ed->usbWriteByte('b');
    }

    return 0;
}

// vim:ts=4:sw=4:expandtab
