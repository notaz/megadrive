#define CMD_PREFIX	0x5a
#define CMD_MD_SEND	0xc1	/* send to MD:   addr[3], len[3], data[] */
#define CMD_MD_RECV	0xc2	/* recv from MD: addr[3], len[3], data[] */
#define CMD_JUMP	0xc3	/* jump to addr: addr[3] */
#define CMD_TEST	0xc4	/* test code */

#define CMD_FIRST	CMD_MD_SEND
#define CMD_LAST	CMD_TEST
