

bool getQueryLatest(HTTPConnectionRef const conn, HTTPMethod const method, str_t const *const URI, EFSRepoRef const repo) {
	if(HTTP_GET != method) return false; // TODO: POST instead?
	// TODO: Check /query/latest == uri

	for(;;) {
		sleep(30);
		if(-1 == write(stream, " ", 1)) break;
	}
	// Problems with this approach:
	// - We don't notice the connection is closed for up to 30 seconds (DoS target)
	// - We reserve a whole thread for each query
	// TODO: HTTPConnectionDetatchStream() solves both problems
	// Also, heartbeat all of the queries in one big loop, so we only have to wake up once every 30 seconds for all of them

	return true;
}

typedef struct EFSQuery *EFSQueryRef;
typedef struct EFSFilter *EFSFilterRef;

EFSFilterRef *EFSFilterCreate() {

}
// oh god... dynamic tree structures without polymorphism






typedef struct EFSFilterBuilder *EFSFilterBuilderRef;


EFSFilterBuilderRef EFSFilterBuilderCreate(void);
void EFSFilterBuilderFree(EFSFilterBuilderRef const builder);
void EFSFilterBuilderParseJSON(EFSFilterBuilderRef const builder, str_t const *const JSON);
EFSFilterRef EFSFilterBuilderCreateFilter(EFSFilterBuilderRef const builder);
















#include <yajl/yajl_parse.h>


typedef struct EFSFilter* EFSFilterRef;


EFSJSONFilterBuilderRef EFSJSONFilterBuilderCreate(void);
void EFSJSONFilterBuilderFree(EFSJSONFilterBuilderRef const builder);
void EFSJSONFilterBuilderParse(EFSJSONFilterBuilderRef const builder, strarg_t const json, size_t const len);
EFSFilterRef EFSJSONFilterBuilderDone(EFSJSONFilterBuilderRef const builder);

#define DEPTH_MAX 5

typedef enum {
	EFSFilterInvalid,
	EFSNoFilter,
	EFSIntersectionFilter,
	EFSUnionFilter,
	EFSFullTextFilter,
	EFSLinkSourceFilter,
	EFSLinkTargetFilter,
} EFSFilterType;

typedef struct {
	count_t count;
	count_t size;
	EFSFilterRef items[0];
} EFSFilterList;

struct EFSFilter {
	EFSFilterType type;
	union {
		str_t *string;
		EFSFilterList *filters;
	} data;
};

struct EFSJSONFilterBuilder {
	yajl_handle parser;
	EFSFilterRef stack[DEPTH_MAX];
	index_t depth;
};

EFSJSONFilterBuilderRef EFSJSONFilterBuilderCreate(void) {
	EFSJSONFilterBuilderRef builder = calloc(1, sizeof(struct EFSJSONFilterBuilder));
	builder->parser = yajl_alloc(&callbacks, NULL, b);
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
	(void)yajl_parse(builder->parser, json, len);
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

static int yajl_string(EFSFilterBuilderRef const builder, strarg_t const  str, size_t const len) {
	index_t const depth = builder->depth;
	EFSFilterRef filter = builder->stack[depth];
	if(filter) {
		err_t const err = EFSFilterAddStringArg(filter, str, len);
		if(err) return false;
	} else {
		filter = EFSFilterCreate(EFSFilterTypeFromString(str, len));
		if(!filter) return false;
		buidler->stack[depth] = filter;
		if(depth) {
			err_t const err = EFSFilterAddFilterArg(builder->stack[depth-1], filter);
			if(err) return false;
		}
	}
	return true;
}
static int yajl_start_array(EFSFilterBuilderRef const builder) {
	EFSFilterRef const filter = builder->stack[builder->depth];
	if(!filter) return false; // Filter where type string was expected.
	++builder->depth;
	if(builder->depth > DEPTH_MAX) return false;
	return true;
}
static int yajl_end_array(EFSFilterBuilderRef const builder) {
	BTAssert(builder->depth, "Parser ended too many arrays");
	EFSFilterRef const filter = builder->stack[builder->depth];
	if(!filter) return false; // Empty array.
	builder->stack[builder->depth--] = NULL;
	return true;
}
static yajl_callbacks const callbacks = {
	.yajl_string = yajl_string,
	.yajl_start_array = yajl_start_array,
	.yajl_end_array = yajl_end_array,
};



EFSFilterType EFSFilterTypeFromString(strarg_t const type, size_t const len) {
	if(substr("all", type, len)) return EFSNoFilter;
	if(substr("intersection", type, len)) return EFSIntersectionFilter;
	if(substr("union", type, len)) return EFSUnionFilter;
	if(substr("fulltext", type, len)) return EFSFullTextFilter;
	if(substr("source", type, len)) return EFSLinkSourceFilter;
	if(substr("target", type, len)) return EFSLinkTargetFilter;
	return EFSFilterInvalid;
}
EFSFilterRef EFSFilterCreate(EFSFilterType const type) {
	if(EFSFilterInvalid == type) return NULL;
	EFSFilterRef const filter = calloc(1, sizeof(struct EFSFilter));
	filter->type = type;
	return filter;
}
void EFSFilterFree(EFSFilterRef const filter) {
	if(!filter) return;
	switch(filter->type) {
		case EFSNoFilter:
			break;
		case EFSFullTextFilter:
		case EFSLinkSourceFilter:
		case EFSLinkTargetFilter:
			FREE(&filter->data.string);
			break;
		case EFSIntersectionFilter:
		case EFSUnionFilter:
			EFSFilterList *const list = filter->data.filters;
			for(index_t i = 0; i < list->count; ++i) {
				EFSFilterFree(list->items[i]); list->items[i] = NULL;
			}
			FREE(&filter->data.filters);
			break;
		default:
			BTAssert(0, "Invalid filter type %d", (int)filter->type);
	}
	free(filter);
}
err_t EFSFilterAddStringArg(EFSFilterRef const filter, strarg_t const str, size_t const len) {
	if(!filter) return 0;
	switch(filter->type) {
		case EFSFullTextFilter:
		case EFSLinkSourceFilter:
		case EFSLinkTargetFilter:
			break;
		default: return -1;
	}
	if(filter->data.string) return -1;
	filter->data.string = strndup(str, len);
	return 0;
}
err_t EFSFilterAddFilterArg(EFSFilterRef const filter, EFSFilterRef const subfilter) {
	if(!filter) return 0;
	switch(filter->type) {
		case EFSIntersectionFilter:
		case EFSUnionFilter:
			break;
		default: return -1;
	}
	EFSFilterList *filters = filter->data.filters;
	count_t size = filters ? filters->size : 0;
	count_t count = filters ? filters->count : 0;
	if(++count > size) {
		size = MAX(10, size * 2);
		filters = realloc(filters,  sizeof(EFSFilterList) + (sizeof(EFSFilterRef) * size));
		filter->data.filters = filters;
		if(!filters) return -1;
		filters->size = size;
	}
	filters->count = count;
	filters->items[count-1] = subfilter;
	return 0;
}


