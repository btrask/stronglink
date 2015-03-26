
typedef struct HeaderList* HeaderListRef;

typedef struct {
	str_t *field;
	str_t *value;
} Header;
struct HeaderList {
	count_t count;
	count_t size;
	Header *items;
};

HeaderListRef HeaderListCreate(void) {
	HeaderListRef const list = calloc(1, sizeof(struct HeaderList));
	return list;
}
void HeaderListFree(HeaderListRef const list) {
	if(!list) return;
	for(index_t i = 0; i < list->count; ++i) {
		FREE(&list->items[i].field);
		FREE(&list->items[i].value);
	}
	FREE(&list->items);
	free(list);
}
err_t HeaderListAdd(HeaderListRef const list, strarg_t const field, strarg_t const value) {
	if(!list) return 0;
	if(++list->count > list->size) {
		list->size = MAX(10, list->size * 2);
		list->items = realloc(list->items, list->size * sizeof(Header));
		if(!list->items) return -1;
		memset(list->items[list->count], 0, (list->size - list->count) * sizeof(Header));
	}
	list->items[list->count-1].field = strdup(field);
	list->items[list->count-1].value = strdup(value);
	return 0;
}
strarg_t HeaderListGet(HeaderListRef const list, strarg_t const field) {
	if(!list) return NULL;
	for(index_t i = 0; i < list->count; ++i) {
		if(0 == strcmp(field, list->items[i].field)) return list->items[i].value;
	}
	return NULL;
}



