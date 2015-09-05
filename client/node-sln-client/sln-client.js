// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

var sln = exports;

var crypto = require("crypto");
var fs = require("fs");
var http = require("http");
var https = require("https");
var qs = require("querystring");

var PassThroughStream = require("stream").PassThrough;
var urlmodule = require("url");

function has(obj, prop) {
	return Object.prototype.hasOwnProperty.call(obj, prop);
}

sln.metatype = "application/vnd.stronglink.meta";

// returns: { algo: string, hash: string, query: string, fragment: string }
sln.parseURI = function(uri) {
	if("string" !== typeof uri) throw new TypeError("Invalid URI");
	var x = /^hash:\/\/([\w\d.-]+)\/([\w\d.%_-]+)(\?[\w\d.%_=&-]+)?(#[\w\d.%_-]+)?$/i.exec(uri);
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

// TODO
//sln.createHasher = function() {};
//sln.Hasher = Hasher;

var CONFIG = null;
var REPOS = {};
sln.loadConfig = function() {
	var path = "~/.config/stronglink/client.json".replace(/^~/, process.env.HOME);
	CONFIG = JSON.parse(fs.readFileSync(path, "utf8"));
	REPOS = {};
};
sln.repoForNameOptional = function(name) {
	if(!name) return null;
	if(has(REPOS, name)) return REPOS[name];
	REPOS[name] = null;
	if(!CONFIG) {
		try { sln.loadConfig(); }
		catch(e) {}
	}
	if(!CONFIG || !has(CONFIG, "repos") || !has(CONFIG["repos"], name)) {
		// TODO: Better parsing, handle bare domains.
		var parsed = urlmodule.parse(name);
		if(!parsed || !parsed.hostname) return null;
		REPOS[name] = new Repo(name, null);
		return REPOS[name];
	}
	var obj = CONFIG["repos"][name];
	if("string" !== typeof obj["url"]) return null;
	if("string" !== typeof obj["session"] && obj["session"]) return null;
	REPOS[name] = new Repo(obj["url"], obj["session"] || null);
	return REPOS[name];
};
sln.repoForName = function(name) {
	if(!name) {
		console.error("Error: no repository name specified");
		process.exit(1);
	}
	var repo = sln.repoForNameOptional(name);
	if(!repo) {
		console.error("Error: no StrongLink repository configured for '"+name+"'");
		// TODO: We should have a simple script that the user can run.
		process.exit(1);
	}
	return repo;
};
sln.createRepo = function(url, session) {
	return new sln.Repo(url, session);
};
sln.Repo = Repo;

function Repo(url, session) {
	var obj = urlmodule.parse(url);
	var repo = this;
	repo.hostname = obj.hostname;
	repo.port = obj.port;
	repo.path = obj.pathname; // pathname excludes query string
	repo.session = session;
	repo.cookie = session ? "s="+session : "";
	repo.protocol = "https:" == obj.protocol ? https : http;
	repo.agent = new repo.protocol.Agent({
		keepAlive: true,
		keepAliveMsecs: 1000 * 30,
	});

	if("/" === repo.path.slice(-1)) repo.path = repo.path.slice(0, -1);
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

// opts: { lang: string, start: string, count: number, wait: bool }
// cb: err: Error, URIs: array
Repo.prototype.query = function(query, opts, cb) {
	if(!opts) opts = {};
	if(!has(opts, "count")) opts.count = 50;
	if(!has(opts, "wait")) opts.wait = false;
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
// opts: { lang: string, start: string, count: number, wait: bool, dir: string }
// returns: stream.Readable (object mode, emits uri: string)
Repo.prototype.createQueryStream = function(query, opts) {
	var repo = this;
	// TODO: Use POST, accept non-string queries.
	var req = repo.protocol.get({
		hostname: repo.hostname,
		port: repo.port,
		path: repo.path+"/sln/query?"+qs.stringify({
			"q": query,
			"lang": opts ? opts.lang : "",
			"start": opts ? opts.start : "",
			"count": opts ? opts.count : "",
			"wait": !opts || false !== opts.wait ? "1" : "0",
			"dir": opts ? opts.dir : "",
		}),
		headers: {
			"Cookie": repo.cookie,
		},
		agent: repo.agent,
	});
	return new URIListStream({ meta: false, req: req });
};
// opts: { start: string, count: number, wait: bool, dir: string }
// returns: stream.Readable (object mode, emits { uri: string, target: string })
Repo.prototype.createMetafilesStream = function(opts) {
	var repo = this;
	var req = repo.protocol.get({
		hostname: repo.hostname,
		port: repo.port,
		path: repo.path+"/sln/metafiles?"+qs.stringify({
			"start": opts ? opts.start : "",
			"count": opts ? opts.count : "",
			"wait": !opts || false !== opts.wait ? "1" : "0",
			"dir": opts ? opts.dir : "",
		}),
		headers: {
			"Cookie": repo.cookie,
		},
		agent: repo.agent,
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
	var req = repo.protocol.get({
		method: opts && has(opts, "method") ? opts.method : "GET",
		hostname: repo.hostname,
		port: repo.port,
		path: repo.path+"/sln/file/"+obj.algo+"/"+obj.hash,
		headers: {
			"Cookie": repo.cookie,
			"Accept": opts && has(opts, "accept") ? opts.accept : "*",
		},
		agent: repo.agent,
	});
	return req;
};
// opts: (none)
// cb: err: Error, obj: Object
Repo.prototype.getMeta = function(uri, opts, cb) {
	// TODO: This implementation is a hack until server-side
	// support for `GET /sln/meta/[algo]/[hash]` is added.
	var repo = this;
	var reqopts = {
		"accept": sln.metatype,
		"encoding": "utf8",
	};
	// TODO: We shouldn't be munging strings in the user query language.
	// Use the JSON language instead, once it's properly supported.
	var stream = repo.createQueryStream("target='"+uri+"'", { wait: false });
	var dst = {};
	stream.on("data", function(metaURI) {

		// This ordering is not strictly necessary (since CRDTs can be
		// combined in any order), but it lets us ensure "end" is
		// emitted after all of the work is done, and it keeps us from
		// flooding the agent pool.
		stream.pause();

		repo.getFile(metaURI, reqopts, function(err, obj) {
			// TODO: If the result is Not Acceptable, just continue.
			if(err) return cb(err, null); // TODO: drain stream
			var json = /[\r\n][^]*$/.exec(obj.data)[0]; // TODO?
			var src = JSON.parse(json);
			delete src["fulltext"]; // Not supported by this API.
			merge(src, dst);
			stream.resume();
		});
	});
	stream.on("end", function() {
		cb(null, dst);
	});
	function merge(src, dst) {
		for(var x in src) if(has(src, x)) {
			if(!has(dst, x)) dst[x] = {};
			if("string" === typeof src[x]) {
				dst[x][src[x]] = {};
			} else {
				merge(src[x], dst[x]);
			}
		}
	}
};

// opts: { uri: string }
Repo.prototype.submitFile = function(buf, type, opts, cb) {
	var repo = this;
	var uri;
	if(opts && has(opts, "uri")) {
		uri = sln.parseURI(opts.uri);
	} else {
		var sha256 = crypto.createHash("sha256");
		sha256.end(buf);
		uri = {
			algo: "sha256",
			hash: sha256.read().toString("hex"),
		};
	}
	var req = repo.protocol.request({
		method: "PUT",
		hostname: repo.hostname,
		port: repo.port,
		path: repo.path+"/sln/file/"+uri.algo+"/"+uri.hash,
		headers: {
			"Cookie": repo.cookie,
			"Content-Type": type,
		},
		agent: repo.agent,
	});
	req.end(buf);
	req.on("error", function(err) {
		cb(err, null);
	});
	req.on("response", function(res) {
		if(201 == res.statusCode) {
			cb(null, {
				location: res.headers["x-location"],
			});
		} else {
			var err = new Error("Status code "+res.statusCode);
			err.code = res.statusCode;
			cb(err, null);
		}
		res.resume(); // Drain
	});
};
// opts: { uri: string, size: number }
// returns: stream.Writable (emits "submission": { location: string })
Repo.prototype.createSubmissionStream = function(type, opts) {
	var repo = this;
	var uri, method, path;
	if(opts && has(opts, "uri")) {
		uri = sln.parseURI(opts.uri);
		method = "PUT";
		path = "/sln/file/"+uri.algo+"/"+uri.hash;
	} else {
		method = "POST";
		path = "/sln/file";
	}
	var headers = {
		"Cookie": repo.cookie,
		"Content-Type": type,
	};
	if(opts && has(opts, "size")) headers["Content-Length"] = opts.size;
	var req = repo.protocol.request({
		method: method,
		hostname: repo.hostname,
		port: repo.port,
		path: repo.path+path,
		headers: headers,
		agent: repo.agent,
	});
	var stream = new PassThroughStream();
	stream.pipe(req);
	req.on("error", function(err) {
		stream.emit("error", err);
	});
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
	return stream;
};
// opts: (none)
Repo.prototype.submitMeta = function(uri, meta, opts, cb) {
	if("string" !== typeof uri) throw new Error("Invalid URI");
	if(0 === uri.length) throw new Error("Invalid URI");
	// TODO: Validate `meta`
	var repo = this;
	var buf = new Buffer(uri+"\n\n"+JSON.stringify(meta, null, "    "), "utf8");
	repo.submitFile(buf, sln.metatype, {}, cb);
};




var util = require("util");
var TransformStream = require("stream").Transform;
var StringDecoder = require('string_decoder').StringDecoder;

// opts: { meta: bool }
// TODO: Code duplication.
sln.parseURIList = function(str, opts) {
	var result = [];
	var rest = str;
	var x;
	for(;;) {
		x = /^([^\r\n]*)[\r\n]/.exec(rest);
		if(!x) break;
		rest = rest.slice(x[0].length);
		if(!x[1].length) continue;
		if('#' === x[1][0]) continue; // Comment line.
		if(!opts || !has(opts, "meta") || !opts._meta) {
			result.push(x[1]);
		} else {
			x = /^(.*)\s*->\s*(.*)$/.exec(x[1]);
			if(!x) return cb(new Error("Parse error"));
			result.push({ uri: x[1], target: uri[2] });
		}
	}
	return result;
};

// opts: { meta: bool, req: http.ClientRequest }
sln.URIListStream = URIListStream;
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
				res.on("error", function(err) {
					stream.emit("error", err);
				});
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
			x = /^(.*[^ ]) +-> +(.*)$/.exec(x[1]);
			if(!x) return cb(new Error("Parse error"));
			this.push({ uri: x[1], target: x[2] });
		}
	}
	cb(null);
};
URIListStream.prototype._flush = function(cb) {
	// We ignore incomplete trailing data.
	cb(null);
};

