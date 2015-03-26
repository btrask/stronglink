SELECT mft.docid, mft.meta_file_id, f.file_id
FROM fulltext AS ft
JOIN meta_fulltext AS mft ON (mft.docid = ft.docid)
JOIN meta_files AS mf ON (mf.meta_file_id = mft.meta_file_id)
JOIN file_uris AS f ON (f.uri = mf.target_uri)
WHERE ft.value MATCH "obscure"
ORDER BY ft.docid DESC



