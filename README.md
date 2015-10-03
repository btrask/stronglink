StrongLink - a searchable, syncable, content-addressable notetaking system
==========================================================================

StrongLink is a notetaking/blogging system that supports search and sync. You can use content addresses (hash links) to link between entries, regardless of where they were created or where they're being viewed. Currently, entries can't be edited after they've been written, which you can think of like writing in ink.

A raw hash link in StrongLink looks like this:

>     hash://sha256/6834b5440fc88e00a1e7fec197f9f42c72fd92600275ba1afc7704e8e3bcd1ee
>     hash://sha256/6834b5440fc88e00a1e7fec1 (short version)

- `hash`: the URI scheme. ([`ni`](http://tools.ietf.org/html/rfc6920) might be supported in the future)
- `sha256`: the hash algorithm. A variety of algorithms are supported.
- `6834b544...`: the hash which uniquely identifies a particular file.

Content addresses are unique in that they are _statically_ assigned by a _decentralized_ authority (the hash algorithm, which anyone can run). "Static" means the content for a given hash can never change once the algorithm is created. "Decentralized" means the same file will always be assigned the same hash, even by two people (or computers) without talking to each other. This is different from URLs, which are dynamically assigned by a centralized web server (although you can have many servers), and also different from ISBNs which are assigned dynamically by a central authority but then never changed.

With StrongLink, you resolve hash links using your own repository, which you can run locally (or wherever you want: desktop, server, or eventually mobile).

StrongLink is currently in alpha. Features are missing and some things may be broken. Check the [open issues](https://github.com/btrask/stronglink/issues), where most of the major problems are documented.

It currently [takes some work to set up](https://github.com/btrask/stronglink/blob/master/INSTALL.md), but the goal is to be as easy to run as Notepad or WordPress.

Demo
----

My notes on StrongLink development are self-hosted. You can find over a thousand notes spanning several years [here](https://bentrask.com/). You can [resolve the hash link above](https://bentrask.com/?q=hash://sha256/6834b5440fc88e00a1e7fec197f9f42c72fd92600275ba1afc7704e8e3bcd1ee) or [search for something](https://bentrask.com/?q=Dave+Winer). Keywords are [also supported](https://bentrask.com/?q=keyword%3Dhumor).

If you install StrongLink yourself (which currently is an [involved process](https://github.com/btrask/stronglink/blob/master/INSTALL.md)), you can make your own mirror by syncing from my site.

FAQ
---

**How does it compare to other notetaking systems?**  
StrongLink is designed to be highly scalable. It won't bog down or get harder to use as the number of notes grows. Designing the interface around a search engine is a major part of this.

A lot of notetaking systems still don't have sync. Many that do will lose data during conflicts or just at random. The reason sync systems are so bad is because application developers don't recognize the seriousness of the problem: doing sync properly means building a distributed system. StrongLink is a distributed system based on an append-only log and is available, partition-tolerant, and eventually consistent.

Currently the interface is web-based but native versions are planned.

**How does it compare to other blog platforms?**  
StrongLink is intended to be almost as fast and secure as a static site generator, although it currently falls short on both fronts. Unlike most blog software, it has built-in search, and you can keep a local copy and write offline.

It doesn't have any themes or plugins yet.

**How does it compare to web search engines?**  
StrongLink is built like a search engine, although it has some different design goals.

Since it's designed to search a single repository rather than the entire web, it can take fewer shortcuts and return more accurate results. The index is fully ACID, so there is no delay before results show up or chance of files getting "missed." Results are ordered chronologically rather than by fuzzy relevance heuristics (which is admittedly sometimes a downside). It doesn't (currently) use stopwords, and stemming can be changed on a per-repository basis.

Your searches never leave your machine. It can index private files without exposing them to a third party.

A major challenge of the design is being able to sync a large index quickly between repositories. After a lot of blood, sweat and [tears](https://github.com/btrask/lsmdb/), we at least have the understanding needed to make syncing relatively efficient.

**How does it compare to other content addressing systems?**  
StrongLink follows my [Principles of Content Addressing](https://bentrask.com/notes/content-addressing.html). It focuses on providing content addresses as regular links you can use between your files.

I would be extremely happy if other projects took some notes from StrongLink's design.

I'd like to thank Juan Batiz-Benet, creator of [IPFS](http://ipfs.io/), for giving me some valuable feedback.

**What is the status of the project?**  
I've been using StrongLink (and earlier prototypes) for my own notes for years. So far I haven't lost any data (although there have been some close calls).

It's currently missing a large number of features. Most major problems are [documented as GitHub issues](https://github.com/btrask/stronglink/issues).

The [client API](https://github.com/btrask/stronglink/blob/master/client/README.md) (used for syncing) will change slightly before version 1.0 is reached. There will be a way to migrate your notes.

The server API (used for embedding as a library) is completely unfinished and isn't ready (or documented) for public consumption.

**What happens in case of a hash collision?**  
In the event of a hash collision, StrongLink always uses the oldest file with the given hash, which theoretically allows "squatting" but prevents unexpected overwriting or other abuse.

StrongLink supports several hash lengths. By default, long hashes are used (32 bytes), which are as secure as possible. Medium length hashes (24 bytes) are still secure but slightly shorter. Short hashes (12 bytes) are robust against accidental collisions and convenient to speak, type, or write down.

Long hashes make collisions extremely unlikely. For instance, in the old SHA-1 algorithm, no collisions have ever been found. SHA-256, the default in StrongLink, makes collisions even less likely and harder to find.

**What happens if something "immutable" is changed?**  
There are two basic immutable "things" in StrongLink:

- Files: StrongLink follows the [end-to-end principle](https://en.wikipedia.org/wiki/End_to_end_principle), meaning that hashes are expected to be checked _by the receiver_. If a file within a repository is modified, that repository itself will not notice, but other repositories and validating clients will. StrongLink is not intended to replace filesystem-level redundancy or integrity checks such as from RAID or ZFS.
- Logs: Modifying StrongLink's append-only log after it has been written should only be done with care. Inserting or deleting files is mostly harmless, but reordering files could mess up remote clients.

StrongLink marks immutable files as read-only on systems that support it.

**Will it ever support mutability?**  
Hopefully, yes. The plan is to build a mutable interface layer on top of the immutable data store. This will keep StrongLink's sync protocol simple and reliable while gradually supporting a broader range of uses. Mutable tags will come first, and then eventually mutable files (using diffs rather than block deduplication).

File deletion support is also planned, but deletions will only apply to the current device (they won't sync).

**What about typos?**  
The long term solution is mutability (see above). Semantic hashes (TBA) might also be interesting. Right now, think of it like writing in ink.

You can of course submit a new file with any changes you want.

**Is it secure?**  
Just like the vast majority of software you probably use every day, StrongLink is grossly insecure.

Story time: I've been using [QubesOS](https://qubes-os.org/) as my main system for the past six months. Qubes is the only desktop OS that takes security seriously (like how OpenBSD is the only server OS that takes security seriously), but despite that I've been vulnerable to exploits from all directions. Qubes itself was vulnerable to [many major problems in Xen](http://xenbits.xen.org/xsa/), and it can't even help with problems with the hardware (Rowhammer), network stack (TLS), and [its own core utilities](https://groups.google.com/forum/#!topic/qubes-users/kR2fMpZFtV8). Despite that, everything else is _worse_.

Computer security is currently in a catch-22. Security researchers and cryptographers are too afraid to make real-world applications and protocols because they know they'd mess up and ruin their reputations. Application developers are all too happy to promise the world because they don't know what security means and have no reputation for it worth mentioning. I'm an application developer, no matter how much time I force myself to spend studying security issues. While I know many of the pitfalls, I had to choose features over security in order for this project to ever see the light of day.

Please see the [StrongLink Security Information page](https://github.com/btrask/stronglink/blob/master/SECURITY.md) for more information.

**As a programmer, are there any lessons I can take home?**  
In the process of developing StrongLink, I've learned two things I can say with confidence:

1. Strongly consider using content addresses over UUIDs or GUIDs in almost all cases. This is especially useful for distributed systems.
2. With hashing, less is more. Don't hash meaningless data or your concept of identity will be meaningless.

**My question isn't answered here...**  
Please search [my notes and development log](https://bentrask.com/). Most questions have been answered and most suggestions have been considered. If you still have any questions or concerns, feel free to open a GitHub issue.

