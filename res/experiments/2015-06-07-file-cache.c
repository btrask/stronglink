
// Caching is done with mmap, so this defines address space, not actual RAM use.
// The kernel is free to page out cached files depending on memory pressure.
#if defined(__LP64__) || defined(__ILP64__)
#define SLN_CACHE_SIZE (1024 * 1024 * 1024 * 1)
#else
#define SLN_CACHE_SIZE (1024 * 1024 * 128)
#endif

#define SLN_CACHE_SMALL_FILE_MAX (1024 * 8)
#define SLN_CACHE_LARGE_FILE_FDS 50


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






// we want to have one unified interface for reading chunks from small and large files

typedef struct {
	uv_buf_t buf;
	unsigned refcount;
} SLNFileCacheChunk;

int SLNFileCacheRead(SLNFileCacheRef const cache, strarg_t const path, size_t const len, uint64_t const offset, SLNFileCacheChunk **const out);


// i have such a clear picture of how this should work
// but the devil is in the details...



// note that for very small files (less than one page), mmap is wasteful...
// and read(2) has more overhead the larger a file is

// potential approach
// 1. files less than ~2k: read(2), buffer manually
// 2. files 2k-8mb: mmap entire file
// 3. files greater than 8mb: mmap chunks at a time

// note that each chunk should be cachable separately
// thus is is a path and offset that identifies a cached item


// then for very large files we could also cache fds...
// although actually that might not even be worth it


// note that caching in application memory is a terrible idea
// but for very small files, maybe it can't be helped?


// this code should really be reusable, even as a library
// look at sources from nginx or haproxy...



