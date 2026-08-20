#pragma once
#define CHUNK_SIZE 32
// CHUNK_SIZE as a shift, so pixel -> chunk is a shift rather than a divide.
#define CHUNK_SHIFT 5
#define CHUNK_MASK  (CHUNK_SIZE - 1)

// A chunk is not a container: blocks live in one flat row-major array on the Grid, and a
// chunk is just two activity bytes for a 32x32 square. Tiling blocks per chunk instead cost
// an extra index multiply per pixel access and scattered the activity flags across the array.
