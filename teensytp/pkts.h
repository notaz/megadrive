
enum tas_pkt_type {
	PKT_UPD_MODE = 0xe301, // op_mode
	PKT_UPD_BTNS = 0xe302,
};

struct tp_pkt {
	uint16_t type;
	uint16_t pad;
	union {
		uint8_t data[60];
		uint32_t mode;
		struct {
			uint32_t changed_players;
			uint32_t bnts[4]; // MXYZ SACB RLDU
		};
	};
} __attribute__((packed));

enum op_mode {
	OP_MODE_3BTN = 0,
	OP_MODE_6BTN = 1,
	OP_MODE_TEAMPLAYER = 2,
};
