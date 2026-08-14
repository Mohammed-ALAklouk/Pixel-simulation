#pragma once
#define CHUNK_SIZE 32
// CHUNK_SIZE as a shift, so pixel -> chunk is a shift rather than a divide.
#define CHUNK_SHIFT 5
#define CHUNK_MASK  (CHUNK_SIZE - 1)

// A chunk is no longer a container. Blocks live in one flat row-major array on
// the Grid, and a chunk is just the activity bookkeeping for a 32x32 square of
// them -- two bytes in a side array, indexed by chunk coordinate.
//
// Holding the blocks inside the chunk meant every pixel access resolved a chunk
// index (a multiply by sizeof(Chunk)) before it could resolve the block (another
// multiply), and the two activity flags sat 5 KB apart from each other across the
// grid, so marking chunks awake strode over the whole block array to touch two
// bytes per chunk.
