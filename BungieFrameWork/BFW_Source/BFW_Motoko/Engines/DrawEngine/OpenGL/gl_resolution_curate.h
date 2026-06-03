// ======================================================================
// gl_resolution_curate.h
// Pure helper: turn a raw list of (width,height) display modes into a
// clean list for the resolution menu. No SDL, no engine types, no globals,
// no allocation — unit-testable in isolation.
// ======================================================================
#ifndef GL_RESOLUTION_CURATE_H
#define GL_RESOLUTION_CURATE_H

// Curate raw (width,height) pairs into a clean menu list. Rules, in order:
//   1. Drop any pair with width < 640 or height < 480.
//   2. Deduplicate by exact (width,height).
//   3. Keep at most maxOut: if more remain, keep the LARGEST maxOut (the
//      native/largest resolution is never dropped).
//   4. Sort the result ascending (by width, then height).
//
// raw_w/raw_h hold rawCount input pairs. out_w/out_h are caller-provided
// buffers of at least maxOut ints. Returns the number written (0..maxOut).
// Returns 0 on any NULL pointer or maxOut <= 0.
int gl_curate_resolutions(const int *raw_w, const int *raw_h, int rawCount,
                          int *out_w, int *out_h, int maxOut);

#endif // GL_RESOLUTION_CURATE_H
