#include "async.h"
#include "async_sqlite.h"
#include "EarthFS.h"
#include "http/HTTPServer.h"

#ifdef SQLITE_ENABLE_SQLLOG
void sqlite3_init_sqllog(void);
#endif

static void sqlite_error(void *const ctx, int const err, char const *const msg) {
	if(SQLITE_LOCKED_SHAREDCACHE == err) return;
	fprintf(stderr, "sqlite: (%d) %s\n", err, msg);
}

bool_t EFSServerDispatch(EFSRepoRef const repo, HTTPMessageRef const msg, HTTPMethod const method, strarg_t const URI);

typedef struct Blog* BlogRef;

BlogRef BlogCreate(EFSRepoRef const repo);
void BlogFree(BlogRef *const blogptr);
bool_t BlogDispatch(BlogRef const blog, HTTPMessageRef const msg, HTTPMethod const method, strarg_t const URI);

static EFSRepoRef repo = NULL;
static BlogRef blog = NULL;
static HTTPServerRef server = NULL;

static void listener(void *ctx, HTTPMessageRef const msg) {
	HTTPMethod const method = HTTPMessageGetRequestMethod(msg);
	strarg_t const URI = HTTPMessageGetRequestURI(msg);
	if(EFSServerDispatch(repo, msg, method, URI)) return;
	if(BlogDispatch(blog, msg, method, URI)) return;
	HTTPMessageSendStatus(msg, 400);
}
static void init(void) {
	repo = EFSRepoCreate("/home/ben/Documents/testrepo");
	blog = BlogCreate(repo);
	server = HTTPServerCreate((HTTPListener)listener, blog);
	HTTPServerListen(server, "8000", INADDR_LOOPBACK);
	EFSRepoStartPulls(repo);
	co_terminate();
}
static void term(void) {
	// TODO: EFSRepoStopPulls(repo);
	HTTPServerClose(server);
	HTTPServerFree(&server);
	BlogFree(&blog);
	EFSRepoFree(&repo);
	co_terminate();
}
int main(int const argc, char const *const *const argv) {
	signal(SIGPIPE, SIG_IGN);

	int err = sqlite3_config(SQLITE_CONFIG_LOG, sqlite_error, NULL);
	assertf(SQLITE_OK == err, "Couldn't enable SQLite error log");
#ifdef SQLITE_ENABLE_SQLLOG
	setenv("SQLITE_SQLLOG_DIR", "/home/ben/Documents/testrepo", 1);
	sqlite3_init_sqllog();
#endif

	async_init();
	async_sqlite_register();

	// Even our init code wants to use async I/O.
	async_wakeup(co_create(STACK_DEFAULT, init));
	uv_run(loop, UV_RUN_DEFAULT);

	async_wakeup(co_create(STACK_DEFAULT, term));
	uv_run(loop, UV_RUN_DEFAULT); // Allows term() to execute.

	return EXIT_SUCCESS;
}

