#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#include "mfs.h"
#include "log.h"

static char *progname;

static struct mfs_handle *mfs;
static int hexvals = 0;

static void
usage ()
{
	fprintf (stderr, "Usage:\n%s [-i inode] [-f fsid] [-t transaction] [-s sector] [-c count] [-h] [-b] /dev/hda [/dev/hdb]\n", progname);
	fprintf (stderr, "    -f	Read from fsid\n");
	fprintf (stderr, "    -i	Read from inode\n");
	fprintf (stderr, "    -l	Read from transaction log\n");
	fprintf (stderr, "    -s	Read from sector, or from offset into file\n");
	fprintf (stderr, "    -c	Read count sectors, where applicable\n");
	fprintf (stderr, "    -C    Perform consistency checkpoint before displaying data\n");
	fprintf (stderr, "    -h	Display in hex, no matter the format\n");
	fprintf (stderr, "    -b	Display in binary, no matter the format\n");
	fprintf (stderr, "    -x    Display formatted values in hex\n");
}

static void
hexdump (unsigned char *buf, unsigned int sector, unsigned int size)
{
	int ofs;

	for (ofs = 0; ofs < 512 && size > 0; ofs += 16)
	{
		unsigned char line[20];
		int myo;

		if (sector == 0xdeadbeef)
		{
			printf ("%03x ", ofs);
		}
		else if (sector == 0xffffffff)
		{
			printf ("\t");
		}
		else
		{
			printf ("%08x:%03x ", sector, ofs);
		}

		for (myo = 0; myo < 16; myo++)
		{
			if (size--)
			{
				printf ("%02x%c", buf[myo + ofs], myo < 15 && (myo & 3) == 3 ? '-' : ' ');
				line[myo] = (isprint (buf[myo + ofs]) ? buf[myo + ofs] : '.');
			}
			else
			{
				line[myo] = '|';
				line[myo + 1] = '\n';
				line[myo + 2] = 0;
				do
				{
					printf ("  %c", myo < 15 && (myo & 3) == 3 ? '-' : ' ');
				}
				while (++myo < 16);
				printf ("|%s", line);
				return;
			}
		}

		printf ("|");
		line[16] = '|';
		line[17] = '\n';
		line[18] = 0;
		printf ("%s", line);
	}
}

int
dump_inode_log (log_inode_update *entry)
{
	unsigned char date[17] = "xx/xx/xx xx:xx";
	time_t modtime;

	printf ("Inode: %-13xFSid: %x\n", intswap32 (entry->inode), intswap32 (entry->fsid));
	printf ("Refcount: %-10xType: ", intswap32 (entry->refcount));

	switch (entry->type)
	{
	case tyDir:
		printf ("tyDir\n");
		break;
	case tyDb:
		printf ("tyDb\n");
		break;
	case tyStream:
		printf ("tyStream\n");
		break;
	case tyFile:
		printf ("tyFile\n");
		break;
	default:
		printf ("??? (%d)\n", entry->type);
	}

	modtime = intswap32 (entry->lastmodified);
	strftime (date, 16, "%D %R", localtime (&modtime));
	printf ("Last modified: %s\n", date);

	printf ("Last update boot: %-15dSecs: %d\n", intswap32 (entry->bootcycles), intswap32 (entry->bootsecs));
	if (entry->type == tyStream)
	{
		if (hexvals)
		{
			printf ("Size: %d blocks of %x bytes (%llx)\n", intswap32 (entry->size), intswap32 (entry->unk3), (unsigned long long) intswap32 (entry->unk3) * (unsigned long long) intswap32 (entry->size));
			printf ("Used: %d blocks of %x bytes (%llx)\n", intswap32 (entry->blockused), intswap32 (entry->blocksize), (unsigned long long) intswap32 (entry->blockused) * (unsigned long long) intswap32 (entry->blocksize));
		}
		else
		{
			printf ("Size: %d blocks of %d bytes (%llu)\n", intswap32 (entry->size), intswap32 (entry->unk3), (unsigned long long) intswap32 (entry->unk3) * (unsigned long long) intswap32 (entry->size));
			printf ("Used: %d blocks of %d bytes (%llu)\n", intswap32 (entry->blockused), intswap32 (entry->blocksize), (unsigned long long) intswap32 (entry->blockused) * (unsigned long long) intswap32 (entry->blocksize));
		}
	}
	else
	{
		if (hexvals)
		{
			printf ("Size: %x bytes\n", intswap32 (entry->size));
		}
		else
		{
			printf ("Size: %d bytes\n", intswap32 (entry->size));
		}
	}
	if (entry->inodedata && entry->inodedata != intswap32 (1))
		printf ("Data in inode: %d\n", intswap32 (entry->inodedata));
	if (!entry->inodedata)
	{
		int loop;
		if (mfs->is_64)
		{
			printf ("Data is in %d blocks:\n", intswap32 (entry->datasize) / sizeof (entry->datablocks.d64[0]));
			for (loop = 0; loop < intswap32 (entry->datasize) / sizeof (entry->datablocks.d64[0]); loop++)
			{
				if (hexvals)
				{
					printf ("At %llx %x sectors\n", intswap64 (entry->datablocks.d64[loop].sector), intswap32 (entry->datablocks.d64[loop].count));
				}
				else
				{
					printf ("At %lld %d sectors\n", intswap64 (entry->datablocks.d64[loop].sector), intswap32 (entry->datablocks.d64[loop].count));
				}
			}
		}
		else
		{
			printf ("Data is in %d blocks:\n", intswap32 (entry->datasize) / sizeof (entry->datablocks.d32[0]));
			for (loop = 0; loop < intswap32 (entry->datasize) / sizeof (entry->datablocks.d32[0]); loop++)
			{
				if (hexvals)
				{
					printf ("At %x %x sectors\n", intswap32 (entry->datablocks.d32[loop].sector), intswap32 (entry->datablocks.d32[loop].count));
				}
				else
				{
					printf ("At %d %d sectors\n", intswap32 (entry->datablocks.d32[loop].sector), intswap32 (entry->datablocks.d32[loop].count));
				}
			}
		}
	}
	else
	{
		printf ("Data is in inode block.\n");
		hexdump ((void *)&entry->datablocks.d32[0], 0, intswap32 (entry->datasize));
	}

	return 1;
}

