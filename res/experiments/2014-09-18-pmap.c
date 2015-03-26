struct work {
	void **inputs;
	void **outputs;
};
static void pmap_worker(void *ctx) {
	struct work *const work = ctx;
	co_switch(yield);
	for(;;) {
		async_worker_enter(?);


		async_worker_leave(?);
		co_switch();
	}
}


void async_worker_pmap() {

}

// forget this
// each worker should have a built-in fiber that it can use


void async_worker_background(async_worker_t *worker, void (*work)(void *), void (*done)(void *), void *ctx);








