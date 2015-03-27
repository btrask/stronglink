#include <yajl/yajl_parse.h>
#include "../StrongLink.h"

#define DEPTH_MAX 5

struct SLNJSONFilterParser {
	yajl_handle JSONParser;
	SLNFilterRef stack[DEPTH_MAX];
	int depth;
};

static yajl_callbacks const callbacks;

SLNJSONFilterParserRef SLNJSONFilterParserCreate(void) {
	SLNJSONFilterParserRef parser = calloc(1, sizeof(struct SLNJSONFilterParser));
	if(!parser) return NULL;
	parser->JSONParser = yajl_alloc(&callbacks, NULL, parser);
	parser->depth = -1;
	return parser;
}
void SLNJSONFilterParserFree(SLNJSONFilterParserRef *const parserptr) {
	SLNJSONFilterParserRef parser = *parserptr;
	if(!parser) return;
	if(parser->JSONParser) {
		yajl_free(parser->JSONParser); parser->JSONParser = NULL;
	}
	for(index_t i = 0; i < DEPTH_MAX; ++i) {
		SLNFilterFree(&parser->stack[i]);
	}
	assert_zeroed(parser, 1);
	FREE(parserptr); parser = NULL;
}
void SLNJSONFilterParserWrite(SLNJSONFilterParserRef const parser, strarg_t const json, size_t const len) {
	if(!parser) return;
	assertf(parser->JSONParser, "Parser in invalid state");
	(void)yajl_parse(parser->JSONParser, (unsigned char const *)json, len);
}
SLNFilterRef SLNJSONFilterParserEnd(SLNJSONFilterParserRef const parser) {
	if(!parser) return NULL;
	assertf(parser->JSONParser, "Parser in invalid state");
	yajl_status const err = yajl_complete_parse(parser->JSONParser);
	yajl_free(parser->JSONParser); parser->JSONParser = NULL;
	assertf(-1 == parser->depth, "Parser ended at invalid depth %d", parser->depth);
	SLNFilterRef const filter = parser->stack[0];
	parser->stack[0] = NULL;
	return yajl_status_ok == err ? filter : NULL;
}

SLNFilterType SLNFilterTypeFromString(strarg_t const type, size_t const len) {
	if(substr("all", type, len)) return SLNAllFilterType;
	if(substr("intersection", type, len)) return SLNIntersectionFilterType;
	if(substr("union", type, len)) return SLNUnionFilterType;
	if(substr("fulltext", type, len)) return SLNFulltextFilterType;
	if(substr("metadata", type, len)) return SLNMetadataFilterType;
	if(substr("links-to", type, len)) return SLNLinksToFilterType;
	if(substr("linked-from", type, len)) return SLNLinkedFromFilterType;
	return SLNFilterTypeInvalid;
}


// INTERNAL

static int yajl_string(SLNJSONFilterParserRef const parser, strarg_t const  str, size_t const len) {
	int const depth = parser->depth;
	if(depth < 0) return false;
	SLNFilterRef filter = parser->stack[depth];
	if(filter) {
		int const err = SLNFilterAddStringArg(filter, str, len);
		if(err) return false;
	} else {
		filter = SLNFilterCreate(SLNFilterTypeFromString(str, len));
		if(!filter) return false;
		parser->stack[depth] = filter;
		if(depth) {
			int const err = SLNFilterAddFilterArg(parser->stack[depth-1], filter);
			if(err) return false;
		}
	}
	return true;
}
static int yajl_start_array(SLNJSONFilterParserRef const parser) {
	if(parser->depth >= 0 && !parser->stack[parser->depth]) return false; // Filter where type string was expected.
	++parser->depth;
	assertf(!parser->stack[parser->depth], "Can't re-open last filter");
	if(parser->depth > DEPTH_MAX) return false;
	return true;
}
static int yajl_end_array(SLNJSONFilterParserRef const parser) {
	assertf(parser->depth >= 0, "Parser ended too many arrays");
	SLNFilterRef const filter = parser->stack[parser->depth];
	if(!filter) return false; // Empty array.
	// TODO: Validate args.
	if(parser->depth > 0) parser->stack[parser->depth] = NULL;
	--parser->depth;
	return true;
}
static yajl_callbacks const callbacks = {
	.yajl_string = (int (*)())yajl_string,
	.yajl_start_array = (int (*)())yajl_start_array,
	.yajl_end_array = (int (*)())yajl_end_array,
};

