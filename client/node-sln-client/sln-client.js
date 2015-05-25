// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

var sln = exports;

var fs = require("fs");
var http = require("http");
var qs = require("querystring");
var PassThroughStream = require("stream").PassThrough;
var urlmodule = require("url");

function has(obj, prop) {
	return Object.prototype.hasOwnProperty.call(obj, prop);
}

// returns: { algo: string, hash: string, query: string, fragment: string }
sln.parseURI = function(uri) {
	var x = /hash:\/\/([\w\d.-]+)\/([\w\d_-]+)(\?[\w\d%=_-]*)?(#[\w\d_-])?/.exec(uri);
	if(!x) return null;
	return {
		algo: x[1],
		hash: x[2],
		query: x[3] || null,
		fragment: x[4] || null,
	};
};
// obj: { algo: string, hash: string, query: string, fragment: string }
sln.formatURI = function(obj) {
	if(!obj.algo) return null;
	if(!obj.hash) return null;
	return "hash://"+
		obj.algo+
		"/"+obj.hash+
		(obj.query||"")+
		(obj.fragment||"");
};
//sln.createHasher = function() {};
//sln.Hasher = Hasher;

var mainRepo = undefined;
sln.mainRepoOptional = function() {
	if(undefined === mainRepo) {
		try {
			// TODO: Suitable config location depending on platform.
			var config = "~/.config/stronglink/client.json".replace(/^~/, process.env.HOME);
			var info = JSON.parse(fs.readFileSync(config, "utf8"));
			if("string" !== typeof info.url) throw new Error("Invalid URL");
			if("string" !== typeof info.session) throw new Error("Invalid session");
			mainRepo = new Repo(info.url, info.session);
		} catch(e) {
			mainRepo = null;
		}
	}
	return mainRepo;
};
sln.mainRepo = function() {
	var repo = sln.mainRepoOptional();
	if(repo) return repo;
	console.error("Error: no StrongLink repository configured");
	// TODO: We should have a simple script that the user can run.
	process.exit(1);
};
sln.createRepo = function(url, session) {
	return new sln.Repo(url, session);
};
sln.Repo = Repo;

function Repo(url, session) {
	var obj = urlmodule.parse(url);
	this.hostname = obj.hostname;
	this.port = obj.port;
	this.path = obj.path;
	this.session = session;
	this.client = http; // TODO

	if("/" === this.path.slice(-1)) this.path = this.path.slice(0, -1);
}
Repo.prototype.info = function(cb) {
	// TODO
	process.nextTick(function() {
		cb(null, {
			reponame: null,
			username: null,
		});
	});
};

// opts: { lang: string, start: string, count: number, wait: bool, agent: http.Agent }
// cb: err: Error, uri: string
Repo.prototype.query = function(query, opts, cb) {
	if(!opts) opts = {};
	if(!has(opts, "count")) opts.count = 50;
	if(!has(opts, "wait")) opts.wait = false;
	if(!has(opts, "agent")) opts.agent = undefined;
	var stream = this.createQueryStream(query, opts);
	var URIs = [];
	stream.on("data", function(URI) {
		URIs.push(URI);
	});
	stream.on("end", function() {
		cb(null, URIs);
	});
	stream.on("error", function(err) {
		cb(err, null);
	});
};
// opts: { lang: string, start: string, count: number, wait: bool, agent: http.Agent }
// returns: stream.Readable (object mode, emits uri: string)
Repo.prototype.createQueryStream = function(query, opts) {
	var repo = this;
	// TODO: Use POST, accept non-string queries.
	var req = repo.client.get({
		hostname: repo.hostname,
		port: repo.port,
		path: repo.path+"/sln/query?"+qs.stringify({
			"q": query,
			"lang": opts ? opts.lang : "",
			"start": opts ? opts.start : "",
			"count": opts ? opts.count : "",
			"wait": !opts || false !== opts.wait ? "1" : "0",
		}),
		headers: {
			"Cookie": "s="+repo.session,
		},
		agent: opts && has(opts, "agent") ? opts.agent : false,
	});
	return new URIListStream({ meta: false, req: req });
};
// opts: { start: string, count: number, wait: bool, agent: http.Agent }
// returns: stream.Readable (object mode, emits { uri: string, target: string })
Repo.prototype.createMetafilesStream = function(opts) {
	var repo = this;
	var req = repo.client.get({
		hostname: repo.hostname,
		port: repo.port,
		path: repo.path+"/sln/metafiles?"+qs.stringify({
			"start": opts ? opts.start : "",
			"count": opts ? opts.count : "",
			"wait": !opts || false !== opts.wait ? "1" : "0",
		}),
		headers: {
			"Cookie": "s="+repo.session,
		},
		agent: opts && has(opts, "agent") ? opts.agent : false,
	});
	return new URIListStream({ meta: true, req: req });
};
// opts: { accept: string, encoding: string }
// cb: err: Error, { data: Buffer/string, type: string }
Repo.prototype.getFile = function(uri, opts, cb) {
	var repo = this;
	var req = repo.createFileRequest(uri, opts);
	req.on("response", function(res) {
		if(200 !== res.statusCode) return cb(new Error("Status "+res.statusCode), null);
		var parts = [];
		var enc = opts && has(opts, "encoding") ? opts.encoding : null;
		if(enc) res.setEncoding(enc);
		res.on("data", function(chunk) {
			parts.push(chunk);
		});
		res.on("end", function() {
			cb(null, {
				type: res.headers["content-type"],
				data: enc ? parts.join("") : Buffer.concat(parts),
			});
		});
	});
	req.on("error", function(err) {
		cb(err, null);
	});
};
// opts: { method: string, accept: string }
// returns: http.ClientRequest
Repo.prototype.createFileRequest = function(uri, opts) {
	var repo = this;
	var obj = sln.parseURI(uri);
	if(!obj) throw new Error("Bad URI "+uri);
	var req = repo.client.get({
		method: opts && has(opts, "method") ? opts.method : "GET",
		hostname: repo.hostname,
		port: repo.port,
		path: repo.path+"/sln/file/"+obj.algo+"/"+obj.hash,
		headers: {
			"Cookie": "s="+repo.session,
			"Accept": opts && has(opts, "accept") ? opts.accept : "*",
		},
	});
	return req;
};
Repo.prototype.getMeta = function(uri, cb) {
	throw new Error("Not implemented");

	// TODO: This is just completely wrong.
	// 1. We should expose a reusable meta-file parser
	// 2. /sln/meta/* returns meta-data in JSON, not an individual meta-file

/*	var repo = this;
	var req = repo.client.get({
		hostname: repo.hostname,
		port: repo.port,
		path: repo.path+"/sln/meta/"+obj.algo+"/"+obj.hash,
		headers: {
			"Cookie": "s="+repo.session,
		},
	});
	req.on("response", function(res) {
		var str = "";
		res.setEncoding("utf8");
		res.on("readable", function() {
			str += res.read();
		});
		res.on("end", function() {
			var x = /^([^\r\n]*)[\r\n]/.exec(str);
			if(!x) return cb(new Error("Invalid meta-file"), null);
			if(!x[1].length) return cb(new Error("Invalid meta-file"), null);
			var obj;
			try { obj = JSON.parse(str.slice(x[0].length)); }
			catch(err) { cb(err, null); }
			cb(null, obj);
		});
	});
	req.on("error", function(err) {
		cb(err, null);
	});*/
};

// opts: (none)
Repo.prototype.submitFile = function(buf, type, opts, cb) {
	var repo = this;
	var stream;
	try { stream = repo.createSubmissionStream(type, opts); }
	catch(err) { process.nextTick(function() { cb(err, null); }); }
	stream.on("submission", function(obj) {
		cb(null, obj);
	});
	stream.on("error", function(err) {
		cb(err, null);
	});
	stream.end(buf);
};
// opts: (none)
// returns: stream.Writable (emits "submission": { location: string })
Repo.prototype.createSubmissionStream = function(type, opts) {
	var repo = this;
	var req = repo.client.request({
		method: "POST",
		hostname: repo.hostname,
		port: repo.port,
		path: repo.path+"/sln/file",
		headers: {
			"Cookie": "s="+repo.session,
			"Content-Type": type,
		},
	});
	var stream = new PassThroughStream();
	stream.pipe(req);
	req.on("response", function(res) {
		if(201 == res.statusCode) {
			stream.emit("submission", {
				location: res.headers["x-location"],
			});
		} else {
			var err = new Error("Status code "+res.statusCode);
			err.code = res.statusCode;
			stream.emit("error", err);
		}
		res.resume(); // Drain
	});
	req.on("error", function(err) {
		stream.emit("error", err);
	});
	return stream;
};
// opts: (none)
Repo.prototype.submitMeta = function(uri, meta, opts, cb) {
	if("string" !== typeof uri) throw new Error("Invalid URI");
	if(0 === uri.length) throw new Error("Invalid URI");
	// TODO: Validate `meta`
	var repo = this;
	var type = "application/vnd.stronglink.meta";
	var stream;
	try { stream = repo.createSubmissionStream(type, opts); }
	catch(err) { process.nextTick(function() { cb(err, null); }); }
	stream.end(uri+"\n\n"+JSON.stringify(meta, null, "    "), "utf8");
	stream.on("submission", function(obj) {
		cb(null, obj);
	});
	stream.on("error", function(err) {
		cb(err, null);
	});
};




var util = require("util");
var TransformStream = require("stream").Transform;
var StringDecoder = require('string_decoder').StringDecoder;

// opts: { req: http.ClientRequest }
function URIListStream(opts) {
	var stream = this;
	TransformStream.call(stream, { readableObjectMode: true });
	stream._buffer = "";
	stream._decoder = new StringDecoder("utf8");
	stream._meta = opts ? !!opts.meta : false;

	if(opts && opts.req) {
		opts.req.on("response", function(res) {
			if(200 == res.statusCode) {
				res.pipe(stream);
			} else {
				var err = new Error("Status code "+res.statusCode);
				err.statusCode = res.statusCode;
				stream.emit("error", err);
			}
		});
		opts.req.on("error", function(err) {
			stream.emit("error", err);
		});
	}
}
util.inherits(URIListStream, TransformStream);

URIListStream.prototype._transform = function(chunk, encoding, cb) {
	// TODO: What to do with `encoding`?
	this._buffer += this._decoder.write(chunk);
	var x;
	for(;;) {
		x = /^([^\r\n]*)[\r\n]/.exec(this._buffer);
		if(!x) break;
		this._buffer = this._buffer.slice(x[0].length);
		if(!x[1].length) continue;
		if('#' === x[1][0]) continue; // Comment line.
		if(!this._meta) {
			this.push(x[1]);
		} else {
			x = /^(.*)\s*->\s*(.*)$/.exec(x[1]);
			if(!x) return cb(new Error("Parse error"));
			this.push({ uri: x[1], target: uri[2] });
		}
	}
	cb(null);
};
URIListStream.prototype._flush = function(cb) {
	// We ignore incomplete trailing data.
	cb(null);
};

