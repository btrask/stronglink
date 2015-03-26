// TODO: Don't show the message if we're already registered.
// How can we tell?
if(navigator.registerProtocolHandler) {
	var sethandler = document.getElementById("sethandler");
	sethandler.removeAttribute("hidden");
	sethandler.onclick = function(e) {
		e.preventDefault();
		var resolver = window.location.origin+"/?q=%s";
		var reponame = document.getElementById("title").textContent;
		navigator.registerProtocolHandler("hash", resolver, reponame);
	};
}

