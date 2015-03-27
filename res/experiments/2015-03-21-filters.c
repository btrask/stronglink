

// okay, new attempt
// giving up on r-trees an all performance optimization for now
// the focus is 100% on figuring out what to do and doing it correctly



// previously:

int EFSFilterPassMetaFile(EFSFilterRef const filter, uint64_t const metaFileID);
int EFSFilterMatchFile(EFSFilterRef const filter, uint64_t const fileID, uint64_t const sortID);
int EFSFilterMatchMetaFile(EFSFilterRef const filter, uint64_t const metaFileID);


void EFSFilterFileStatus(EFSFilterRef const filter, uint64_t const fileID, uint64_t *const sortID);



- (uint64_t)age:(uint64_t const)sortID :(uint64_t const)fileID;




// what if the correct answer is moving more logic to the receiver?
// have the receiver download all the meta-files and ignore the ones it doesn't have targets for
// then every time it downloads a file, also request all of the meta-files for it

// or take it a step further and perform all querying on the receiver...

// its so ugly i cant stand it
// but what's better?

// there's gotta be something we're missing

/*

degrees of sync optimization:
0. transfer everything, do all querying on the receiver
	implications: no partial pulls at all
1. transfer all meta-files, have receiver decide which files to retrieve when
	implications: all meta-data gets synced everywhere
2. transfer matching files, have receiver retrieve meta-files
	implications: no way to notify on updates
	could re-emit the file and rely on the receiver's de-dup (not a bad idea?)
	naturally supports "bumping" for light clients (not sure we want that, but it could work)


okay lets stop here and think
that seems really simple and elegant and might be just what we want

i think we want to AVOID bumping for light clients
but they could have logic to prevent that themselves, probably

under this setup, when we pull a file we also pull all of the meta-files right away
which seems like a good thing

star wars: a new hope



but... why do we need extra requests in order to get the list of meta-files?
what if the list what just meta-files instead?
or... is that the problem?


NOTE: META-FILES DON'T MATCH QUERIES, FILES MATCH QUERIES
a single filter can be dependent on attributes from more than one meta-file
thus it doesnt even make sense to talk in terms of "which meta-files match a given query"



also, emitting files whenever theyre updated involves noticing the file was updated
which means checking whether the meta-file targeted any matching files

so like, back to the drawing board?


also i'm not sure moving queries to the reciever would help
if we cant do the queries it doesnt matter where we try to do them


okay so
simply "transfer matching files" is apparently a tall order
we could transfer them once, when they first match
or we could transfer them whenever CERTAIN meta-files get added
where those meta-files themselves (at least partially) match the query
but that's not very useful, is it?

basically, the sender is going to have to do a LOT of work for every new meta-file
because for each connected receiver, it has to determine whether any of the targets match

on meta-file:
	for connections:
		for targets:
			if target matches:
				send
				break;

it'd be easier to just send everything


so now what we're looking at
is two separate modes
one is like history mode, which sends a list of matching files with pagination, but no updates
the second one is real-time mode, which sends all meta-files
then the receiver can decide which meta-files to download based on whether it already has the target

in fact if the uri list had both the meta-file uri and the target uri, that'd be best
which is also something we've been trying to avoid
but if we just send the target uri we dont know which meta-file to download

i also dont like the idea of having two different modes
and having to switch between them

well we could have a single connection that sends both files and meta-files
either using the lazy meta-file uri -> target uri format
or else doing the target checking automatically, as above



the whole idea of "flatting" meta-files into the present time seems flawed
if we're trying to mostly preserve ordering...
which is looking more and more hopeless



what if we sent the list of metafile -> target pairs but the client didnt download them until it saw the target?


somehow the fundamental idea of "the receiver downloads the meta-files for the files it has" seems like a good one

the problem is just integrating the two ideas


nothing but the obvious to do, really...?


i like how simple it is
i hate how it has no symmetry


incidentally with this design we only need one connection for meta-files for any number of concurrent queries from the same host
so maybe mixing them is a bad idea

*/




























