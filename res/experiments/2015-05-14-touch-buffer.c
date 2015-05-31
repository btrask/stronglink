#include <unistd.h>

// i'm trying to figure out the least we can do to force the system to load each mapped page from disk

// note that mlock has the wrong semantics, on top of being gratuitously slow
// the goal here is not to prevent swapping if the system decides its necessary
// the goal is merely to hint to the system we're going to need the memory soon
// and obviously we'd rather wait on a worker thread than on the main one

// the basic idea is to mark the buffer volatile and read one byte from each page
// if it's volatile the compiler has to emit the read, even if we dont use the result, right?
// unfortunately i can't read x86 assembly well enough to tell...
// this might be a good time to learn how...

typedef struct {
	void *data;
	size_t size;
} DB_val;

void touch(DB_val const *const buf) {
	static size_t pagesize = 0;
	if(!pagesize) pagesize = sysconf(_SC_PAGE_SIZE);
	volatile char const *const x = buf->data;
	if(!x) return;
	if(!buf->size) return;
	int total = 0;
	for(size_t pos = 0; pos < buf->size; pos += pagesize) {
		total += x[pos];
	}
	printf("%d", total);
}

int main() {
	return 0;
}

// oh...
// this is truly not a problem
// because when we close the cursor/txn we no longer have the right to touch the database's buffers
// so we must either use them or copy them out before leaving the thread
// i actually knew that, i just forgot

