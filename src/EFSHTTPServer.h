#include <netinet/in.h>
#include "EarthFS.h"

typedef struct EFSHTTPServer* EFSHTTPServerRef;

typedef struct {
	void (*header)(void *const context, str_t const *const field, str_t const *const value);
	void (*data)(void *const context, byte_t const *const buf, size_t const len);
	void (*end)(void *const context);
	void (*error)(void *const context, int err);
	void *context;
} EFSHTTPCallbacks;

typedef void (*EFSHTTPListener)(void *const context, str_t const *const URI, fd_t const response, EFSHTTPCallbacks *const callbacks);

EFSHTTPServerRef EFSHTTPServerCreate(EFSHTTPListener const listener, void *const context);
void EFSHTTPServerFree(EFSHTTPServerRef const server);
int EFSHTTPServerListen(EFSHTTPServerRef const server, in_port_t const port, in_addr_t const address);
void EFSHTTPServerClose(EFSHTTPServerRef const server);

