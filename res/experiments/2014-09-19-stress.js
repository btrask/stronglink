#!/usr/bin/env node

var http = require("http");
var urlModule = require("url");

var url = process.argv[2];
var total = 1000;
var data = 0;
var statuses = {};

if(!url) throw new Error("No URL");
http.globalAgent.maxSockets = 100;

function get(obj) {
	var req = http.get(obj);
	req.on("response", function(res) {
		var status = res.statusCode;
		if(!statuses[status]) statuses[status] = 1;
		else statuses[status]++;
		res.on("readable", function() {
			var x = res.read();
			data += x.length;
//			console.log(x.toString("utf8"));
		});
//		res.on("end", function() {
//		});
	});
	req.on("error", function(err) {
		if(!statuses["conn"]) statuses["conn"] = 1;
		else statuses["conn"]++;
	});
}

var obj = urlModule.parse(url);
obj.headers = { "Cookie": "s=1:not-very-random" };
var then = +new Date;
for(var i = 0; i < total; ++i) {
	get(obj);
}

process.on("exit", function() {
	var duration = (+new Date - then) / 1000.0;
	console.log("Seconds: "+duration);
	console.log("Req/s: "+(total / duration));
	console.log("KB/s: "+((data / 1024) / duration));
	console.log("Statuses:", statuses);
});

