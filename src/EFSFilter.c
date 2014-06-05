#include "EarthFS.h"

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
		case EFSUnionFilter: {
			EFSFilterList *const list = filter->data.filters;
			for(index_t i = 0; i < list->count; ++i) {
				EFSFilterFree(list->items[i]); list->items[i] = NULL;
			}
			FREE(&filter->data.filters);
			break;
		} default:
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

