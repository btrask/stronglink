
/*
how do mutable tags work with queries?
normally with queries, once a result shows up, it never disappears
so the whole query history is immutable

or at least it should be
but there's also negations and other things which can cause problems...?

really, our queries reflect
1. the first point at which an item matched all of the terms
2. without considering whether it later stopped matching them

thats good for computers but less good for humans

do we need two separate modes?

or maybe we should have an "as-of" ID
so you can ask whether a file matched at a given point in time

or you can ask if it matched at any point in time, which is the above behavior

...OR, without negations, is it impossible for this problem to manifest?
mutable tags being a form of negation...



it seems like two modes are valid
1. list all files at the first point they matched, regardless of what happened later
2. list all of the files that still match and the the point at which existing files stop matching

perhaps these could be described as optimistic and pessimistic modes
or client-logic and server-logic?
or just broadest-match and narrowest-match?

in the second mode you actually want to notify each time each file becomes both available or unavailable



also our meta-file filter must be broken, right?
because the historically matching meta-files can change after the fact


basically "early" meta-files should bunch up at the target's first match


if the user is watching a live timeline while tags are being added and removed, what should happen?
i'd say those files should flicker into and out of existance in their existing positions


... i'm tempted to say we should move all filtering to the client
which is actually sort of okay now that we've gotten rid of per-user permissions
theres no longer any sort of information leak

this would only apply to the real-time results
but we need to be able to do queries anyway
so it shouldnt really matter right?

its also impossible for clients to do filtering and pagination at the same time



okay, so
without filtering, it'd be easy
just a list of file uris to read

with co-variant mutability, its still easy
each file just matches as of the first point it satisfies everything

contra-variance is the problem
if a real-time client is already displaying a file, we have to tell it to stop
if it matched but stopped, we have to tell them when it starts again

and changes that happen while not-matching should pile up until matching starts again
thats the key, isnt it?


then theres the question of what format the results should be in
mixed files and meta?
files only, meta only?

files-only doesnt make sense
but meta-only probably does...?


the other problem is how this interacts with pagination
a "light client" wont have all of a files history
so we need to be able to request aggregate state as of the the new matching point

that aggregate is not intended to be independently meaningful
its just a sop to light clients that dont track full state themselves



maybe for light clients we want to send the file uris
and for heavy clients we want to send the meta-file uris?

makes some sense because heavy clients care about the full historical state
whereas light clients only want aggregates anyway

in fact light clients only really need the "instantaneous aggregate"
they dont care about getting the aggregate as of a certain point in time

so light clients need to get each file uri any time that file changes
including one final time when it stops matching the query


for heavy clients its the same except with the meta-file uri


worth noting that for both heavy and light clients, the filter algo is the same
the only difference is how the results are presented, as files or meta-files
this is reassuring, makes me think we're on the right track for once


so how does this work
right now we compute one age and compare it to the current position
but in this brave new world
a single file/meta-file can have multiple "ages"

actually, meta-files still only have one age, right?


maybe we need to add an extra bit of information somewhere?
before, files either already matched or havent matched yet
but now, its also for files to previously to have matched, but no longer


we cant use simple comparisons to find some sort of global intersection
each filter/file combo will result in several ages
for example as a tag is removed and readded...?

thats like, so complicated

and then our collection filters are performing operations on these range sets...


if we're in a "matching mode," then all of the meta-files that match should pass through
if we're not, then all of the meta-files should be delayed unless we get one that makes us start matching

thats actually a very different model from before


basically we need to determine whether the current meta-file is the crucial piece that makes us start/stop matching

that means either:
1. the meta-file matches the filter and one of the targets doesnt otherwise
2. the meta-file doesnt ...wait


if the file already matches the filter, then the meta-file should be passed through as-is

...how the hell do we do this?




okay
so we have some filters
each of those filters matches some files/meta-files

we should probably move mime types into meta-files


anyway
full-text index must list meta-files, not files
thus our sort-id must be based on meta-file-id

so we go from filters to meta-files
and we get "meta-files that match", which causes a "start match" event
but we also get "meta-files that discontinue the match", end match event
thus we have these ranges for each filter

so we can ask a filter
"as of sort-id X, what is the status?"
and it just looks at the previous meta-file and checks whether it's positive or negative

except, status of what?
sort-id and file-id
thus we have to compute it for every file?


filter tag:flagged
well i dont know the storage/indexing details yet



presumably we have an index like
tags/meta-file-id

then for each meta-file we can look up files
and for each file we can look up its meta-files
and figure out if this particular meta-file is relevant...?

thats what we're doing already for fts etc


okay so i think the basic design is right
with using the sort-merge join
the union of all meta-files is the collection of possible "appearance points"

wait
what about negative meta-files?


as we go through the list
we reach two types of points
positives and negatives

when we reach a negative, it might match by itself
when we reach a positive, it might "unblock" several other meta-files too
between it and the past negative for that filter


we already have -match:, can we use that?
idk


it would be nice if we could reduce the abstractness of this code
but i'm not even sure that's possible


hmm
i didnt realize -age: was called by the top level function
for each sortid/fileid returned by our merge join

in which case it seems unlikely that our merge join has to change
except maybe it has to return more info




okay, hope
right now, the whole filter system answers "which files...?"
however, i believe we need it to answer "which meta-files?" instead

heavy clients can use the list of meta-files directly
for light clients we need to check whether the files are new or updated

that makes a certain amount of sense
because light client support seems like it should be built on top of the normal system


...however...
"which metafiles?" is directly based on which files they match



also, oh crap
we need to return all of the metafiles that target any matching files
not just the matching meta-files themselves


i'm starting to think we have to flatten our index from meta-files down to files
because its the files that determine which meta-files match

technically thats not as big a problem as i once thought
when we get a submission
we just look up the meta-files that match it
and reparse them, with the appropriate file-id

although technically it can mean that submitting a meta-file causes a lot of work
if it targets a large number of files
but realistically, thats probably fine


the real advantage of this denormalization
would be keeping data sorted... or something?

why not just use sort-merge joins everywhere?

wait
why not just use regular joins?
wtf are we even doing


filter -> metafiles -> files -> metafiles

the problem is
we're starting from the filter
the metafiles are sorted
but they can target files in any order
and then those files can have metafiles in any order

the sort-merge join we've been doing is a brilliant hack
because it takes the input meta-files and lets you narrow down the file positions
then -age: lets us sort without sorting

of course even that is still slow for intersections


i'm thinking we'd be better off coming from the other direction

metafiles -> files -> metafiles -> filters

that gives us proper pagination because we can start at the right spot and scan as far as necessary
and then just use real honest caching for everything


how does this setup effect light clients?

files -> metafiles ->filters
OR
files -> metafiles -> files -> metafiles -> filters
?

presumably the former, right?
actually...
neither
we still want metafiles -> files -> metafiles -> filters
we just present the list slightly differently
which i guess means the latter...

but no
metafiles still provide the ordering



maybe theres a lesson here
1. make it right
2. make it fast

dont try to optimize before we even know what we're doing


in which case
all we have to do
is give the filter a meta-file-id
and it can look up
1. for each file that is targeted
2. for each meta-file that targets it
3. does the meta-file match the filter
4. and if so, is it the same as the original meta-file-id

thats not quite right, but its the general idea




conceptually, we're sort of looping over all the meta-files twice

for each position:
	for each meta-file:
		if match then print

pretty gross

the meta-files that might possibly match at a given position are the meta^2
look for which files are touched by that meta-file
then look for which files reference them

if we open all of the targeted files in parallel
then we can walk through all of the targeting meta-files by merge-sort join

of course that by itself might be an unacceptably large number


start by doing it the obvious way, and then add "levels" later

so for meta-file-id X
we get A file-ids
then B URIs
then C meta-file-ids...?

so we end up with a bunch of cursors open on TargetURIAndMetaFileID
note that this isn't actually that bad
because even if there's 10 target files they probably mostly share uris
so the cursors collapse back down to a relatively fixed number
one per algo if we start doing uri normalization


so we need a function like
db_metacursor *EFSMetaFileOpenRelated(metaFileID);

and actually our meta-cursor could be implemented by one cursor plus seeking...?

if we can its better to parse the data and cache it
so that instead of several cursors or full keys
we could just store a list of individual integers


but basically, the point is
we dont care how its implemented, we want to walk the metafiles that reference a set of files
and its gotta be in order


okay, just assume we can do that, then what?
well, we seek to the current meta-file, and walk backward
at each step building up state about which filters are matching?

so for example
9 +c +d <--
8 -c
7 +b +c
6 +a

we start at meta-file-id 9
and we know c and d match
then we step backwards and c stops matching
then we step backwards and b and c match, plus d still
then we step backwards and now a-d all match

however... that probably involves stepping back to the beginning of time, to find out the original state
plus we dont know if maybe 'a' matched before meta-file-id 6

in conclusion we have to determine file matching from the beginning
not from the end or from the middle



okay so
if we take the current system
with the sort-merge join
and expand it to match negation (for mutable filter values)
and then enhance the matching

at each merged position
we have to determine all of the potentially backlogged meta-files



one idea to make this thing easier to understand
is to give the two main steps more prominent names
basically, we have -step: and -age:


so we're sort of on the right track

the problem is that right now we stop early in -age:
we just compute the first match for the given meta-file

instead we need to keep going up to the specified sort-id
basically each meta-file can have multiple ages (which we knew)

but the problem is that in order to compute the age
we have to start from the very beginning
and go through the whole history of each file, up to the desired point

this is not an uncommon problem in the world of versioning
how is it typically dealt with?
in fact, what is this problem called?

calculating the sum of several state changes...

also part of the problem is that a single meta-file can have several targets
so a lot of caching/state tracking probably isnt possible


....also
our -step: logic really is wrong, i think
right now the "set of possible steps" is based on changes to the filters
but thats just wrong, which is why our meta-file-filter is so broken

the changes to filtered attributes control what can show up or disappear
but if something is matching, any other changes to it are valid in the meanwhile


so if all we had were the up-edges and down-edges
could we flatten that into ranges?

in fact, it seems like that could be way more efficient than what we're doing now

wow, that might get rid of our age logic entirely

in each filter's meta-data cursor, seek to the appropriate sort-id
then just scan forward and backward to figure out the state of the range at that point
IFF all of our mutable attributes are binary, then there's no complicated state to compute
if the last state before X is on, then it must be on at X
as long as all of our filters are "simple", which seems fair

i guess the problem is there are zillions of states, one for each different file
so the last "ON" isnt for the file we're interested in (any of them)

if our data is organized by meta-file (i.e. forward search indices)
then we can look up the state for each file
which is linear time?



this could be represented by sql
when we talk about a single filter
for the purpose of debugging and optimization




-match:
file/attr/metafile
seek -1

thus we get either the current meta-file, if it matches directly
or the status of the previous match

however... the problem is this idea of indexing on file plus meta-file

instead what we have to do
is look up the meta-files for the given file
and then seek -1 in metafile/attr (or attr/metafile?)
must be attr/metafile

but we will get meta-files with different targets

*/





