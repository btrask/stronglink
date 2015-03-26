CREATE TEMPORARY VIEW x AS
SELECT f.file_id AS file_id, MIN(mf.file_id) AS age
FROM fulltext AS ft
INNER JOIN meta_fulltext AS mft
	ON (mft.docid = ft.docid)
INNER JOIN meta_files AS mf
	ON (mf.meta_file_id = mft.meta_file_id)
INNER JOIN file_uris AS f
	ON (f.uri = mf.target_uri)
WHERE ft.value MATCH 'slow'
GROUP BY f.file_id


select *
from x
inner join y
on x.file_id = y.file_id


CREATE TEMPORARY VIEW y AS
SELECT f.file_id AS file_id, MIN(mf.file_id) AS age
FROM fulltext AS ft
INNER JOIN meta_fulltext AS mft
	ON (mft.docid = ft.docid)
INNER JOIN meta_files AS mf
	ON (mf.meta_file_id = mft.meta_file_id)
INNER JOIN file_uris AS f
	ON (f.uri = mf.target_uri)
WHERE ft.value MATCH 'fast'
GROUP BY f.file_id

