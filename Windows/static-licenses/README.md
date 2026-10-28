`winpthreads-COPYING` is the complete upstream
[mingw-w64 winpthreads COPYING file](https://github.com/mingw-w64/mingw-w64/blob/master/mingw-w64-libraries/winpthreads/COPYING),
retrieved on 2026-10-28. It includes both the mingw-w64 and Lockless notices.
MinGW builds append it to the packaged `third-party-licenses.txt` because
`-static -pthread` embeds the toolchain's winpthreads runtime.
