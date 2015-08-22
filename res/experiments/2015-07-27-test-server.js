#!/usr/bin/env node

var http = require("http");

http.createServer(function(req, res) {
	res.writeHead(200, {
		"Content-Type": "text/plain; charset=utf-8",
		"Content-Length": 0,
	});
	res.end();
}).listen(9000, "localhost");

