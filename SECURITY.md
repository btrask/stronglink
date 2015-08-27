StrongLink Security Information
===============================

Security practices
------------------

The long-term plan for StrongLink security is to follow the basic model of [qmail](http://cr.yp.to/qmail/qmailsec-20071101.pdf): avoid bloat and clamp down on unnecessary churn while trying to make the code obvious and going over everything with a fine-tooth comb.

Please remember this project as a whole is still very young. One goal of releasing it this early is to get feedback on the relative priority of different areas, including security.

Practices in use (pro tip: contributing guidelines):

- Language-agnostic
	- Keep the whole project small and auditable
	- Use read-only access when possible (for the file system, variables, etc.)
	- Read the man pages/docs before using functions
	- Know which functions accept format strings and don't pass arbitrary strings by mistake
	- Don't use SQL (for performance, but no chance of SQLi)
	- Use established parsing libraries like http\_parser and YAJL
	- Encrypt network connections using LibTLS, which has a very simple API
	- Use bcrypt for passwords
	- Use constant-time string comparison when necessary (currently nowhere)
	- Use 128 bits of entropy and SHA-256 for session cookies
	- Use purely random tokens for session cookies, rather than some sort of meaningful data
	- Don't require cloud storage for backup or syncing
	- Perform privileged user operations through objects that manage their own access
	- Do privilege checks as early and as low level as possible
	- Use established HTML sanitization instead of DIY
	- Variables holding sanitized data are given suffixed names to make it very obvious
	- Times are stored in UTC to avoid inadvertently leaking time zone information
	- Hash tables are randomly salted on startup
	- Use assertions to assert obvious invariants
	- Ship with assertions on
	- Test under Valgrind
	- Test under Clang static analyzer
	- Test under [Facebook Infer](http://fbinfer.com/) static analyzer
- C-specific
	- See the [C Programming Substance Guidelines](https://github.com/btrask/stronglink/blob/master/SUBSTANCE.md)

Planned (pro tip: places bugs might be hiding):

- Look into `-fsanitize`
- Check with other static analyzers
- Use guard pages around sensitive memory? (e.g. cookie cache)
- Support client-side encryption for remote backups
- Check all math operations for possible overflow (possibly using compiler intrinsics)
- Perform fuzz testing on inputs
- Support socket activation
- Create some sort of high level test suite (mostly end-to-end tests rather than unit tests)
- Support Markdown parsing in a separate process with sandboxing
- Do thorough error checking everywhere (currently only in some places)
- Do parsing through a safe string wrapper/library (something like Nom...)
- Write a simple new template system based on [Google's Gumbo HTML parser](https://github.com/google/gumbo-parser)
- Release a set of contributing guidelines (more than just the above)
- Contribute some new compiler warnings to Clang and/or GCC

Rejected:

- Built-in encrypted storage (should be left to a dedicated project)
- Drop privileges if started as root (shouldn't be run as root)
- Use static allocation to keep potential buffer overflows off the stack (not a real solution)
- Switch to Rust (maybe once it's ready for prime time in five years)
- [Content Security Policy](http://lcamtuf.coredump.cx/postxss/), except in certain cases (defense in depth is good, but security through obscurity is bad)

Analysis of major security decisions
------------------------------------

**Passwords**  
Passwords are hashed with [bcrypt](http://www.openwall.com/crypt/) using 2^13 rounds and the `$2b$` mode. Hashing only happens when creating a session, so performance isn't that critical. 2^13 rounds takes about half a second on my laptop. scrypt isn't used simply because it's too new, but the plan is to switch to it in a few years if it continues to gain traction.

**Sessions**  
Session keys are 16 bytes (128 bits) of random data given to clients in the format "[session-id]:[key-hex]". Session IDs are monotonic and not related to user IDs or other identifying information. Internally, session keys are stored as SHA-256 hashes (which is performance-critical) both in the database and in the session cache. Sessions also have an associated bitmask of permissions (currently just read and write).

**Session cache**  
The session cache keeps up to 1000 session IDs and hashed keys in memory to avoid hitting the database. StrongLink's API uses one HTTP request per resource, which means large numbers of small requests can be made while syncing. The session cache code is currently too complicated, buying performance at the cost of security. With the MDB backend, the cost of database lookups is very low.

**HTTPS**  
HTTPS support is provided using the high level LibTLS API that is part of LibreSSL. LibreSSL is currently statically linked, which means that the application will need to be rebuilt to update it (e.g. in the case of a security vulnerability). Dynamic linking support is planned, especially for platforms that bundle LibreSSL and other dependencies.

If HTTPS is enabled, StrongLink will refuse to start if there is an error from LibTLS or the server setup. If HTTPS and HTTP are simultaneously enabled, StrongLink will forcefully redirect all incoming connections from HTTP to HTTPS. It also sends the HSTS header over all HTTPS connections.

**On-disk storage**  
All storage, including database data and temporary files, is kept in the repo directory. Encryption of this data is seen as out of the scope of the StrongLink project, at least for now. At some point there may be a virtual file system layer added so that encryption could be done directly within StrongLink, similar to SQLite, although this would constrain the choice of database backends. For secure storage, users are recommended to use a loop mount or FUSE file system.

**Client API and syncing**  
Syncing is done using the client API, which supports HTTP and HTTPS. It will be possible to pin certificates for pushes and pulls. The client API allows applications and "plugins" to be isolated from the StrongLink instance, preventing bugs in the application from compromising the StrongLink repository. The client API uses sessions and can be constrained by session permissions.

**Templates**  
Templates are currently implemented using simple string substitution and must be carefully written (putting quotes around all HTML attribute variables) to avoid allowing JavaScript injection. A new template system using DOM manipulation is planned.

**Security documentation**  
Security issues are documented outside of the source code (here and using GitHub issues) in order to make basic security/trust analysis possible without full audits for busy, unpaid experts or even non-programmers.

Low stakes bug bounty program
-----------------------------

I will pay out $10 USD for each new security-related bug found in StrongLink.

Why so low?

- Because I'm paying out of my own pocket
- Because StrongLink is still alpha quality
- Because payouts are "no questions asked"

What does "no questions asked" mean? The intention is that I will err on the side of paying rather than arguing and not paying. For $10 it's not worth anyone's time to argue over.

- The bug does NOT have to be exploitable
- You do NOT have to provide a real-world scenario
- The bug DOES have to be "security-related"
- The bug does NOT have to occur under the default configuration
- But it DOES have to make some sense
- Bugs in libraries that StrongLink uses DO count
- You DO have to give details about the bug so it can be fixed
- Some questions MAY be asked (despite "no questions asked")
- But it DOESN'T have to be fixed for you to get paid

"Security-related" means "hypothetically exploitable in absence of other mitigating factors." "Defense in depth" is NOT an excuse. Denial of service or use in attacks against other systems (e.g. the qmail spam reflection problem) DO count.

Bugs in StrongLink should be reported via GitHub issues. Bugs in other libraries should be reported to the maintainer of said library FIRST, and then referenced in a GitHub issue for the StrongLink project. Payouts for other libraries will be made when the those libraries' maintainers acknowledge their bugs, or possibly before if the bug is just that obvious.

Bugs must be new. Known issues DON'T count, even if there is no GitHub issue for them. But issues that aren't PUBLICLY documented somewhere DO count (unless they're embargoed, but we don't do that for now). If a version of StrongLink is released that uses an outdated library with known vulnerabilities, that DOES count.

Only libraries that StrongLink "bundles" count. Currently those libraries are:

- cmark
- content-disposition (really part of StrongLink)
- crypt_blowfish
- fts3
- http_parser
- leveldb
- libco
- libressl-portable
- lsmdb
- mdb
- multipart-parser-c
- mumurhash3
- snappy
- libuv
- yajl

libc and other system libraries are NOT included. libcoro is excluded because it is only used for debugging under Valgrind. lsmdb is included despite not being used by default (because I wrote it).

In order to discourage "high frequency trading" of widely publicized upstream bug reports, the original discoverer of a published bug has 7 days to bring it to our attention. If we still haven't documented/fixed the issue after 7 days, anyone may report it and claim the bounty. If no bounty was awarded for a particular bug, the original discoverer may claim it at any time indefinitely, even after it was fixed.

Payments will be made via PayPal.

The "minimum maximum" total payout is $500 (50 bugs). I won't cancel the offer before then, and I might choose extend it indefinitely beyond that (at least until I go broke). (Hopefully the limit will never matter because 50 security-related bugs would be a disaster.)

Anyone interested in the security of StrongLink or these libraries is encouraged to make a matching offer to increase the payout.

In the event of a dispute, my decision is NOT final. You can take me to court for your ten lousy dollars.

False positives are worth negative $2.50. That means if you report 4 false positives and one true positive, you won't get paid anything. We won't come after you to collect on a negative balance, but we might blacklist you from reporting more bugs.

Critical vulnerability reporting
--------------------------------

Standard vulnerabilities should be reported through GitHub issues. Remember to set the "security" label.

If you're an established security researcher, contact me privately and I'll give you my personal cell phone number so you can report "critical" vulnerabilities ASAP. (If you're not established, sorry, but just find and report some bugs first.)

For now there are no embargo requirements.

Bug advisory
------------

Reverse chronological order by date fixed (bugs that aren't fixed yet should also be listed):

- Unfixed: CSRF tokens are not used
- Unfixed: Digital signatures (e.g. GPG or [OpenBSD's Signify](http://www.openbsd.org/papers/bsdcan-signify.html)) are not supported
- Unfixed: DOM-based template parsing is not used
- Unfixed: The custom parsers (user queries, content dispositions and query strings) use lots of raw pointer manipulation
- Unfixed: Potentially untrusted raw files are hosted within the same origin as the rest of the site and API
- Unfixed: A small number of slow queries can saturate the thread pool (denial of service)
- 2015-08-17: HTTPS support added
- 2015-07-18: Create dates in UTC to avoid leaking timezone (privacy leak; fixed in 73df2e409685f7acf320095b57af2aa3884988a2)
- 2015-05-30: Confused UV errors with DB errors in upload handler (not exploitable; fixed in 0065f969845981781bf2d3eab330a74a070dd079)
- 2015-05-21: Always ensure filter representation is nul-terminated (probably not exploitable; fixed in c9bd941573e124f06274ed9a9e24a8d115902b09)
- 2015-05-18: Read beyond string termination in user query parser (not exploitable; fixed in f9291586fbef19e85f30c32c502bbd5eacd777e3)
- 2015-04-08: Single quotes weren't being escaped in HTML output (not exploitable; fixed in c5acc7ec665dfd46967b8012c1021630c462d099)

Please see the [GitHub Issues "security" label](https://github.com/btrask/stronglink/issues?q=is%3Aissue+label%3Asecurity) for the most up-to-date and exhaustive list.

