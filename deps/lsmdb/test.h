#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MAP_SIZE (1024 * 1024 * 256)

#define WRITES (1000 * 1000)
#define TXN_SIZE 1000
#define KEY_SIZE sizeof(uint64_t)
#define DATA_SIZE 16
#define SYNC 1
#define READ 0

#ifdef SEQ
#define GENKEY genkey_seq
#define PUT_FLAGS MDB_APPEND
#else
#define GENKEY genkey_rnd
#define PUT_FLAGS 0
#endif

#define chk(x) ({ \
	int const __rc = (x); \
	if(MDB_SUCCESS != __rc) { \
		fprintf(stderr, "%s:%d %s: %s: %s\n", \
			__FILE__, __LINE__, __PRETTY_FUNCTION__, \
			#x, mdb_strerror(__rc)); \
		abort(); \
	} \
})

static char *tohex(char const *const buf, size_t const size) {
	char const *const map = "0123456789abcdef";
	char *const hex = calloc(size*2+1, 1);
	for(off_t i = 0; i < size; ++i) {
		hex[i*2+0] = map[0xf & (buf[i] >> 4)];
		hex[i*2+1] = map[0xf & (buf[i] >> 0)];
	}
	return hex;
}

uint64_t seed = 1;
static inline void genkey_rnd(uint8_t buf[KEY_SIZE]) {
	// Little endian, which essentially randomizes our insertions.
	buf[0] = 0xff & (seed >> 0x00);
	buf[1] = 0xff & (seed >> 0x08);
	buf[2] = 0xff & (seed >> 0x10);
	buf[3] = 0xff & (seed >> 0x18);
	buf[4] = 0xff & (seed >> 0x20);
	buf[5] = 0xff & (seed >> 0x28);
	buf[6] = 0xff & (seed >> 0x30);
	buf[7] = 0xff & (seed >> 0x38);
	seed++;
}
static inline void genkey_seq(uint8_t buf[KEY_SIZE]) {
	// Big endian.
	buf[7] = 0xff & (seed >> 0x00);
	buf[6] = 0xff & (seed >> 0x08);
	buf[5] = 0xff & (seed >> 0x10);
	buf[4] = 0xff & (seed >> 0x18);
	buf[3] = 0xff & (seed >> 0x20);
	buf[2] = 0xff & (seed >> 0x28);
	buf[1] = 0xff & (seed >> 0x30);
	buf[0] = 0xff & (seed >> 0x38);
	seed++;
}

uint64_t last = 0;
static inline void chkkey(uint8_t const buf[KEY_SIZE]) {
	uint64_t x = 0;
	x |= ((uint64_t)buf[7]) << 0x00;
	x |= ((uint64_t)buf[6]) << 0x08;
	x |= ((uint64_t)buf[5]) << 0x10;
	x |= ((uint64_t)buf[4]) << 0x18;
	x |= ((uint64_t)buf[3]) << 0x20;
	x |= ((uint64_t)buf[2]) << 0x28;
	x |= ((uint64_t)buf[1]) << 0x30;
	x |= ((uint64_t)buf[0]) << 0x38;
//	char *hex = tohex(buf, KEY_SIZE);
//	fprintf(stderr, "%s\n", hex);
//	free(hex);
	if(x > last) {
		last = x;
	} else {
		fprintf(stderr, "Key mismatch: %llu (%s) <= %llu\n", x, tohex(buf, KEY_SIZE), last);
		abort();
	}
}

