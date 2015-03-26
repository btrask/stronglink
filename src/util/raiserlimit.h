// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

static void raiserlimit(void) {
#ifdef __POSIX__
  // Raise the open file descriptor limit.
  {  // NOLINT (whitespace/braces)
    struct rlimit lim;
    if (getrlimit(RLIMIT_NOFILE, &lim) == 0 && lim.rlim_cur != lim.rlim_max) {
      // Do a binary search for the limit.
      rlim_t min = lim.rlim_cur;
      rlim_t max = 1 << 20;
      // But if there's a defined upper bound, don't search, just set it.
      if (lim.rlim_max != RLIM_INFINITY) {
        min = lim.rlim_max;
        max = lim.rlim_max;
      }
      do {
        lim.rlim_cur = min + (max - min) / 2;
        if (setrlimit(RLIMIT_NOFILE, &lim)) {
          max = lim.rlim_cur;
        } else {
          min = lim.rlim_cur;
        }
      } while (min + 1 < max);
    }
  }
#endif  // __POSIX__
}

