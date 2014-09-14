EarthFS - High Level Content Addressable File System
====================================================

EarthFS is a storage system that uses content addressing and search to organize and retrieve information. It exposes content addressing to the user as `hash:` links to immutable files.

EarthFS is well suited for personal notetaking. Its interface bears some similarity to [Notational Velocity](http://notational.net/). It makes linking between notes trivial, similar to a wiki such as [VoodooPad](https://plausible.coop/voodoopad/), but because notes are immutable, each note contains the changes as of a single point in time. This basically means turning the change log into the first class object, rather than the page. Instead of viewing a page that was built up over time, you search for a term and see each of the notes on that topic in order. Sync between repositories is fast, powerful (with full query support), and real-time.

The main interface runs in the web browser and can be used as a blog platform. Notes written locally (even while offline) can be automatically synced to a remote server, and it's easy to configure queries to control which files are published. The interface follows Dave Winer's design principle, [river of news](http://www.reallysimplesyndication.com/riverOfNews) \[broken link\]. EarthFS is nearly as fast as a static site but also has full search functionality. Entries in [GitHub-flavored Markdown](https://help.github.com/articles/github-flavored-markdown) are rendered using [Sundown](https://github.com/vmg/sundown).

EarthFS provides a complete API over HTTP (and HTTPS) so that other applications can access it directly. One such client, a Firefox extension for archiving web pages and making them content-addressable, is already in development.

EarthFS is written in C with a small amount of low-level, cross-platform Objective-C for the filter system. It uses the low level, high performance database [LMDB](http://symas.com/mdb/) to perform queries. Files are stored directly in the OS file system under their content hashes (with atomic operations and fsync). The server is asynchronous and uses [libco](http://byuu.org/programming/libco/) from BSNES.

Like anything, EarthFS has some limitations and makes some tradeoffs. There is no FUSE interface because EarthFS just isn't good at the same things as a traditional file system. This is true for [any storage system built on an ACID database](http://www.mail-archive.com/sqlite-users@sqlite.org/msg73451.html), and doubly true for content addressing systems, given their preference for immutability.

Rather than emulating mutable files via block de-duplication, EarthFS clients that need mutability (which is not expected to be many) should store diffs and use `hash:` links to track history relationships, similar to [how Git works](http://www.git-scm.com/book/en/Git-Internals-Git-Objects). EarthFS's sync system is _available_ and _eventually consistent_ per the [CAP theorem](http://aphyr.com/posts/278-timelike-2-everything-fails-all-the-time), which makes mutability difficult to get right, but EarthFS gives application developers the best tools to do it if they choose.

Building
--------

External dependencies (TODO):

- YAJL
- libarchive (TODO)

Commands:

```
$ make
$ sudo make install (TODO)
```

FAQ
---

**What about security?**
I know the climate, especially around new projects written in C. I believe I've taken reasonable precautions to avoid obvious bugs. For overall security I'd give myself a B. If you want a communication platform written by a real cryptographer where security is the top priority (above usability, etc.), try [Pond](https://pond.imperialviolet.org/).

The cryptography in EarthFS is plain HTTPS with OpenSSL (for portability; LibreSSL support is planned), based on [stud](https://github.com/bumptech/stud)/[stunnel](https://www.stunnel.org/index.html). Disk encryption is left to the underlying file system or disk.

You can use it locally for your own notes without any network access at all. Syncing can be done over LAN.

**How does this project compare to [Camlistore](http://camlistore.org/)?**
I have immense respect for Brad Fitzpatrick and the Camlistore team, and I only wish Camlistore the best. That said, I think they're making several _technical_ mistakes regarding content addressing and I hope they learn from EarthFS.

- Content addresses are not links (mantra: "raise high the [merkle DAG](https://en.wikipedia.org/wiki/Merkle_tree)!")
- Content addresses aren't portable (they're ["namespaced"](http://www.bentrask.com/notes/content-addressing.html) because they encode all sorts of Camlistore-specific meta-data)
- Random data (perma-nodes) are given content addresses (it's no longer a content addressing system because random data is not content; it can't handle network partitions where the same data is added on both sides)
- Only one pre-baked content addressing scheme (not sure about the validity of this criticism; they could easily add more if they haven't already)
- No concept of semantic hashes (more abstract/longer term problem)

I don't get the impression that Camlistore is the best at any of the many things it does. EarthFS is starting with a very narrow focus (notetaking and then maybe blogging) and definitely trying to beat existing players on their own turf (and it's hard enough even with such a narrow focus).

Camlistore is "multi-layer" (it says it right in the name) which I just don't think is generally a good idea. For example, block-level de-duplication is better done in the file system or disk controller than in an application that also syncs photos. Block de-dup also comes with [significant overhead](https://code.google.com/p/camlistore/issues/detail?id=197) when the (OS) file system doesn't know you're doing it.

**How does this project compare to [IPFS](http://ipfs.io/)?**
I exchanged emails with Juan Batiz-Benet and he was supportive and gave me some great feedback. I'm very thankful to him.

I'm very sorry about the similarity of the name, but I've been using this name privately for two years and I never managed to come up with a better one. But I'm happy with EarthFS being seen as a smaller project than InterPlanetaryFS. (Next up is PodunkFS.)

By providing mutability support and a FUSE layer I think IPFS has its work cut out for it. If someone opens up a file that uses [SQLite as its application format](https://sqlite.org/appfileformat.html) and starts doing lots of changes and transactions, what's going to happen? But on the other hand, no one seems to complain about Dropbox, so maybe it isn't a problem or can be easily resolved.

Juan didn't like the idea of content addresses as links, which I think is critical for a notetaking system. If IPFS has a standard mount-point, then for example `file:///ipfs/[hash]` could work.

I think that algorithm names should be human readable in links, whereas he proposes to [encode them in hexadecimal](https://github.com/jbenet/multihash).

**How does this project compare to [Named Data Networking](https://en.wikipedia.org/wiki/Named_data_networking)?**
I didn't hear about NDN until fairly recently, but it sounds like close to the same thing done at the networking layer rather than the application/storage layer. On top of the difficulty of deploying a new network, I suspect NDN has an incentive problem. When you shout your request into the network, why would any given system bother to respond? Maybe if it can attach its own ads to the content you requested, it would, but I don't think we (users or content producers) want that. Or maybe the expectation is for everyone's ISP to become their resolver, but I don't want them to have any more power than they already do.

With EarthFS, content address resolution is always performed by a single pre-configured repository (typically your own, run locally), so the responsibilities and incentives are clear.

**How does this project compare to a static blog?**
When hosted, EarthFS works similar to how a static site generator plus web server work. Its server is extremely high performance and written in C, using http_parser from Node.js and async network I/O from libuv. Files are converted to HTML once and then cached (forever, since they're immutable).

The biggest difference is that EarthFS directly supports user searches, which means that that the relevant files are opened and concatenated together into the output (which is by far the slowest part). Most queries take literally under a millisecond for 50 results (each additional search term adds around 500 _micro_seconds). Sending 50 files (180KB total) takes around 100 milliseconds on [my ancient laptop](TODO).

The attack surface is significantly larger at present. The web server itself is small, and there are only a few APIs, but EarthFS currently does HTML conversion (from Markdown, etc.) in-process. Supporting fully sandboxed parsing/conversion would be a very good idea.

Most people will probably find it easier to use than a static site generator.

EarthFS can be configured to accept comments directly. However, much of that functionality is still incomplete.

**Why would a content addressing system focus on notetaking?**
For me, the priority is notetaking (and, to a lesser extent, organizing other files). Content addressing is just a means to an end. If I could've made do with an existing notetaking system or something simpler without content addressing, I would've.

Immutability eliminates the (for me, crippling) mental pressure to go back and fix up old notes. Now I consider my old notes a record of exactly how stupid I was at any given point in time. They're evidence of what I didn't know as much as of what I did. That keeps me honest and saves me from the burden of maintaining a lie.

At the same time, hash links let me reference any note unambiguously, in a way that won't break, and without making me come up with unique names for each note. They're less fragile than time stamps or UUIDs. So when I want to correct something, I can reliably cite the original.

In my experience, personal wikis tend to accumulate cruft over time. Some pages grow without bound, while others never get more than a couple words. EarthFS shows search results in reverse chronological order, so the system is able to naturally "forget" stale or irrelevant information without actually throwing anything away. I believe this is critical for [any system with a human in the loop](TODO).

**Why 50 results per page (by default)?**
Because I'm not getting paid by the page view, unlike so many web sites, including search engines.

This fundamental shift of incentives explains a lot of decisions about how EarthFS works. For example, the filter system is very particular about the order it returns results in (basically reverse chronological, but with some deep subtleties) because it doesn't cost the user anything (not even a measureable amount of battery life) to generate results in that order. Google, on the other hand, could probably save millions if it shaved 0.1% without most people noticing the difference in results. They also have to index the whole Web, fight spammers, etc.

For my notes, on my screen, 50 is about the maximum before the scroll bar becomes completely unusable.

**Why doesn't it use URNs?**
TODO

**Why doesn't it use magnet links?**
TODO

**Why `hash:`?**
Because it's the most descriptive word for the requirements. All valid content addresses are hashes of one sort or another. `content:` could be a content literal, like `data:`.

And I like that it's four letters and begins with H, like `http:`.

`hash:` URIs can be resolved by any application that hashes files and maintains a mapping of which files have which hashes. I'd like to write a demo resolver in probably 30 lines of Python at some point. No need for EarthFS.

**Why LMDB instead of SQLite or <my favorite DB>?**
I started with SQLite (actually, I started with PostgreSQL, before deciding that a single process design was too important to pass up). I even wrote a [custom VFS](res/async_sqlite.c) that turns SQLite into a completely asynchronous database (without changing its API! Disclaimer: don't use, it's slow and not production-ready).

Translating arbitrary user queries into efficient SQL is nearly impossible. I came up with three basic approaches but all of them had significant downsides. When I feel like a conspiracy theorist, I wonder if SQL is a tool promoted by Google to keep regular developers from [writing decent search engines](http://sommarskog.se/dyn-search-2008.html).

I also considered several LSM-tree databases, but LMDB has a very efficient implementation and I didn't end up completely sold that LSM-trees are worth it over b-trees.

Once SQLite4 is released, the plan is to switch to its back-end API so you can drop in any back-end that supports SQLite.

**What about copyright?**
Don't upload things you don't own the copyright to. You're free to ask people not to republish your content and send takedown requests to their web host or ISP if they don't comply. The laws don't magically change just because a new technology comes along.

That said, EarthFS would make mirroring much easier and more useful for people who allow it. Perhaps the benefits will encourage more people to share freely.

Just like everything these days, EarthFS could help the flow of information in repressive countries. Cell phones, Twitter--how does a modern dictator keep up with it all?

**What is the plan for "semantic hashes"?**
Once more people start using EarthFS (or other content addressing systems), I think demand for them will appear. For now they're on the back burner.

**How many files have you tested it with?**
Currently only around 15,000 (half files, half meta-files), which is my personal collection of notes. Most of these files are indexed for full-text search.

**What was influenced by your ego as a designer?**
Using (low level, portable) Objective-C instead of C++ for the filter system was partly due to my skill-set and partly due to the soft spot I have for that language. That was probably the biggest conceit I allowed. All (?) of the other decisions have stronger justifications.

**What's missing?**
A lot:

- Digital signatures, unfortunately (time constraints, crypto inexperience)
- Meta-data user interface
- Blog statistics
- Real-time blog interface (using a pinch of JS)
- Full-text search spelling correction
- RSS publishing and subscribing
- Dump/reload (can be separate from the core project)
- Gzip compression (still needs `content-range` support)
- Sharding
- And more, of course


License
-------

EarthFS is provided under the MIT license. Please see LICENSE for details.

