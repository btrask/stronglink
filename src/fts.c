#include <assert.h>
#include <stdlib.h>
#include "fts.h"

#define thread_local __thread // TODO

void sqlite3Fts3PorterTokenizerModule(
	sqlite3_tokenizer_module const**ppModule
);

static thread_local sqlite3_tokenizer_module const *_fts = NULL;
static thread_local sqlite3_tokenizer *_tokenizer = NULL;

void fts_get(sqlite3_tokenizer_module const **const fts, sqlite3_tokenizer **const tokenizer) {
	if(!_fts) {
		sqlite3Fts3PorterTokenizerModule(&_fts);
		assert(fts);
		int rc = _fts->xCreate(0, NULL, &_tokenizer);
		assert(SQLITE_OK == rc);
	}
	*fts = _fts;
	*tokenizer = _tokenizer;
}

