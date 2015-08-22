#!/usr/bin/env node

var sln = require("/home/user/Code/stronglink/client/node-sln-client/sln-client");
var repo = sln.repoForName("test");

function trampoline(func/* (next) */) {
	var called, finished;
	for(;;) {
		called = false;
		finished = false;
		func(next);
		finished = true;
		if(!called) break;
	}
	function next() {
		called = true;
		if(finished) trampoline(func);
	}
}

/*process.on("uncaughtException", function(err) {
	console.log(err);
});*/

repo.query("", { count: 1 }, function(err, URIs) {
	if(err) throw err;
	var URI = URIs[0];
	trampoline(function(next) {
		repo.getFile(URI, {method: "GET"}, function(err, data) {
			if(err) throw err;
			console.log(new Date);
			next();
		});
	});
});


/*var protocol = require("http");
var agent = new protocol.Agent({
	keepAlive: true,
	maxSockets: 1,
});
protocol.createServer(function(req, res) {
	res.writeHead(200, {
		"Content-Length": 4,
		"Connection": "keep-alive",
		"Content-Type": "text/markdown",
		"Cache-Control": "max-age=31536000",
		"ETag": "1",
		"Content-Security-Policy": "'none'",
		"X-Content-Type-Options": "nosniff",
	});
	res.end("test", "utf8");
}).listen(8002);
trampoline(function(next) {
	protocol.get({
		hostname: "localhost",
		port: 8002,
		agent: agent,
	}, function(res) {
		res.on("data", function() {});
		res.on("end", function() {
			console.log(new Date);
			next();
		});
	});
});*/