int
dump_inode (mfs_inode *inode_buf, unsigned char *buf, unsigned int bufsize)
{
	unsigned char date[17] = "xx/xx/xx xx:xx";
	time_t modtime;

	if (!inode_buf && bufsize >= 512)
	{
		// If it wasn't read as an inode, check if it looks like one
		inode_buf = (mfs_inode *)buf;
		if (inode_buf->sig != intswap32(0x91231ebc) ||
			!MFS_check_crc (inode_buf, 512, inode_buf->checksum))
			return 0;
	}

	do
	{
		printf("\n    Inode block\n");
		printf ("Inode: %-13dFSid: %d\n", intswap32 (inode_buf->inode), intswap32 (inode_buf->fsid));
		printf ("Refcount: %-10dType: ", intswap32 (inode_buf->refcount));

		switch (inode_buf->type)
		{
		case tyDir:
			printf ("tyDir\n");
			break;
		case tyDb:
			printf ("tyDb\n");
			break;
		case tyStream:
			printf ("tyStream\n");
			break;
		case tyFile:
			printf ("tyFile\n");
			break;
		default:
			printf ("??? (%d)\n", inode_buf->type);
		}

		modtime = intswap32 (inode_buf->lastmodified);
		strftime (date, 16, "%D %R", localtime (&modtime));
		printf ("Last modified: %s\n", date);

		printf ("Last update boot: %-15dSecs: %d\n", intswap32 (inode_buf->bootcycles), intswap32 (inode_buf->bootsecs));
		if (inode_buf->type == tyStream)
		{
			if (hexvals)
			{
				printf ("Size: %d blocks of %x bytes (%llx)\n", intswap32 (inode_buf->size), intswap32 (inode_buf->unk3), (unsigned long long) intswap32 (inode_buf->unk3) * (unsigned long long) intswap32 (inode_buf->size));
				printf ("Used: %d blocks of %x bytes (%llx)\n", intswap32 (inode_buf->blockused), intswap32 (inode_buf->blocksize), (unsigned long long) intswap32 (inode_buf->blockused) * (unsigned long long) intswap32 (inode_buf->blocksize));
			}
			else
			{
				printf ("Size: %d blocks of %d bytes (%llu)\n", intswap32 (inode_buf->size), intswap32 (inode_buf->unk3), (unsigned long long) intswap32 (inode_buf->unk3) * (unsigned long long) intswap32 (inode_buf->size));
				printf ("Used: %d blocks of %d bytes (%llu)\n", intswap32 (inode_buf->blockused), intswap32 (inode_buf->blocksize), (unsigned long long) intswap32 (inode_buf->blockused) * (unsigned long long) intswap32 (inode_buf->blocksize));
			}
		}
		else
		{
			if (hexvals)
			{
				printf ("Size: %x bytes\n", intswap32 (inode_buf->size));
			}
			else
			{
				printf ("Size: %d bytes\n", intswap32 (inode_buf->size));
			}
		}
		printf ("Checksum: %08x  Flags:", inode_buf->checksum);
		if (inode_buf->inode_flags & intswap32(INODE_CHAINED))
		{
			printf (" CHAINED");
		}
		if (inode_buf->inode_flags & intswap32(INODE_DATA))
		{
			printf (" DATA");
		}
		if (inode_buf->inode_flags & intswap32(~(INODE_DATA | INODE_CHAINED)))
		{
			printf (" ? (%08x)\n", intswap32(inode_buf->inode_flags));
		}
		else
		{
			printf ("\n");
		}
		printf ("Sig: %08x (%d bit)\n", intswap32 (inode_buf->sig), (inode_buf->sig & intswap32 (0x40000000)) ? 64 : 32);
		if (intswap32 (inode_buf->numblocks))
		{
			int loop;
			if (mfs->is_64)
			{
				printf ("Data is in %d blocks:\n", intswap32 (inode_buf->numblocks));
				for (loop = 0; loop < intswap32 (inode_buf->numblocks); loop++)
				{
					if (hexvals)
					{
						printf ("At %llx %x sectors\n", intswap64 (inode_buf->datablocks.d64[loop].sector), intswap32 (inode_buf->datablocks.d64[loop].count));
					}
					else
					{
						printf ("At %lld %d sectors\n", intswap64 (inode_buf->datablocks.d64[loop].sector), intswap32 (inode_buf->datablocks.d64[loop].count));
					}
				}
			}
			else
			{
				printf ("Data is in %d blocks:\n", intswap32 (inode_buf->numblocks));
				for (loop = 0; loop < intswap32 (inode_buf->numblocks); loop++)
				{
					if (hexvals)
					{
						printf ("At %x %x sectors\n", intswap32 (inode_buf->datablocks.d32[loop].sector), intswap32 (inode_buf->datablocks.d32[loop].count));
					}
					else
					{
						printf ("At %d %d sectors\n", intswap32 (inode_buf->datablocks.d32[loop].sector), intswap32 (inode_buf->datablocks.d32[loop].count));
					}
				}
			}
		}
		else
		{
			printf ("Data is in inode block.\n");
		}
		
		buf += 1024;
		bufsize -= 1024;
		inode_buf = (mfs_inode *)buf;
	}
	while (bufsize > 512 && inode_buf->sig == intswap32(0x91231ebc) &&
		MFS_check_crc (inode_buf, 512, inode_buf->checksum));

	return 1;
}

