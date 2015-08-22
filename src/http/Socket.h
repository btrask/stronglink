// Copyright 2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include "../async/async.h"
#include "../../deps/libressl-portable/include/tls.h"
#include "../common.h"

typedef struct Socket *SocketRef;

int SocketAccept(uv_stream_t *const sstream, struct tls *const ssecure, SocketRef *const out);
int SocketConnect(strarg_t const host, strarg_t const port, SocketRef *const out);
void SocketFree(SocketRef *const socketptr);
int SocketStatus(SocketRef const socket);

int SocketPeek(SocketRef const socket, uv_buf_t *const out);
void SocketPop(SocketRef const socket, size_t const len);

int SocketWrite(SocketRef const socket, uv_buf_t const *const buf);
int SocketFlush(SocketRef const socket, bool const more);

int SocketWriteFromFile(SocketRef const socket, uv_file const file, size_t const len, int64_t const offset);

