#include "stdio.h"
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

static void wakeup_cb(uv_handle_t *const handle) {
	cothread_t const thread = handle->data;
	free(handle);
	co_switch(thread);
}
void async_wakeup(cothread_t const thread) {
	// TODO: Use one global timer with a queue of threads to wake.
	uv_timer_t *const timer = malloc(sizeof(uv_timer_t));
	timer->data = thread;
	uv_timer_init(loop, timer);
	uv_close((uv_handle_t *)timer, wakeup_cb);
}