int
dump_mfs_header (unsigned char *buf, unsigned int bufsize)
{
	volume_header *hdr;

	if (bufsize < sizeof (volume_header_32))
		return 0;

	hdr = (volume_header *)buf;
	switch (intswap32 (hdr->v32.magic))
	{
		case MFS32_MAGIC:
			return dump_mfs_header_32 (&hdr->v32, buf, bufsize);
		case MFS64_MAGIC:
			return dump_mfs_header_64 (&hdr->v64, buf, bufsize);
	}

	return 0;
}

int
dump_mfs_header_32 (volume_header_32 *hdr, unsigned char *buf, unsigned int bufsize)
{
	if (bufsize < sizeof (volume_header_32))
		return 0;

	if (!MFS_check_crc (hdr, sizeof (volume_header_32), hdr->checksum))
		return 0;

	printf ("\n    MFS Volume Header\n");
	printf ("State: %-13dFirst partition size: %dx1024 sectors (%dmb)\n", intswap32 (hdr->state), intswap32 (hdr->firstpartsize), intswap32 (hdr->firstpartsize) / 2);
	printf ("Sig: %08x   CRC: %08x   Size: %d\n", intswap32 (hdr->magic), intswap32 (hdr->checksum), intswap32 (hdr->total_sectors));
	printf ("MFS Partitions: %s\n", hdr->partitionlist);
	printf ("Root FSID: %-13dNext FSID: %d\n", intswap32 (hdr->root_fsid), intswap32 (hdr->next_fsid));
	if (hexvals)
	{
		printf ("Redo log start: %08x     Size: %08x\n", intswap32 (hdr->logstart), intswap32 (hdr->lognsectors));
		printf ("?        start: %08x     Size: %08x\n", intswap32 (hdr->unkstart), intswap32 (hdr->unksectors));
		printf ("Zone map start: %08x     Size: %08x\n", intswap32 (hdr->zonemap.sector), intswap32 (hdr->zonemap.length));
		printf ("        backup: %08x     Zone size: %08x      Allocation size: 08x\n", intswap32 (hdr->zonemap.sbackup), intswap32 (hdr->zonemap.size), intswap32 (hdr->zonemap.min));
	}
	else
	{
		printf ("Redo log start: %-13dSize: %d\n", intswap32 (hdr->logstart), intswap32 (hdr->lognsectors));
		printf ("?        start: %-13dSize: %d\n", intswap32 (hdr->unkstart), intswap32 (hdr->unksectors));
		printf ("Zone map start: %-13dSize: %d\n", intswap32 (hdr->zonemap.sector), intswap32 (hdr->zonemap.length));
		printf ("        backup: %-13dZone size: %-13dAllocation size: %d\n", intswap32 (hdr->zonemap.sbackup), intswap32 (hdr->zonemap.size), intswap32 (hdr->zonemap.min));
	}
	printf ("Last sync boot: %-13dTimestamp: %-13dLast Commit: %d\n", intswap32 (hdr->bootcycles), intswap32 (hdr->bootsecs), intswap32 (hdr->logstamp));

	if (hdr->off0c || hdr->off14 || hdr->off1c || hdr->off20 || hdr->offa8 || hdr->offc0 || hdr->offe4)
	{
		printf ("Unknown data\n");
		if (hdr->off0c)
		{
			printf ("00000000:00c %02x %02x %02x %02x\n", buf[12], buf[13], buf[14], buf[15]);
		}
		if (hdr->off14)
		{
			printf ("00000000:014 %02x %02x %02x %02x\n", buf[20], buf[21], buf[22], buf[23]);
		}
		if (hdr->off1c || hdr->off20)
		{
			printf ("00000000:01c %02x %02x %02x %02x %02x %02x %02x %02x\n", buf[28], buf[29], buf[30], buf[31], buf[32], buf[33], buf[34], buf[35]);
		}
		if (hdr->offa8)
		{
			printf ("00000000:0a8 %02x %02x %02x %02x\n", buf[168], buf[169], buf[170], buf[171]);
		}
		if (hdr->offc0)
		{
			printf ("00000000:0c0 %02x %02x %02x %02x\n", buf[192], buf[193], buf[194], buf[195]);
		}
		if (hdr->offe4)
		{
			printf ("00000000:0e4 %02x %02x %02x %02x\n", buf[228], buf[229], buf[230], buf[231]);
		}
	}

	return 1;
}

