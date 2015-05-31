#!/usr/bin/env node

// TODO
var sln = require("/home/user/Code/stronglink/client/node-sln-client/sln-client");

var repo = sln.mainRepo();
if(!repo) {
	console.error("No StrongLink repository configured");
	process.exit(1);
}

if(process.argv.length < 3) {
	console.error("Usage: "+process.argv[1]+" <tags> <query>");
	process.exit(1);
}
var query = process.argv.slice(-1)[0];
var meta = {
	"tag": process.argv.slice(2, -1),
};

var paused = false; // Even Node 0.12 doesn't support stream.pause() properly.
var stream = repo.createQueryStream(query, { wait: false });
stream.on("readable", function() {
	if(paused) return;
	var URI = stream.read();
	if(!URI) return;
	paused = true;
//	stream.pause();
	repo.submitMeta(URI, meta, function(err, obj) {
		paused = false;
//		stream.resume();
		if(err) throw err;
		console.log(URI);
		stream.emit("readable"); // Hack to resume stream
	});
});


