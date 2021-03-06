#ifndef FSID_H
#define FSID_H

/* Prime number used in hash for finding base inode of fsid. */
#define MFS_FSID_HASH 0x106d9

typedef enum fsid_type_e
{
	tyNone = 0,
	tyFile = 1,
	tyStream = 2,
	tyDir = 4,
	tyDb = 8,
}
__attribute__ ((packed)) fsid_type;

/* For inode_flags below. */
#define INODE_CHAINED	0x80000000	/* More than one fsid that hash to this inode follow */
#define INODE_DATA	0x40000000	/* Data for this inode is in the inode header */

#define MFS32_INODE_SIG 0x91231EBC
#define MFS64_INODE_SIG 0xD1231EBC

typedef struct mfs_inode_s
{
	unsigned int fsid;			/* This FSID */
	unsigned int refcount;		/* References to this FSID */
	unsigned int bootcycles;	/* Number of boot cycles as of modification */
	unsigned int bootsecs;		/* Seconds since boot of modification */
	unsigned int inode;			/* Should be (sectornum - 1122) / 2 */
	unsigned int unk3;			/* Also block size? */
	unsigned int size;			/* In bytes or blocksize sized blocks */
	unsigned int blocksize;
	unsigned int blockused;
	unsigned int lastmodified;	/* In seconds since epoch */
	fsid_type type;				/* For files not referenced by filesystem */
	unsigned char zone;
	unsigned short pad;		/* Unused space, underlying 0xdeadbeef shows */
	unsigned int sig;			/* Seems to be 0x91231ebc */
	unsigned int checksum;
	unsigned int inode_flags;	/* It seems to be flags at least. */
	unsigned int numblocks;		/* Number of data blocks. */
	union
	{
		struct
		{
			unsigned int sector;
			unsigned int count;
		}
		d32[0];
		struct
		{
			uint64_t sector;
			uint32_t count;
		}
		d64[0];
	} datablocks;
}
mfs_inode;

typedef struct fs_entry_s
{
	unsigned int fsid;
	unsigned char entry_length;
	fsid_type type;
	unsigned char name[0];
}
fs_entry;

uint32_t mfs_inode_count (struct mfs_handle *mfshnd);
uint64_t mfs_inode_to_sector (struct mfs_handle *mfshnd, uint32_t inode);
mfs_inode *mfs_read_inode (struct mfs_handle *mfshnd, uint32_t inode);
int mfs_read_inode_to_buf (struct mfs_handle *mfshnd, unsigned int inode, mfs_inode *inode_buf);
mfs_inode *mfs_read_inode_by_fsid (struct mfs_handle *mfshnd, uint32_t fsid);
mfs_inode *mfs_find_inode_for_fsid (struct mfs_handle *mfshnd, uint32_t fsid);
int mfs_write_inode (struct mfs_handle *mfshnd, mfs_inode *inode);
int mfs_read_inode_data_part (struct mfs_handle *mfshnd, mfs_inode * inode, unsigned char *data, uint64_t start, unsigned int count);
unsigned char *mfs_read_inode_data (struct mfs_handle *mfshnd, mfs_inode * inode, int *size);
int mfs_write_inode_data_part (struct mfs_handle *mfshnd, mfs_inode * inode, unsigned char *data, unsigned int start, unsigned int count);

/* Simplified "greedy" allocation scheme */
/* Works well on a fresh MFS, not so well on a well used volume */
int mfs_alloc_greedy (struct mfs_handle *mfshnd, mfs_inode *inode, uint64_t highest);

#endif /*FSID_H */