int
dump_mfs_header_64 (volume_header_64 *hdr, unsigned char *buf, unsigned int bufsize)
{
	if (bufsize < sizeof (volume_header_64))
		return 0;

	if (!MFS_check_crc (hdr, sizeof (volume_header_64), hdr->checksum))
		return 0;

	printf ("\n    MFS Volume Header\n");
	printf ("State: %-13dFirst partition size: %dx1024 sectors (%dmb)\n", intswap32 (hdr->state), intswap32 (hdr->firstpartsize), intswap32 (hdr->firstpartsize) / 2);
	printf ("Sig: %08x   CRC: %08x   Size: %lld\n", intswap32 (hdr->magic), intswap32 (hdr->checksum), intswap64 (hdr->total_sectors));
	printf ("MFS Partitions: %s\n", hdr->partitionlist);
	printf ("Root FSID: %-13dNext FSID: %d\n", intswap32 (hdr->root_fsid), intswap32 (hdr->next_fsid));
	if (hexvals)
	{
		printf ("Redo log start: %09llx    Size: %08x\n", intswap64 (hdr->logstart), intswap32 (hdr->lognsectors));
		printf ("?        start: %09llx    Size: %08x\n", intswap64 (hdr->unkstart), intswap32 (hdr->unknsectors));
		printf ("Zone map start: %09llx    Size: %08llx\n", intswap64 (hdr->zonemap.sector), intswap64 (hdr->zonemap.length));
		printf ("        backup: %09llx    Zone size: %09llxAllocation size: %08llx\n", intswap64 (hdr->zonemap.sbackup), intswap64 (hdr->zonemap.size), intswap64 (hdr->zonemap.min));
	}
	else
	{
		printf ("Redo log start: %-13lldSize: %d\n", intswap64 (hdr->logstart), intswap32 (hdr->lognsectors));
		printf ("?        start: %-13lldSize: %d\n", intswap64 (hdr->unkstart), intswap32 (hdr->unknsectors));
		printf ("Zone map start: %-13lldSize: %lld\n", intswap64 (hdr->zonemap.sector), intswap64 (hdr->zonemap.length));
		printf ("        backup: %-13lldZone size: %-13lldAllocation size: %lld\n", intswap64 (hdr->zonemap.sbackup), intswap64 (hdr->zonemap.size), intswap64 (hdr->zonemap.min));
	}
	printf ("Last sync boot: %-13dTimestamp: %-13dLast Commit: %d\n", intswap32 (hdr->bootcycles), intswap32 (hdr->bootsecs), intswap32 (hdr->logstamp));

	if (hdr->off0c || hdr->off14 || hdr->off1c || hdr->off20 || hdr->offb8 || hdr->offc8 || hdr->off100 || hdr->off110 || hdr->off114)
	{
		printf ("Unknown data\n");
		if (hdr->off0c)
		{
			printf ("00000000:00c %02x %02x %02x %02x\n", buf[12], buf[13], buf[14], buf[15]);
		}
		if (hdr->off14)
		{
			printf ("00000000:014 %02x %02x %02x %02x\n", buf[20], buf[21], buf[22], buf[23]);
		}
		if (hdr->off1c || hdr->off20)
		{
			printf ("00000000:01c %02x %02x %02x %02x %02x %02x %02x %02x\n", buf[28], buf[29], buf[30], buf[31], buf[32], buf[33], buf[34], buf[35]);
		}
		if (hdr->offb8)
		{
			printf ("00000000:0b8 %02x %02x %02x %02x\n", buf[184], buf[185], buf[186], buf[187]);
		}
		if (hdr->offc8)
		{
			printf ("00000000:0c0 %02x %02x %02x %02x\n", buf[200], buf[201], buf[202], buf[203]);
		}
		if (hdr->off100)
		{
			printf ("00000000:100 %02x %02x %02x %02x\n", buf[256], buf[257], buf[258], buf[259]);
		}
		if (hdr->off110 || hdr->off114)
		{
			printf ("00000000:110 %02x %02x %02x %02x %02x %02x %02x %02x\n", buf[272], buf[273], buf[274], buf[275], buf[276], buf[277], buf[278], buf[279]);
		}
	}

	return 1;
}

int
dump_zone_map (uint64_t sector, unsigned char *buf, unsigned int bufsize)
{
	if (mfs->is_64)
		return dump_zone_map_64 (sector, buf, bufsize);
	else
		return dump_zone_map_32 (sector, buf, bufsize);
}

void
dump_bitmaps (char *base, unsigned bufsize, unsigned *fsmem_ptrs, uint64_t sector, uint64_t size, uint32_t num, uint64_t blocksize)
{
	unsigned nbits = size / blocksize;
	unsigned int intwidth;
	unsigned int loop;
	uint64_t bigloop;

	/* Find how wide the sector size is in the appropriate number base */
	if (hexvals)
	{
		for (bigloop = 1, intwidth = 0; sector + size > bigloop; intwidth++, bigloop *= 16);
	}
	else
	{
		for (bigloop = 1, intwidth = 0; sector + size > bigloop; intwidth++, bigloop *= 10);
	}

	for (loop = 0; loop < num; loop++, nbits /= 2, blocksize *= 2)
	{
		int loop2;
		unsigned *bits;
		int found = 0;
		int linelength = 0;

		bitmap_header *bitmap = (void *)((unsigned)intswap32 (fsmem_ptrs[loop]) - (unsigned)intswap32 (fsmem_ptrs[0]) + (unsigned)base);

		if ((unsigned)bitmap < (unsigned)base || (unsigned)bitmap > (unsigned)base + bufsize)
		{
			fprintf (stderr, "\nBitmap %d out of range\n", loop);
			continue;
		}

		if (hexvals)
		{
			printf ("\nBitmap at %08x  Blocksize: %09x\n", intswap32 (fsmem_ptrs[loop]), blocksize);
		}
		else
		{
			printf ("\nBitmap at %08x  Blocksize: %d\n", intswap32 (fsmem_ptrs[loop]), blocksize);
		}
		printf (" Words: %-12dBits: %-14dActual: %d\n", intswap32 (bitmap->nints), intswap32 (bitmap->nbits), nbits);
		if (hexvals)
		{
			printf (" Free: %-13dNext alloc search: %0.*x (%d)\n", intswap32 (bitmap->freeblocks), intwidth, intswap32 (bitmap->last) * blocksize + sector, intswap32 (bitmap->last));
		}
		else
		{
			printf (" Free: %-13dNext alloc search: %d (%d)\n", intswap32 (bitmap->freeblocks), intswap32 (bitmap->last) * blocksize + sector, intswap32 (bitmap->last));
		}

		bits = (unsigned *)(bitmap + 1);
		for (loop2 = 0; loop2 < intswap32 (bitmap->nints); loop2++)
		{
			if (bits[loop2])
			{
				int bitloop;

				for (bitloop = 0; bitloop < 32; bitloop++)
				{
					if (intswap32 (bits[loop2]) & (1 << (31 - bitloop)))
					{
						unsigned bitaddr = (loop2 * 32 + bitloop) * blocksize + sector;
						if (!found)
						{
							linelength = 8 + intwidth * 2 + 1;
							linelength = linelength - linelength % (intwidth * 2 + 2) + 8;
							printf ("%-*s ", linelength - 1, "    Free blocks:");
							++found;
						}
						else if (linelength == 0)
						{
							printf ("        ");
							linelength = 8;
						}
						else
						{
							printf (" ");
							linelength++;
						}

						if (hexvals)
						{
							printf ("%0.*x-%0.*x", intwidth, bitaddr, intwidth, bitaddr + blocksize - 1);
						}
						else
						{
							printf ("%.*d-%-.*d", intwidth, bitaddr, intwidth, bitaddr + blocksize - 1);
						}
						++found;
						linelength += intwidth * 2 + 1;
						if (linelength + intwidth * 2 + 1 >= 80)
						{
							printf ("\n");
							linelength = 0;
						}
					}
				}
			}
		}

		if (linelength)
			printf ("\n");
	}
}

