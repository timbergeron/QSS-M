# Screenshot readback and PNG encoding

The screenshot command previously read GPU pixels directly into CPU memory.
Encoding and saving already used the image worker, but `glReadPixels` could
still wait for rendering and transfer to finish on the frame thread.

Screenshots now use a pixel pack buffer and a fence when the current GL context
supports both. The main thread polls the oldest transfer with a zero timeout,
copies its pixels only after completion, then hands it to the existing worker.
GPU transfers and worker jobs share the two-image / 256 MiB queue limits.
The same framebuffer is captured at command execution time. Video restart and
shutdown drain transfers before destroying GL state. Drivers without the
required support, unavailable workers, and failed transfer setup use the
existing direct readback path.

Hardware gamma tables are captured with each request and applied on the worker,
so subsequent gamma changes or video restarts cannot alter a pending image.
PNG compression uses zlib's Huffman-only path when `USE_ZLIB` is enabled. The
existing PNG filtering and lossless pixel precision are preserved. Builds
without zlib retain the built-in compressor.

## Measurements

A local benchmark encoded the same 960 x 720 screenshot pixel fixture with
the old and new PNG paths, using optimized builds and 18 samples per path
across alternating batches:

| Encoder | Median time | PNG bytes |
| --- | ---: | ---: |
| Previous built-in compressor | 56.288 ms | 927,975 |
| zlib Huffman-only | 33.435 ms | 919,451 |

This fixture encoded about 41% faster and produced a 0.9% smaller PNG. This
measures encoding, not the entire screenshot command. Content, resolution,
zlib version, and CPU affect the result.

A separate comparison of zlib strategies used the same fixture and 18 samples
per strategy across alternating batches. Huffman-only took 36.465 ms versus
39.741 ms for `Z_RLE`. RLE produced 914,955 bytes, only 0.5% smaller than
Huffman-only, while taking about 9% longer. Huffman-only remains the default.

GPU readback was exercised with Mesa llvmpipe (software OpenGL 4.5). Its
full-engine timing was noisy and showed only a modest difference. No hardware
GPU is exposed in this environment, so a hardware readback speedup has not been
established. Buffer submission can still stall inside the driver; the change
avoids explicitly mapping an unfinished transfer during normal frames.

## Validation

- Linux engine build completed; changed GL code also passed MinGW x86 and x64
  syntax checks.
- `python3 Misc/stress/test_screenshot_readback.py` passed with ASan/UBSan and
  a real offscreen GL context. It checks exact pixels, frame ownership, FIFO
  ordering, nonblocking polling, queue limits, pack-state restoration, failure
  cleanup, gamma snapshots, and draining before context destruction.
- `ASAN_OPTIONS=detect_leaks=0 python3 Misc/stress/test_sdl3_clipboard.py` passed
  with both PNG compressors. It decodes PNGs to check pixels, gamma correction,
  file/clipboard reuse, orientation, and clipboard ownership. Leak detection is
  disabled because the local execution environment cannot run LeakSanitizer;
  the harness also tracks its own image ownership.
- An instrumented full-engine run saved 17 TGA/JPEG screenshots without readback
  errors. A synchronous/buffered pair captured from the same framebuffer was
  byte-identical.
- The final production binary exercised PNG/TGA/JPEG captures, burst queue
  rejection, video restart, and normal exit without readback errors. The pending
  pre-restart PNG retained its 960 x 720 dimensions; the following capture used
  the new 1024 x 768 mode.
