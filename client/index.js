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

/*global url DOM io Text marked*/

marked.setOptions({
	gfm: true,
	breaks: true,
	sanitize: true,
});

var MAX_ENTRIES = 50;
var OVERLAP_ENTRIES = 10;

var bt = {}; // TODO: Use separate file.
bt.has = function(obj, prop) {
	return Object.prototype.hasOwnProperty.call(obj, prop);
};
bt.union = function(target, obj) {
	for(var prop in obj) if(bt.has(obj, prop)) target[prop] = obj[prop];
	return target;
};

var http = {}; // TODO: Drop-in browser Node-style http class?
http.request = function(opts, callback) {
	var req = new XMLHttpRequest(), h = opts.headers;
	req.open(opts.method || "GET", opts.url, true);
	req.onreadystatechange = callback;
	if(h) for(var x in h) if(bt.has(h, x)) {
		req.setRequestHeader(x, h[x]);
	}
	return req;
};
http.get = function(opts, callback) {
	var req = http.request(opts, callback);
	req.send("");
	return req;
};

var client = {}; // TODO: Don't copy-paste from node-efs-client.
client.parseHashURI = function(URIString) {
	var URI = /hash:\/\/([^\/#?]+)\/([^#?]+)/.exec(URIString);
	if(!URI) return null;
	return {
		algorithm: URI[1].toLowerCase(),
		hash: URI[2],
	};
};
client.formatHashURI = function(URI) {
	return "hash://"+URI.algorithm.toLowerCase()+"/"+URI.hash;
};

function URIForAPI(URI) {
	var obj = client.parseHashURI(URI);
	return encodeURIComponent(obj.algorithm)+"/"+encodeURIComponent(obj.hash);
};

function Stream(query) {
	var stream = this;
	stream.elems = {};
	stream.element = DOM.clone("stream", this.elems);
	stream.elems.searchText.value = query;
	stream.query = query;
	stream.pinned = true;
	stream.sidebarSize = 150;
	stream.editor = null;

	stream.entries = null;
	stream.URNs = null;
	stream.req = null;
	stream.retry = null;
	stream.username = null;
	stream.password = null;

	var editors = [TextEditor, FileEditor, AccountEditor].map(function(Editor) {
		var editor = new Editor(stream);
		var button = editor.button = DOM.clone("modeButton");
		button.setAttribute("value", editor.label);
		button.onclick = function(event) {
			stream.setEditor(editor);
		};
		stream.elems.toolbar.appendChild(button);
		return editor;
	});

	function submitQuery(event) {
		window.location = Stream.location({"q": stream.elems.searchText.value});
	}
	stream.elems.searchText.onkeypress = function(event) {
		var e = event || window.event;
		var keyCode = e.keyCode || e.which;
		if(13 === keyCode || 10 === keyCode) submitQuery();
	};
	stream.elems.searchButton.onclick = submitQuery;

	DOM.addListener(window, "resize", function(event) {
		stream.reflow();
	});
	DOM.addListener(stream.elems.toolbar, "mousedown", function(event) {
		if(event.target !== stream.elems.toolbar || !stream.editor) return;
		var cursor = stream.element.offsetHeight - event.clientY;
		var offset = stream.sidebarSize - cursor;
		var move, end;
		DOM.addListener(document, "mousemove", move = function(event) {
			var cursor = stream.element.offsetHeight - event.clientY;
			stream.setSidebarSize(cursor + offset);
			if(event.preventDefault) event.preventDefault();
			return false;
		});
		DOM.addListener(document, "mouseup", end = function(event) {
			DOM.removeListener(document, "mousemove", move);
			DOM.removeListener(document, "mouseup", end);
			if(event.preventDefault) event.preventDefault();
			return false;
		});
		if(event.preventDefault) event.preventDefault();
		return false;
	});
	DOM.addListener(stream.elems.content, "scroll", function(event) {
		var c = stream.elems.content;
		stream.pinned = c.scrollTop >= (c.scrollHeight - c.clientHeight);
	});

	stream.load();
}
Stream.prototype.load = function() {
	var stream = this;
	if(stream.req) {
		var req = stream.req;
		stream.req = null;
		req.abort();
	}
	if(stream.retry) {
		clearTimeout(stream.retry);
		stream.retry = null;
	}
	DOM.fill(stream.elems.entries);
	stream.pinned = true;
	stream.entries = [];
	stream.URNs = {};
	stream.pull(MAX_ENTRIES);
};
Stream.prototype.pull = function(history) {
	var stream = this;
	if(stream.req) throw new Error("Already pulling (req)");
	if(stream.retry) throw new Error("Already pulling (retry timeout)");
	var offset = 0;
	var req = stream.req = new XMLHttpRequest();
	req.onreadystatechange = function() {
		if(!stream.req) return; // We've been canceled.
		if(stream.req !== req) throw new Error("Invalid request update");
		if(req.readyState < 3) return;
		// TODO: This is messy.
		for(var i; -1 !== (i = req.responseText.indexOf("\n", offset)); offset = i+1) {
			stream.addURN(req.responseText.slice(offset, i));
		}
		if(4 === req.readyState) {
			stream.req = null;
			if(200 === req.status) return stream.pull(OVERLAP_ENTRIES);
			stream.retry = setTimeout(function() {
				stream.retry = null;
				stream.pull(MAX_ENTRIES);
			}, 1000 * 5);
		}
	};
	req.open("GET", "/efs/query/?"+url.formatQuery({
		"q": stream.query,
		"count": 50,
		"t": +new Date,
		"u": stream.username,
		"p": stream.password,
	}), true);
	req.send("");
};
Stream.prototype.addURN = function(URN) {
	var stream = this;
	if("" === URN) return; // Ignore blank lines (heartbeat).
	if(bt.has(stream.URNs, URN)) return; // Duplicate.
	stream.URNs[URN] = true;
	while(stream.entries.length > MAX_ENTRIES) {
		DOM.remove(stream.entries.shift().element);
	}
	var entry = new Entry(stream, URN);
	stream.entries.push(entry);
	stream.elems.entries.appendChild(entry.element);
	stream.keepPinned();
	entry.load(function() {
		stream.keepPinned();
	});
};
Stream.prototype.reflow = function() {
	var stream = this;
	var toolbarHeight = stream.elems.toolbar.offsetHeight;
	var windowHeight = stream.element.offsetHeight;
	var clamped = stream.editor ? Math.max(100, Math.min(stream.sidebarSize, windowHeight)) : toolbarHeight;
	stream.elems.content.style.bottom = clamped+"px";
	stream.elems.sidebar.style.height = clamped+"px";
	stream.keepPinned();
};
Stream.prototype.keepPinned = function() {
	var stream = this;
	if(stream.pinned) stream.elems.content.scrollTop = stream.elems.content.scrollHeight;
};
Stream.prototype.setSidebarSize = function(val) {
	var stream = this;
	stream.sidebarSize = val;
	stream.reflow();
};
Stream.prototype.setEditor = function(editor) {
	var stream = this;
	if(stream.editor) {
		DOM.remove(stream.editor.element);
		DOM.classify(stream.editor.button, "selected", false);
	}
	if(stream.editor === editor) stream.editor = null;
	else stream.editor = editor;
	if(stream.editor) {
		stream.elems.sidebar.appendChild(stream.editor.element);
		DOM.classify(stream.editor.button, "selected", true);
		stream.editor.activate();
	}
	stream.reflow();
};
Stream.prototype.upload = function(blob, targets) {
	if(!blob) throw new Error("Bad upload");
	var stream = this;
	var form = new FormData();
	var req = new XMLHttpRequest();
	form.append("file", blob);
	stream.pinned = true;
	stream.keepPinned();
	if(req.upload) req.upload.onprogress = function(event) {
		var complete = event.loaded / event.total;
		console.log("complete", complete);
	};
	req.onreadystatechange = function() {
		if(4 !== req.readyState) return;
		// TODO
		console.log("status", req.status);
	};
	req.open("POST", "/efs/file/?"+url.formatQuery({
		"u": stream.username,
		"p": stream.password,
		"t": targets.join("\n"),
	}));
	req.send(form);
};
Stream.location = function(params) {
	return "/?"+url.formatQuery(params);
};
Stream.current = null;

function TextEditor(stream) {
	var editor = this;
	editor.element = DOM.clone("textEditor", this);
	editor.label = "New Entry";
	function trimBlankLines(str) {
		return str.replace(/^[\n\r]+|[\n\r]+$/g, "");
	}
	var refresh = null;
	editor.textarea.oninput = editor.textarea.onpropertychange = function() {
		refresh = setTimeout(function() {
			var src = trimBlankLines(editor.textarea.value);
			var html = marked(src);
			DOM.fill(editor.preview, Entry.parseHTML(html));
			refresh = null;
		}, 1000 * 0.25);
	};
	editor.submitButton.onclick = function(event) {
		if("" === editor.textarea.value.replace(/\s/g, "")) {
			// TODO: Display submission error?
		} else {
			stream.upload(new Blob(
				[trimBlankLines(editor.textarea.value)],
				{"type": "text/markdown"}
			), editor.targets.value.split(/,\s*/g));
			// TODO: Selectable MIME types.
			// TODO: Share target code with FileEditor.
		}
		editor.textarea.value = "";
		DOM.fill(editor.preview);
	};
}
TextEditor.prototype.activate = function() {
	var editor = this;
	editor.textarea.focus();
};
function FileEditor(stream) {
	var editor = this;
	editor.element = DOM.clone("fileEditor", this);
	editor.label = "Upload File";
	editor.uploadButton.onclick = function(event) {
		stream.upload(editor.uploadInput.files[0], []); // TODO: Disable button when no file selected.
	};
}
FileEditor.prototype.activate = function() {};

function AccountEditor(stream) {
	var editor = this;
	editor.element = DOM.clone("accountEditor", this);
	editor.label = "Account";
	editor.submit.onclick = function(event) {
		// TODO: Should we pre-load account details?
		// - Readable/writable permissions
		// - Other account settings?
		// - Easier to handle login errors
		stream.username = editor.username.value;
		stream.password = editor.password.value;
		stream.load();
		editor.username.value = "";
		editor.password.value = "";
	};
}
AccountEditor.prototype.activate = function() {
	var editor = this;
	editor.username.focus();
};

function Entry(stream, URN) {
	var entry = this;
	entry.elems = {};
	entry.element = DOM.clone("entry", this.elems);
	entry.stream = stream;
	entry.URN = URN;
	DOM.fill(entry.elems.content, DOM.clone("loading"));
	entry.addURN(URN);
}
Entry.prototype.addURN = function(URN) {
	var entry = this;
	var elems = {}, e = DOM.clone("entryURN", elems);
	DOM.fill(elems.URN, entry.URN);
	elems.URN.href = Stream.location({"q": entry.URN});
	elems.raw.href = "/efs/file/"+URIForAPI(URN)+url.formatQuery({
		"u": stream.username,
		"p": stream.password,
	});
	entry.elems["URNs"].appendChild(e);
};
Entry.prototype.load = function(callback) {
	var entry = this;
	var stream = entry.stream;
	var URI = client.parseHashURI(entry.URN);
	var algorithm = encodeURIComponent(URI.algorithm);
	var hash = encodeURIComponent(URI.hash);
	var entryReq = http.get({
		url: "/efs/html/"+URIForAPI(entry.URN)+"/index.html?"+url.formatQuery({
			"u": stream.username,
			"p": stream.password,
		}),
	}, function() {
		if(4 !== entryReq.readyState) return;
		switch(entryReq.status) {
			case 200: // OK
				// TODO: Parse headers.
/*				for(var i = 0; i < URNs.length; ++i) entry.addURN(URNs[i]);
				DOM.fill(entry.elems["sources"], obj["sources"].join(", "));
				DOM.fill(entry.elems["targets"], obj["targets"].join(", ")); // TODO: Sort, "public" first?
				*/
				DOM.fill(entry.elems.content, Entry.parseHTML(entryReq.responseText));
				break;
			case 406: // Not Acceptable
				DOM.fill(entry.elems.content, DOM.clone("noPreviewMessage"));
				break;
		}
		callback();
	});
};
Entry.parseHTML = function(html) {
	function linkifyTextNode(text) {
		// <http://daringfireball.net/2010/07/improved_regex_for_matching_urls>
		var exp = /\b((?:[a-z][\w\-]+:(?:\/{1,3}|[a-z0-9%])|www\d{0,3}[.]|[a-z0-9.\-]+[.][a-z]{2,4}\/)(?:[^\s()<>]+|\(([^\s()<>]+|(\([^\s()<>]+\)))*\))+(?:\(([^\s()<>]+|(\([^\s()<>]+\)))*\)|[^\s`!()\[\]{};:'".,<>?«»“”‘’]))/ig;
		var x, unlinked, rest, link;
		while((x = exp.exec(text.data))) {
			link = document.createElement("a");
			link.appendChild(document.createTextNode(x[0]));
			link.href = x[0];
			unlinked = text.splitText(x.index);
			rest = unlinked.splitText(x[0].length);
			unlinked.parentNode.replaceChild(link, unlinked);
		}
	}
	function linkifyURNs(node) {
		if("A" === node.tagName) return;
		var a = node.childNodes;
		for(var i = 0; i < a.length; ++i) {
			if(a[i] instanceof Text) linkifyTextNode(a[i]);
			else linkifyURNs(a[i]);
		}
	}
	function convertURNs(node) {
		var a = node.childNodes, l = a.length, x;
		for(var i = 0; i < l; ++i) {
			if(!a[i].getAttribute) continue;
			x = a[i].getAttribute("href");
			if(x && /^hash:/.test(x)) {
				a[i].setAttribute("href", "?q="+encodeURIComponent(x));
				if(a[i].text.trim() === x) DOM.classify(a[i], "URN");
			}
			x = a[i].getAttribute("src");
			if(x && /^hash:/.test(x)) {
				a[i].setAttribute("src", "/efs/file/"+URIForAPI(x));
			}
			convertURNs(a[i]);
		}
	}
	function targetBlank(node) {
		var a = node.childNodes, l = a.length, x;
		for(var i = 0; i < l; ++i) {
			if("A" === a[i].tagName && a[i].getAttribute("href")) {
				a[i].setAttribute("target", "_blank");
			}
			targetBlank(a[i]);
		}
	}
	var elem = document.createElement("div");
	elem.innerHTML = html;
	linkifyURNs(elem);
	convertURNs(elem);
	targetBlank(elem);
	return elem;
};



var inputQuery = url.parseQuery(window.location.search);
Stream.current = new Stream(undefined === inputQuery.q ? "" : inputQuery.q);
document.body.appendChild(Stream.current.element);
Stream.current.reflow();

