EarthFS - High Level Content Addressable File System
====================================================

EarthFS is a storage system that uses content addressing and search to organize and retrieve information. It exposes content addressing to the user as `hash:` links to immutable files.

EarthFS is well suited for personal notetaking. Its interface bears some similarity to [Notational Velocity](http://notational.net/). It makes linking between notes trivial, similar to a wiki such as [VoodooPad](https://plausible.coop/voodoopad/), but because notes are immutable, each note contains the changes as of a single point in time. This basically means turning the change log into the primary view, rather than the page. Instead of reading a page that was built up over time, you search for a term and see each of the notes on that topic in order. Sync between repositories is fast, powerful (with full query support), and real-time.

The main interface runs in the web browser and can be used as a blog platform. Notes written locally (even while offline) can be automatically synced to a remote server, and it's easy to configure queries to control which files are published. The interface follows Dave Winer's design principle, [river of news](http://www.reallysimplesyndication.com/riverOfNews) \[broken link\]. EarthFS is nearly as fast as a static site but also has full search functionality. Entries in [GitHub-flavored Markdown](https://help.github.com/articles/github-flavored-markdown) are rendered using [Sundown](https://github.com/vmg/sundown).

EarthFS provides a complete API over HTTP (and HTTPS) so that other applications can access it directly. One such client, a Firefox extension for archiving web pages and making them content-addressable, is already in development.

EarthFS is written in C with a small amount of low-level, cross-platform Objective-C for the filter system. It uses the low level, high performance database [LMDB](http://symas.com/mdb/) to perform queries. Files are stored directly in the OS file system under their content hashes (with atomic operations and `fsync`). The server is asynchronous and uses [libco](http://byuu.org/programming/libco/) from BSNES.

Like anything, EarthFS has some limitations and makes some tradeoffs. There is no FUSE interface because EarthFS just isn't good at the same things as a traditional file system. This is true for [any storage system built on an ACID database](http://www.mail-archive.com/sqlite-users@sqlite.org/msg73451.html), and doubly true for content addressing systems, given their preference for immutability.

Rather than emulating mutable files via block de-duplication, EarthFS clients that need mutability (which is not expected to be many) should store diffs and use `hash:` links to track history relationships, similar to [how Git works](http://www.git-scm.com/book/en/Git-Internals-Git-Objects). EarthFS's sync system is _available_ and _eventually consistent_ per the [CAP theorem](http://aphyr.com/posts/278-timelike-2-everything-fails-all-the-time), which makes mutability difficult to get right, but EarthFS gives application developers the best tools to do it if they choose.

Building
--------

Currently the only external dependency is YAJL for JSON encoding/decoding. (TODO)

Commands:

```
$ make
$ sudo make install (TODO)
```

Running
-------

TODO

FAQ
---

**What about security?**
I know the current climate, especially around new projects written in C. I believe I've taken reasonable precautions to avoid obvious bugs. For overall security I'd give myself a B. If you want a communication platform written by a professional cryptographer where security is the top priority (above usability, etc.), try Adam Langley's [Pond](https://pond.imperialviolet.org/).

The cryptography in EarthFS is plain HTTPS with OpenSSL (for portability; LibreSSL support is planned), based on [stud](https://github.com/bumptech/stud)/[stunnel](https://www.stunnel.org/index.html) (TODO). Disk encryption is left to the underlying file system or disk.

You can use it locally for your own notes without any network access at all. Syncing can be done over LAN.

**How does this project compare to [Camlistore](http://camlistore.org/)?**
I have immense respect for Brad Fitzpatrick and the Camlistore team, and I only wish Camlistore the best. That said, I think they're making several _technical_ mistakes regarding content addressing and I hope they consider these ideas.

- Content addresses are not links (the [merkle DAG](https://en.wikipedia.org/wiki/Merkle_tree) should be composed of _user documents_)
- Content addresses aren't portable (they're ["namespaced"](http://www.bentrask.com/notes/content-addressing.html) because they encode all sorts of Camlistore-specific meta-data)
- Random data (perma-nodes) are given content addresses (it's no longer a content addressing system because random data is not content; it can't handle network partitions where the same data is added on both sides)
- Only one pre-baked content addressing scheme (not sure about the validity of this criticism; they could easily add more if they haven't already)
- No concept of semantic hashes (more abstract/longer term problem)

I don't get the impression that Camlistore is the best at any of the many things it does. EarthFS is starting with a very narrow focus (notetaking and then maybe blogging) and definitely trying to beat existing players on their own turf (and it's hard enough even with such a narrow focus).

Camlistore is "multi-layer" (it says it right in the name) which I just don't think is generally a good idea. For example, block-level de-duplication is better done in the file system or disk controller than in an application that also syncs photos. Block de-dup also comes with [significant overhead](https://code.google.com/p/camlistore/issues/detail?id=197) when the (OS) file system doesn't know you're doing it.

**How does this project compare to [IPFS](http://ipfs.io/)?**
I exchanged emails with Juan Batiz-Benet and he was supportive and gave me some great feedback. I'm very thankful to him.

I'm sorry about the similarity of the name, but I've been using this name privately for two years and I never managed to come up with a better one. But I'm happy with EarthFS being seen as a smaller project than InterPlanetaryFS. (Next up is PodunkFS.)

By providing mutability support and a FUSE layer I think IPFS has its work cut out for it. How is it going to perform if someone tries to run a database on top of it (and remember, [SQLite is an application file format](https://sqlite.org/appfileformat.html))? But on the other hand, no one seems to complain about Dropbox, so maybe it isn't a problem or can be easily resolved.

Juan didn't like the idea of content addresses as links, which I think is critical for a notetaking system. If IPFS has a standard mount-point, then for example `file:///ipfs/[hash]` could work.

I think that algorithm names should be human readable in links, whereas he proposes to [encode them in hexadecimal](https://github.com/jbenet/multihash).

**How does this project compare to [Named Data Networking](https://en.wikipedia.org/wiki/Named_data_networking)?**
I didn't hear about NDN until fairly recently, but it sounds like close to the same thing done at the networking layer rather than the application/storage layer. On top of the difficulty of deploying a new network, I suspect NDN has an incentive problem. When you shout your request into the network, why would any given system bother to respond? Maybe if it can attach its own ads to the content you requested, it would, but I don't think we (users or content producers) want that. Or maybe the expectation is for everyone's ISP to become their resolver, but I don't want them to have any more power than they already do.

With EarthFS, content address resolution is always performed by a single pre-configured repository (typically your own, run locally), so the responsibilities and incentives are clear.

**How does this project compare to a static blog?**
When hosted, EarthFS works similar to how a static site generator plus web server work. Its server is extremely high performance and written in C, using http_parser from Node.js and async network I/O from libuv. Files are converted to HTML once and then cached (forever, since they're immutable).

The biggest difference is that EarthFS directly supports user searches, which means that that the relevant files are opened and concatenated together into the output (which is by far the slowest part). Most queries take literally under a millisecond for 50 results (each additional search term adds around 500 _micro_seconds). Sending 50 files (180KB total) takes around 100 milliseconds on my ancient laptop.

The attack surface is significantly larger at present. The web server itself is small, and there are only a few APIs, but EarthFS currently does HTML conversion (from Markdown, etc.) in-process. Supporting fully sandboxed parsing/conversion would be a very good idea.

Most people will probably find it easier to use than a static site generator.

EarthFS can be configured to accept comments directly. However, much of that functionality is still incomplete.

**Why would a content addressing system focus on notetaking?**
For me, the priority is notetaking (and, to a lesser extent, organizing other files). Content addressing is just a means to an end. If I could've made do with an existing notetaking system or something simpler without content addressing, I would have.

Immutability eliminates the (for me, crippling) mental pressure to go back and fix up old notes. Now I consider my old notes a record of exactly how stupid I was at any given point in time. They're evidence of what I didn't know as much as of what I did. That keeps me honest with myself and saves me from the burden of maintaining the illusion of perfection. (I think the internet and society as a whole could benefit from this too.)

At the same time, hash links let me reference any note unambiguously, in a way that won't break, and without making me come up with unique names for each note. They're less fragile than time stamps or UUIDs. So when I want to correct something, I can reliably cite the original.

In my experience, personal wikis tend to accumulate cruft over time. Some pages grow without bound, while others never get more than a couple words. EarthFS shows search results in reverse chronological order, so the system is able to naturally "forget" stale or irrelevant information without actually throwing anything away. I believe this is critical for [any system with a human in the loop](http://www.c2.com/cgi/wiki?RecentChangesJunkie).

**Why 50 results per page (by default)?**
Because I'm not getting paid by the page view, unlike so many web sites, including search engines.

This fundamental shift of incentives explains a lot of decisions about how EarthFS works. For example, the filter system is very particular about the order it returns results in (basically reverse chronological, but with some deep subtleties) because it doesn't cost the user anything (not even a measureable amount of battery life) to generate results in that order. Google, on the other hand, could probably save millions if it shaved 0.1% without most people noticing the difference in results. They also have to index the whole Web, fight spammers, etc.

For my notes, on my screen, 50 is about the maximum before the scroll bar becomes completely unusable.

**Is it a backup system?**
EarthFS is append-only and low-latency, which means it has a lot of the benefits of an online backup system without many of the drawbacks. Edits and deletions are synchronized as diffs so the data cannot be permanently lost. Underlying files can be deleted (e.g. to save space), but these deletions are not synced. EarthFS has no way to permanently delete files remotely. By default, synchronization is one-way.

Like any automated backup system, EarthFS mirrors could be obliterated by a bug or security exploit. It's possible to keep mirrors offline and synchronize them less frequently to guard against these risks.

**What do hash links look like?**
Here is the hash for a random note of mine:

`hash://sha256/d3cab4a85dcef3b10e2e2e7cdc23a7d12ba3153354310e59786409d534b62694`
`hash://sha256/d3cab4a85dcef3b10e2e2e7c` (short version)

The short version is intended to be robust against accidental collisions, but not against intentional collisions (e.g. malicious attacks). With a billion files on the network, the odds of an accidental collision for a short hash are (TODO); for a full hash (TODO). Note that a full hash collision for SHA-1 (which is already considered obsolete) has never been found (or publicly revealed).

In this scheme, the hash algorithm takes the role of the URI authority (instead of e.g. DNS). These URIs have to be resolved relative to a known location, which in EarthFS is usually a repository running locally. When displayed on a web site, hash links are normally resolved through the site itself (the same way a wiki resolves its own wiki-links).

Hash links typically don't need to be displayed to the user since they are opaque pointers, although the algorithm used might be relevant sometimes (for example, for audio fingerprint hashes). Short hashes are useful for the case where a link must be entered manually.

**Why doesn't it use URNs?**
I believe URNs failed to catch on due to technical problems as much as social ones. At the time, Uniform Resource Identifiers were seen as split into Uniform Resource Locators and Uniform Resource Names. I believe that locators and names have effectively the same characteristics: they are both dynamically assigned by a central authority (a server or agency). I believe there is a third type of resource identifier, hashes, that are distinct: they are decentralized because they can be assigned by anyone who knows the algorithm, and they are static because the algorithm is fixed.

http://www.w3.org/Provider/Style/URI
Cool URIs don't change

> Most URN schemes I have seen look something like an authority ID followed by either a date and a string you choose, or just a string you choose. This looks very like an HTTP URI. In other words, if you think your organization will be capable of creating URNs which will last, then prove it by doing it now and using them for your HTTP URIs.

Furthermore, the only [officially registered URN namespaces](https://www.iana.org/assignments/urn-namespaces/urn-namespaces.xhtml) were for names like ISBN. No hash algorithm was ever registered. Some projects, most notably BitTorrent, use (non-standard) SHA-1 URNs, but BitTorrent hashes aren't portable since they encode BitTorrent-specific data, so compatibility is impossible either way. For a new system to use URNs when it can't support _any_ of the existing addresses seems pointlessly confusing. Enough has changed that it makes sense to have a clean break.

**Why doesn't it use magnet links?**
Magnet links were originally designed for tunneling other, arbitrary URIs over a single scheme, with the intended purpose of having a JavaScript handler on each web page be able to pop up and ask the user which application to resolve the magnet link with. They aren't technically related to content addressing at all. BitTorrent just uses them to wrap URNs.

**Why `hash:`?**
Because it's the most descriptive word for the requirements. All valid content addresses are hashes of one sort or another. `content:` could be a content literal, like `data:`.

I like that it's four letters and begins with H, like `http:`.

`hash:` URIs can be resolved by any application that hashes files and maintains a mapping of which files have which hashes. I'd like to write a demo resolver in probably 30 lines of Python at some point. No need for EarthFS.

**Why LMDB instead of SQLite or \[my favorite DB\]?**
I started with SQLite (actually, I started with PostgreSQL, before deciding that a single process design was too important to pass up). I even wrote a [custom VFS](res/async_sqlite.c) that turns SQLite into a completely asynchronous database (without changing its API! Disclaimer: don't use, it's slow and not production-ready).

Translating arbitrary user queries into efficient SQL is nearly impossible. I came up with three basic approaches but all of them had significant downsides. When I feel like a conspiracy theorist, I wonder if SQL is a tool promoted by Google to keep regular developers from [writing decent search engines](http://sommarskog.se/dyn-search-2008.html).

I also considered several LSM-tree databases, but LMDB has a very efficient implementation and I didn't end up completely sold that LSM-trees are worth it over b-trees.

Once SQLite4 is released, the plan is to switch to its back-end API so you can drop in any back-end that supports SQLite.

**Why build a "high level file system" on top of the real (OS) file system?**
EarthFS is built on top of the file system for the same reason most databases are built on top of the file system: because file systems are completely effective at their core purpose of multiplexing block devices between multiple applications.

EarthFS uses the file system as much as it can to provide concurrent write access, and uses a low level database to efficiently and atomically track meta-data. The database doesn't support concurrent writes, and the file system doesn't support ACID (directly).

EarthFS can't replace low level file systems and doesn't try. Carelessly mapping content addresses to file names, without all of features EarthFS provides to make them usable, doesn't work. EarthFS can track file names as meta-data, but it can't guarantee that names are unique. EarthFS files are immutable, and handling edits safely (in the face of network partitions) and efficiently requires extra planning.

Asking users to throw away their existing file systems in order to run EarthFS would be a show-stopper. The World Wide Web is fairly popular (to put it lightly), but ChromeOS still hasn't supplanted traditional operating system.

**What about copyright?**
Don't upload things you don't own the copyright to. You're free to ask people not to republish your content and send takedown requests to their web host or ISP if they don't comply. The laws don't magically change just because a new technology comes along.

That said, EarthFS would make mirroring much easier and more useful for people who allow it. Perhaps the benefits will encourage more people to share freely.

Just like everything these days, EarthFS could help the flow of information in repressive countries. Cell phones, Twitter--how does a modern dictator keep up with it all?

**What is the plan for "semantic hashes"?**
Once more people start using EarthFS (or other content addressing systems), I think demand for them will appear. For now they're on the back burner.

**How many files have you tested it with?**
Currently only around 15,000 (half files, half meta-files), which is my personal collection of notes. Most of these files are indexed for full-text search.

**Is it a platform?**
This might be better phrased as, "does EarthFS have value to offer other developers?"

The single biggest benefit of a platform is the audience of established users. Developers put up with a lot of abuse from companies like Facebook and Apple because they want access to a huge audience. Obviously, EarthFS isn't doing much on that front, and won't be for a while, at least.

If your application needs extremely robust decentralized sync support, then EarthFS might be of some use to you. If you can run EarthFS as a separate process, its HTTP API is very simple and powerful. If you want to embed it (which might be more practical for users who aren't already running it), the C API isn't stable yet.

From a practical, self-interested standpoint, you may be better off studying how EarthFS works and reimplementing just the parts you need. But if you need a significant chunk of it (e.g. the query system), it may be worth using directly.

If you need the features EarthFS provides _and_ would like to see it succeed, then using it in your applications would be mutually beneficial.

**What was influenced by your ego as a designer?**
Using (low level, portable) Objective-C instead of C++ for the filter system was partly due to my skill-set and partly due to the soft spot I have for that language. That was probably the biggest conceit I allowed. All (?) of the other decisions have stronger justifications.

**What's missing?**
A lot:

- Digital signatures, unfortunately (time constraints, crypto inexperience)
- Stable C API (libefs)
- Even lower latency by re-transmitting before validating
- Meta-data user interface
- Real-time blog interface (using the barest amount of JS)
- Blog statistics
- Full-text search spelling correction
- RSS publishing and subscribing
- Dump/reload
- Gzip compression (still needs `content-range` support)
- Sharding
- And more, of course


License
-------

EarthFS is provided under the MIT license. Please see LICENSE for details.