int
dump_zone_map_32 (uint64_t sector, unsigned char *buf, unsigned int bufsize)
{
	unsigned fsmem_base;
	int loop;
	unsigned blocksize;
	unsigned *fsmem_ptrs;

	zone_header_32 *zone;
	unsigned char *bitmaps;

	if (bufsize < sizeof (zone_header_32))
		return 0;

	zone = (zone_header_32 *)buf;

	if (sector != intswap32 (zone->sector) && sector != intswap32 (zone->sbackup) || intswap32 (zone->length) * 512 > bufsize || !MFS_check_crc (zone, intswap32 (zone->length) * 512, zone->checksum))
		return 0;

	printf ("\n    Zone map ");

	switch (intswap32 (zone->type))
	{
		case ztInode:
			printf ("(Inode)\n");
			break;
		case ztApplication:
			printf ("(Application)\n");
			break;
		case ztMedia:
			printf ("(Media)\n");
			break;
		default:
			printf ("(Unknown type %d)\n", intswap32 (zone->type));
	}

	fsmem_ptrs = (unsigned *)(zone + 1);

	blocksize = intswap32 (zone->min);

	fsmem_base = intswap32 (fsmem_ptrs[0]) - (sizeof (*zone) + intswap32 (zone->num) * 4);

	if (hexvals)
	{
		printf ("This zone:                Sector: %08x         Backup: %08x\n", intswap32 (zone->sector), intswap32 (zone->sbackup));
		printf ("   Length: %08x         Size: %08x     Block size: %08x\n", intswap32 (zone->length), intswap32 (zone->size), intswap32 (zone->min));
		printf ("Next zone:                Sector: %08x         Backup: %08x\n", intswap32 (zone->next.sector), intswap32 (zone->next.sbackup));
		printf ("   Length: %08x         Size: %08x     Block size: %08x\n", intswap32 (zone->next.length), intswap32 (zone->next.size), intswap32 (zone->next.min));
		printf ("First    : %08x         Last: %08x\n", intswap32 (zone->first), intswap32 (zone->last));
		printf (" Size    : %08x         Free: %08x\n", intswap32 (zone->size), intswap32 (zone->free));
	}
	else
	{
		printf ("This zone:                Sector: %-13d    Backup: %d\n", intswap32 (zone->sector), intswap32 (zone->sbackup));
		printf ("   Length: %-13d    Size: %-13dBlock size: %d\n", intswap32 (zone->length), intswap32 (zone->size), intswap32 (zone->min));
		printf ("Next zone:                Sector: %-13d    Backup: %d\n", intswap32 (zone->next.sector), intswap32 (zone->next.sbackup));
		printf ("   Length: %-13d    Size: %-13dBlock size: %d\n", intswap32 (zone->next.length), intswap32 (zone->next.size), intswap32 (zone->next.min));
		printf ("First    : %-13d    Last: %d\n", intswap32 (zone->first), intswap32 (zone->last));
		printf (" Size    : %-13d    Free: %d\n", intswap32 (zone->size), intswap32 (zone->free));
	}
	printf ("Logstamp : %-13dChecksum: %08x           Zero: %d\n", intswap32 (zone->logstamp), intswap32 (zone->checksum), intswap32 (zone->zero));
	printf ("Bitmaps: %-13dfsmem base: %08x\n", intswap32 (zone->num), fsmem_base);

	bitmaps = buf + intswap32 (fsmem_ptrs[0]) - fsmem_base;
	dump_bitmaps (bitmaps, bufsize - (bitmaps - buf), fsmem_ptrs, intswap32 (zone->first), intswap32 (zone->size), intswap32 (zone->num), intswap32 (zone->min));

	return 1;
}

