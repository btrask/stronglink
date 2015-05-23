# StrongLink Client API Information

**GET /sln/file/[algo]/[hash]**  
Returns the data of a given file. Clients should be aware this is arbitrary user data and potentially malicious.

Planned features: content negotiation, range requests

Implementation status: working but incomplete

**GET /sln/meta/[algo]/[hash]**  
Returns the current, cummulative meta-data snapshot of a given file in JSON format.

Note: The result is *not* a meta-file. If you want a specific meta-file, look it up with `/sln/file/[algo]/[hash]`. If you want all of the meta-files for a given file, use a query (not implemented). Full text content is not returned.

Implementation status: not implemented

**POST /sln/file**  
Submits a file (meta-file or regular file). Responds with 201 Created on success. The response header `X-Location` gives the hash link of the file.

Implementation status: working

**GET /sln/query**  
Returns a URI list of files that match a given query.

Parameters:
- `q`: the query string
- `lang`: language of the query string (not implemented)
- `wait`: use long-polling to notify of new submissions (default `true`)
- `start`: starting URI for pagination (prefix with `-` for paging backwards)
- `count`: maximum number of results (not implemented)

Implementation status: working but incomplete

**POST /sln/query**  
Returns a URI list of files that match a given query. Large queries are accepted via request body in "simple" or JSON format. Use the request `Content-Type` header to indicate the query type/language.

Parameters: (same as above except `q` and `lang`)

Implementation status: not implemented

**GET /sln/metafiles**  
Returns a list of all meta-files.

Return syntax: `[meta-file URI] -> [target file URI]` each line

Parameters:
- `wait`: use long-polling to notify of new submissions (not implemented, default true)
- `start`: starting URI for pagination (prefix with `-` for paging backwards)
- `count`: maximum number of results (not implemented)

Note: No query is accepted. Clients are expected to filter the results based on target URI.

Implementation status: incomplete (wrong syntax)

**GET /sln/query-obsolete**  
DEPRECATED

## URI Lists

MIME type: `text/uri-list; charset=utf-8`

See [RFC2483 section 5](https://tools.ietf.org/html/rfc2483#section-5). One URI per line. Lines beginning with `#` are comments and ignored (must be the first character of the line). Lines are delimited with CRLF but clients should be prepared to handle CR or LF too.

By default, long-polling APIs will send a blank line every minute or less during idle to keep the connection open. This will be configurable in the future.

## Meta-files

MIME type: `application/vnd.stronglink.meta` (pending registration)
Encoding: always UTF-8

The first line is the URI of the meta-file's target. Only hash links are recognized. The URI is followed by two line breaks (a blank line).

Then follows a JSON body describing the meta-data. Each field accepts an _array_ of values. If only one value is set, it can be a single item instead of an array. For each value, currently only strings are accepted.

```
hash://[algo]/[hash]

{
	"random attribute": "single value",
	"another attribute": [
		"multiple",
		"values"
	]
}
```

The reason for storing the target URI outside of the JSON object is to enable single-pass parsing without additional buffering. During sync, StrongLink can be required to parse thousands of meta-files (or more) while the user waits, so parse performance is a high priority.

The following special fields are recognized:

- `title`: the file's title or filename (note that a file can even have multiple titles)
- `description`: user information about the file
- `tag`: tags (not implemented)
- `link`: URIs that the file provides external links to
- `embed`: URIs that the file embeds (note that these are considered dependencies when syncing; not implemented)
- `fulltext`: full text content (this is the only attribute that may _not_ have multiple values within a single meta-file; this restriction might be lifted in the future)

Additionally, some standard fields are:

- `submitter-name`: the name of the user that submitted the file
- `submitter-repo`: the name of the repository where the file was submitted
- `submission-time`: an ISO 8601 time-stamp in UTC (e.g. "2015-05-13T10:02:47Z")
- `submission-software`: the name of the application that submitted the file

Applications are free to define their own fields. Please consider whether a field name should be considered "global" or "application-specific," and prefix application-specific fields with the name of the application. The `sln.` prefix is reserved for future use.

Values from multiple meta-files with the same target are combined. Applications should be aware that any field can have zero or more values. (As mentioned above, `fulltext` is currently the sole exception, in that it can only have one value per meta-file. It can still have multiple values from independent meta-files.)

Currently meta-data values are append-only. In the future this format will be extended to indicate values to be removed.

Meta-files are always excoded as UTF-8. Line endings are recommended to be LF-only, since that's what most JSON libraries use.

