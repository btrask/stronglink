

var urlModule = require("url");
var repo;
var bcrypt;
var FORBIDDEN;
var cookie;
var crypto;
var bcrypt;

var O_NONE = 0;
var O_RDONLY = 1 << 0;
var O_WRONLY = 1 << 1;
var O_RDWR = O_RDONLY | O_WRONLY;

function has() {}

function auth(req, res, mode, callback/* (err, userID) */) {
	var opts = urlModule.parse(req.url, true).query;
	if(has(opts, "u") && has(opts, "p")) {
		var username = opts["u"];
		var password = opts["p"];
		var remember = has(opts, "r") && opts["r"];
		return authUser(req, res, username, password, remember, mode, callback);
	}
	var cookies = cookie.parse(req.headers["cookie"]);
	var match = /(\d+):(.*)+/.exec(cookies["s"] || "");
	if(match) {
		var sessionID = match[1];
		var session = match[2];
		return authSession(req, res, sessionID, session, mode, callback);
	}
	authPublic(req, res, mode, callback);
}
function authUser(req, res, username, password, remember, mode, callback/* (err, userID) */) {
	repo.db.query(
		'SELECT "userID", "passwordHash", "tokenHash" FROM "users" WHERE "username" = $1',
		[username],
		function(err, results) {
			var row = results.rows[0];
			if(err) return res.sendError(err);
			if(results.rows.length < 1) return res.sendError(FORBIDDEN);
			if(bcrypt.compareSync(password, row.passwordHash)) {
				return createSession(row.userID, O_RDWR, remember, function(err, cookie) {
					if(err) return res.sendError(err);
					res.setHeader("Cookie", cookie);
					callback(null, row.userID);
				});
			}
			if((mode & O_RDONLY) !== mode) return res.sendError(FORBIDDEN);
			if(bcrypt.compareSync(password, row.tokenHash)) {
				return createSession(row.userID, O_RDONLY, remember, function(err, cookie) {
					if(err) return res.sendError(err);
					res.setHeader("Cookie", cookie);
					callback(null, row.userID);
				});
			}
			res.sendError(FORBIDDEN);
		}
	);
}
function authSession(req, res, sessionID, session, mode, callback/* (err, userID) */) {
	repo.db.query(
		'SELECT "sessionHash", "userID", "modeRead", "modeWrite" FROM "sessions"'+
		' WHERE "sessionID" = $1 AND "sessionTime" > NOW() - INTERVAL \'14 days\'',
		[sessionID],
		function(err, results) {
			var row = results.rows[0];
			var sessionMode = 
				(row.modeRead ? O_RDONLY : 0) |
				(row.modeWrite ? O_WRONLY : 0);
			if(err) return res.sendError(err);
			// TODO: If FORBIDDEN, tell the client to clear the cookie?
			// Not in the case of insufficient mode though.
			if(results.rows.length < 1) return res.sendError(FORBIDDEN);
			if((mode & sessionMode) !== mode) return res.sendError(FORBIDDEN);
			if(bcrypt.compareSync(session, row.sessionHash)) {
				return callback(null, row.userID);
			}
			res.sendError(FORBIDDEN);
		}
	);
}
function authPublic(req, res, mode, callback/* (err, userID) */) {
	var PUBLIC_MODE = O_RDONLY; // TODO: Configurable.
	if((mode & PUBLIC_MODE) !== mode) return res.sendError(FORBIDDEN);
	callback(null, 0);
}

function createSession(userID, mode, remember, callback/* (err, cookie) */) {
	crypto.randomBytes(20, function(err, buffer) {
		var session = buffer.toString("base64");
		var sessionHash = bcrypt.hashSync(session, 10);
		repo.db.query(
			'INSERT INTO "sessions" ("sessionHash", "userID", "modeRead", "modeWrite")'+
			' VALUES ($1, $2, $3, $4) RETURNING "sessionID"',
			[sessionHash, userID, Boolean(mode & O_RDONLY), Boolean(mode & O_WRONLY)],
			function(err, result) {
				var row = result.rows[0];
				if(err) return callback(err, null);
				callback(null, cookie.serialize("s", row.sessionID+":"+session, {
					httpOnly: true,
					secure: true,
					maxAge: remember ? 14 * 24 * 60 * 60 : 0,
				}));
			}
		);
	});
}



