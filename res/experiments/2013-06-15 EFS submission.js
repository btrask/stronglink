function genHash(path, callback/* (err, hash) */) {}
function genURNs(path, callback/* (err, URNs) */) {}
function plainText(path, callback/* (err, str) */) {}
function extractURIsByType(str) {}

//function ancestors(URIs, callback/* (err, URIs) */) {}
function descendants(hash, callback/* (err, URIs) */) {}
function metaDescendants(hash, callback/* (err, URIs) */) {}

function cacheAncestors(hash, callback/* (err) */) {}

///

function Entry() {}
Entry.prototype.textContent = ;
Entry.prototype.

Repo.prototype.addEntry = function(path) {};

///

function File(path) {
	var file = this;
	file.stream = null;
	file.URNs = null;
	file.hash = null;
}
File.prototype.load = function() {
	var file = this;
	file.stream = fs.createReadStream(path, {});
};
File.prototype.loadURNs = function() {};


Repo.prototype.addEntryFile = function(file, callback) {};

///

function generateURNs(path, callback/* (err, URNs, hash) */) {}
function loadPlainText(path, callback/* (err, str) */) {}
function parseURIs(str) {}
function trackLinks(hash, URIs, callback/* (err) */) {}


/*

Search
- Do we need to be able to show ancestors? Descendants? When?
- When searching for a keyword, should descendants/ancestors be visible?
- What kind of options do we want? What kind of interface should they have?

*/




