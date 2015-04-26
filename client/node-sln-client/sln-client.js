var sln = exports;

var fs = require("fs");
var http = require("http");
var qs = require("querystring");
var PassThroughStream = require("stream").PassThrough;
var urlModule = require("url");

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
sln.formatURI = function(obj) {
	return "hash://"+
		obj.algo+
		"/"+obj.hash+
		(obj.query||"")+
		(obj.fragment||"");
};
//sln.createHasher = function() {};
//sln.Hasher = Hasher;

var mainRepo = (function() {
	try {
		// TODO: Suitable config location depending on platform.
		var config = "~/.config/stronglink/client.json".replace(/^~/, process.env.HOME);
		var info = JSON.parse(fs.readFileSync(config, "utf8"));
		if("string" !== typeof info.url) throw new Error("Invalid URL");;
		if("string" != typeof info.session) throw new Error("Invalid session");
		return new Repo(info.url, info.session);
	} catch(e) {
		return null;
	}
})();
sln.mainRepo = function() {
	return mainRepo;
};
sln.createRepo = function(url, session) {
	return new sln.Repo(url, session);
};
sln.Repo = Repo;

function Repo(url, session) {
	var obj = urlModule.parse(url);
	this.hostname = obj.hostname;
	this.port = obj.port;
	this.path = obj.path;
	this.session = session;
	this.client = http; // TODO

	if("/" === this.path.slice(-1)) this.path = this.path.slice(0, -1);
}
Repo.prototype.info = function(cb) {
	return {
		reponame: null,
		username: null,
	};
};

Repo.prototype.query = function(query, opts, cb) {
	// TODO: opts must have a count limit or else this will run forever?
	var stream = this.createQueryStream(query, opts);
	var URIs = [];
	stream.on("readable", function() {
		var URI = stream.read();
		if(URI) URIs.push(URI);
	});
	stream.on("end", function() {
		cb(null, URIs);
	});
	stream.on("error", function(err) {
		cb(err, null);
	});
};
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
			"wait": opts && false !== opts.wait ? "1" : "0",
		}),
		headers: {
			"Cookie": "s="+repo.session,
		},
		agent: false,
	});
	return new URIListStream({ meta: false, req: req });
};
Repo.prototype.createMetaStream = function(opts) {
	var repo = this;
	var req = repo.client.get({
		hostname: repo.hostname,
		port: repo.port,
		path: repo.path+"/sln/metafiles?"+qs.stringify({
			"start": opts ? opts.start : "",
			"count": opts ? opts.count : "",
			"wait": opts && false !== opts.wait ? "1" : "0",
		}),
		headers: {
			"Cookie": "s="+repo.session,
		},
		agent: false,
	});
	return new URIListStream({ meta: true, req: req });
};
Repo.prototype.createFileStream = function(uri, opts) {
	var repo = this;
	var obj = sln.paseURI(uri);
	if(!obj) throw new Error("Bad URI "+uri);
	var stream = new PassThroughStream();
	var req = repo.client.get({
		hostname: repo.hostname,
		port: repo.port,
		path: repo.path+"/sln/file/"+obj.algo+"/"+obj.hash,
		headers: {
			"Cookie": "s="+repo.session,
		},
	});
	req.on("response", function(res) {
		if(200 == res.statusCode) {
			res.pipe(stream);
		} else {
			var err = new Error("Status code "+res.statusCode);
			err.statusCode = res.statusCode;
			stream.emit("error", err);
		}
	});
	req.on("error", function(err) {
		stream.emit("error", err);
	});
	return stream;
};
Repo.prototype.getMeta = function(uri, cb) {
	var repo = this;
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
	});
};

Repo.prototype.createSubStream = function(type, opts) {
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
	});
	req.on("error", function(err) {
		stream.emit("error", err);
	});
	return stream;
};
Repo.prototype.submitMeta = function(uri, meta, cb) {
	if("string" !== typeof uri) throw new Error("Invalid URI");
	if(0 === uri.length) throw new Error("Invalid URI");
	// TODO: Validate `meta`
	var repo = this;
	var type = "text/x-sln-meta+json; charset=utf-8";
	var stream = repo.createSubStream(type, {});
	stream.end(uri+"\r\n\r\n"+JSON.stringify(meta), "utf8");
	stream.on("submission", function(obj) {
		cb(null, obj);
	});
	stream.on("error", function(err) {
		cb(err, null);
	});
};

// Future APIs
// - Pulls
// - Other configuration?



var util = require("util");
var TransformStream = require("stream").Transform;
var StringDecoder = require('string_decoder').StringDecoder;

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

