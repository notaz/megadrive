#include "common.h"

void spin(int loops);
u16  read_frt(void);

void main_c(u32 is_slave)
{
    u16 v, *r = (u16 *)0x20004000;

    for (;;)
    {
        u16 cmd, cmdr;

        mem_barrier();
        cmd = read16(&r[0x20/2]);
        mem_barrier();
        cmdr = read16(&r[0x20/2]);
        if (cmd != cmdr  // documented as "normal" case
            || ((cmd & 0x8000) ^ (is_slave << 15))) {
            spin(64);
            continue;
        }
        cmd &= 0x7fff;
        switch (cmd)
        {
        case 0:
        case 0x4d5f:    // 'M_' from BIOS
            spin(64);
            continue;
        case CMD_ECHO:
            v = read16(&r[0x22/2]) ^ (is_slave << 15);
            write16(&r[0x24/2], v);
            break;
        case CMD_READ_FRT:
            v = read_frt();
            write16(&r[0x24/2], v);
            break;
        }
        write16(&r[0x20/2], 0);
    }
}

// vim:ts=4:sw=4:expandtab
