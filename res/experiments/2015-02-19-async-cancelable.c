// i think we need a cancelable version of `async_yield()`
// we can implement it with thread-local storage if necessary

enum {
	ASYNC_ECANCELED = ECANCELED,
};
int async_yield(void);
int async_cancel(cothread_t const thread);
void async_wakeup(cothread_t const thread, int const status);

// is supporting arbitrary statuses overkill?
// in some languages you can yield arbitrary objects...

// wakeup is called between unrelated fibers so it shouldn't take an argument
// if you're doing something between fibers, you can store a tls value yourself


// okay hang on
// we definitely cant make everything cancelable
// for example plenty of libuv methods dont support it

while(ASYNC_ECANCELED == async_yield());

while(async_cancel(thread) < 0);

// sounds great...
// two fibers in an infinite loop

// plus what about our worker thread pool?
// if you try to cancel a fiber on another thread, youre going to have a bad time...
// and right now, any fiber can use worker threads transparently
// in fact theres no difference between using workers and libuv async...

// of course if you replace the second loop with one that calls async_sleep(100) then you're fine...

// and you already have to distinguish between things that can be canceled and things that cant
// so we have to stop using the worker pool whenever possible
// and carefully document fibers which perform database operations...?


// of course we could theoretically even support cancelation of database operations
// the database itself certainly supports it (rollback)

// of course while on the worker thread, the fiber isn't yielding
// we'd have to check for cancelation manually (not very useful if it doesn't work while blocking)
// or rely on posix cancelation points, which i dont know anything about
// and probably dont work under windows



// basically, when you truly only have one thread
// you can get away with a lot of tricks
// when you have more threads, you gotta be more careful

// what if cancel was guaranteed to wait until the fiber was terminated?

// but the application doesnt even own the fibers
// the web server does, per connection (not per message)
// so killing a fiber means killing the whole connection, not just stopping one response



// so our options...
// 1. try to support universal cancellation, including between threads
// 2. just worry about canceling certain http requests
// 3. external iteration with filter-based http system...?


// not sure which is worse...


// external iteration sounds appealing
// but then the main fiber has to know about every connection anyway
// and every sub-fiber in order to wake them when new data arrives



// what about having some sort of global "async cancelable" object type?
// basically universal cancelation except opt-in

// or then you might as well support it everywhere possible...
// just explicitly unsupport it at thread boundaries


// we could have a lock for each fiber around critical sections
// so that if you tried to cancel the fiber, you would block until it was unlocked
// although do we usually want to block the main thread?

// sometimes we definitely want to join threads...


typedef struct async_fiber_s* async_fiber_t;
struct async_fiber_s {
	cothread_t fiber;
	cothread_t join;
	int status;
	int critical;
};

int async_yield(void);
int async_yield_nocancel(void);
void async_cancel(async_fiber_t const fiber);
int async_fiber_join(async_fiber_t const fiber);


int async_yield(void) {
	co_switch(yield);
	async_fiber_t const active = async_active();
	int const status = active->status;
	active->status = 0;
	return status;
}
int async_yield_nocancel(void) {
	int status = 0, rc;
	for(;;) {
		rc = async_yield();
		if(rc >= 0) break;
		if(ASYNC_ECANCELED != rc) return rc;
		status = rc;
	}
	return status;
}
void async_cancel(async_fiber_t const fiber) {
	if(!fiber) return;
	fiber->status = ASYNC_ECANCELED;
	if(!fiber->critical) async_wakeup(fiber);
}
int async_fiber_join(async_fiber_t const fiber) {
	assert(fiber);
	
}

// ugly...








enum {
	ASYNC_CANCELED = 1 << 0,
	ASYNC_CANCELABLE = 1 << 1,
	ASYNC_COMPLETE = 1 << 2,
};
typedef struct {
	cothread_t fiber;
	unsigned flags;
	async_t *join;
} async_t;


void async_yield(void) {
	co_switch(yield->fiber);
}
int async_yield_cancelable(void) {
	async_t *const thread = async_active();
	if(ASYNC_CANCELED & thread->flags) {
		thread->flags &= ~ASYNC_CANCELED;
		return UV_ECANCELED;
	}
	assert(!(ASYNC_CANCELABLE & thread->flags));
	thread->flags |= ASYNC_CANCELABLE;
	async_yield();
	thread->flags &= ~ASYNC_CANCELABLE;
	if(ASYNC_CANCELED & thread->flags) {
		thread->flags &= ~ASYNC_CANCELED;
		return UV_ECANCELED;
	}
	return 0;
}
int async_canceled(void) {
	async_t *const thread = async_active();
	if(ASYNC_CANCELED & thread->flags) {
		thread->flags &= ~ASYNC_CANCELED;
		return UV_ECANCELED;
	}
	return 0;
}
void async_cancel(async_t *const thread) {
	if(!thread) return;
	thread->flags |= ASYNC_CANCELED;
	if(ASYNC_CANCELABLE & thread->flags) async_wakeup(thread);
}

void async_join(async_t *const thread) {
	assert(thread);
	assert(!thread->join);
	thread->join = async_active();
	if(!(ASYNC_COMPLETE & thread->flags)) {
		if(thread == thread->join) return;
		async_yield();
		thread->join = NULL;
	}
	if(thread == thread->join) {
		async_call(co_delete, thread->fiber);
	} else {
		co_delete(thread->fiber);
	}
}

// TODO: threads that don't need to be joined? automatically self-destruct
// perhaps call async_join(async_active()) ?

// then also, if the thread has alreayd terminated by the time we go to join it
// we need to just delete it immediately without yielding
// ASYNC_COMPLETE, ASYNC_SELFJOIN ?


// pretty...

// amazing how subtle the difference between bad code and good code is

async_t thread_local *yield;

static async_t thread_local main[1];
static async_t thread_local *active;


static void async_start(void) {
	async_t thread[1];
	thread->fiber = co_active();
	thread->flags = 0;
	thread->join = NULL;
	active = thread;
	void (*const func)(void *) = arg_func;
	void *const arg = arg_arg;
	func(arg);
	thread->flags |= ASYNC_COMPLETE;
	if(thread == thread->join) {
		async_call(co_delete, thread->fiber);
	} else if(thread->join) {
		co_switch(thread->join->fiber);
	} else {
		async_yield();
	}
}
int async_spawn(size_t const stack, void (*const func)(void *), void *const arg) {
	cothread_t const fiber = co_create(stack, async_start);
	if(!fiber) return UV_ENOMEM;
	arg_func = func;
	arg_arg = arg;
	async_wakeup0(fiber);
	return 0;
}


static void async_wakeup0(cothread_t const fiber) {
	assert(fiber != yield->fiber);
	async_t *const original = yield;
	yield = async_active();
	co_switch(fiber);
	yield = original;
}
void async_wakeup(async_t *const thread) {
	active = thread;
	async_wakeup0(active->fiber);
}
































