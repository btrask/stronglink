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

1. `./configure && make`
2. `sudo make install`
3. `stronglink /repo-dir`

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
- Logging: `SERVER_LOG_FILE` in `src/blog/main.c` (`stdout` or `NULL` for disabled)
- TLS ciphers and protocols: `TLS_CIPHERS` and `TLS_PROTOCOLS` in `src/blog/main.c`

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

What can I do with it?
----------------------

Once you install StrongLink, you will have an empty repository, which most people won't find very interesting. Here are some simple things you can do to play with it.

**Sync from my website using the `sln-pipe` tool**  
This is at least a cool demo even if it's not very useful. After configuring a local repository named "main" as above:

```sh
# Sync everything
./client/node-sln-client/examples/sln-pipe https://bentrask.com main
# Sync files matching a specific query
./client/node-sln-client/examples/sln-pipe https://bentrask.com main "query"
```

**Import files**  
You can import arbitrary files into the repository. You can tag and search them, although the interface/tools for this are currently kind of lacking. Full support is provided for plain text and Markdown files.

```sh
./client/node-sln-client/examples/sln-import main /path/to/files
```

**Add tags**  
Simple tagging is possible, although this isn't very friendly yet.

```sh
./client/node-sln-client/examples/sln-add-tags main tag "query"
```

**Look up a file by content hash on the command line**  
The `sln-cat` tool just takes a hash link and writes the content to standard output.

```sh
# Load a file from my site
./client/node-sln-client/examples/sln-cat https://bentrask.com hash://sha256/6834b5440fc88e00a1e7fec197f9f42c72fd92600275ba1afc7704e8e3bcd1ee
# Load a file from your own repo
./client/node-sln-client/examples/sln-cat main hash://sha256/address-of-your-file
```

**Write a custom importer**  
If you want to import your notes from an existing notetaking system, you might be able to export them and reimport them as files (see above), or you might need to write a new script. This should be pretty easy and the API is documented [here](https://github.com/btrask/stronglink/blob/master/client/README.md). Also take a look at the `sln-import` and `sln-blog-reimport` scripts.

**Start writing notes**  
StrongLink will be most useful to you if you use it for notetaking or blogging.

You can set up multiple repositories and sync notes between them. If you want bi-directional sync, you will need to run `sln-pipe` in each direction.

Please remember StrongLink is still alpha-quality software, and while it's generally pretty reliable, it's still very incomplete. The `sln-pipe` tool will eventually be replaced with a much faster native sync system, there will be an interface for seeing and modifying tags, etc.

Feedback
--------

If you need any help installing, configuring or using StrongLink, feel free to open a GitHub issue, even if it's not a "bug" per se. Updates or corrections on the documentation are also welcome.

