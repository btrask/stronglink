#include <yajl/yajl_parse.h>
#include "../StrongLink.h"

#define DEPTH_MAX 5

struct EFSJSONFilterParser {
	yajl_handle JSONParser;
	EFSFilterRef stack[DEPTH_MAX];
	int depth;
};

static yajl_callbacks const callbacks;

EFSJSONFilterParserRef EFSJSONFilterParserCreate(void) {
	EFSJSONFilterParserRef parser = calloc(1, sizeof(struct EFSJSONFilterParser));
	if(!parser) return NULL;
	parser->JSONParser = yajl_alloc(&callbacks, NULL, parser);
	parser->depth = -1;
	return parser;
}
void EFSJSONFilterParserFree(EFSJSONFilterParserRef *const parserptr) {
	EFSJSONFilterParserRef parser = *parserptr;
	if(!parser) return;
	if(parser->JSONParser) {
		yajl_free(parser->JSONParser); parser->JSONParser = NULL;
	}
	for(index_t i = 0; i < DEPTH_MAX; ++i) {
		EFSFilterFree(&parser->stack[i]);
	}
	assert_zeroed(parser, 1);
	FREE(parserptr); parser = NULL;
}
void EFSJSONFilterParserWrite(EFSJSONFilterParserRef const parser, strarg_t const json, size_t const len) {
	if(!parser) return;
	assertf(parser->JSONParser, "Parser in invalid state");
	(void)yajl_parse(parser->JSONParser, (unsigned char const *)json, len);
}
EFSFilterRef EFSJSONFilterParserEnd(EFSJSONFilterParserRef const parser) {
	if(!parser) return NULL;
	assertf(parser->JSONParser, "Parser in invalid state");
	yajl_status const err = yajl_complete_parse(parser->JSONParser);
	yajl_free(parser->JSONParser); parser->JSONParser = NULL;
	assertf(-1 == parser->depth, "Parser ended at invalid depth %d", parser->depth);
	EFSFilterRef const filter = parser->stack[0];
	parser->stack[0] = NULL;
	return yajl_status_ok == err ? filter : NULL;
}

EFSFilterType EFSFilterTypeFromString(strarg_t const type, size_t const len) {
	if(substr("all", type, len)) return EFSAllFilterType;
	if(substr("intersection", type, len)) return EFSIntersectionFilterType;
	if(substr("union", type, len)) return EFSUnionFilterType;
	if(substr("fulltext", type, len)) return EFSFulltextFilterType;
	if(substr("metadata", type, len)) return EFSMetadataFilterType;
	if(substr("links-to", type, len)) return EFSLinksToFilterType;
	if(substr("linked-from", type, len)) return EFSLinkedFromFilterType;
	return EFSFilterTypeInvalid;
}


// INTERNAL

static int yajl_string(EFSJSONFilterParserRef const parser, strarg_t const  str, size_t const len) {
	int const depth = parser->depth;
	if(depth < 0) return false;
	EFSFilterRef filter = parser->stack[depth];
	if(filter) {
		int const err = EFSFilterAddStringArg(filter, str, len);
		if(err) return false;
	} else {
		filter = EFSFilterCreate(EFSFilterTypeFromString(str, len));
		if(!filter) return false;
		parser->stack[depth] = filter;
		if(depth) {
			int const err = EFSFilterAddFilterArg(parser->stack[depth-1], filter);
			if(err) return false;
		}
	}
	return true;
}
static int yajl_start_array(EFSJSONFilterParserRef const parser) {
	if(parser->depth >= 0 && !parser->stack[parser->depth]) return false; // Filter where type string was expected.
	++parser->depth;
	assertf(!parser->stack[parser->depth], "Can't re-open last filter");
	if(parser->depth > DEPTH_MAX) return false;
	return true;
}
static int yajl_end_array(EFSJSONFilterParserRef const parser) {
	assertf(parser->depth >= 0, "Parser ended too many arrays");
	EFSFilterRef const filter = parser->stack[parser->depth];
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

