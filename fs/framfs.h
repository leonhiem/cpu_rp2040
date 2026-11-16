/**
 * fs/framfs.h — a tiny flat filesystem on the FM24CL64B FRAM
 *
 * The FRAM is split into 64-byte blocks. A file is a chain of blocks,
 * not one contiguous run, so a file can grow without ever moving another
 * one (the FAT idea):
 *
 *   block 0        superblock: "pfs1", version, block size, block count
 *   blocks 1-8     directory: 32 entries of 16 bytes
 *                    name[12]  1-11 chars + '\0' ("" = free entry)
 *                    size      uint16, bytes
 *                    first     uint16, first block, or END for an empty file
 *   blocks 9-12    chain table: one uint16 per block
 *                    FREE (0) / next block / END (0xffff)
 *   blocks 13-123  data, 111 blocks = 7104 bytes
 *   blocks 124-127 NOT the filesystem's: the system area (board.h,
 *                  fs/hours.h), which mkfs never touches
 *
 * Chain entries are 16-bit so the same format scales to a bigger FRAM
 * (FRAM_SIZE in board.h): mkfs works out the layout from it.
 *
 * Write order keeps a power cut from leaving a directory entry that
 * points at garbage: new data blocks first, then their chain entries,
 * then the directory entry (one 16-byte write), then the old blocks are
 * freed. A cut in between can only leak some blocks (reclaim: mkfs).
 *
 * Directory and chain table are cached in RAM (768 bytes) and written
 * through to the FRAM on every change -- reads never touch metadata on
 * the chip, which keeps the code small.
 *
 * No offsets: a read returns the whole file, a write replaces it (or
 * appends). Same "one read, one whole value" model as every device.
 */
#pragma once

#define FRAMFS_MAX_FILE  1024   // keeps the append buffer and fs_pread()'s fallback small
#define FRAMFS_NAME_MAX  11

bool framfs_mount(void);                 // true if the FRAM holds a filesystem
bool framfs_mkfs(void);                  // format: all files gone

int  framfs_read(const char *name, char *buf, int len);   // bytes, or -1 if no such file
int  framfs_write(const char *name, const char *buf, int len, bool append); // len, or -1
int  framfs_remove(const char *name);    // 0, or -1
// Rename: rewrites only the name in the directory entry (one write, no
// data copied). An existing `to` is removed first, as Unix mv does.
int  framfs_rename(const char *from, const char *to); // 0, or -1

// index-th file (0..): name + size. Returns -1 past the last file.
int  framfs_list(int index, char *name, int namelen, int *size);
int  framfs_df(int *free_bytes, int *total_bytes);         // 0, or -1 if not mounted

// Check the filesystem (directory + chain table, from the RAM cache: no
// FRAM reads). Every file must have a valid name, size <= 1 KB, and a
// chain of exactly the right length ending in END, with no block used
// twice or out of range; names must be unique. Blocks marked in use that
// no file reaches are "lost" (a power cut mid-write leaves those).
// Fixes only what is safe: lost blocks are freed, and only when every
// file checked out fine. Damaged files are reported, never changed.
// Writes a report into out (may be 0). Returns the number of problems
// left (damaged files), 0 if clean, -1 if not mounted.
struct framfs_check { int files, used, free, lost, freed, damaged; };
int  framfs_fsck(framfs_check *r, char *out, int outlen);
