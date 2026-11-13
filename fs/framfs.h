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
 *   blocks 13-127  data, 115 blocks = 7360 bytes
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

#define FRAMFS_MAX_FILE  1024   // fits jobs.h's STAGE_BUF: pipes pass whole buffers
#define FRAMFS_NAME_MAX  11

bool framfs_mount(void);                 // true if the FRAM holds a filesystem
bool framfs_mkfs(void);                  // format: all files gone

int  framfs_read(const char *name, char *buf, int len);   // bytes, or -1 if no such file
int  framfs_write(const char *name, const char *buf, int len, bool append); // len, or -1
int  framfs_remove(const char *name);    // 0, or -1

// index-th file (0..): name + size. Returns -1 past the last file.
int  framfs_list(int index, char *name, int namelen, int *size);
int  framfs_df(int *free_bytes, int *total_bytes);         // 0, or -1 if not mounted
