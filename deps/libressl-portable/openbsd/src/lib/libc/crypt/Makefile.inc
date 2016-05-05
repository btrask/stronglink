#	$OpenBSD: Makefile.inc,v 1.26 2014/12/08 20:46:04 tedu Exp $

.PATH: ${LIBCSRCDIR}/arch/${MACHINE_CPU}/crypt ${LIBCSRCDIR}/crypt

SRCS+=	crypt.c cryptutil.c arc4random.c arc4random_uniform.c \
	blowfish.c bcrypt.c

MAN+=	crypt.3 crypt_checkpass.3 blowfish.3 arc4random.3
