SELECT f."fileID"
FROM "files" AS f
LEFT JOIN "fileHashes" AS fh ON (fh."fileID" = f."fileID")
LEFT JOIN "hashes" AS h ON (h."hashID" = fh."hashID")
LEFT JOIN "fileIndexes" AS i ON (i."fileID" = f."fileID")
LEFT JOIN "fileLinks" AS l ON (l."fileID" = f."fileID")
LEFT JOIN "submissions" AS s ON (s."fileID" = f."fileID")
LEFT JOIN "targets" AS t ON (t."submissionID" = s."submissionID")
WHERE

--query.Source
(s."userID" = $x)
(f."fileID" IN (SELECT "fileID" FROM "submissions" WHERE "userID" = $x))

--query.Target
(t."userID" = $x)
(f."fileID" IN (SELECT s."fileID" FROM "submissions" AS s INNER JOIN "targets" AS t ON (t."submissionID" = s."submissionID") WHERE t."userID" = $x))

--query.Terms
(plainto_tsquery('english', $x) = ''::tsquery OR i."index" @@ plainto_tsquery('english', $x))

--query.LinksTo
(l."normalizedURI" = $x)
	-- Add the files own hashes as `fileLinks` to itself.

--query.All
(x AND y AND z)

--query.Any
(x OR y OR z)

ORDER BY f."fileID" ASC