int EFSFilterPassMetaFile(EFSFilterRef const filter, uint64_t const metaFileID);
int EFSFilterMatchFile(EFSFilterRef const filter, uint64_t const fileID, uint64_t const sortID);
int EFSFilterMatchMetaFile(EFSFilterRef const filter, uint64_t const metaFileID);


// okay this isnt right but we're on the right track


// metafiles - just have a meta-file id
// look up all files the meta-file targets
// we want to know the state of the file as of the meta-file id (sort-id)
// files - file id and sort id
// we shouldnt have to look up all of the file's meta-files
// i think we should be able to start from the sort-id and scan backwards
// thats where things are likely to get slow, and where something like an r-tree might make sense

// however, you'll notice that this is actually a very close parallel to the code we have already
// match-meta-file is -match:
// and match-file is -age:, although it does the wrong thing

// one other idea
// right now the code is organized by class
// what if we organized it by function instead?
// so all of the match functions were in one file, all of the age functions were in one file
// etc...
// unfortunately that sounds a little bit kooky
// and it would make it a lot harder to add new filters...


// anyway
// at this point i think we have a reasonable base design
// unfortunately it's purely feed-forward
// we want to be able to reverse the direction of the join at least sometimes...

// incidentally thats a perfect use case for sql

// except that at the very least sqlite cant do it because its join algorithms are too limited?
// we absolutely need the output in sorted order after all...


