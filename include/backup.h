#ifndef BACKUP_H
#define BACKUP_H

struct backup_block
{
	unsigned int firstsector;
	unsigned int sectors;
};

struct backup_partition
{
	unsigned int sectors;
	char partno;
	char devno;
	char reserved[2];
};

struct zone_map_info
{
	unsigned int map_length;
	unsigned int zone_type;
	unsigned int fsmem_base;
	unsigned int min_au;
	uint64_t size;
};

struct device_info
{
	struct tivo_partition_file **files;
	int nparts;
	char *devname;
#ifdef RESTORE
	int swab;
	unsigned int sectors;
#endif
};

enum backup_format { bfV1, bfV3, bfWinMFS };

enum backup_state_ret {
	bsError = -1,
	bsMoreData = 0,
	bsNextState = 1
};

/* States of the state machine for backup and restore */
/* In addition to the state, there are 2 state specific values state_val1 */
/* and state_val2 which are set to 0 at every state change. */
/* The pointer in state_ptr1 is also set to NULL at every state change. */
/* The value shared_val1 is shared between states. */
enum backup_state {
	bsScanMFS = 0,
		// --- no state val used
			// No data consumed, just scans MFS for what should be backed up
	bsBegin,
		// shared_val1 initialized to sizeof backup header padded to 8 bytes.
			// Write backup header
/* Backup description collection */
	bsInfoPartitions,
		// state_val1 as current partition offset.
		// shared_val1 as offset within current block of last partition,
			// List follows immediately after backup header
/* v1 backup only */
	bsInfoBlocks,
		// state_val1 as current MFS block offset.
		// shared_val1 as offset within current block of last MFS block,
			// List follows immediately after partition list
	bsInfoMFSPartitions,
		// state_val1 as current MFS partition offset.
		// shared_val1 as offset within current block of last MFS partition,
			// List follows immediately after inode or block list
/* v3 backup only */
	bsInfoZoneMaps,
		// state_val1 as current zone map offset.
		// shared_val1 as offset within current block
			// List follows immediately after partition list
	bsInfoExtra,
		// state_val1 as current extra info offset.
		// state_val2 as current extra info index.
		// shared_val1 as offset within current block
			// List follows immediately after  zone map info
	bsInfoEnd,
		// If shared_val1 is not 0 or 512, consume remainder of block.
			// Consume partial block left by MFS partition list
	bsBootBlock,
		// --- no state val used
			// Sector 0 of the A drive
	bsPartitions,
		// state_val1 as current partition number.
		// state_val2 as offset within current partition.
			// Raw partitions to backup, one after another
	bsMFSInit,
		// --- no state val used
			// Loads the MFS volumes (Restore only)
/* v1 backup only */
	bsBlocks,
		// state_val1 as current MFS block.
		// state_val2 as offset within current MFS blocks.
			// Blocks read from MFS - all of MFS backed up
/* v3 backup only after this point */
	bsVolumeHeader,
		// --- no state val usage
			// Offset 0 of MFS volume
	bsTransactionLog,
		// --- no state val usage
			// Region referenced by volume header
	bsUnkRegion,
		// --- no state val usage
			// Region referenced by volume header
	bsMfsReinit,
		// --- no state val usage
			// Create zone maps and initialize MFS
	bsInodes,
		// state_val1 as current inode number.
		// state_val2 as offset within current inode.
		// state_ptr1 as pointer to current inode structure.
			// For each inode, inode meta-data followed by inode data in the
			// next 512 byte aligned block
	bsComplete,
		// --- no state val usage
			// 512 bytes with CRC at the end
			// Restore should check for CRC32_RESIDUAL as crc value at end
	bsMax
};

struct backup_info;

typedef enum backup_state_ret (*backup_state_handler[bsMax]) (struct backup_info *, void *, unsigned, unsigned *);

/* Backup engines */
extern backup_state_handler backup_v1;
extern backup_state_handler backup_v3;
/* Restore engines */
extern backup_state_handler restore_v1;
extern backup_state_handler restore_v3;

struct backup_info
{
/* Backup size */
	int cursector;
	int nsectors;

/* Backup state machine */
	uint64_t state_val1;
	uint64_t state_val2;
	uint64_t shared_val1;
	void *state_ptr1;
	enum backup_state state;

	enum backup_format format;

	backup_state_handler *state_machine;

	int ndevs;
	struct device_info *devs;

/* Stuff to backup */
	int nparts;
	struct backup_partition *parts;

// V1 backups only
	int nblocks;
	struct backup_block *blocks;
// V3 backups only
	int ninodes;
	unsigned *inodes;

	int nextrainfo;
	int extrainfosize;
	struct extrainfo **extrainfo;

	int nmfs;
	struct backup_partition *mfsparts;

// V3 backups only
	int nzones;
	struct zone_map_info *zonemaps;

/* Type of transaction to use for inode updates */
	unsigned int ilogtype;

	uint64_t appsectors;	/* Number of application data sectors backed up */
	uint64_t mediasectors;	/* Number of media data sectors backed up */
	uint32_t appinodes;		/* Number of inodes accounting for app data */
	uint32_t mediainodes;	/* Number of inodes accounting for media data */

/* Other backup stuff stuff */
	int back_flags;
	int crc;

/* Compression */
	struct z_stream_s *comp;
	char *comp_buf;

	struct mfs_handle *mfs;

/* Error reporting */
	char *err_msg;
	void *err_arg1;
	void *err_arg2;
	void *err_arg3;

#ifdef RESTORE
	struct volume_handle *vols;
	int nnewparts;
	struct backup_partition *newparts;
	int varsize;
	int swapsize;
	int bswap;
	int bitsize;

