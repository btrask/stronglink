#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../deps/libco/libco.h"

// Good for CORO_USE_VALGRIND.
#include "../deps/libcoro/coro.h"

typedef struct {
	coro_context context;
	struct coro_stack stack;
} coro_thread;

static cothread_t _co_active;

static int init = 0;
static void co_init(void) {
	coro_thread *const t = calloc(1, sizeof(coro_thread));
	assert(t && "libco_coro couldn't init");
	coro_create(&t->context, NULL, NULL, NULL, 0);
	_co_active = t;
}

cothread_t co_active(void) {
	if(!init) {
		co_init();
		init = 1;
	}
	return _co_active;
}
cothread_t co_create(unsigned int s, void (*f)(void)) {
	if(!init) {
		co_init();
		init = 1;
	}
	coro_thread *const t = malloc(sizeof(coro_thread));
	if(!t) return NULL;
	if(!coro_stack_alloc(&t->stack, s / sizeof(void *))) {
		free(t);
		return NULL;
	}
	coro_create(&t->context, (void (*)())f, NULL, t->stack.sptr, t->stack.ssze);
	return t;
}
void co_delete(cothread_t const thread) {
	coro_thread *t = thread;
	coro_destroy(&t->context);
	coro_stack_free(&t->stack);
	free(t);
}
void co_switch(cothread_t const thread) {
	coro_thread *const a = _co_active;
	coro_thread *const b = thread;
	_co_active = thread;
	coro_transfer(&a->context, &b->context);
}

