# StrongLink Client API Information

This interface is used for syncing and is the recommended way for applications to talk to StrongLink repositories.

**GET /sln/file/[algo]/[hash]**  
Returns the data of a given file. Clients should be aware this is arbitrary user data and potentially malicious.

As much caching as possible should be enabled for these URIs because their content is immutable. For example, StrongLink always sends an ETag of "1".

In the event of a hash collision, it's undefined which version will be returned. Server implementations are recommended to return the oldest matching file, to prevent existing files from being "overwritten."

Planned features: content negotiation, range requests

Implementation status: working but incomplete

**GET /sln/meta/[algo]/[hash]**  
Returns the current, cumulative meta-data snapshot of a given file in JSON format.

Note: The result is *not* a meta-file. If you want a specific meta-file, look it up with `/sln/file/[algo]/[hash]`. If you want all of the meta-files for a given file, use a query (`target=[URI]`). Full-text content is not returned.

In the event of a hash collision, the meta-data for one single file will be returned, rather than the combined meta-data for all matching files. Server implementations are recommended to chose the same file as returned by `/sln/file/[algo]/[hash]`.

Implementation status: not implemented

**GET /sln/alts/[algo]/[hash]**  
TODO - should return a URI list of hash URIs for a given file.

Implementation status: not implemented

**POST /sln/file**  
Submits a file (meta-file or regular file). Responds with 201 Created on success. The response header `X-Location` gives the hash link of the file.

Implementation status: working

**PUT /sln/file/[algo]/[hash]**  
Like `POST /sln/file` above except that the intended algorithm and hash are stated up front. If the hash doesn't match, the file isn't added and an error is returned (409 Conflict). If a file with the given hash already exists, then the upload doesn't have to be saved at all, which is much faster. Note that the upload still takes place, so for large files you should consider checking explicitly with `HEAD /sln/file/[algo]/[hash]`.

You should generally prefer this version if it's practical to generate the hash in advance.

Implementation status: working

**GET /sln/query**  
Returns a URI list of files that match a given query.

Parameters:
- `q`: the query string
- `lang`: language of the query string
- `wait`: use long-polling to notify of new submissions (default `true`)
- `start`: starting URI for pagination (prefix with `-` for paging backwards)
- `count`: maximum number of results
- `dir`: `a` (ascending) or `z` (descending) direction (default `a`)

Implementation status: working

**POST /sln/query**  
Returns a URI list of files that match a given query. Large queries are accepted via request body in "simple" or JSON format. Use the request `Content-Type` header to indicate the query type/language.

Parameters: (same as above except `q` and `lang`)

Implementation status: not implemented

**GET /sln/metafiles**  
Returns a URI list of all meta-files.

Return syntax: `[meta-file URI] -> [target file URI]` each line

Parameters:
- `wait`: use long-polling to notify of new submissions (default `true`)
- `start`: starting URI for pagination (prefix with `-` for paging backwards)
- `count`: maximum number of results
- `dir`: `a` (ascending) or `z` (descending) direction (default `a`)

Note: No query is accepted. Clients are expected to filter the results based on target URI.

Implementation status: working

**GET /sln/all**  
Returns a URI list of all files and meta-files.

The ordering between files and meta-files is undefined. It's valid to return meta-files before files, meta-files after files, or files and meta-files interleaved. It should still be consistent between requests so that the `start` parameter behaves predictably.

Parameters:
- `wait`: use long-polling to notify of new submissions (default `true`)
- `start`: starting URI for pagination (prefix with `-` for paging backwards)
- `count`: maximum number of results
- `dir`: `a` (ascending) or `z` (descending) direction (default `a`)

Note: No query is accepted.

Implementation status: working

**GET /sln/info**  
TODO - should return information about the repository, current user, and current session.

Implementation status: not implemented

## Query Languages

TODO - currently just a single, human-friendly syntax

## URI Lists

MIME type: `text/uri-list; charset=utf-8`

