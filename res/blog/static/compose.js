var input = document.getElementById("input");
var output = document.getElementById("output");

var parser = new commonmark.Parser();
var renderer = new commonmark.HtmlRenderer({sourcepos: true});
renderer.softbreak = "<br>";

// Ported from C version in markdown.c
// The output should be identical between each version
function md_escape(iter) {
	var event, node, p, text;
	for(;;) {
		event = iter.next();
		if(!event) break;
		if(!event.entering) continue;
		node = event.node;
		if("HtmlBlock" != node.type) continue;

		p = new commonmark.Node("Paragraph");
		text = new commonmark.Node("Text");
		text.literal = node.literal;
		p.appendChild(text);
		node.insertBefore(p);
		node.unlink();
	}
}
function md_escape_inline(iter) {
	var event, node, text;
	for(;;) {
		event = iter.next();
		if(!event) break;
		if(!event.entering) continue;
		node = event.node;
		if("Html" != node.type) continue;

		text = new commonmark.Node("Text");
		text.literal = node.literal;
		node.insertBefore(text);
		node.unlink();
	}
}
function md_autolink(iter) {
	// <http://daringfireball.net/2010/07/improved_regex_for_matching_urls>
	// Painstakingly ported to POSIX and then back
	// TODO: Get original
	// Pretty sure this is broken because it's capturing trailing punctuation...
	var linkify = /([a-z][a-z0-9_-]+:(\/{1,3}|[a-z0-9%])|www[0-9]{0,3}[.]|[a-z0-9.-]+[.][a-z]{2,4}\/)([^\s()<>]+|(([^\s()<>]+|(([^\s()<>]+)))*))+((([^\s()<>]+|(([^\s()<>]+)))*)|[^\[\]\s`!(){};:'".,<>?«»“”‘’])/;
	var event, node, match, str, text, link, face;
	for(;;) {
		event = iter.next();
		if(!event) break;
		if(!event.entering) continue;
		node = event.node;
		if("Text" != node.type) continue;

		str = node.literal;
		while((match = linkify.exec(str))) {
			text = new commonmark.Node("Text");
			link = new commonmark.Node("Link");
			face = new commonmark.Node("Text");
			text.literal = str.slice(0, match.index);
			link.destination = str.slice(match.index, match.index+match[0].length)
			face.literal = link.destination;
			link.appendChild(face);
			node.insertBefore(text);
			node.insertBefore(link);
			str = str.slice(match.index+match[0].length);
		}

		if(str !== node.literal) {
			text = new commonmark.Node("Text");
			text.literal = str;
			node.insertBefore(text);
			node.unlink();
		}
	}
}
function md_convert_hashes(iter) {
	var event, node, URI, hashlink, sup_open, sup_close, face;
	for(;;) {
		event = iter.next();
		if(!event) break;
		if(!event.entering) continue;
		node = event.node;
		if("Link" != node.type) continue;

		URI = node.destination;
		if(!URI) continue;
		if("hash:" != URI.toLowerCase().slice(0, 5)) continue;

		hashlink = new commonmark.Node("Link");
		hashlink.destination = URI;
		hashlink.title = "Hash URI (right click and choose copy link)";

		sup_open = new commonmark.Node("Html");
		sup_close = new commonmark.Node("Html");
		face = new commonmark.Node("Text");
		sup_open.literal = "<sup>[";
		sup_close.literal = "]</sup>";
		face.literal = "#";
		hashlink.appendChild(face);

		node.insertAfter(sup_open);
		sup_open.insertAfter(hashlink);
		hashlink.insertAfter(sup_close);

		iter.resumeAt(sup_close, false);

		node.destination = "../?q="+URI;
	}
}

var coalesce = null;
input.oninput = input.onpropertychange = function() {
	clearTimeout(coalesce); coalesce = null;
	coalesce = setTimeout(function() {
		var node = parser.parse(input.value);
		md_escape(node.walker());
		md_escape_inline(node.walker());
		md_autolink(node.walker());
		md_convert_hashes(node.walker());
		// TODO: Use target=_blank for links.
		output.innerHTML = renderer.render(node);
	}, 1000 * 0.5);
};

var TAB = 9;
input.onkeydown = function(e) {
	if(TAB !== e.keyCode && TAB !== e.which) return;
	e.preventDefault();
	var t = "\t";
	var s = this.selectionStart + t.length;

	// TODO: With long documents, this is slow and messes up scrolling.
	// There's gotta be a better way.
	// Only tested on Firefox so far...
	this.setRangeText(t, this.selectionStart, this.selectionEnd);

	this.setSelectionRange(s, s);

	this.oninput();
};
