

typedef struct {
	uint64_t sortID;
	uint64_t fileID;
} slot;
@interface SLNFileAttributeFilter : SLNFilter
{
	slot slots[32];
}
@end

@interface SLNURIFilter : SLNFileAttributeFilter
@end



@implementation SLNFileAttributeFilter
- (void)free {

	[super free];
}

- (SLNFilter *)unwrap {
	return self;
}

- (int)prepare:(DB_txn *const)txn {
	if([super prepare:txn] < 0) return -1;
	// ???
	return 0;
}


@end

@implementation SLNURIFilter
- (void)free {
	FREE(&URI);
	[super free];
}

- (SLNFilterType)type {
	return SLNURIFilterType;
}
- (strarg_t)stringArg:(index_t const)i {
	if(0 == i) return URI;
	return NULL;
}
- (int)addStringArg:(strarg_t const)str :(size_t const)len {
	if(!URI) {
		URI = strndup(str, len);
		return 0;
	}
	return -1;
}
- (void)print:(count_t const)depth {
	indent(depth);
	fprintf(stderr, "(uri \"%s\")\n", URI);
}
- (size_t)getUserFilter:(str_t *const)data :(size_t const)size :(count_t const)depth {
	return wr(data, size, URI);
}

@end




















