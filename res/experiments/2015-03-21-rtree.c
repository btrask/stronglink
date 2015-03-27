




// okay, lets figure this out

// the previous design wouldn't work, as mentioned before



/*

lets replace ids with "inodes"
not directly equivalent to file system inodes, but similar

each point has an inode number
points that are part of the same leaf node have the same inode


the root inode is probably always 0
so when you first start inserting things, they all have inode 0

then when a split happens
0 stops being a leaf and becomes a branch
and new inodes are created, 1 and 2

the existing points must be deleted and reinserted under the new inode values
half under 1 and half under 2

at this point, 0 stops being a leaf node
so it doesnt have any keys of its own at all
instead we add it to the branches table...?


the branches table is just {parent, child}
0 is obviously implicit because it has no parent
minor detail: in that case maybe the first inode should be 1


this seems simple, workable and efficient

oh i forgot, the branches table also needs to specify the regions of each branch
so its {parent, child, region}


and i think neither table (branches nor leafs) needs to have any payloads
since its all stored in the keys


one question is, how should we expose our region support?
for stronglink, the r-tree is just 2-dimensional uint64s
but it'd be nice to have a flexible r-tree interface if possible

*/

typedef struct rtree_t rtree_t;

struct rtree_t {
	dbid_t table;
	type (*cmp)();
};


// simple is best



int rtree_add(DB_txn *const txn, dbid_t const table, uint64_t const x, uint64_t const y);


typedef struct {
	uint64_t x;
	uint64_t y;
	uint64_t w;
	uint64_t h;
} rrect_t;

int rtree_search(DB_txn *const txn, dbid_t const table, rrect_t const vec, rrect_t *const out, size_t const count);

// using a rect as a vector to specify search position and direction is a good idea
// but it doesn't exactly work with unsigned integers...




// maybe the solution is to have an opaque point/range type


// btw rtrees are an argument for our schema system using type names rather than an enum
// gotta be extensible


int db_schema_table(DB_txn *const txn, dbid_t const table, char const *const name);
int db_schema_column(DB_txn *const txn, dbid_t const table, uint64_t const col, char const *const name, char const *const type);

db_schema_table(txn, EFSSearchRTree, "EFSSearchRTree");
db_schema_column(txn, EFSSearchRTree, 0, "rtree", RTREE_TYPE);
db_schema_column(txn, EFSSearchRTree, 1, "x", DB_UINT64_TYPE);
db_schema_column(txn, EFSSearchRTree, 2, "y", DB_UINT64_TYPE);


struct rtree_t {
	dbid_t table;
	
};



typedef int (*rtree_nearest)(void *const ctx, DB_val const *const a, DB_val const *const b);
typedef 




// when returning a long list of items from the database
// we cant simply use pointers to the db's buffers
// because our leveldb backend only keeps a fixed number of buffers valid at a time
// mdb would have no trouble, of course
// (actually it would have trouble if they eventually implement windowed mapping for 32-bit)

// point is, simply using raw db_vals actually becomes more of a mess
// because not only do we have to memcpy them around and track their lengths
// we still have to decode them to do anything


typedef struct {
	uint64_t min;
	uint64_t max;
} rrange_t;

int rtree_add(DB_txn *const txn, dbid_t const table, size_t const d, rrange_t const point[]);
int rtree_del(DB_txn *const txn, dbid_t const table, size_t const d, rrange_t const point[]);

int rtree_nearest(DB_txn *const txn, dbid_t const table, size_t const d, rrange_t const point[], int const dir[], rrange_t out[][], size_t *const count);
int rtree_get(DB_txn *const txn, dbid_t const table, size_t const d, rrange_t const point[], int const dir[], rrange_t out[]);


// okay, this is actually pretty good
// however we need to support several different search operations
// == (within), <, >, <=, >=

// although "within" isn't very good because if theres too many items theres no way to get the rest


// if at some point we want to add alternate types (e.g. floating point) we should just duplicate the interface
// sucks but thats how c works
// no point in worrying about it for now



// okay well
// nearest already accepts a point-range
// so that takes care of the different operators we need
// all of the results must be within the input range
// and they are ordered based on `dir`



// our `dir` parameters arent enough to fully specify comparison
// since there's the different dimensions to take into account
// but for our purposes it doesn't matter
// results are sorted by d0, d1, d2, etc...
// the only purpose of using an r-tree in the first place is to constrain the other dimensions
// not to order by them

// its not quite up to the standard of Postgres's GiST, but it's all we need
// i hope



#define BRANCH_FACTOR 10

#define DIFF(a, b) (MAX(a, b) - MIN(a, b))

enum {
	RTREE_BRANCH = 0,
	RTREE_LEAF = 1,
};

static uint64_t dist(size_t const d, rrange_t const a[], rrange_t const b[]) {
	uint64_t t = 0;
	for(size_t i = 0; i < d; i++) {
		t += DIFF(a[i], b[i]);
		// TODO: Handle overflow
	}
	return t;
}
static int split(DB_txn *const txn, dbid_t const table, size_t const d, uint64_t const node) {
	
}
// TODO: balancing?

int rtree_add(DB_txn *const txn, dbid_t const table, size_t const d, rrange_t const point[]) {
	
}
int rtree_del(DB_txn *const txn, dbid_t const table, size_t const d, rrange_t const point[]) {
	return DB_EINVAL;
}

int rtree_nearest(DB_txn *const txn, dbid_t const table, size_t const d, rrange_t const point[], int const dir[], rrange_t out[][], size_t *const count) {
	
}
int rtree_get(DB_txn *const txn, dbid_t const table, size_t const d, rrange_t const point[], int const dir[], rrange_t out[]) {
	size_t count = 1;
	int rc = rtree_nearest(txn, table, d, point, dir, &out, &count);
	if(DB_SUCCESS != rc) return rc;
	if(!count) return DB_NOTFOUND;
	return DB_SUCCESS;
}




// uh... our r-trees are getting weirder and weirder
// our distance function is the absolute sum instead of the hypotenuse...?
// and how is this supposed to store our full-text index terms anyway?



















