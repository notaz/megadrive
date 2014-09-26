#include <stdarg.h>

#define u8      unsigned char
#define u16     unsigned short
#define u32     unsigned int

#define noinline __attribute__((noinline))

#include "edos.h"
#include "asmtools.h"

#define GFX_DATA_PORT    0xC00000
#define GFX_CTRL_PORT    0xC00004

#define TILE_MEM_END     0xB000

#define FONT_LEN         128
#define TILE_FONT_BASE   (TILE_MEM_END / 32  - FONT_LEN)

/* note: using ED menu's layout here.. */
#define WPLAN            (TILE_MEM_END + 0x0000)
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
        write16(GFX_DATA_PORT, (printf_ypos - 27) * 8);
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
            while (fwidth > 0 && uval < (1 << (fwidth - 1) * 4)) {
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

void exception(void)
{
    VDP_drawTextML("============", APLANE, 0, 0);
    VDP_drawTextML(" exception! ", APLANE, 0, 1);
    VDP_drawTextML("============", APLANE, 0, 2);
    while (1)
        ;
}

void vbl(void)
{
}

static int usb_read_while_ready(OsRoutine *ed,
    void *buf_, int maxlen)
{
    u8 *buf = buf_;
    int r = 0;

    while (ed->usbRdReady() && r < maxlen)
        buf[r++] = ed->usbReadByte();

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

    switch (b3)
    {
    case 'j':
        test_joy_latency(&min, &max);
        printf("latency: %d - %d\n\n", min, max);
        return 0;
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

static int do_run(OsRoutine *ed, u8 b3)
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
    default:
        return -1;
    }

    while (read32(GFX_CTRL_PORT) & 2)
        ;
    ed->VDP_setReg(VDP_MODE1, 0x04); 
    ed->VDP_setReg(VDP_MODE2, 0x44); 

    ed->usbWriteByte('k');

    run_game(mapper);
    /* should not get here.. */

    return -1;
}

int main()
{
    OsRoutine *ed;
    u8 buf[16];
    int len;
    int i, d, ret;

    ed = (OsRoutine *) *(u32 *)0x1A0;
    ed->memInitDmaCode(); 

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

    /* note: relying on ED menu's font setup here.. */

    printf("version: %02x\n", read8(0xa10001));
    printf("ED os/fw: %x/%x\n\n", ed->osGetOsVersion(),
           ed->osGetFirmVersion());

    for (;;) {
        if (!ed->usbRdReady()) {
            asm volatile("stop #0x2000");
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
            buf[2] = ed->usbReadByte();
            ret = do_run(ed, buf[2]);
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
