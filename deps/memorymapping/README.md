fmemopen for Mac OS and iOS
===========================

Originally ported from [ingenuitas python-tesseract](https://github.com/ingenuitas/python-tesseract/blob/master/fmemopen.c). Ported by Jeff Verkoeyen under the Apache 2.0 License.

From the fmemopen man page:

> FILE *fmemopen(void *buf, size_t size, const char *mode);
>
> The fmemopen() function opens a stream that permits the access specified by mode. The stream
> allows I/O to be performed on the string or memory buffer pointed to by buf. This buffer must be
> at least size bytes long.

Alas, this method does not exist on BSD operating systems (specifically Mac OS X and iOS). It is
possible to recreate this functionality using a BSD-specific method called `funopen`.

From the funopen man page:

> FILE * funopen(const void *cookie, int (*readfn)(void *, char *, int),
>                int (*writefn)(void *, const char *, int), fpos_t (*seekfn)(void *, fpos_t, int),
>                int (*closefn)(void *));
>
> The funopen() function associates a stream with up to four ``I/O functions''.  Either readfn or
> writefn must be specified; the others can be given as an appropriately-typed NULL pointer.  These
> I/O functions will be used to read, write, seek and close the new stream.

fmemopen.c provides a simple implementation of fmemopen using funopen so that you can create FILE
pointers to blocks of memory.

Adding it to your Project
=========================

Drag fmemopen.h and fmemopen.c to your project and add them to your target. `#include "fmemopen.h"`
wherever you need to use `fmemopen`.

Examples
========

```obj-c
#import "fmemopen.h"

NSString* string = @"fmemopen in Objective-C";
const char* cstr = [string UTF8String];
FILE* file = fmemopen((void *)cstr, sizeof(char) * (string.length + 1), "r");

// fread on file will now read the contents of the NSString

fclose(file);
```
