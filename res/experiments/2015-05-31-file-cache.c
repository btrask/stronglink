
typedef struct SLNFileCache *SLNFileCacheRef;

struct item {
	str_t *path;
	uv_buf_t buf[1];
	unsigned refcount;
	struct item *next;
};
struct SLNFileCache {
	async_mutex_t mutex[1];
	struct item *items;
	struct item *head;
	struct item *tail;
};

// is it even sln?
// or is it just part of the blog system?

// well the server api could use it
// although not as desperately

SLNFileCacheRef SLNFileCacheCreate(void);
int SLNFileCacheGet(SLNFileCacheRef const cache, strarg_t const path, struct item **const out);

// we need reference counting?

// one simplification... we don't need to be thread-safe
// we only need to send files on the main thread

// actually at some point it might be nice to support multiple threads

// one lock for the whole cache (perfect for the single threaded case)
// a linked list to track usage recency?

// our item struct is pretty bloated

// what if buffers were detached while used?
// we wouldnt need a refcount
// but they couldnt be shared either, which is unacceptable



