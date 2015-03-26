var querystream = exports;

var Stream = require("stream").PassThrough;

var client = require("efs-client");
var sqlModule = require("../utilities/sql");

querystream.ongoing = function(session, ast) {
	var sql = ast.SQL(2, '\t');
	var stream = new Stream;
	session.repo.on("submission", onsubmission);
	stream.on("end", function() {
		session.repo.removeListener("submission", onsubmission);
	});
	return stream;

	function onsubmission(submission) {
		sqlModule.debug(session.db,
			'SELECT $1 IN \n'+
				sql.query+
			'AS matches', [submission.fileID].concat(sql.parameters),
			function(err, results) {
				if(err) console.log(err);
				if(!results.rows[0].matches) return;
				stream.write(client.formatEarthURI({
					algorithm: "sha1",
					hash: submission.internalHash,
				})+"\n", "utf8");
			}
		);
	}
};
querystream.history = function(session, ast, count) {
	var sql = ast.SQL(2, '\t\t');
	var results = sqlModule.debug2(session.db,
		'SELECT * FROM (\n'
			+'\t'+'SELECT "fileID", "internalHash"\n'
			+'\t'+'FROM "files"\n'
			+'\t'+'WHERE "fileID" IN\n'
				+sql.query
			+'\t'+'ORDER BY "fileID" DESC\n'
			+'\t'+'LIMIT $1\n'
		+') x ORDER BY "fileID" ASC',
		[count].concat(sql.parameters));
	return wrap(results);
};
querystream.paginated = function(session, ast, offset, limit) {
	var sql = ast.SQL(3, '\t');
	var results = sqlModule.debug2(session.db,
		'SELECT "fileID", "internalHash\n'
		+'FROM "files"\n'
		+'WHERE "fileID" IN\n'
			+sql.query
		+'ORDER BY "fileID" ASC\n'
		+'OFFSET $1\n'
		+'LIMIT $2',
		[offset, limit].concat(sql.parameters));
	return wrap(results);
};

function wrap(results) {
	var stream = new Stream;
	results.on("row", onrow);
	results.on("end", onend);
	results.on("error", onerror);
	stream.on("end", function() {
		results.removeListener("row", onrow);
		results.removeListener("end", onend);
		results.removeListener("error", onerror);
	});
	return stream;

	function onrow(row) {
		stream.write(client.formatEarthURI({
			algorithm: "sha1",
			hash: row.internalHash,
		})+"\n", "utf8");
	}
	function onend() {
		stream.end();
	}
	function onerror(err) {
		stream.emit("error", err);
	}
}

