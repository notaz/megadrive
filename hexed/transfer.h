/* all data is big endian */

#define CMD_PREFIX	0x5a
#define CMD_PC_SEND	0xc1	/* send to MD:   addr[3], len[3], data[] */
#define CMD_PC_RECV	0xc2	/* recv from MD: addr[3], len[3], data[] */
#define CMD_JUMP	0xc3	/* jump to addr: addr[3] */
#define CMD_IOSEQ	0xc4	/* perform i/o ops: count[2], [type[1], addr[3], data[{0,1,2,4}]]* */
#define CMD_LOADSTATE	0xc5	/* load PD state: vram[64k], cram[128], vsram[128], vdp[32] */
#define CMD_VRAM_RECV	0xc6	/* recv from MD: vram[64k] */
#define CMD_TEST	0xc7	/* test code */

#define CMD_FIRST	CMD_PC_SEND
#define CMD_LAST	CMD_TEST

#define IOSEQ_R8	0xb0
#define IOSEQ_R16	0xb1
#define IOSEQ_R32	0xb2
#define IOSEQ_W8	0xb3
#define IOSEQ_W16	0xb4
#define IOSEQ_W32	0xb5
