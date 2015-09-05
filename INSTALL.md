StrongLink Installation Guide
=============================

Warning: Not for the faint of heart! This process will be streamlined over time. The eventual goal is to have native GUI apps and one-click web hosting. For now, it will take some elbow grease.

Dependencies
------------

Most of the dependencies are included. You might need to install Git to clone the repository, or you can use GitHub's ZIP download.

Additional dependencies:

Ubuntu / Debian / Linux Mint: `sudo apt-get install gcc g++ gobjc cmake automake autoconf libtool pkg-config`

Fedora / RedHat: `sudo yum install gcc gcc-c++ gcc-objc cmake automake autoconf libtool`

OS X: Install the developer tools from the App Store and cmake from Homebrew. [TODO]

Windows: untested, probably needs work

You can substitute Clang for GCC if you prefer.

A semi-recent version of [Node.js](https://nodejs.org/) or [io.js](https://iojs.org/) is required for the Node example scripts including `sln-pipe`.

Basic Installation
------------------

1. `cd deps/libressl-portable && ./update.sh` (TODO: make this step go away)
2. `./configure && make`
3. `sudo make install`
4. `stronglink /repo-dir`

If you don't want to run `make install`, you will need to manually symlink or copy the `res/blog` directory into the repo directory before running. For example: `ln -s res/blog /repo-dir/`

Server Configuration
--------------------

Right now there is no configuration interface whatsoever (not even a config file). That means you need to edit the code and recompile to change any settings.

- Port number: set `SERVER_PORT_RAW` and `SERVER_PORT_TLS` in `src/blog/main.c`
- Server access: set `SERVER_ADDRESS` in `src/blog/main.c`
- Database backend: use `DB=xx make` where `xx` is empty (for MDB), `leveldb`, `rocksdb`, or `hyper`
- Guest access: set `repo->pub_mode` from `0` to `SLN_RDONLY` or `SLN_RDWR`
- Number of results per page: `RESULTS_MAX` in `src/blog/Blog.c`
- Repo name: by default the name of the directory, but override `reponame` in `src/blog/main.c`
- Internal algorithm: `SLN_INTERNAL_ALGO` in `src/StrongLink.h` (only change for new repos!)
- Number of bcrypt rounds: `BCRYPT_ROUNDS` in `src/util/pass.c` (default 13)

Database Backends
-----------------

StrongLink supports several database backends including MDB (the default), LevelDB, RocksDB, and HyperLevelDB. MDB is the smallest and most stable, and it has the best read performance (making it good for public-facing sites). LevelDB has better write performance and compression (so the database storage can take 1/5th the space).

RocksDB is not recommended but might be useful for specialized applications since it has a lot of tuning options. HyperLevelDB is not recommended since it's generally worse than LevelDB.

The backend can be chosen at build time by setting the `DB` environment variable (see above). This might become a runtime option in the future.

It's not too difficult to add support for any transactional (ACID) key-value store. Embedded, write-optimized stores and distributed stores are the most interesting.

MDB and LevelDB are included in the Git repository. Other backends need to be installed separately to be used.

Client Configuration
--------------------

Clients (for example the Node example scripts) are configured through a JSON file located at `~/.config/stronglink/client.json`. That config file looks something like this:

```json
{
	"repos": {
		"main": {
			"url": "http://localhost:8000/",
			"session": "[...]"
		}
	}
}
```

Currently there is no interface for managing session keys. The only way to get one is by logging into the blog interface using a web browser or a tool like cURL and checking the cookie that is sent back.

You can set up any number of repositories and the names (like "main" above) are up to you.

You can also use URLs instead of pre-configured names. The session will be `null` (public access).

Feedback
--------

If you need any help installing, configuring or using StrongLink, feel free to open a GitHub issue, even if it's not a "bug" per se. Updates or corrections on the documentation are also welcome.

