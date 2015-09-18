// Copyright 2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include "../http/HTTP.h"
#include "../StrongLink.h"

typedef struct RSSServer *RSSServerRef;

int RSSServerCreate(SLNRepoRef const repo, RSSServerRef *const out);
void RSSServerFree(RSSServerRef *const rssptr);

int RSSServerDispatch(RSSServerRef const rss, SLNSessionRef const session, HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers);

