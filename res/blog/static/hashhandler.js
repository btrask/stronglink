// TODO: It'd be nice if we could tell whether we were already registered.
var hashhandler = document.getElementById("hashhandler");
hashhandler.onclick = function(e) {
	e.preventDefault();
	if(!navigator.registerProtocolHandler) return; // TODO: Something intelligent?
	var resolver = window.location.origin+"/?q=%s";
	var reponame = document.getElementById("title").textContent;
	navigator.registerProtocolHandler("hash", resolver, reponame);
};

