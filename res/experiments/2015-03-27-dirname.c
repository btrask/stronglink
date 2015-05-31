

// because the standard dirname(3) is from a simpler time...



// we need to use the full path, in case the relative path is "." or something
// a bigger problem is if we start supporting chroot, wont the path be "/"?


// one idea is to immediately chdir to the repo dir
// then basically everything can use simple relative paths
// and it works with chroot just fine

// also in that case we dont have to worry about manaully concatenating strings
// just chdir and then getcwd
// and then we can use standard dirname?



// oh, the problem with this approach
// is that 1. it sucks for a host application when we're running as a library
// and 2. doesnt really work for opening multiple repos at once
// which ought to be supported, even if we currently have no use for it


// thus we should support "." or "/" as the repo dir, but not require it
// which means either way the repo itself cant use dirname


// we also want to support configurable repo names that arent the same as the directory name
// but i think it's reasonable to have EFSRepoCreate(dir, name)
// and then override name later if need be



// we could do some crazy function
// accept an array of relative/absolute paths
// then scan backwards from the last one until we get a real name

// separation of concerns, better to have two functions


// then even better, portability and windows support...



// okay, practical solution
// use chdir to handle resolution
// but change back afterward...?


// still need a portable dirname...

// lets just look for a library yo




