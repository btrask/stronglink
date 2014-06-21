#include "async.h"

uv_loop_t *loop = NULL;
cothread_t yield = NULL;

static cothread_t reap = NULL;
static cothread_t zombie = NULL;

static void reaper(void) {
	for(;;) {
		co_delete(zombie); zombie = NULL;
		co_switch(yield);
	}
}

void async_init(void) {
	loop = uv_default_loop();
	yield = co_active();
	reap = co_create(1024 * sizeof(void *) / 4, reaper);
}
void co_terminate(void) {
	zombie = co_active();
	co_switch(reap);
}

