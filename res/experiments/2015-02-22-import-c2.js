#!/usr/bin/env node

var http = require("http");
var fs = require("fs");
var Stream = require("stream").PassThrough;

/*function trampoline(func) {
	var called, ended;
	function next() {
		called = true;
		if(ended) func(next);
	}
	for(;;) {
		called = false;
		ended = false;
		func(next);
		ended = true;
		if(!called) break;
	}
}*/


var dir = "/home/user/Desktop/c2.com_cgi_20150104"; //process.argv[1];
var files = fs.readdirSync(dir);

//http.globalAgent.maxSockets = 64;


function post(path, cb) {
	var boundary = "---------------------------40264783344685678619300005"
	var req = http.request({
		method: "POST",
		hostname: "localhost",
		port: 8000,
		path: "/submission", // "/efs/file/",
		headers: {
			"Content-Type": "multipart/form-data; boundary="+boundary,
			//"Content-Type": "text/plain; charset=utf-8",
			//"Content-Length": 0,
			"Cookie": "s=1:not-very-random",
		},
		//agent: false,
	});
	req.on("response", function(res) {
		if(303 != res.statusCode) throw new Error("Bad status "+res.statusCode);
		//res.setEncoding("utf8");
		res.on("data", function(chunk) {
			
		});
		res.on("end", function() {
			cb(null);
		});
		res.on("error", function(err) {
			throw err;
		});
	});
	req.on("error", function(err) {
		throw err;
		//cb(err);
	});
	var file = fs.createReadStream(path, {encoding: "utf8"});
	file.on("error", function(err) {
		throw err;
		//cb(err);
	});
	req.write(
		"--"+boundary+"\r\n"+
		"Content-Disposition: form-data; name=\"markdown\"\r\n"+
		//"Content-Type: text/html; charset=utf-8\r\n"+
		"\r\n", "utf8");
	file.pipe(req, {end: false});
	file.on("end", function() {
		req.write("\r\n--"+boundary+"--\r\n");
		req.end();
	});
}

var i = 0, done = 0;
function once() {
	if(i >= files.length) return;
	post(dir+"/"+files[i++], function(err) { ++done; once() });
}
for(var j = 0; j < 64; ++j) once();


var last = 0;
var then = +new Date;
var interval = setInterval(function() {
	console.log("Completed "+(done-last)+" files per second ("+(done)+"/"+(files.length)+") ("+i+")");
	last = done;
	if(done == files.length) {
		var now = +new Date;
		console.log("Complete in "+(now-then)+" seconds");
		clearInterval(interval);
	}
}, 1000 * 1);

