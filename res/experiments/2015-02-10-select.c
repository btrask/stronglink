




{



	async_read_t r1[1];
	async_read_t r2[1];
	async_select_t sel[1];
	async_read_for_select(sel, r1, stream1);
	async_read_for_select(sel, r2, stream2);
	void *const ptr = select(sel);
	async_read_stop(r1);
	async_read_stop(r2);
	if(r1 == ptr) {
		
	} else if(r2 == ptr) {
		
	}

// this is just... crazy
// well, useless
// it doesn't help with what we want to do at all

}


