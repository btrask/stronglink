# StrongLink Client API Information

**GET /sln/file/[algo]/[hash]**  
Returns the data of a given file. Clients should be aware this is arbitrary user data and potentially malicious.

Planned features: content negotiation, range requests

Implementation status: working but incomplete

**GET /sln/meta/[algo]/[hash]**  
Returns the current, cummulative meta-data snapshot of a given file in JSON format.

Implementation status: not implemented

**POST /sln/file**  
Submits a file (meta-file or regular file). Responds with 201 Created on success. The response header `X-Location` gives the hash link of the file.

Implementation status: working

**GET /sln/query**  
Returns a URI list of files that match a given query.

Parameters:
- `q`: the query string
- `lang`: language of the query string (not implemented)
- `wait`: use long-polling to notify of new submissions (default true)
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

Long-polling APIs will send a blank line every minute or less during idle to keep the connection open.

