#include <yajl/yajl_parse.h>
#include "EarthFS.h"

#define DEPTH_MAX 5

struct EFSJSONFilterBuilder {
	yajl_handle parser;
	EFSFilterRef stack[DEPTH_MAX];
	index_t depth;
};

static yajl_callbacks const callbacks;

EFSJSONFilterBuilderRef EFSJSONFilterBuilderCreate(void) {
	EFSJSONFilterBuilderRef builder = calloc(1, sizeof(struct EFSJSONFilterBuilder));
	builder->parser = yajl_alloc(&callbacks, NULL, builder);
	return builder;
}
void EFSJSONFilterBuilderFree(EFSJSONFilterBuilderRef const builder) {
	if(!builder) return;
	yajl_free(builder->parser); builder->parser = NULL;
	for(index_t i = 0; i < DEPTH_MAX; ++i) {
		EFSFilterFree(builder->stack[i]); builder->stack[i] = NULL;
	}
	free(builder);
}
void EFSJSONFilterBuilderParse(EFSJSONFilterBuilderRef const builder, strarg_t const json, size_t const len) {
	if(!builder) return;
	BTAssert(builder->parser, "Builder in invalid state");
	(void)yajl_parse(builder->parser, (unsigned char const *)json, len);
}
EFSFilterRef EFSJSONFilterBuilderDone(EFSJSONFilterBuilderRef const builder) {
	if(!builder) return NULL;
	BTAssert(builder->parser, "Builder in invalid state");
	yajl_status const err = yajl_complete_parse(builder->parser);
	yajl_free(builder->parser); builder->parser = NULL;
	BTAssert(0 == builder->depth, "Parser ended at depth %d", (int)builder->depth);
	EFSFilterRef const filter = builder->stack[0];
	builder->stack[0] = NULL;
	return yajl_status_ok == err ? filter : NULL;
}

EFSFilterType EFSFilterTypeFromString(strarg_t const type, size_t const len) {
	if(substr("all", type, len)) return EFSNoFilter;
	if(substr("intersection", type, len)) return EFSIntersectionFilter;
	if(substr("union", type, len)) return EFSUnionFilter;
	if(substr("fulltext", type, len)) return EFSFullTextFilter;
	if(substr("source", type, len)) return EFSLinkSourceFilter;
	if(substr("target", type, len)) return EFSLinkTargetFilter;
	return EFSFilterInvalid;
}


// INTERNAL

static int yajl_string(EFSJSONFilterBuilderRef const builder, strarg_t const  str, size_t const len) {
	index_t const depth = builder->depth;
	EFSFilterRef filter = builder->stack[depth];
	if(filter) {
		err_t const err = EFSFilterAddStringArg(filter, str, len);
		if(err) return false;
	} else {
		filter = EFSFilterCreate(EFSFilterTypeFromString(str, len));
		if(!filter) return false;
		builder->stack[depth] = filter;
		if(depth) {
			err_t const err = EFSFilterAddFilterArg(builder->stack[depth-1], filter);
			if(err) return false;
		}
	}
	return true;
}
static int yajl_start_array(EFSJSONFilterBuilderRef const builder) {
	EFSFilterRef const filter = builder->stack[builder->depth];
	if(!filter) return false; // Filter where type string was expected.
	++builder->depth;
	if(builder->depth > DEPTH_MAX) return false;
	return true;
}
static int yajl_end_array(EFSJSONFilterBuilderRef const builder) {
	BTAssert(builder->depth, "Parser ended too many arrays");
	EFSFilterRef const filter = builder->stack[builder->depth];
	if(!filter) return false; // Empty array.
	builder->stack[builder->depth--] = NULL;
	return true;
}
static yajl_callbacks const callbacks = {
	.yajl_string = (int (*)())yajl_string,
	.yajl_start_array = (int (*)())yajl_start_array,
	.yajl_end_array = (int (*)())yajl_end_array,
};

