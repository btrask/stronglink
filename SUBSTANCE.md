C Programming Substance Guidelines
==================================

**AKA Everything You Need to Know to Write Good C Code**  
**AKA The StrongLink Programming Guidelines**

This document is intended to provide _substance_ guidelines, as opposed to _style_ guidelines frequently proposed and debated elsewhere. Every guideline herein should have some intuitive (if not provable) rationale for how it improves code quality (mainly by making code less error-prone or more secure). Conversely, I will not tell you where to put your braces.

This article is written for experienced programmers, although not necessarily experienced in C. Familiarity with C is assumed. This article is _not_ about C++ (which I'm not at all qualified to write about).

If you're interested in learning C, Zed Shaw's [Learn C the Hard Way](http://c.learncodethehardway.org/book/) is probably very good, although I've only read a few parts.

This document is mainly about avoiding problems specific to the C programming language. Language-specific bugs are only a portion of the whole picture:

- Bugs in usage: careless use, social engineering...
- Bugs in deployment: misconfiguration, difficulty upgrading...
- Bugs in distribution: insecure channels, key management...
- Bugs in development - language-agnostic: databases, networking...
- Bugs in development - language-specific (**you are here**)
- Bugs in architecture: insufficient isolation, big ball of mud...
- Bugs in design: bad balance of tradeoffs...
- Bugs in requirements: not considering security a priority, over-complexity...

That said, I don't want to under-represent the importance of these bugs. Misuse-of-language bugs probably accounts for (pulling a number of out my hat) close to half of all software vulnerabilities, and C specifically has a lot of ways to be misused.

_Disclaimer: I didn't invent almost any of these techniques, although I independently rediscovered several of them._

_Disclaimer: This only explains how to program, not what to program or why._

_Disclaimer: This article is a work in progress. Like a list on Wikipedia, it may never be complete._

- General
	- Older C isn't better when it comes to security and avoiding bugs
		- K&R C is much beloved, but that innocent era is gone
		- We can't afford to [fight each other](http://zedshaw.com/2015/01/04/admitting-defeat-on-kr-in-lcthw/), or [C will](http://techcrunch.com/2015/05/02/and-c-plus-plus-too/) really [die](http://trevorjim.com/lets-sunset-c-c++/)
	- Things that are large and frequently changing will NEVER be secure
		- Doesn't matter what language they're in
		- The Linux kernel will never be secure, but thankfully you can just run it under a hypervisor
			- Unfortunately most hypervisors are ever-expanding as well...
	- Larger scales (e.g. project size) requires heavier duty techniques
		- I think it's possible to write secure programs in C up to maybe 25-50KLOC using these guidelines
		- The problem domain must be "well defined," meaning the code is not constantly changing
		- Larger projects get disproportionately hard to audit, and audits must keep up with the pace of changes
		- I'm skeptical whether something like a web browser can ever be secure, but I completely acknowledge that Rust is the only hope for it
		- If you can keep your project small, I think C can be used effectively
		- For the record, StrongLink is currently 10KLOC including some parts that could be broken off into separate libraries
	- If you care about security, the best thing you can do is keep the project small
		- I don't think there is actually much relation between programming language and code size
		- A small code size should be a badge of honor ("lines spent")
		- It's harder to write less code than more
- Memory management
	- Freeing
		- Immediately clear pointers after freeing
			- E.g. `free(x); x = NULL;`
			- Define a macro like `FREE(&x)` to do this for you
			- Prevents use-after-free and double free
			- If you can't clear a pointer marked `const`, you don't own it
		- Assert that buffers are entirely zeroed before freeing
			- Don't just `memset` to 0, `FREE`/clear each field individually
			- Catches memory leaks
			- You can skip this for known safe buffers (like strings)
		- When you free a sensitive buffer, use `explicit_bzero()` first
		- When you move a pointer from one variable to another, null out the old one
			- E.g. `x = y; y = NULL;`
			- Then you can unconditionally free `y` without problem
		- It would be nice to have a compiler feature to assert that all stack variables are zeroed when they go out of scope
	- Allocation
		- Use `calloc` instead of `malloc`
		- Be careful when using `reallocarray`
			- You should generally track used and unused space in dynamic arrays, so uninitialized memory shouldn't be a problem there
	- Lifespans
		- Use `const` to indicate and enforce ownership
			- `X *const` can't be null'd so it can't be freed
			- `X const *` doesn't coerce to `void *` so it can't be freed either
		- Have a single owner and know what it is
			- Have related objects share an owner to simplify lifespans and cleanup
		- Use reference counting when necessary (last resort)
			- Avoid cycles
			- Parent pointers should be weak (non-owning)
				- Avoid them entirely when possible
		- Use `get`/`init` or `create`/`copy`/`alloc` in function names to indicate ownership versus borrowing for return values
	- Stack size
		- Limit recursion
		- Use loops instead of relying on TCO (personal preference)
			- Don't be fancy
		- Don't decare large arrays on the stack
		- Always declare the worst case size instead of using `alloca(3)`
		- Use nested local scopes when necessary
	- Cleanup in case of errors
		- C's biggest weakness
			- But other languages don't even allow allocations to fail
			- C error handling is hard but that's because it's trying
		- Limited options in C (in order of preference)
			1. Use stack allocation so no cleanup is needed
			2. Do allocations in a parent function and pass them in
			3. Use `goto`
			4. Use separate functions so cleanup stays simple
			5. Tie many allocations together into a single object
			6. Combine error handlers after several smaller operations
			7. Free temporary buffers ASAP so future errors can ignore them
		- Note: all of these options are situational and require consideration
		- Also consider using an assertion or abort() on failure
			- `assert(x)` is basically equivalent to Rust's `x.unwrap()`
	- Make allocations as static as possible
		- Just allocate the maximum always
	- A buffer overflow on the stack is worse than one elsewhere
		- [Some people will say](https://news.ycombinator.com/item?id=9202212) you should avoid reading untrusted data into stack-allocated buffers
		- I think that's a bridge too far
		- Safe-Stack, Code-Pointer Integrity, and other techniques will help (if you can use them)
- General errors
	- Off-by-one errors
		- Know the difference between indexes and counts
		- Consider what will happen in the case of zero or one items
		- Write loops in a few consistent patterns
	- Floating point
		- Don't use floats, aside from user input and output (including graphics)
			- Don't use floats for money, duh
		- Don't compare floats using strict equality
		- Don't divide by zero
		- NaN
			- Don't use floats and you'll never run into it...
	- Error checking
		- Do it, always, even in sample code
			- Error handling is all programming is
				- The problem is an error and your program is the handler
			- Handle errors like everyone is watching
		- Keep error checks short so they are less painful to read/write
			- Single line if possible: `if(rc < 0) return rc;`
			- When several steps are needed use `rc = rc < 0 ? rc : expr;`
			- If you're writing a web server, you should be able to use `return 404;`
			- Anything longer is suffering, even `return HTTP(404);`
		- Translate codes from meaningful to callee to meaningful to caller
			- Single line if possible: `if(X == rc) return Y;`
			- Most of the time this shouldn't be necessary
		- Try not to mix error code sets within a single function or API
			- Convert all error types into one as early as possible
			- Don't use libraries that have bad error handling
				- Or worst case, wrap them
		- Functions should have a policy of either reporting or returning errors
			- Low level reusable functions should only return them
			- High level app-specific functions can report them
				- Don't go too crazy, just log to `stderr`
				- Or show them to the user, depending
	- Assertions
		- Don't do anything with side effects in assertion statements
		- Don't assert things that aren't obviously invariants
		- Consider shipping with assertions on
		- Include descriptive strings like `assert(cond && "msg")`
			- Or `assert(!"msg")`
		- Use an assertion macro that accepts a format string
			- Have the assertion print the value of what it's checking
			- `assertf(rc >= 0, "Example error: %s", my_strerror(rc));`
	- Line-based diff bugs ("goto fail")
		- Put one-statement conditionals on the same line as the condition
			- `if(rc < 0) goto fail;`
			- Copy and paste or diff errors are less dangerous and more obvious
			- If it doesn't fit on one line, put it in braces
			- Be very careful about adding a second statement on the same line (with braces obviously)
				- I do this sometimes but I know it's bad practice
		- Put a comma after the last element in arrays (if one element per line)
		- Put logical units on the same line
			- Otherwise wrap them in parentheses or braces
	- Integer overflow/underflow
		- Use `calloc`/`reallocarray` (from OpenBSD) instead of `malloc`/`realloc`
		- Use overflow-checking compiler intrinsics
		- Respect the integer types used by each API (`int`, `size_t`, `ssize_t`...)
	- Macros
		- Wrap variable expansions in parentheses
		- Wrap multiple statements in `do {} while(0)` or `({})`
			- Do not be afraid of trivial, widely supported compiler extensions
		- Prefix scoped variable names with underscores to prevent shadowing
			- Double underscore is reserved for the implementation, but you have my permission to use it judiciously
			- If you're worried, come up with your own prefix like `xx__something`
		- Use local variables to avoid evaluating expressions multiple times
			- And use `__typeof__(var)` on compilers that support it
	- Never explicitly compare booleans to true or false
		- False is unnecessary and true is incorrect
		- Don't bother explicitly comparing things to `NULL` either
	- Use the style of expression to convey the type of check being done
		- Don't use `!strcmp()`, compare with zero instead
- Strings
	- Format strings
		- Always use strings literals to specify format strings
		- Use "%s" instead of directly passing string variables
		- Know which APIs expect format strings
			- Passing a dynamic string where a format string was expected is a security vulnerability
			- Name functions taking format strings ending in "f"
	- `scanf`
		- Always specify string widths
		- Be careful about locales
	- String functions
		- Use `strlcpy` and `strlcat` from OpenBSD
	- Define string length constants as `(X+1)` to indicate nul termination
		- If you're using UTF-8 (which you should be), remember the length is bytes, not characters
	- Use `signed char` for strings and `unsigned char` for buffers
		- `typedef unsigned char byte_t;`
		- Compiler warnings will catch accidental conversions
	- Have a string type for borrowed strings
		- I like `typedef char const *strarg_t;`
- Threading
	- Shared resources
		- Know when to use locks, conditions and semaphores
			- They should come in pairs
		- Declare the mutex together with the variables it protects
		- Generally avoid read-write locks
			- If you need a read-write lock, you need a better data structure
		- Know when `volatile` is necessary and then use locks instead
	- Patterns
		- Thread pools (task scheduling)
			- Best general approach?
		- Worker threads (data consumers)
			- Not as good because pool logic is mixed with task logic?
			- Finer grained cancellation?
- File system
	- `fsync` (including on directories after renaming contents)
	- Atomic operations (rename, link, unlink...)
	- Specify permissions on `open` to avoid race conditions
	- Use `O_CREAT | O_EXCL`
	- Short `read`s and `write`s
		- Handle EINTR
- Creating good APIs
	- [Some say](http://mollyrocket.com/jacs/jacs_0004_0008.html) it's the hardest skill in programming
	- Empathize with the person who has to use your API
	- Write sample code using your API before you define it
	- Don't hide power or assume you know better than your user
	- Public APIs should have some sort of prefix or namespace
- Modularity
	- Code duplication
		- Plugins should have the minimum boilerplate possible
			- The more plugins you have, the less duplication you can afford
			- Abuse preprocessor macros if you have to?
	- Code that changes together should be grouped together
	- Code should be factored so that changes don't ripple throughout the codebase
	- Files are C's primary namespaces
		- Declare file-local variables and functions as `static`
- Testing
	- Tools
		- Your best tool is the compiler
			- Read the list of available compiler warnings
		- Static analyzers (but compiler warnings are better)
		- Sanitizers and runtime checkers (e.g. Valgrind)
		- Fuzzers (e.g. [american fuzzy lop](http://lcamtuf.coredump.cx/afl/))
	- No one can physically see what a computer is doing, so we need tools to visualize it for us
		- A lot of these tools don't exist yet, in any language
		- Unfortunately we need them to be universal and open source, so there isn't much money in it
		- For niche cases you will need to write your own tools anyway
	- Unit testing
		- At least do end-to-end testing for stable APIs you expect other people to use
	- Manual testing
		- There is no substitute for manual testing
		- Test how things break, not just how they work
	- Debug with the same optimization mode as release (eg. `-g -O2`)
		- Turn it down when necessary for the debugger
- Header files
	- Use them properly for keeping your public and private interfaces separate
		- Aside: any language that uses keywords like `public` and `private` to determine visibility instead of placement/scoping is bad
	- You should probably have one header for a collection of `.c` files
		- Not one header per `.c`
	- `static` functions in headers are always hacks
		- Not all hacks are bad, just keep it in mind
- Pointers
	- Use `struct x asdf[1]` to get pointers with inline storage
		- Lets you avoid `.` or excessive use of `&`
		- You should pretty much always use `->` instead of `.`
		- I think this is the most important trick for making C code beautiful
	- Use `&x[5]` instead of `x+5` when you need the address of an array element
		- It's just more explicit
	- Use `x[0]` to avoid `(*x)`, sometimes?
		- Both are bad though
		- If you have an "OO" API, define a macro like `MSG(obj, func, arg)`
			- Try to avoid APIs like that though
	- Use `const` in function signatures to communicate use of pointer args
		- Declare the top pointer `const` to make it clear what can change
			- E.g. `X **const asdf`
		- It helps, despite the fact that the top `const` isn't actually part of the signature
	- Use in-out args for recursion, especially tree walking
- Naming conventions
	- Different conventions from different libraries is not a bad thing
		- Consistency is a poor substitute for taste
			- Although it's better than nothing
		- When wrapping/extending a library, keep its conventions
	- Use a different convention between low level and high level interfaces
		- Or between public and private interfaces
		- An example of this is SQLite
	- It's nice if related names are the same length
		- E.g. `next` and `prev`, `src` and `dst`
		- Preserve and enhance symmetry
		- In a comparator callback, `a` and `b` are perfectly suitable
	- Simplify complex expressions with named local variables
		- And declare them `const`
		- Write code in "single static assignment" form
	- I don't care if you use camelCase, under_scores or whatever
- Avoid conspicuous complexity
	- Use post-increment over pre-increment unless it matters
	- Yoda conditions...? Iffy... (I'm used to them now...)
		- It's better to catch accidental assignment with compiler warnings
		- There's still an argument for putting the shorter term first
	- Complicated solutions better solve very important problems
	- Go out of your way to keep common things simple (eg. error handling)
	- Preserve, create and exploit symmetry
		- E.g. use vectors (in the physics sense) instead of enums for directions
- Performance
	- Choosing good optimizations and avoiding bad ones is the core of programming
		- Know when an optimization is too complicated to be worth it
		- If it's really necessary, find a simpler way
		- 20% of the optimizations make 80% of the difference
	- Do not worry about 1% speedups
		- SQLite has gotten significantly faster from lots of ~1% boosts
			- ...And it'll never be as fast as MDB
	- Almost never use fancy data structures
		- Funny how C is so fast without built in hash tables or anything else
		- Most of the time it doesn't matter and the overhead isn't worth it
		- Sometimes it does and gets inconvenient, though...
	- Study data-oriented design
		- The appropriate memory representation depends on the access patterns
		- Random Access Memory doesn't exist
		- When performance matters, linked lists are really bad
	- The `restrict` keyword should be used rarely if ever
	- The `inline` keyword should be used rarely if ever
		- Compilers basically ignore it, for good reason
		- If function call overhead is your bottleneck, you've probably done something wrong
		- Don't deal with audio one sample at a time
		- Don't deal with buffers one byte at a time
			- Ask Linus Torvalds... Or Kay Sievers
- Build system (not a thorough analysis)
	- Use `-Wall -Wextra -Werror`
		- But turn off errors you don't care about instead of cluttering your code
			- `-Wno-unused`
		- If you turn off a warning, document why
			- It will be the only thing keeping your head attached when that warning would've caught a critical vulnerability
	- Use `-Wwrite-strings`
		- Makes string literals `const`
		- Forces you to use `const` correctly almost everywhere
			- More importantly, prevents you from freeing constant strings
	- I wish we had more good compiler warnings
- Study great real-world C projects
	- [SQLite](https://sqlite.org/) (the gold standard for any codebase in any language)
	- [PostgreSQL](http://www.postgresql.org/) (although I haven't personally looked at it)
	- [MDB](http://symas.com/mdb/) (not always perfect but very efficiently written)
	- Code from [OpenBSD](http://www.openbsd.org/) (it doesn't look special, it's just simple and error-free)
	- [Apple's CoreFoundation API](https://developer.apple.com/library/prerelease/mac/documentation/CoreFoundation/Reference/CoreFoundation_Collection/) (my model for modern object-oriented C code)

See, that wasn't so hard, was it?

Why is this list so long and complicated?
-----------------------------------------

Optimization always causes problems, regardless of what you're optimizing for. Look at the failures of capitalism. When you try to write efficient, security-critical code in C, you are serving two masters: security and performance. Obviously, serving one master is easier.

It's worth pointing out that while there is a lot of messy shit written in C, some of the [most secure](http://cr.yp.to/qmail/qmailsec-20071101.pdf) software [of all time](https://sel4.systems/) is written in it too.

Incidentally, these examples sit at opposite ends of the spectrum: qmail was written by a single genius who basically kept it all in his head; seL4 was written using formal methods. Both techniques constrain the size and change rate of projects, which I believe is the true key to security.

Real-world code
---------------

I follow these techniques as much as possible in my project [StrongLink](https://github.com/btrask/stronglink/). You can read the code to see these principles applied in the real world. Nothing is perfect, so bug reports are welcome.

Also check the [Security Information page](https://github.com/btrask/stronglink/blob/master/SECURITY.md).

Other sources
-------------

- [Linux kernel coding style](https://www.kernel.org/doc/Documentation/CodingStyle)
- [Tips for C libraries on GNU/Linux](https://git.kernel.org/?p=linux/kernel/git/kay/libabc.git;a=blob_plain;f=README)
- [JPL Coding Standard](http://lars-lab.jpl.nasa.gov/JPL_Coding_Standard_C.pdf)

