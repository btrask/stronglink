#!/usr/bin/env node

var fs = require("fs");
var http = require("http");

// TODO: Load this from ~/.config/stronglink/client.json?
var repo = {
	host: "localhost",
	port: 8000,
	session: "1:69a39f07f2dbefdb0ffbaf66e9d36d4f",
};

var query = process.argv[2] || "";

var meta = {
	"tag": "public",
};
var data = "\r\n\r\n"+JSON.stringify(meta);

function Query(repo, q) {
	this.repo = repo;
	this.q = q;
}
util.inherits(Query, EventEmitter);
Query.prototype.connect = function() {
	
};
Query.prototype.next = function(cb) {

};


function Repo(obj) {
	this.host = obj.host;
	this.port = obj.port;
	this.session = obj.session;
}
Repo.prototype.query = function(q, cb) {
	
};
Repo.prototype.post = function(type, cb) {
	
};


function doquery(q, cb) {
	var req = http.get({
		host: repo.host,
		port: repo.port,
		path: "/sln/query?q="+encodeURIComponent(q),
		headers: {
			"Cookie": "s="+repo.session,
		},
	});
	req.on("response", function(res) {
		// This code is complicated because we want to put back-pressure
		// on the stream while we ruch each callback asynchronously.
		// Actually it's complicated because Node kind of sucks.
		var str = "";
		var waiting = true;
		var ended = false;
		res.setEncoding("utf8");
		res.on("readable", function() {
			if(!waiting) return;
			str += res.read();
			next();
		});
		res.on("error", function(err) {
			ended = true;
			if(waiting) cb(err, null, null);
		});
		res.on("end", function() {
			ended = true;
			if(waiting) cb(null, null, null);
		});
		function next() {
			var x;
			waiting = true;
			for(;;) {
				x = str.match(/^([^\r\n]*)[\r\n]/);
				if(!x) return;
				str = str.slice(x[0].length);
				if(!x[1].length) continue;
				break;
			}
			waiting = false;
			cb(null, x[1], function() {
				if(ended) return;
				next();
			});
		}
	});
	req.on("error", function(err) {
		cb(err);
	});
}

function submit(URI, cb) {
	if(!URI) return cb(new Error("Bad URI"));
	var req = http.request({
		method: "POST",
		host: repo.host,
		port: repo.port,
		path: "/sln/file",
		headers: {
			"Cookie": "s="+repo.session,
			"Content-Type": "text/x-sln-meta+json; charset=utf-8",
		},
	});
	req.end(URI+data, "utf8");
	req.on("response", function(res) {
		if(201 != res.statusCode) return cb(new Error("Submission failure: "+res.statusCode), null);
		cb(null, res.headers["x-location"]);
	});
	req.on("error", function(err) {
		cb(err, null);
	});
}




doquery("", function(err, URI, cb) {
	if(err) throw err;
	if(null === URI) return;
	submit(URI, function(err, hash) {
		if(err) throw err;
		console.log(hash);
		cb();
	});
});
















