#include <limits.h>
#include "async.h"
#include "fts.h"
#include "EarthFS.h"
#include "http/HTTPServer.h"

bool_t EFSServerDispatch(EFSRepoRef const repo, HTTPMessageRef const msg, HTTPMethod const method, strarg_t const URI);

typedef struct Blog* BlogRef;

BlogRef BlogCreate(EFSRepoRef const repo);
void BlogFree(BlogRef *const blogptr);
bool_t BlogDispatch(BlogRef const blog, HTTPMessageRef const msg, HTTPMethod const method, strarg_t const URI);

static str_t *path = NULL;
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
static void init(void *const unused) {
	repo = EFSRepoCreate(path);
	blog = BlogCreate(repo);
	server = HTTPServerCreate((HTTPListener)listener, blog);
	HTTPServerListen(server, "8000", INADDR_ANY); //INADDR_LOOPBACK);
	EFSRepoStartPulls(repo);
}
static void term(void *const unused) {
	// TODO: EFSRepoStopPulls(repo);
	HTTPServerClose(server);
	HTTPServerFree(&server);
	BlogFree(&blog);
	EFSRepoFree(&repo);
}
int main(int const argc, char const *const *const argv) {
	signal(SIGPIPE, SIG_IGN);
	async_init();

	if(argc > 1) {
		path = strdup(argv[1]);
	} else {
		str_t str[PATH_MAX];
		size_t len = PATH_MAX;
		int err = uv_cwd(str, &len);
		assertf(err >= 0, "Couldn't get working directory");
		path = strdup(str);
	}

	// Even our init code wants to use async I/O.
	async_thread(STACK_DEFAULT, init, NULL);
	uv_run(loop, UV_RUN_DEFAULT);

	async_thread(STACK_DEFAULT, term, NULL);
	uv_run(loop, UV_RUN_DEFAULT); // Allows term() to execute.

	FREE(&path);

	return EXIT_SUCCESS;
}