// oh, hang on
// the other problem is
// we cant handle delayed meta-files like this
// when a file starts matching again, what do we do?

// its almost like we should return a cursor/iterator object thing


// and when we match a file as of a given sort-id
// we should get back an earlier sort-id for possible delayed items?

void EFSFilterFileStatus(EFSFilterRef const filter, uint64_t const fileID, uint64_t *const sortID);

// where sort-id is when to get the status of on input
// and the status for meta-files that target that file on output
// if output > input then the given meta-file should be passed for now
// if output <= input then you should emit all relevant meta-files between then and now

// incidentally that's pretty much how -age:: works now
// it takes a sort-id and returns a sort-id


// i think i get it
// we need to fix -age:: per above
// and we need to evaluate ALL meta-files the target file of each step
// and then maybe we need a final step of UINT64_MAX
// in order to evaluate all of the files that match at present
// well UINT64_MAX is probably a bad idea because it implies the future is set in stone
// but use the current sort-id

// dammit, that doesnt quite work either
// the meta-files between concurrently matching files need to be interleaved

// also we cant use UINT64_MAX or current sort-id without knowing all of the currently active files

// so close yet so far


// it seems fundamentally more efficient to get all of the sort-ids per matched file
// rather than go through each sort-id and check whether its file matches
// the problem is then we end up with everything in the wrong order
// merging doesnt work because we can have an arbitrary number of files