int
dump_zone_map_64 (uint64_t sector, unsigned char *buf, unsigned int bufsize)
{
	unsigned fsmem_base;
	int loop;
	unsigned blocksize;
	unsigned *fsmem_ptrs;

	zone_header_64 *zone;
	unsigned char *bitmaps;

	if (bufsize < sizeof (zone_header_64))
		return 0;

	zone = (zone_header_64 *)buf;

	if (sector != intswap64 (zone->sector) && sector != intswap64 (zone->sbackup) || intswap32 (zone->length) * 512 > bufsize || !MFS_check_crc (zone, intswap32 (zone->length) * 512, zone->checksum))
		return 0;

	printf ("\n    Zone map ");

	switch (intswap32 (zone->type))
	{
		case ztInode:
			printf ("(Inode)\n");
			break;
		case ztApplication:
			printf ("(Application)\n");
			break;
		case ztMedia:
			printf ("(Media)\n");
			break;
		default:
			printf ("(Unknown type %d)\n", intswap32 (zone->type));
	}

	fsmem_ptrs = (unsigned *)(zone + 1);

	blocksize = intswap32 (zone->min);

	fsmem_base = intswap32 (fsmem_ptrs[0]) - (sizeof (*zone) + intswap32 (zone->num) * 4);

	if (hexvals)
	{
		printf ("This zone:                Sector: %09llx        Backup: %09llx\n", intswap64 (zone->sector), intswap64 (zone->sbackup));
		printf ("   Length: %08x         Size: %08x    Block size: %08x\n", intswap32 (zone->length), intswap32 (zone->size), intswap32 (zone->min));
		printf ("Next zone:                Sector: %09llx        Backup: %09llx\n", intswap64 (zone->next_sector), intswap64 (zone->next_sbackup));
		printf ("   Length: %08x         Size: %08x    Block size: %08x\n", intswap32 (zone->next_length), intswap32 (zone->next_size), intswap32 (zone->next_min));
		printf ("First    : %09llx        Last: %09llx\n", intswap64 (zone->first), intswap64 (zone->last));
		printf (" Size    : %08x         Free: %08x\n", intswap32 (zone->size), intswap32 (zone->free));
	}
	else
	{
		printf ("This zone:                Sector: %-13lld    Backup: %lld\n", intswap64 (zone->sector), intswap64 (zone->sbackup));
		printf ("   Length: %-13d    Size: %-13dBlock size: %d\n", intswap32 (zone->length), intswap32 (zone->size), intswap32 (zone->min));
		printf ("Next zone:                Sector: %-13lld    Backup: %lld\n", intswap64 (zone->next_sector), intswap64 (zone->next_sbackup));
		printf ("   Length: %-13d    Size: %-13dBlock size: %d\n", intswap32 (zone->next_length), intswap32 (zone->next_size), intswap32 (zone->next_min));
		printf ("First    : %-13lld    Last: %lld\n", intswap64 (zone->first), intswap64 (zone->last));
		printf (" Size    : %-13d    Free: %d\n", intswap32 (zone->size), intswap32 (zone->free));
	}
	printf ("Logstamp : %-13dChecksum: %08x           Zero: %d\n", intswap32 (zone->logstamp), intswap32 (zone->checksum), intswap32 (zone->zero));
	printf ("Bitmaps: %-13dfsmem base: %08x\n", intswap32 (zone->num), fsmem_base);

	bitmaps = buf + intswap32 (fsmem_ptrs[0]) - fsmem_base;
	dump_bitmaps (bitmaps, bufsize - (bitmaps - buf), fsmem_ptrs, intswap64 (zone->first), intswap32 (zone->size), intswap32 (zone->num), intswap32 (zone->min));

	return 1;
}

