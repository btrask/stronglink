StrongLink - a searchable, syncable, content-addressable notetaking system
==========================================================================

StrongLink is a notetaking/blogging system that supports search and sync. You can use content addresses in the format `hash://sha256/asdf` to link between entries, regardless of where they were created or where they're being viewed. Entries can't be edited after they've been written, which you can think of like writing in ink.

StrongLink is currently in alpha. Features are missing and things are broken.

Demo
----

The StrongLink development log is self-hosting. You can find thousands of notes spanning several years [here](http://notes.bentrask.com/).

FAQ
---

**How does it compare to other notetaking systems?**
StrongLink is designed to be highly scalable. It won't bog down or get harder to use as the number of notes grows. Designing the interface around a search engine is a major part of this.

A lot of notetaking systems still don't have sync. Many will lose data during conflicts or just at random. The reason sync systems are so bad is because application developers don't recognize the seriousness of the problem: doing sync properly means building a distributed system. StrongLink is a distributed system based on an append-only log and is available, partition-tolerant, and eventually consistent.

Currently the interface is web-based but I'd like to write a native Cocoa version at some point.

**How does it compare to other blog platforms?**
StrongLink is almost as fast as a static site generator and has built-in search. You can keep a local copy and write offline.

It doesn't have any themes or plugins yet.

**How does it compare to other content addressing systems?**
StrongLink follows my [Principles of Content Addressing](http://bentrask.com/notes/content-addressing.html). It focuses on providing content addresses as regular links you can use between your files.

I would be extremely happy if other projects took some notes from StrongLink's design.

I'd like to thank Juan Batiz-Benet, creator of IPFS, for giving me some valuable feedback.

**What is the status of the project?**
I've been using StrongLink (and earlier prototypes) for my own notes for years. So far I haven't lost any data.

It's currently missing a large number of features. There is no configuration interface yet, so you will need to recompile the code just to set it up. Basic security features are still missing too, like HTTPS and CSRF tokens, so you should know what you're doing.

The sync API will change before final release. There will be a way to migrate your notes.

**Why is it written in C?**
Eventually the core of StrongLink (without the blog interface) will be bundled as a reusable library. In this mode StrongLink would function more like a high level message queue, with high level filtering features. I'd also like to release a native version for iOS and Android, where footprint matters a lot.

A small portion of the code is actually written in portable Objective-C. So far it's been tested on Linux and Mac OS X.

We take [a long list of precautions](http://notes.bentrask.com/?q=hash://sha256/b5cfd43def108b74b5bb5da3ae92613fc27624811df8a6d1aea7ff558e8bc934) to ensure security, with more planned.

**My question isn't answered here...**
Please search the [development log](http://notes.bentrask.com/). Most questions have been answered and most suggestions have been considered.