// incidentally leveldb is very good at sorting
// but its not a very good cache because it doesnt support ranged deletions




// more problems
// under this new system
// uri lists can change after the fact
// which is mostly good because it means you always get an optimized version
// and it shouldn't cause any problems with syncing
// but... we cant use the uris themselves for pagination then

// also that obviously means all of our caching efforts are doomed

// if we paginate on actual file uri, then it might be stable enough...?


// okay, change the definition slightly
// files that were once visible stay visible
// so the log is immutable forever
// only new files are delayed until needed




// i think the above difference is pretty simple
// we just need to compute visibility as of then instead of as of now
// it might actually be easier, in fact
// and it should mean that all caching stuff works as expected



// the problem with "just cache everything and let leveldb sort it out"
// is how do we handle real time results, especially as repos grow
// we need some ability to search particular ranges



// oh man
// i think i can actually see how r-trees would work
// quickly detect if there are no results in a large range
// rather than scanning each item in the range one by one

// is there an easy way to implement r-tree-like behavior on top of an existing key-value store?

// it seems like the obvious answer is, "its difficult"
// our best hope is to check the sqlite (and possibly postgres) source


// for the record this is like terrifying ground-breaking stuff here
// combining lsm-trees with r-trees? what do you bet that its never been done before?

// we're practically to the point where it makes sense ot write a custom data structure
// fucking multi-layer r-trees

// on the other hand we should also consider giving up on performance if we've proven its impossible with our current tools


// dammit
// i just checked and sqlite does in fact build its r-trees in the obvious way
// by storing each node of the tree in a big blob
// which we cant do while still getting any of the benefit of leveldb


// i think i can see an easy way of mapping r-trees to a kv store
// basically you assign each point an id, and then create an index based on id ranges
// the problem is assigning ids
// the idea being that you can do everything by remapping ranges rather than changing ids
// but that requires always assigning perfect ids up front
// in our case points are integer-based so we can always know the minimum possible distance
// if our r-tree layer exposes a "distance" function, we can do it i think

// wait no
// thats only if we're taking into account a single dimension...?

// and no we cant use random ids either, the whole point is to have nearby items group together

// floating point seems like it might help but it wouldnt really

// actually maybe the thing to do is use a string
// we can always make it longer to add "in between" values if we need to
// and initially all of the ids can be one character long

// that sounds pretty good
// and we can use, like the whole alphabet or something
// actually we could use 0x01-0xff...



// hang on for a second
// can we simplify this by specializing the secondary index to our existing data?

// by that i mean
// can we build an index on top of our index rather than defining some r-tree abstraction?

// yes we can, although...
// there comes a point when its better to just have the index abstracted out
// r-trees might still be the best way to do it

// anyway i like this idea about a million times better than caching



// hash://sha256/3949d37e06cbb1225d1c6a469cde699df30e3ecfbb7eb5493ec8f8b4f7438b0a

// okay after writing this i immediately realized our simple id system wouldnt work
// when we split a "node" (range), we have to divide up the items in it
// which means reordering them
// which means we need a real tree structure, in order to avoid remapping






























