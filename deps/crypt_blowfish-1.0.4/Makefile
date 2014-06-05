#
# Written by Solar Designer and placed in the public domain.
# See crypt_blowfish.c for more information.
#

CC = gcc
AS = $(CC)
LD = $(CC)
RM = rm -f
CFLAGS = -Wall -O2 -fomit-frame-pointer -funroll-loops
ASFLAGS = -c
LDFLAGS = -s

BLOWFISH_OBJS = \
	crypt_blowfish.o x86.o

CRYPT_OBJS = \
	$(BLOWFISH_OBJS) crypt_gensalt.o wrapper.o

TEST_OBJS = \
	$(BLOWFISH_OBJS) crypt_gensalt.o crypt_test.o

TEST_THREADS_OBJS = \
	$(BLOWFISH_OBJS) crypt_gensalt.o crypt_test_threads.o

EXTRA_MANS = \
	crypt_r.3 crypt_rn.3 crypt_ra.3 \
	crypt_gensalt.3 crypt_gensalt_rn.3 crypt_gensalt_ra.3

all: $(CRYPT_OBJS) man

check: crypt_test
	./crypt_test

crypt_test: $(TEST_OBJS)
	$(LD) $(LDFLAGS) $(TEST_OBJS) -o $@

crypt_test.o: wrapper.c
	$(CC) -c $(CFLAGS) wrapper.c -DTEST -o $@

check_threads: crypt_test_threads
	./crypt_test_threads

crypt_test_threads: $(TEST_THREADS_OBJS)
	$(LD) $(LDFLAGS) $(TEST_THREADS_OBJS) -lpthread -o $@

crypt_test_threads.o: wrapper.c
	$(CC) -c $(CFLAGS) wrapper.c -DTEST -DTEST_THREADS=4 -o $@

man: $(EXTRA_MANS)

$(EXTRA_MANS):
	echo '.so man3/crypt.3' > $@

.c.o:
	$(CC) -c $(CFLAGS) $*.c

.S.o:
	$(AS) $(ASFLAGS) $*.S

clean:
	$(RM) crypt_test crypt_test_threads *.o $(EXTRA_MANS) core
