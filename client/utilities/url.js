/* Copyright Ben Trask and other contributors. All rights reserved.
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to
deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
IN THE SOFTWARE. */
var url = {};
url.parse = function(str, parseQuery) {
	// http://tools.ietf.org/html/rfc3986
	// URI Generic Syntax
	// Appendix B. Parsing a URI Reference with a Regular Expression
	var obj = /^(([^:/?#]+):)?(\/\/([^/?#]*))?([^?#]*)(\?([^#]*))?(#(.*))?/.exec(str);
	if(!obj) return null;
	return {
		scheme: obj[2],
		authority: obj[4],
		path: obj[5],
		query: parseQuery ? url.parseQuery(obj[7]) : obj[7],
		fragment: obj[9],
	};
};
url.format = function(obj) {
	return [
		undefined === obj.scheme ? "" : obj.scheme+":",
		undefined === obj.authority ? "" : "//"+obj.authority,
		undefined === obj.path ? "/" : obj.path,
		undefined === obj.query ? "" : "?"+url.formatQuery(obj.query),
		undefined === obj.fragment ? "" : "#"+obj.fragment,
	].join("");
};
url.parseQuery = function(str) {
	// TODO: This is not very robust.
	var rx = /([^?&;]+)=([^?&;]+)/g, r = {};
	for(var m; (m = rx.exec(str));) r[url.decodeQuery(m[1])] = url.decodeQuery(m[2]);
	return r;
};
url.formatQuery = function(obj) {
	if("string" === typeof obj) return obj;
	var has = Object.prototype.hasOwnProperty, fields = [], val;
	for(var key in obj) if(has.call(obj, key)) {
		val = obj[key];
		if(undefined === val || null === val) continue;
		fields.push(url.encodeQuery(key)+"="+url.encodeQuery(val));
	}
	if(!fields.length) return "";
	return fields.join("&");
};
url.encodeQuery = encodeURIComponent;
url.decodeQuery = decodeURIComponent;

