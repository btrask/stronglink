
// abandoned, see 2014-06-10-efs-uri-list-parser.c

typedef struct EFSIndexer* EFSIndexerRef;

struct EFSIndexer {

};

EFSIndexerRef EFSIndexerCreate(strarg_t const type) {
	if(!type) return NULL;
	if(!prefix("text/", type)) return NULL; // TODO: Indexers for other types.
	EFSIndexerRef const indexer = calloc(1, sizeof(struct EFSIndexer));


	return indexer;
}
void EFSIndexerWrite(EFSIndexerRef const indexer, byte_t const *const buf, size_t const len) {
	if(!indexer) return;
	
}


