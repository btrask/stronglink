



typedef enum {
	LSMDB_NONE = 0,
	LSMDB_ABC = 1,
	LSMDB_ACB = 2,
	LSMDB_BAC = 3,
	LSMDB_BCA = 4,
	LSMDB_CAB = 5,
	LSMDB_CBA = 6,
} LSMDB_state;

static LSMDB_state lsmdb_swap_12(LSMDB_state const x) {
	switch(x) {
		case LSMDB_ABC: return LSMDB_BAC;
		case LSMDB_ACB: return LSMDB_CAB;
		case LSMDB_BAC: return LSMDB_ABC;
		case LSMDB_BCA: return LSMDB_CBA;
		case LSMDB_CAB: return LSMDB_ACB;
		case LSMDB_CBA: return LSMDB_BCA;
		default: assert(0); return 0;
	}
}
static LSMDB_state lsmdb_swap_23(LSMDB_state const x) {
	switch(x) {
		case LSMDB_ABC: return LSMDB_ACB;
		case LSMDB_ACB: return LSMDB_ABC;
		case LSMDB_BAC: return LSMDB_BCA;
		case LSMDB_BCA: return LSMDB_BAC;
		case LSMDB_CAB: return LSMDB_CBA;
		case LSMDB_CBA: return LSMDB_CAB;
		default: assert(0); return 0;
	}
}

