#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include "mfs.h"
#include "backup.h"

#define BUFSIZE 512 * 2048

#define backup_usage()

static unsigned int
get_percent (unsigned int current, unsigned int max)
{
	unsigned int prcnt;
	if (max <= 0x7fffffff / 10000)
	{
		prcnt = current * 10000 / max;
	}
	else if (max <= 0x7fffffff / 100)
	{
		prcnt = current * 100 / (max / 100);
	}
	else
	{
		prcnt = current / (max / 10000);
	}

	return prcnt;
}

#define SABLOCKSEC 1630000

unsigned int
sectors_no_reserved (unsigned int sectors)
{
	if (sectors < 14 * 1024 * 1024 * 2)
		return sectors;
	if (sectors > 72 * 1024 * 1024 * 2)
		return sectors - 12 * 1024 * 1024 * 2;
	return sectors - (sectors - 14 * 1024 * 1024 * 2) / 4;
}

void
display_backup_info (struct backup_info *info)
{
	zone_header *hdr = 0;
	unsigned int sizes[32];
	int count = 0;
	int loop;
	unsigned int backuptot = 0;
	unsigned int backupmfs = 0;

	for (loop = 0; loop < info->nmfs; loop++)
	{
		backupmfs += info->mfsparts[loop].sectors;
	}

	while ((hdr = mfs_next_zone (hdr)) != 0)
	{
		if (htonl (hdr->type) == ztMedia)
		{
			unsigned int size = htonl (hdr->size);
			if (htonl (hdr->first) < backupmfs)
				backuptot += size;
			sizes[count++] = size;
		}
		else
			while (count > 1)
			{
				sizes[0] += sizes[--count];
			}
	}

	if (sizes > 0)
	{
		unsigned int running = sizes[0];
		fprintf (stderr, "Source drive size is %d hours\n", sectors_no_reserved (running) / SABLOCKSEC, 0);
		if (count > 1)
			for (loop = 1; loop < count; loop++)
			{
				running += sizes[loop];
				fprintf (stderr, "       - Upgraded to %d hours\n", sectors_no_reserved (running) / SABLOCKSEC, 0);
			}
		if (info->back_flags & BF_SHRINK)
			fprintf (stderr, "Backup image will be %d hours\n", sectors_no_reserved (backuptot) / SABLOCKSEC, 0);
	}
}

int
backup_main (int argc, char **argv)
{
	struct backup_info *info;
	int loop, thresh = 0;
	unsigned int flags = BF_BACKUPVAR;
	char threshopt = '\0';
	char *drive, *drive2;
	char *filename;
	char *tmp;
	int quiet = 0;
	int compressed = 0;

	tivo_partition_direct ();

	while ((loop = getopt (argc, argv, "o:123456789vsf:l:tTaq")) > 0)
	{
		switch (loop)
		{
		case 'o':
			filename = optarg;
			break;
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			flags |= BF_SETCOMP (loop - '0');
			compressed = 0;
			break;
		case 'v':
			flags &= ~BF_BACKUPVAR;
			break;
		case 's':
			flags |= BF_SHRINK;
			break;
		case 'f':
			if (threshopt)
			{
				fprintf (stderr, "%s: -f and -%c cannot be used together\n", argv[0], threshopt);
				return 1;
			}
			threshopt = loop;
			thresh = strtoul (optarg, &tmp, 10);
			if (*tmp)
			{
				fprintf (stderr, "%s: Non integer argument to -f\n", argv[0]);
				return 1;
			}
			break;
		case 'l':
			if (threshopt)
			{
				fprintf (stderr, "%s: -l and -%c cannot be used together\n", argv[0], threshopt);
				return 1;
			}
			threshopt = loop;
			thresh = strtoul (optarg, &tmp, 10);
			thresh *= 1024 * 2;
			flags |= BF_THRESHSIZE;
			if (*tmp)
			{
				fprintf (stderr, "%s: Non integer argument to -l\n", argv[0]);
				return 1;
			}
			break;
		case 't':
			flags |= BF_THRESHTOT;
			break;
		case 'T':
			flags += BF_STREAMTOT;
			break;
		case 'a':
			if (threshopt)
			{
				fprintf (stderr, "%s: -a and -%c cannot be used together\n", argv[0], threshopt);
				return 1;
			}
			threshopt = loop;
			thresh = ~0;
			break;
		case 'q':
			quiet++;
			break;
		default:
			backup_usage ();
		}
	}

	if (!filename)
	{
		fprintf (stderr, "%s: No filename given.\n", argv[0]);
		return 1;
	}

	drive = 0;
	drive2 = 0;
	if (optind < argc)
		drive = argv[optind++];
	if (optind < argc)
		drive2 = argv[optind++];
	if (optind < argc || !drive)
	{
		fprintf (stderr, "%s: No devices to backup!\n", argv[0]);
		return 1;
	}

	info = init_backup (drive, drive2, flags);
	if (!info)
	{
		fprintf (stderr, "%s: Backup failed to startup.  Make sure you specified the right\ndevices, and that the drives are not locked.\n", argv[0]);
		return 1;
	}
	else
	{
		int secleft = 0;
		char buf[BUFSIZE];
		unsigned int cursec = 0, curcount;
		int fd;

		if (filename[0] == '-' && filename[1] == '\0')
			fd = 1;
		else
			fd = open (filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);

		if (fd < 0)
		{
			perror (filename);
			return 1;
		}

		if (threshopt)
			backup_set_thresh (info, thresh);

		if (quiet < 2)
			fprintf (stderr, "Scanning source drive.  Please wait a moment.\n");

		if (backup_start (info) < 0)
		{
			if (last_err (info))
				fprintf (stderr, "Backup failed: %s\n", last_err (info));
			else
				fprintf (stderr, "Backup failed.\n");
			return 1;
		}

		if (quiet < 1)
			display_backup_info (info);

		if (quiet < 2)
			fprintf (stderr, "Uncompressed backup size: %d megabytes\n", info->nsectors / 2048);
		while ((curcount = backup_read (info, buf, BUFSIZE)) > 0)
		{
			unsigned int prcnt, compr;
			if (write (fd, buf, curcount) != curcount)
			{
				printf ("Backup failed: %s: %s\n", filename, sys_errlist[errno]);
				return 1;
			}
			cursec += curcount / 512;
			prcnt = get_percent (info->cursector, info->nsectors);
			compr = get_percent (info->cursector - cursec, info->cursector);
			if (quiet < 1)
			{
				if (compressed)
					fprintf (stderr, "Backing up %d of %d megabytes (%d.%02d%%) (%d.%02d%% compression)    \r", info->cursector / 2048, info->nsectors / 2048, prcnt / 100, prcnt % 100, compr / 100, compr % 100);
				else
					fprintf (stderr, "Backing up %d of %d megabytes (%d.%02d%%)     \r", info->cursector / 2048, info->nsectors / 2048, prcnt / 100, prcnt % 100);
			}
		}

		if (curcount < 0)
		{
			if (last_err (info))
				fprintf (stderr, "Backup failed: %s\n", last_err (info));
			else
				fprintf (stderr, "Backup failed.");
			return 1;
		}
	}

	if (quiet < 1)
		fprintf (stderr, "\n");
	if (backup_finish (info) < 0)
	{
		if (last_err (info))
			fprintf (stderr, "Backup failed: %s\n", last_err (info));
		else
			fprintf (stderr, "Backup failed!\n");
		return 1;
	}

	if (quiet < 2)
		fprintf (stderr, "Backup done!\n");

	return 0;
}