See [RFC2483 section 5](https://tools.ietf.org/html/rfc2483#section-5). One URI per line. Lines beginning with `#` are comments and ignored (must be the first character of the line). Lines are delimited with CRLF but clients should be prepared to handle CR or LF too.

By default, long-polling APIs will send a blank line every minute or less during idle to keep the connection open. This will be configurable in the future.

## Meta-files

MIME type: `application/vnd.stronglink.meta` (pending registration)
Encoding: always UTF-8

The first line is the URI of the meta-file's target. Only hash links are recognized. The URI is followed by two line breaks (a blank line). LF is recommended (it's the most common for JSON) but CRLF and CR are also recognized. By putting this field first, more efficient single-pass parsing is possible during syncing.

Then follows a JSON body describing the meta-data. Arbitrary keys are accepted. Values must be strings or objects (`{}`). _A string is equivalent to an object with the string as its only key, and an empty object as its only value._ This allows meta-files to operate as [CRDTs](https://en.wikipedia.org/wiki/Conflict-free_replicated_data_type). The current state of the meta-data of a file is the merged "sum" of all of its meta-files.

```
hash://[algo]/[hash]

{
	"field 1": "single value",
	"field 2": {
		"equivalent syntax": {}
	},
	"title": {
		"almost any field can have": {},
		"multiple values": {},
		"even the file's title": {}
	},
	"fulltext": "fulltext is the only exception (must be a single string)",
	"nesting test": {
		"first level of nesting - ok": {
			"deeper nesting - not supported yet": {}
		}
	}
}
```

Note: currently only one level of nesting is supported. A nesting limit of at least 8 levels (and possibly much more) is planned. Deeper values are persisted but ignored and cannot be queried on.

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
- `source-path`: the local path where the file was imported from
- `source-uri`: the URI where the file was imported from

Applications are free to define their own fields. Please consider whether a field name should be considered "global" or "application-specific," and prefix application-specific fields with the name of the application. The `sln` top-level field is reserved for future use.

Values from multiple meta-files with the same target are combined. Applications should be aware that any field can have zero or more values. (As mentioned above, `fulltext` is currently the sole exception, in that it can only have one value per meta-file. It can still have multiple values from independent meta-files.)

Although fields can contain multiple values, it's strongly recommended that applications define field names as singular terms (e.g. "hashtag" rather than "hashtags"). This is so that queries make more sense (e.g. `hashtag=[tag]` rather than `hashtags=[tag]`).

Meta-data values are append-only. Mutability can be built on top using [CRDT](https://en.wikipedia.org/wiki/Conflict-free_replicated_data_type) structures.

Meta-files are always encoded as UTF-8. Line endings are recommended to be LF-only, since that's what most JSON libraries use.

Currently the maximum meta-file size is capped at 1MB. Data after that point will be preserved but ignored and cannot be be queried on. This may be made a configurable setting.

## The default repository and authorization

It's generally expected that the user have a default repository configured, and tools should use it when appropriate. All APIs accept an optional session key for authorization (and without it, you might not be able to do anything, depending on configuration).

These are specified by a config file in JSON:

```json
{
	"repos": {
		"main": {
			"url": "http://localhost:8000/",
			"session": "[...]"
		}
	}
}
```

On Unix-like systems this file is located at `~/.config/stronglink/client.json`. On Windows its location is TBD.

The URI path is significant. For example, if the path is `/`, the API end-point is `/sln/`. If the path is `/example/`, the end-point is `/example/sln/`. This allows more flexibility in hosting.

The session key should be sent via the HTTP `Cookie` header:

```
Cookie: s=[...]
```

There currently is no convenient way for users or applications to generate session keys. This is a known issue. Applications should avoid asking for usernames and passwords. Something like OAuth will eventually be implemented.

Sessions can have different permission levels, including read-only and read-write. This functionality is currently quite limited.

An additional CSRF token will eventually be required for requests that change server state.

