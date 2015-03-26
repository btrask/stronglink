#include <stdbool.h>




static bool_t substr(str_t const *const a, str_t const *const b, size_t const blen) {
	for(off_t i = 0; i < blen; ++i) {
		if(a[i] != b[i]) return false;
		if(!a[i]) return false; // Terminated early.
	}
	if(a[i]) return false; // Terminated late.
	return true;
}
static ssize_t prefix(str_t const *const a, str_t const *const b) {
	for(off_t i = 0; ; ++i) {
		if(!a[i]) return i;
		if(a[i] != b[i]) return -1;
	}
}
static bool_t pathterm(str_t const *const URI, size_t const len) {
	char const x = URI[len];
	return '\0' == x || '?' == x || '#' == x;
}


static EFSSessionRef auth(EFSRepoRef const repo, HTTPConnectionRef const conn, HTTPMethod const method, str_t const *const qs) {
	str_t const *user = NULL;
	str_t const *pass = NULL;

	str_t const *pos = qs;
	for(;;) {
		size_t flen, vlen;
		if(!QSRead(pos, &flen, &vlen)) break;

		if(substr("u", pos+1, flen-1) && !user) {
			user = strndup(pos+flen+1, vlen-1);
		}
		if(substr("p", pos+1, flen-1) && !pass) {
			pass = strndup(pos+flen+1, vlen-1);
		}

		pos += flen + vlen;
	}

	// TODO: Cookie

	EFSSessionRef const session = EFSRepoCreateSession(repo, user, pass, NULL); // TODO: Use method/mode.
	FREE(&user);
	FREE(&pass);
	return session;
}


typedef struct {
	str_t *path;
	str_t *type;
	size_t size;
	// alternate uris?
	// tags? [we don't actually support tags, we just support links]
	// 	but we support links to tag files... we could have a table for them too
} EFSFileInfo;

EFSFileInfo *EFSSessionCopyInfoForAddress(EFSSessionRef const session, str_t const *const URI);


static bool getFile(EFSRepoRef const repo, HTTPConnectionRef const conn, HTTPMethod const method, str_t const *const URI) {
	if(HTTP_GET != method && HTTP_HEAD != method) return false;
	str_t algo[32] = {};
	str_t hash[256] = {};
	size_t pathlen = 0; // TODO: correct type for scanf %n ?
	(void)sscanf(URI, "/efs/file/%31[a-zA-Z0-9%_-]/%255[a-zA-Z0-9%_-]%n", algo, hash, &pathlen);
	if(!pathterm(URI, pathlen)) return false;
	EFSSessionRef const session = auth(repo, conn, method, URI+pathlen);
	if(!session) {
		HTTPConnectionSendStatus(conn, 403);
		return true;
	}


	return true;
}
static bool postFile(EFSRepoRef const repo, HTTPConnectionRef const conn, HTTPMethod const method, str_t const *const URI) {
	if(HTTP_POST != method) return false;
	ssize_t const pathlen = prefix("/efs/file", URI);
	if(pathlen < 0) return false;
	if(!pathterm(URI, (size_t)pathlen)) return false;
	EFSSessionRef const session = auth(repo, conn, method, URI+pathlen);
	if(!session) {
		HTTPConnectionSendStatus(conn, 403);
		return true;
	}



	return true;
}
static bool postQuery(EFSRepoRef const repo, HTTPConnectionRef const conn, HTTPMethod const method, str_t const *const URI) {
	if(HTTP_POST != method) return false;
	ssize_t const pathlen = prefix("/efs/query", URI);
	if(pathlen < 0) return false;
	if(!pathterm(URI, (size_t)pathlen)) return false;
	EFSSessionRef const session = auth(repo, conn, method, URI+pathlen);
	if(!session) {
		HTTPConnectionSendStatus(conn, 403);
		return true;
	}

	return true;
}


static void listener(EFSRepoRef const repo, HTTPConnectionRef const conn) {
	HTTPMethod const method = HTTPConnectionGetRequestMethod(conn);
	str_t const *const URI = HTTPConnectionGetRequestURI(conn)

	if(getFile(repo, conn, method, URI)) return;
	if(postFile(repo, conn, method, URI)) return;
	if(postQuery(repo, conn, method, URI)) return;

	HTTPConnectionSendStatus(conn, 404);
}



