# StellarFS

StellarFS is a zero-dependency, user-space virtual file system and storage container written in C11. It provides robust copy-on-write structural sharing, transparent block deduplication via SHA-256 content hashes, adaptive block compression, and a crash-safe multi-version write-ahead journaling log. Designed for systems engineers and storage architects who need an embeddable, predictable, and fully verifiable storage engine without complex OS kernel modules or unsafe memory management.

## Architecture

StellarFS manages a single contiguous block device partition, divided into uniform fixed-size blocks (default 4KB). The on-disk layout comprises:

1. **Superblock**: Located at block 0, storing magic bytes, partition metadata, total block counts, free-space bitmap pointers, journal head/tail offsets, and root B+Tree root node block IDs.
2. **Free-Space Bitmap**: A contiguous array of allocation bitmaps tracking every block's usage status (free, allocated, journal, or dedup reference).
3. **Write-Ahead Journal (WAL)**: A dedicated ring buffer region guaranteeing crash atomicity across multi-block updates. Modifications to inodes, bitmaps, or data blocks are committed to the journal before being applied to the live filesystem structures.
4. **B+Tree Inode Index**: A balanced multi-way search tree mapping canonical file paths (hashed or direct string keys) to inode descriptors containing permissions, file sizes, timestamps, and direct/indirect block pointer arrays.
5. **Deduplication Reference Tables & Content Store**: A cryptographic lookup table mapping SHA-256 block digests to physical block locations, enabling instant content-addressable deduplication.
6. **Data Blocks**: Raw payload storage with transparent compression headers.

```
+-------------------------------------------------------------------------+
                    StellarFS On-Disk Storage Layout                      
+-------------------------------------------------------------------------+
| Superblock | Free-Space Bitmap | Journal Log | B+Tree Inodes | Data/Dedup |
+-------------------------------------------------------------------------+
```

## Algorithms and Data Structures

- **B+Tree Inode Index**: All filesystem paths are indexed in an on-disk B+Tree supporting fast lookups, insertions, and deletions with guaranteed logarithmic search depth $O(\log N)$. Internal nodes store child block pointers and separator keys; leaf nodes store sorted key-value pairs mapping path hashes to inode block addresses.
- **Write-Ahead Journaling (WAL)**: Transactions group block mutations (bitmap updates, node writes, data writes). The engine writes transaction records containing header magic, sequence numbers, payload lengths, and checksums. During recovery, the journal is replayed if an ungraceful shutdown occurred.
- **Content-Addressable Deduplication**: Before writing any data block, StellarFS computes its SHA-256 digest. If the digest exists in the deduplication index, the physical write is bypassed, and the reference count for that physical block is incremented.
- **Transparent Compression**: Data blocks undergo an internal run-length and byte-packing compression pass. If the compressed payload size is smaller than the raw block size, the smaller payload is stored with a compression header, saving physical disk sectors.
- **Sparse File Allocation**: Unwritten file offsets or explicit holes are tracked as null block pointers in inode structures, ensuring zero physical disk consumption for unallocated regions.

## Invariants

1. **Atomicity**: No partial transaction state is ever exposed to the file system. Either all modifications within a journal transaction are committed or none are.
2. **Bitmap Consistency**: The number of set bits in the free-space allocation bitmap strictly equals the count of blocks marked as allocated in the superblock and metadata structures.
3. **Reference Count Integrity**: Deduplicated physical blocks maintain reference counts that never drop below zero and are accurately decremented upon file truncation or deletion.
4. **Tree Balance**: B+Tree node pointers remain valid and balanced across all insert, split, and delete operations.

## Build and Run

To build and run the test suite and CLI:

```bash
make all
./bin/stellar_cli create storage.img 1024
./bin/stellar_cli write storage.img /test.txt "Hello, StellarFS!"
./bin/stellar_cli read storage.img /test.txt
./bin/test_engine
./bin/test_integration
```

## Testing

StellarFS includes two comprehensive test suites:
- `test_engine.c`: Unit tests verifying individual subsystems including the block allocator, B+Tree manipulation, SHA-256 deduplication, compression, and sparse file mapping (10+ explicit assertions).
- `test_integration.c`: Integration and fault-injection tests simulating power failures mid-journal-write, bit-flip corruptions, disk full conditions, and recovery verification (10+ explicit assertions).

## Security and Privacy

StellarFS operates entirely in user space, eliminating kernel privilege escalation risks. SHA-256 cryptographic hashing guarantees collision resistance for deduplication blocks. Memory management uses strict boundary checks, explicit zeroization of sensitive buffers, and zero dynamic external dependencies.

## Performance Characteristics

- **Space Efficiency**: $O(1)$ duplicate elimination for identical blocks; sparse file support avoids allocating empty blocks.
- **Lookup Latency**: $O(\log N)$ for file path resolution via the on-disk B+Tree index.
- **Throughput**: Minimized disk I/O through batched journal commits and cached bitmap allocation scans.

## Limitations

- Fixed maximum partition size determined at initialization time.
- Single-node concurrency model (file locking must be managed by upper-layer application threads).
- Maximum path length and file size bounds constrained by fixed block pointer arrays.
