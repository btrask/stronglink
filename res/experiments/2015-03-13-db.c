

#define DB_VAL(name, bytes) \
	if((bytes) < DB_VARINT_MAX) abort(); \
	uint8_t __buf_##name[(bytes)]; \
	DB_val name[1] = {{ 0, __buf_##name }}


DB_VAL(fileID_key, DB_VARINT_MAX * 2 + DB_INLINE_MAX * 1);
db_bind_uint64(fileID_key, EFSURIAndFileID);
db_bind_string(curtxn, fileID_key, targetURI);
db_bind_uint64(fileID_key, fileID);
rc = db_cursor_seekr(step_files, fileIDs, fileID_key, NULL, dir);

// get rid of DB_val?


typedef uint8_t DB_buf;
DB_buf fileID_buf[DB_VARINT_MAX * 2 + DB_INLINE_MAX * 1];
size_t fileID_len = EFSURIAndFileID(targetURI, fileID);
rc = db_cursor_seekp(step_files, fileIDs_buf, fileIDs_len, fileID_buf, fileID_len, NULL, dir);

// 'p' means "prefix", instead of "range"
// not really an improvement...


// it'd be kind of nice to have these buffers defined as much as possible at compile-time...
// that's something c++ would be good for but c can't really do
// jonathan blow's language would be the best for it, of course



DB_buf fileID_buf[] = { 0x01, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 };

// lol...


// could we start from a high level description of the schema?


EFSURIAndFileIDs: uint64_t table, char *targetURI, uint64_t fileID;

// thats just the key
// most tables would also have value fields


// i know a lot of projects these days are using leveldb with json
// how crazy is that?
// maybe not entirely crazy, although it's very poor at handling long strings for you

[ table, targetURI, fileID ]

// i guess the nice thing is that the format is self-describing?

// although i think lexical sorting doesnt work

[ 20 ]
[ 3 ]


// part of the thing
// json isn't that concise to use from c

// which raises the question
// back when we were using sqlite
// we were binding all of these arguments
// which was actually more verbose than what we have now
// so why is the current setup less readable?

// and the answer is pretty obvious
// there's actually two reasons
// the main one is that we have to explicitly and manually define the size of each field
// which is redundant and a huge pain

// the second reason is because we don't do any error checking or verification

// and i guess the third reason is that ranges are ugly... lol



// maybe step one is just to define lengths for every field type


#define EFSURIAndFileID_SIZE (DB_VARINT_MAX * 2 + DB_INLINE_MAX * 1)



// another thing we can do is check that the capacity of the buffer is the right size for what we're using it for

// although doing that dynamically requires defining yet another local variable



// one nice idea would be keeping a separate buffer as a slab allocator for all of these keys and values, instead of doing it on the stack

// unfortunately its hard to keep per-fiber data
// plus by using the stack the compiler tracks it for us


// we could use alloca...
// but that still requires a macro and basically doesnt help



#define EFSURIAndFileID(name, txn, URI, fileID) \
	DB_VAL(name, DB_VARINT_MAX * 2 + DB_INLINE_MAX * 1); \
	db_bind_uint64(name, EFSURIAndFileID); \
	db_bind_string(name, (URI), (txn)); \
	db_bind_uint64(name, (fileID));


// the simplest thing
// could possibly work


// i hate to keep hiding stuff behind more and more layers of macros
// but this honestly seems like a reasonable approach



















