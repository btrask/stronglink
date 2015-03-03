
struct EFSPush {
	uint64_t pushID;
	EFSSessionRef session;
	str_t *host;
	str_t *username;
	str_t *password;
	str_t *cookie;
	str_t *query;

	HTTPConnectionRef conn;
	HTTPMessageRef msg;

	EFSFilterRef filter;

};



EFSPushRef EFSRepoCreatePush(EFSRepoRef const repo, uint64_t const pullID, uint64_t const userID, strarg_t const host, strarg_t const username, strarg_t const password, strarg_t const cookie, strarg_t const query) {


}
void EFSPushFree() {}


static int head(EFSPushRef const push, HTTPConnectionRef const conn, strarg_t const URI) {
	HTTPMessageRef const head = HTTPMessageCreate(conn);
	if(!head) return -1;
	HTTPMessageWriteRequest(head, HTTP_HEAD, "/efs/file/asdf/asdf", push->host);
	HTTPMessageBeginBody(head);
	HTTPMessageEnd(head);
	return HTTPMessageGetResponseStatus(head);
}
static err_t post(EFSPushRef const push, HTTPConnectionRef const conn, strarg_t const URI) {
	EFSFileInfo info[1];
	err_t rc = EFSSessionGetFileInfo(push->session, URI, info);
	if(rc < 0) return rc;
	
}
static err_t submit(EFSPushRef const push, HTTPConnectionRef const conn, strarg_t const URI) {
	int const status = head(push, conn, URI);
	if(status < 0) return status;
	if(status > 0) return 0; // Already submitted.
	return post(push, conn, URI);
	// Not going to work, thundering herd.
}
static err_t connect() {
	
}
err_t EFSPushStart(EFSPushRef const push) {

}