int
dump_log_entry (unsigned int sector, unsigned char *buf, unsigned int bufsize)
{
	log_hdr *hdr;
	unsigned int off;
	unsigned int hdroff;

	if (bufsize < 512)
		return 0;

	hdr = (log_hdr *)buf;

	if (sector != 0xdeadbeef && sector != mfs_log_stamp_to_sector (mfs, intswap32 (hdr->logstamp)) || !MFS_check_crc(buf, 512, hdr->crc))
		return 0;

	printf ("\n    Log entry stamp %d\n", intswap32 (hdr->logstamp));
	printf ("Size: %-13dFirst: %-13dCRC: %08x\n", intswap32 (hdr->size), intswap32 (hdr->first), intswap32 (hdr->crc));

	off = intswap32 (hdr->first);
	hdroff = 0;
	while (off < bufsize && off < intswap32 (hdr->size) || hdroff + 512 <= bufsize)
	{
		unsigned char *allocated = NULL;
		unsigned int allocwritten = 0;
		log_entry_all *entry;

		if (off >= intswap32 (hdr->size))
		{
			unsigned int oldlogstamp = intswap32 (hdr->logstamp);

			hdroff += 512;
			off = 0;
			hdr = (log_hdr *)(buf + hdroff);
			if (hdroff >= bufsize || oldlogstamp + 1 != intswap32 (hdr->logstamp) || !MFS_check_crc(buf + hdroff, 512, hdr->crc))
				return 1;

			printf ("\n    Log entry stamp %d\n", intswap32 (hdr->logstamp));
			printf ("Size: %-13dFirst: %-13dCRC: %08x\n", intswap32 (hdr->size), intswap32 (hdr->first), intswap32 (hdr->crc));

			continue;
		}

		entry = (log_entry_all *)(buf + off + hdroff + sizeof (log_hdr));

		if (entry->log.length == 0)
		{
			off += 2;
			continue;
		}

		// Entry extends into the next log sector
		while (off + intswap16 (entry->log.length) + 2 - allocwritten > intswap32 (hdr->size))
		{
			unsigned int oldlogstamp = intswap32 (hdr->logstamp);

			if (!allocated)
			{
				allocated = malloc (intswap16 (entry->log.length) + 2);
				allocwritten = 0;
				entry = (log_entry_all *)allocated;
			}
			memcpy (allocated + allocwritten, buf + hdroff + off + sizeof (log_hdr), intswap32 (hdr->size) - off);
			allocwritten += intswap32 (hdr->size) - off;

			hdroff += 512;
			off = 0;

			hdr = (log_hdr *)(buf + hdroff);
			if (hdroff >= bufsize || oldlogstamp + 1 != intswap32 (hdr->logstamp) || !MFS_check_crc(buf + hdroff, 512, hdr->crc))
			{
				printf("... Continued in next log entry\n");
				free (allocated);
				return 1;
			}

			printf ("\n    Continued in log entry stamp %d\n", intswap32 (hdr->logstamp));
			printf ("Size: %-13dFirst: %-13dCRC: %08x\n", intswap32 (hdr->size), intswap32 (hdr->first), intswap32 (hdr->crc));

			continue;
		}

		if (allocated)
		{
			memcpy (allocated + allocwritten, buf + hdroff + off + sizeof (log_hdr), intswap16 (entry->log.length) + 2 - allocwritten);
			off += intswap16 (entry->log.length) + 2 - allocwritten;
		}
		else
			off += intswap16 (entry->log.length) + 2;

		printf ("\nLog entry length: %-13dType: ", intswap16 (entry->log.length));
		switch (intswap32 (entry->log.transtype))
		{
			case ltMapUpdate:
				printf ("Zone Map Update\n");
				break;
			case ltMapUpdate64:
				printf ("Zone Map Update 64 bit\n");
				break;
			case ltInodeUpdate:
				printf ("Inode Update\n");
				break;
			case ltInodeUpdate2:
				printf ("Inode Update 2\n");
				break;
			case ltCommit:
				printf ("Log Commit\n");
				break;
			case ltFsSync:
				printf ("FS Sync Complete\n");
				break;
			case ltLogReplay:
				printf ("Replay Transaction Log\n");
				break;
			default:
				printf ("Unknown (%d)\n", intswap32 (entry->log.transtype));
		}

		printf ("Boot: %-13dTimestamp: %d\n", intswap32 (entry->log.bootcycles), intswap32 (entry->log.bootsecs));
		printf ("FSId: %-13d???: %-13d???: %d\n", intswap32 (entry->log.fsid), intswap32 (entry->log.unk1), intswap32 (entry->log.unk2));

		switch (intswap32 (entry->log.transtype))
		{
			case ltMapUpdate:
				printf ("Zone map update:\n");
				if (!entry->zonemap_32.remove)
					printf ("Change: Allocate     ");
				else if (entry->zonemap_32.remove == intswap32 (1))
					printf ("Change: Free         ");
				else
					printf ("Change: ?%-12d", intswap32 (entry->zonemap_32.remove));
				if (entry->zonemap_32.unk)
					printf ("???: %d\n", intswap32 (entry->zonemap_32.unk));
				if (hexvals)
				{
					printf ("Sector: %08x     Size: %08x\n", intswap32 (entry->zonemap_32.sector), intswap32 (entry->zonemap_32.size));
				}
				else
				{
					printf ("Sector: %-13dSize: %d\n", intswap32 (entry->zonemap_32.sector), intswap32 (entry->zonemap_32.size));
				}
				break;
			case ltMapUpdate64:
				printf ("Zone map update:\n");
				if (!entry->zonemap_64.remove)
					printf ("Change: Allocate     ");
				else if (entry->zonemap_64.remove == intswap32 (1))
					printf ("Change: Free         ");
				else
					printf ("Change: ?%-12d", intswap32 (entry->zonemap_64.remove));
				if (hexvals)
				{
					printf ("Sector: %09llx    Size: %09llx\n", intswap64 (entry->zonemap_64.sector), intswap64 (entry->zonemap_64.size));
				}
				else
				{
					printf ("Sector: %-13lldSize: %lld\n", intswap64 (entry->zonemap_64.sector), intswap64 (entry->zonemap_64.size));
				}
				printf ("Unknown Flag: %d\n", entry->zonemap_64.flag);
				break;
			case ltInodeUpdate:
			case ltInodeUpdate2:
				printf ("Inode update:\n");
				dump_inode_log (&entry->inode);
				break;
		}

		if (allocated)
			free (allocated);
	}
	return 1;
}

