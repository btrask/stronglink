StrongLink Security Information
===============================

StrongLink's security practices
-------------------------------

The long-term plan for StrongLink security is to follow the basic model of [qmail](http://cr.yp.to/qmail/qmailsec-20071101.pdf): avoid bloat and clamp down on unnecessary churn while trying to make the code obvious and going over everything with a fine-tooth comb.

Please remember this project as a whole is still very young. One goal of releasing it this early is to get feedback on the relative priority of different areas, including security.

Practices in use (pro tip: contributing guidelines):

- Zero pointers on free and check that freed memory is zeroed
- Read the man pages/docs before using functions
- Never use dynamic format strings
- Know which functions accept format strings and don't pass arbitrary strings by mistake
- Use explicit lengths in format string specifiers
- Use `snprintf` instead of `sprintf`
- Use custom allocating printf function (designed for ease of use)
- Use `-Wwrite-strings` to have the compiler mark all strings as constant
- Use `-Werror -Wall -Wextra` strict compiler warnings
- Don't use SQL (for performance, but no chance of SQLi)
- Use established parsing libraries like http\_parser and YAJL
- Use bcrypt for passwords
- Use 128 bits of entropy and SHA-256 for session cookies
- Use purely random tokens for session cookies, rather than some sort of meaningful data
- Don't require cloud storage for backup or syncing
- Use simple memory layouts without any custom allocators
- Perform privileged user operations through objects that manage their own access
- Generally prefer `calloc` over `malloc`
- Declare things `const` when possible
- Use `const` to control whether pointers can be freed (thus indicating ownership)
- Use mainly single-threaded code, with very clean abstractions for threaded sections
- Keep the whole project small and auditable
- Use "Yoda expressions" with the constant on the left to avoid accidental assignment
- Use appropriate `open(2)` args to control access and avoid race conditions
- Use read-only access when possible
- For "confusing" functions like `strcmp`, explicitly compare them to zero instead of using `!`
- Never split conditional statements onto multiple lines without braces
- Use `goto` tastefully to simplify error handling and cleanup when appropriate
- Use `reallocarray`
- Use standard, secure string functions like `strlcat` instead of rolling our own (where possible)
- Define string length constants as `(N+1)` to make nul termination explicit
- Use established HTML sanitization instead of DIY
- Use assertions to assert obvious invariants
- Ship with assertions on
- Test under Valgrind
- Test under Clang static analyzer
- Test under [Facebook Infer](http://fbinfer.com/) static analyzer

Planned (pro tip: places bugs might be hiding):

- Look into `-fsanitize`
- Check with other static analyzers
- Use guard pages around sensitive memory? (e.g. cookie cache)
- Support client-side encryption for remote backups
- Enable `-Wuninitialized`
- Check all math operations for possible overflow (possibly using compiler intrinsics)
- Perform fuzz testing on inputs
- Support socket activation
- Create some sort of high level test suite (mostly end-to-end tests rather than unit tests)
- Support Markdown parsing in a separate process with sandboxing
- Do thorough error checking everywhere (currently only in some places)
- Do parsing through a safe string wrapper/library (something like Nom...)
- Release a set of contributing guidelines (more than just the above)
- Contribute some new compiler warnings to Clang and/or GCC

Rejected:

- Built-in encrypted storage (should be left to a dedicated project)
- Drop privileges if started as root (shouldn't be run as root)
- Use static allocation to keep potential buffer overflows off the stack (not a real solution)
- Switch to Rust (maybe once it's ready for prime time in five years)
- [Content Security Policy](http://lcamtuf.coredump.cx/postxss/), except in certain cases (defense in depth is good, but security through obscurity is bad)

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

Bugs in StrongLink should be reported via GitHub issues. Bugs in other libraries should be reported to the maintainer of said library FIRST, and then referenced in a GitHub issue for the StrongLink project. Payouts for other libraries will be made when the those libraries' maintainers acknowledge their bugs.

Bugs must be new. Known issues DON'T count, even if there is no GitHub issue for them. But issues that aren't PUBLICLY documented somewhere DO count (unless they're embargoed, but we don't do that for now). If a version of StrongLink is released that uses an outdated library with known vulnerabilities, that DOES count.

Only libraries that StrongLink "bundles" count. Currently those libraries are:

- cmark
- content-disposition (really part of StrongLink)
- crypt_blowfish
- fts3
- http_parser
- leveldb
- libco
- lsmdb
- mdb
- multipart-parser-c
- openbsd-compat (only portions used by StrongLink)
- mumurhash3
- snappy
- libuv
- yajl

libc and other system libraries (including OpenSSL) are NOT included. libcoro is excluded because it is only used for debugging under Valgrind.

Payments will be made via PayPal.

The "minimum maximum" total payout is $500 (50 bugs). I won't cancel the offer before then, and I might choose extend it indefinitely beyond that (at least until I go broke). (Hopefully the limit will never matter because 50 security-related bugs would be a disaster.)

Anyone interested in the security of StrongLink or these libraries is encouraged to make a matching offer to increase the payout.

In the event of a dispute, my decision is NOT final. You can take me to court for your ten lousy dollars.

Critical vulnerability reporting
--------------------------------

Standard vulnerabilities should be reported through GitHub issues.

If you're an established security researcher, contact me privately and I'll give you my personal cell phone number so you can report "critical" vulnerabilities ASAP. (If you're not established, sorry, but just find and report some bugs first.)

For now there are no embargo requirements.

Bug advisory
------------

Reverse chronological order by date fixed (bugs that aren't fixed yet should also be listed):

- Unfixed: CSRF tokens are not used
- Unfixed: HTTPS is not supported
- Unfixed: Digital signatures (e.g. GPG or [OpenBSD's Signify](http://www.openbsd.org/papers/bsdcan-signify.html)) are not supported
- Unfixed: DOM-based template parsing is not used
- Unfixed: The user query parser and content-disposition parser use lots of raw pointer manipulation
- Unfixed: Potentially untrusted raw files are hosted within the same origin as the rest of the site and API
- 2015-05-30: Confused UV errors with DB errors in upload handler (not exploitable; fixed in 0065f969845981781bf2d3eab330a74a070dd079)
- 2015-05-21: Always ensure filter representation is nul-terminated (probably not exploitable; fixed in c9bd941573e124f06274ed9a9e24a8d115902b09)
- 2015-05-18: Read beyond string termination in user query parser (not exploitable; fixed in f9291586fbef19e85f30c32c502bbd5eacd777e3)
- 2015-04-08: Single quotes weren't being escaped in HTML output (not exploitable; fixed in c5acc7ec665dfd46967b8012c1021630c462d099)

[TODO] There are some older ones I'm sure.

