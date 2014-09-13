EarthFS - High Level Content Addressable File System
====================================================

EarthFS is a storage system that uses content addressing and search to organize and retrieve information. It exposes content addressing to the user as `hash:` links to immutable files.

EarthFS is highly suited for personal notetaking. Its interface bears some similarity to [Notational Velocity](http://notational.net/). It makes linking between notes trivial, similar to a wiki such as [VoodooPad](TODO), but because notes are immutable, each note contains the changes as of a single point in time. The analogue of a mutable wiki page is a search query, returning all the notes on a given topic. Sync between repositories is fast, powerful (with full query support), and real-time.

The main interface runs in the web browser and can be used as a blog platform. Notes written locally (even while offline) can be automatically synced to a remote server, and it's easy to configure queries to control which files are published. The interface follows Dave Winer's design principle, [river of news](TODO). EarthFS is nearly as fast as a static site but also has full search functionality. Entries in [GitHub-flavored Markdown](TODO) are rendered using [Sundown](TODO).

EarthFS provides a complete API over HTTP (and HTTPS) so that other applications can access it directly. One such client, a Firefox extension for archiving web pages and making them content-addressable, is already in development.

EarthFS is written in C with a small amount of low-level, cross-platform Objective-C for the filter system. It uses the low level, high performance database [LMDB](http://symas.com/mdb/) to perform queries. Files are stored directly in the OS file system under their content hashes.

Like anything, EarthFS has some limitations and makes some tradeoffs. There is no FUSE interface because EarthFS just isn't good at the same things as a traditional file system. This is true for [any storage system built on an ACID database](http://www.mail-archive.com/sqlite-users@sqlite.org/msg73451.html), and doubly true for content addressing systems, given their preference for immutability. Rather than emulating mutable files via block de-duplication, EarthFS clients that need mutability (which is not expected to be many) should store diffs and use `hash:` links to track history relationships, similar to [how Git works](TODO).

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

License
-------

EarthFS is provided under the MIT license. Please see LICENSE for details.

