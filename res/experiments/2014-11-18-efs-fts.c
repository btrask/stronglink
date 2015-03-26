
#define EFS_FTS_TOKEN_MAX 20
#define EFS_FTS_VAL(name) \
	uint8_t __buf_##name[DB_VARINT_MAX*3 + EFS_FTS_TOKEN_MAX]; \
	DB_val name[1] = {{ 0, __buf_##name }}
#define EFS_FTS_RANGE(name) \
	EFS_FTS_VAL(__min_##name); \
	EFS_FTS_VAL(__max_##name); \
	DB_range name[1] = {{ __min_##name, __max_##name }}


void efs_fts_bind(DB_val *const key, strarg_t const token, size_t const len, uint64_t const metaFileID, uint64_t const offset) {
	assert(0 == key->size);
	assert(NULL != key->data);
	byte_t *pos = key->data + key->size;

	pos += varint_encode(pos, SIZE_MAX, EFSTermMetaFileIDAndPosition);
	memcpy(pos, token, len);
	pos += len;
	*pos++ = '\0';
//	pos += varint_encode(pos, SIZE_MAX, metaFileID);
//	pos += varint_encode(pos, SIZE_MAX, 0); // TODO: Record offset. Requires changes to EFSFulltextFilter so that each document only gets returned once, no matter how many times the token appears within it.

	key->size = pos - key->data;
}
uint64_t efs_fts_column_metaFileID




void EFSFTSBind(DB_val *const key, strarg_t const token, size_t const len);
void EFSFTSRange(DB_range *const range); // BAD
strarg_t EFSFTSToken(DB_val *const key);
uint64_t EFSFTSMetaFileID(DB_val *const key);
uint64_t EFSFTSOffset(DB_val *const key);




void db_bind_string(DB_val *const val, strarg_t const token, size_t const len);

uint64_t db_column_next();
strarg_t db_column_next_string();