int
mfsd_main (int argc, char **argv)
{
	int curarg;
	unsigned char *buf = NULL;

	unsigned int sector = 0xdeadbeef, count = 1;
	unsigned int fsid = 0;
	unsigned int inode = 0xdeadbeef;
	unsigned int bufsize = 0;
	unsigned int logstamp = 0xdeadbeef;
	int dofssync = 0;
	unsigned int zonemap = 0xdeadbeef;

	mfs_inode *inode_buf = NULL;

	enum
	{ None, Hex, Bin }
	format = None;

	progname = argv[0];

	while ((curarg = getopt (argc, argv, "bhc:s:i:f:l:z:Cx")) >= 0)
	{
		switch (curarg)
		{
		case 'b':
			format = Bin;
			break;
		case 'h':
			format = Hex;
			break;
		case 's':
			sector = strtoul (optarg, 0, 0);
			break;
		case 'l':
			if (zonemap != 0xdeadbeef || fsid || inode != 0xdeadbeef)
			{
				fprintf (stderr, "Only 1 of -z -f -i and -l may be used.\n");
				usage ();
				return 2;
			}
			logstamp = strtoul (optarg, 0, 0);
			break;
		case 'c':
			count = strtoul (optarg, 0, 0);
			break;
		case 'f':
			if (zonemap != 0xdeadbeef || inode != 0xdeadbeef || logstamp != 0xdeadbeef)
			{
				fprintf (stderr, "Only 1 of -z -f -i and -l may be used.\n");
				usage ();
				return 1;
			}
			fsid = strtoul (optarg, 0, 0);
			break;
		case 'i':
			if (zonemap != 0xdeadbeef || fsid || logstamp != 0xdeadbeef)
			{
				fprintf (stderr, "Only 1 of -z -f -i and -l may be used.\n");
				usage ();
				return 2;
			}
			inode = strtoul (optarg, 0, 0);
			break;
		case 'C':
			dofssync = 1;
			break;
		case 'z':
			if (fsid || logstamp != 0xdeadbeef || inode != 0xdeadbeef)
			{
				fprintf (stderr, "Only 1 of -z -f -i and -l may be used.\n");
				usage ();
				return 2;
			}
			zonemap = strtoul (optarg, 0, 0);
			break;
		case 'x':
			hexvals = 1;
			break;
		default:
			usage ();
			return 3;
		}
	}

	if (sector == 0xdeadbeef && !fsid && inode == 0xdeadbeef && logstamp == 0xdeadbeef && zonemap == 0xdeadbeef || optind == argc || optind >= argc + 2 || logstamp != 0xdeadbeef && (fsid || inode != 0xdeadbeef || sector != 0xdeadbeef))
	{
		usage ();
		return 4;
	}

	mfs = mfs_init (argv[optind], optind + 1 < argc? argv[optind + 1] : NULL, O_RDONLY);

	if (mfs_has_error (mfs))
	{
		mfs_perror (mfs, argv[0]);
		return 1;
	}

	if (dofssync)
	{
		mfs_enable_memwrite (mfs);
		if (mfs_log_fssync (mfs) < 1)
		{
			mfs_perror (mfs, "fssync");
		}
	}

	if (fsid)
	{
		inode_buf = mfs_read_inode_by_fsid (mfs, fsid);
		if (!inode_buf)
		{
			mfs_perror (mfs, "Read fsid");
			return 1;
		}
		bufsize = sizeof (*inode_buf) + intswap32 (inode_buf->numblocks) * 8;
		buf = (unsigned char *) inode_buf;
	}
	else if (inode != 0xdeadbeef)
	{
		inode_buf = mfs_read_inode (mfs, inode);
		if (!inode_buf)
		{
			mfs_perror (mfs, "Read inode");
			return 1;
		}
		bufsize = sizeof (*inode_buf) + intswap32 (inode_buf->numblocks) * 8;
		buf = (unsigned char *) inode_buf;
	}
	else if (zonemap != 0xdeadbeef)
	{
		zone_header *zone = NULL;
		sector = 0xdeadbeef;

		for (zone = mfs_next_zone (mfs, zone); zone && zonemap-- > 0; zone = mfs_next_zone (mfs, zone));

		if (!zone)
		{
			fprintf (stderr, "Zone map out of range\n");
			return 0;
		}

		buf = (void *)zone;
		if (mfs_is_64bit (mfs))
		{
			bufsize = intswap32 (zone->z64.length) * 512;
			sector = intswap64 (zone->z64.sector);
		}
		else
		{
			bufsize = intswap32 (zone->z32.length) * 512;
			sector = intswap32 (zone->z32.sector);
		}
	}

	if (logstamp == 0xdeadbeef && sector != 0xdeadbeef && zonemap == 0xdeadbeef)
	{
		buf = calloc (count, 512);

		if (!buf)
		{
			fprintf (stderr, "%s: Couldn't allocate %d bytes!\n", progname, count * 512);
			return 1;
		}

		if (inode_buf)
		{
			int nread = mfs_read_inode_data_part (mfs, inode_buf, buf, sector, count);
			if (nread <= 0)
			{
				mfs_perror (mfs, "Read data");
				return 1;
			}
			bufsize = nread;
		}
		else
		{
			int nread = mfs_read_data (mfs, buf, sector, count);

			if (nread <= 0)
			{
				mfs_perror (mfs, "Read data");
				return 1;
			}

			bufsize = nread;
		}
	}
	else if (logstamp != 0xdeadbeef)
	{
		int loop;

		buf = calloc (count, 512);
		bufsize = 0;

		for (loop = 0; loop < count; loop++)
		{
			int nread = mfs_log_read (mfs, buf + bufsize, logstamp + loop);

			if (nread < 0)
			{
				mfs_perror (mfs, "Read log");
				return 1;
			}

			if (nread == 0)
			{
				if (bufsize == 0)
				{
					fprintf (stderr, "Log entry not found\n");
					return 1;
				}

				break;
			}

			bufsize += nread;
		}
	}

	if (format == Bin)
	{
		write (1, buf, bufsize);
	}
	else
	{
		// Provide a context to break from
		// No, it's not a goto, it's just nicer looking than a long chained if
		do
		{
			if (format != Hex && (!inode_buf || sector == 0xdeadbeef))
			{
				// Try and infer the format of the data
				if (dump_inode (inode_buf, buf, bufsize))
					break;
				if (dump_mfs_header (buf, bufsize))
					break;
				if (dump_zone_map (sector, buf, bufsize))
					break;
				if (dump_log_entry (sector, buf, bufsize))
					break;
			}

			// No known format, just hexdump it
			int offset = 0;
			while (offset * 512 < bufsize)
			{
				hexdump (buf + offset * 512, sector == 0xdeadbeef ? sector : offset + sector, bufsize - offset * 512);
				offset++;
			}
		} while (0);
	}
	return 0;
}
