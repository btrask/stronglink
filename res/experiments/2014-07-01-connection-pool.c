
typedef struct async_db_pool_s async_db_pool_t;

struct async_db_pool_s {
	struct {
		unsigned size;
		sqlite3 **items;
		unsigned cur;
		unsigned count;
	} connections;
	struct {
		unsigned size;
		cothread_t *items;
		unsigned cur;
		unsigned count;
	} queue;
};

async_db_pool_t *async_db_pool_create(unsigned const count) {
	
}