	void *extrainfodata;
#else
	unsigned int thresh;
	char *hda;
	unsigned int shrink_to;
#endif
};

/* Used to track information such as release and model number */
/* Also potentially for hints that restore can use on new backups without */
/* changing the format to add the hints. */
struct extrainfo
{
	unsigned char typelength;
	unsigned char datatype;		// For now 0 = string
	unsigned short datalength;
	char data[0];
};

struct block_info
{
	unsigned int size;
	struct block_info *next;
};

struct backup_head
{
	unsigned int magic;		/* TBAK */
	unsigned int flags;		/* Flags - only the lower 16 bits should be set */
	unsigned int nsectors;	/* Uncompressed backup size (512 byte sectors) */
	unsigned int nparts;	/* Number of non-MFS partitions backed up */
	unsigned int nblocks;	/* Number of blocks of MFS data backed up */
	unsigned int mfspairs;	/* Number of MFS volume pairs found */
	char reserved[488];		/* Padding to 512 bytes */
};

struct backup_head_v3
{
	unsigned int magic;		/* TBK3 */
	unsigned int size;		/* Size of the backup structure */
	unsigned short flags;	/* 16 bit since backup uses the lower 16 bits */
	unsigned short nparts;	/* Number of non-MFS partitions backed up */
	unsigned short nzones;	/* Number of MFS zone maps found */
	unsigned short mfspairs;/* Number of MFS volume pairs found */
	unsigned int ninodes;	/* Number of inodes backed up */
	unsigned int ilogtype;	/* Type of inode update log entries */
	uint64_t nsectors;		/* Uncompressed backup size (512 byte sectors) */
	uint64_t appsectors;	/* Number of application data sectors backed up */
	uint64_t mediasectors;	/* Number of media data sectors backed up */
	uint32_t appinodes;		/* Number of inodes accounting for app data */
	uint32_t mediainodes;	/* Number of inodes accounting for media data */
	unsigned int nextra;	/* Number of informational entries */
	unsigned int extrasize;	/* Size of informational entries */
};

#define TB_MAGIC (('T' << 24) + ('B' << 16) + ('A' << 8) + ('K' << 0))
#define TB_ENDIAN (('T' << 0) + ('B' << 8) + ('A' << 16) + ('K' << 24))
#define TB3_MAGIC (('T' << 24) + ('B' << 16) + ('K' << 8) + ('3' << 0))
#define TB3_ENDIAN (('T' << 0) + ('B' << 8) + ('K' << 16) + ('3' << 24))
#define BF_COMPRESSED	0x00000001	/* Backup is compressed. */
#define BF_MFSONLY		0x00000002	/* Backup is MFS only. */
#define BF_BACKUPVAR	0x00000004	/* /var in backup. */
#define BF_SHRINK		0x00000008	/* Divorced backup. */
#define BF_THRESHSIZE	0x00000010
#define BF_THRESHTOT	0x00000020
#define BF_STREAMTOT	0x00000040
#define BF_NOBSWAP		0x00000080	/* Source isn't byte swapped. */
#define BF_TRUNCATED	0x00000100	/* Backup from incomplete volume. */
#define BF_64			0x00000200	/* Backup is from a 64 bit system */
#define BF_COMPLVL(f)	(((f) >> 12) & 0xf)
#define BF_SETCOMP(l)	((((l) & 0xf) << 12) | BF_COMPRESSED)
#define BF_FLAGS		0x0000ffff
#define RF_INITIALIZED	0x00010000	/* Restore initialized. */
#define RF_ENDIAN		0x00020000	/* Restore from different endian. */
#define RF_NOMORECOMP	0x00040000	/* No more compressed data. */
#define RF_ZEROPART		0x00080000	/* Zero out non restored partitions. */
#define RF_BALANCE		0x00100000	/* Balance partition layout. */
#define RF_NOFILL		0x00200000	/* Leave room for more partitions. */
#define RF_SWAPV1		0x00400000	/* Use version 1 swap signature. */
#define RF_FLAGS		0xffff0000

struct backup_info *init_backup_v1 (char *device, char *device2, int flags);
struct backup_info *init_backup_v3 (char *device, char *device2, int flags);
void backup_set_thresh (struct backup_info *info, unsigned int thresh);
void backup_check_truncated (struct backup_info *info);
int backup_start (struct backup_info *info);
unsigned int backup_read (struct backup_info *info, char *buf, unsigned int size);
int backup_finish (struct backup_info *info);
void backup_perror (struct backup_info *info, char *str);
int backup_strerror (struct backup_info *info, char *str);
int backup_has_error (struct backup_info *info);
void backup_clearerror (struct backup_info *info);

struct backup_info *init_restore (unsigned int flags);
void restore_set_varsize (struct backup_info *info, int size);
void restore_set_swapsize (struct backup_info *info, int size);
void restore_set_mfs_type (struct backup_info *info, int bits);
unsigned int restore_write (struct backup_info *info, char *buf, unsigned int size);
int restore_trydev (struct backup_info *info, char *dev1, char *dev2);
int restore_start (struct backup_info *info);
int restore_finish(struct backup_info *info);
void restore_perror (struct backup_info *info, char *str);
int restore_strerror (struct backup_info *info, char *str);
int restore_has_error (struct backup_info *info);
void restore_clearerror (struct backup_info *info);
#endif
