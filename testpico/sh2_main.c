#include "common.h"

void spin(int loops);
u16  read_frt(void);

// comm area map:
// 00-01: cmd
// 02-03: error
// 04-07: arg0/response
// 08-0b: arg1
// 0c: last_irq_vec_master
// 0d: last_irq_vec_slave
// 0e: exception_index_master
// 0f: exception_index_slave
static void do_cmd(u16 cmd, u16 r[6], u32 is_slave)
{
    u32 *rl = (u32 *)r;
    u32 a, d;
    u16 v;

    switch (cmd)
    {
    case CMD_ECHO:
        v = read16(&r[4/2]) ^ (is_slave << 15);
        write16(&r[6/2], v);
        break;
    case CMD_READ_FRT:
        v = read_frt();
        write16(&r[4/2], v);
        break;
    case CMD_READ8:
        a = read32(&rl[4/4]);
        d = read8(a);
        write32(&rl[4/4], d);
        break;
    case CMD_READ16:
        a = read32(&rl[4/4]);
        d = read16(a);
        write32(&rl[4/4], d);
        break;
    case CMD_READ32:
        a = read32(&rl[4/4]);
        d = read32(a);
        write32(&rl[4/4], d);
        break;
    case CMD_WRITE8:
        a = read32(&rl[4/4]);
        d = read32(&rl[8/4]);
        write8(a, d);
        break;
    case CMD_WRITE16:
        a = read32(&rl[4/4]);
        d = read32(&rl[8/4]);
        write16(a, d);
        break;
    case CMD_WRITE32:
        a = read32(&rl[4/4]);
        d = read32(&rl[8/4]);
        write32(a, d);
        break;
    case CMD_GETGBR:
        asm volatile("stc gbr, %0" : "=r"(d));
        write32(&rl[4/4], d);
        break;
    case CMD_GETVBR:
        asm volatile("stc vbr, %0" : "=r"(d));
        write32(&rl[4/4], d);
        break;
    case CMD_GETSR:
        asm volatile("stc sr, %0" : "=r"(d));
        write32(&rl[4/4], d);
        break;
    case CMD_SETSR:
        d = read32(&rl[4/4]);
        asm volatile("ldc %0, sr" :: "r"(d));
        break;
    default:
        r[2/2]++; // error
        mem_barrier();
        break;
    }
}

void main_c(u32 is_slave)
{
    u16 *r = (u16 *)0x20004000;

    for (;;)
    {
        u16 cmd, cmdr;

        mem_barrier();
        cmd = read16(&r[0x20/2]);
        mem_barrier();
        cmdr = read16(&r[0x20/2]);
        if (cmd == 0
            || cmd != cmdr  // documented as "normal" case
            || ((cmd & 0x8000) ^ (is_slave << 15))
            || cmd == 0x4d5f) { // 'M_' from BIOS
            spin(64);
            continue;
        }
        cmd &= 0x7fff;
        do_cmd(cmd, &r[0x20/2], is_slave);
        write16(&r[0x20/2], 0);
    }
}

// vim:ts=4:sw=4:expandtab
