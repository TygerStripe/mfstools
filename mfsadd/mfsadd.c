#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/param.h>
#include "mfs.h"

#define mfsadd_usage()

int
mfsadd_scan_partitions (struct mfs_handle *mfs, int *used)
{
	char partitions[128];
	char *loop = partitions;

	strcpy (partitions, mfs_partition_list (mfs));

	while (*loop)
	{
		int drive;
		int partition;

		while (*loop && isspace (*loop))
		{
			loop++;
		}

		if (strncmp (loop, "/dev/hd", 7))
		{
			fprintf (stderr, "Non-standard drive in MFS - giving up.\n");
			return -1;
		}
		loop += 7;

		if (*loop != 'a' && *loop != 'b')
		{
			fprintf (stderr, "Non-standard drive in MFS - giving up.\n");
			return -1;
		}

		drive = *loop - 'a';
		loop++;

		partition = strtoul (loop, &loop, 10);

		if (*loop && !isspace (*loop) || partition < 2 || partition > 16)
		{
			fprintf (stderr, "Non-standard partition in MFS - giving up.\n");
			return -1;
		}

		if (used[drive] & (1 << partition))
		{
			fprintf (stderr, "Non-standard partition in MFS - giving up.\n");
			return -1;
		}

		used[drive] |= 1 << partition;
	}

	return 0;
}

int
mfsadd_add_extends (struct mfs_handle *mfs, char **drives, char **xdevs, char **pairs, char *pairnums, int *npairs, int minalloc)
{
	int loop;
	char tmp[MAXPATHLEN];

	for (loop = 0; loop < 2 && xdevs[loop]; loop++)
	{
		unsigned int maxfree = tivo_partition_largest_free (xdevs[loop]);
		unsigned int totalfree = tivo_partition_total_free (xdevs[loop]);
		unsigned int used = maxfree & ~(minalloc - 1);
		unsigned int required = mfs_volume_pair_app_size (used, minalloc);
		unsigned int part1, part2;
		int devn = xdevs[loop] == drives[0]? 0: 1;

		if (maxfree < 1024 * 1024 * 2)
			continue;

		if (totalfree - maxfree < required && maxfree - used < required)
		{
			used = (maxfree - required) & ~(minalloc - 1);
			required = mfs_volume_pair_app_size (used, minalloc);
		}

		if (totalfree - maxfree >= required && maxfree - used < required)
		{
			part2 = tivo_partition_add (xdevs[loop], used, 0, "New MFS Media", "MFS");
			part1 = tivo_partition_add (xdevs[loop], required, part2, "New MFS Application", "MFS");

			part2++;
		}
		else
		{
			part1 = tivo_partition_add (xdevs[loop], required, 0, "New MFS Application", "MFS");
			part2 = tivo_partition_add (xdevs[loop], used, 0, "New MFS Media", "MFS");
		}

		if (part1 < 2 || part2 < 2 || part1 > 16 || part2 > 16)
		{
			fprintf (stderr, "Expand of %s would result in too many partitions.\n", xdevs[loop]);
			return -1;
		}

		pairnums[(*npairs)++] = (devn << 6) | part1;
		pairnums[(*npairs)++] = (devn << 6) | part2;
		sprintf (tmp, "%s%d", xdevs[loop], part1);
		pairs[*npairs - 2] = strdup (tmp);
		sprintf (tmp, "%s%d", xdevs[loop], part2);
		pairs[*npairs - 1] = strdup (tmp);
		if (!pairs[*npairs - 2] || !pairs[*npairs - 1])
		{
			fprintf (stderr, "Memory exhausted!\n");
			return -1;
		}
	}

	return 0;
}

int
check_partition_count (struct mfs_handle *mfs, char *pairnums, int npairs)
{
	int loop, total;

	total = strlen (mfs_partition_list (mfs));

	for (loop = 0; loop < npairs; loop++)
	{
		if (pairnums[loop] & 31 >= 10)
			total += 11;
		else
			total += 10;
	}

	if (total >= 128)
	{
		fprintf (stderr, "Added partitions would exceed MFS limit!\n");
		return -1;
	}

	return 0;
}

int
mfsadd_main (int argc, char **argv)
{
	unsigned int minalloc = 0x800 << 2;
	int opt;
	int extendall = 0;
	char *xdevs[2];
	int npairs = 0;
	char *pairs[32];
	char pairnums[32];
	int loop, loop2;
	char *drives[2] = {0, 0};
	char partitioncount[2] = {0, 0};
	char *tmp;
	int used[2] = {0, 0};
	struct mfs_handle *mfs;

	while ((opt = getopt (argc, argv, "x:s:")) > 0)
	{
		switch (opt)
		{
		case 'x':
			if (extendall < 2)
			{
				xdevs[extendall] = optarg;
				extendall++;
				break;
			}

			fprintf (stderr, "%s: Can only extend MFS to 2 devices.\n", argv[0]);
			return 1;
		case 's':
			minalloc = 0x800 << strtoul (optarg, &tmp, 10);
			if (tmp && *tmp)
			{
				fprintf (stderr, "%S: Integer argument expected for -s.\n", argv[0]);
				return 1;
			}
			if (minalloc < 0x800 || minalloc > (0x800 << 4))
			{
				fprintf (stderr, "%S: Value for -s must be between 4 and 4.\n", argv[0]);
				return 1;
			}
			break;
		default:
			mfsadd_usage ();
			return 1;
		}
	}

	while (optind < argc)
	{
		int len = strlen (argv[optind]);

		if (isdigit (argv[optind][len - 1]))
		{
/* If the device is a partition, add that partition to the list. */
			if (npairs + 4 >= sizeof (pairs) / sizeof (*pairs))
			{
				fprintf (stderr, "%s: Too many new partitions!\n", argv[0]);
				return 1;
			}
			pairs[npairs] = argv[optind];
			npairs++;
		}
		else
		{
/* If the device is a drive, set it as the A or B drive. */
			if (!drives[0])
				drives[0] = argv[optind];
			else if (!drives[1])
				drives[1] = argv[optind];
			else
			{
				mfsadd_usage ();
				return 1;
			}
		}
		optind++;
	}

/* Can't do anything without an A or B drive. */
	if (!drives[0])
	{
		mfsadd_usage ();
		return 1;
	}

/* The error message says it all. */
	if (npairs & 1)
	{
		fprintf (stderr, "%s: Number of new partitions must be even.\n", argv[0]);
		return 1;
	}

/* Map the drives being extended to the A and B drives. */
	for (loop = 0; loop < extendall; loop++)
	{
		if (!strcmp (xdevs[loop], drives[0]))
			xdevs[loop] = drives[0];
		else if (!drives[1])
			drives[1] = xdevs[loop];
 		else if (!strcmp (xdevs[loop], drives[1]))
			xdevs[loop] = drives[1];
		else
		{
			fprintf (stderr, "%s: Arguments to -x must be one of %s or %s.\n", argv[0], drives[0], drives[1]);
			return 1;
		}
	}

/* Make sure both extend drives are not the same device. */
	if (extendall == 2 && xdevs[0] == xdevs[1])
	{
		fprintf (stderr, "%s: -x argument only makes sense once for each device.\n", argv[0]);
		return 1;
	}

/* Go through all the added pairs. */
	for (loop = 0; loop < npairs; loop++)
	{
		int tmp;
		char *str;

/* Figure out what drive it's on. */
		if (!strncmp (pairs[loop], drives[0], strlen (drives[0])))
			pairnums[loop] = 0x00;
		else if (!drives[1] || !strncmp (pairs[loop], drives[1], strlen (drives[1])))
			pairnums[loop] = 0x40;
		else
		{
			fprintf (stderr, "%s: added partitions must be partitions of either %s or %s\n", argv[0], drives[0], drives[1]);
			return 1;
		}

/* If it is a new drive, add it as the B drive. */
		if (!drives[pairnums[loop] >> 6])
		{
			char *num;
			char *new;

/* Known memory leak.  Deal. */
			new = strdup (pairs[loop]);

			if (!new)
			{
				fprintf (stderr, "%s: Memory allocation error.\n", argv[0]);
				return 1;
			}

/* This is just strrspn - if it existed. */
			for (num = new + strlen (new) - 1; num > new && isdigit (*num); num--)
				;
			num++;
			*num = 0;

/* Set the new drive name. */
			drives[pairnums[loop] >> 6] = new;
		}

		tmp = strtoul (pairs[loop] + strlen (drives[pairnums[loop] >> 6]), &str, 10);
		if (str && *str)
		{
			fprintf (stderr, "%s: added partition %s is not a partition of %s\n", argv[0], pairs[loop], drives[pairnums[loop] >> 6]);
			return 1;
		}

		if (tmp < 2 || tmp > 16)
		{
			fprintf (stderr, "%s: TiVo only supports partiton numbers 2 through 16\n", argv[0]);
			return 1;
		}

		pairnums[loop] |= tmp;

		for (loop2 = 0; loop2 < loop; loop2++)
		{
			if (pairnums[loop] == pairnums[loop2])
			{
				fprintf (stderr, "%s: Can only add %s once.\n", argv[0], pairs[loop]);
				return 1;
			}
		}
	}

	setenv ("MFS_HDA", drives[0], 1);
	setenv ("MFS_HDB", drives[1]? drives[1]: "Second MFS Drive Needed", 1);

	mfs = mfs_init (O_RDWR);

	if (!mfs)
	{
		fprintf (stderr, "Unable to open MFS drives.\n");
		return 1;
	}

	if (mfsadd_scan_partitions (mfs, used) < 0)
		return 1;

	for (loop = 0; loop < npairs; loop++)
	{
		if (used[pairnums[loop] >> 6] & (1 << (pairnums[loop] & 31)))
		{
			fprintf (stderr, "%s already in MFS set.\n", pairs[loop]);
			return 1;
		}
	}

	partitioncount[0] = tivo_partition_count (drives[0]);
	if (drives[1])
		partitioncount[1] = tivo_partition_count (drives[1]);
	for (loop = 0; loop < npairs; loop++)
	{
		if (partitioncount[pairnums[loop] >> 6] < (pairnums[loop] & 31))
		{
			fprintf (stderr, "Partition %s doesn't exist!\n", pairs[loop]);
			return 1;
		}

		if ((pairnums[loop] >> 6) == 0 && (pairnums[loop] & 31) < 10)
		{
			fprintf (stderr, "Partition %s would trash system partition!\n", pairs[loop]);
			return 1;
		}
	}

	if (drives[1] && !used[1])
	{
		int swab = 0;
		if (tivo_partition_swabbed (drives[0]))
			swab ^= 1;
		if (tivo_partition_devswabbed (drives[0]))
			swab ^= 1;
		if (tivo_partition_devswabbed (drives[1]))
			swab ^= 1;
		if (tivo_partition_table_init (drives[1], swab) < 0)
		{
			fprintf (stderr, "Error initializing new B drive!\n");
			return 1;
		}
	}

	if (mfsadd_add_extends (mfs, drives, xdevs, pairs, pairnums, &npairs, minalloc) < 0)
		return 1;

	if (check_partition_count (mfs, pairnums, npairs) < 0)
		return 1;

	if (xdevs[0])
		tivo_partition_table_write (xdevs[0]);
	if (xdevs[1])
		tivo_partition_table_write (xdevs[1]);

	for (loop = 0; loop < npairs; loop += 2)
	{
		char app[MAXPATHLEN];
		char media[MAXPATHLEN];

		fprintf (stderr, "Adding pair %s-%s...\n", pairs[loop], pairs[loop + 1]);
		sprintf (app, "/dev/hd%c%d", 'a' + (pairnums[loop] >> 6), pairnums[loop] & 31);
		sprintf (media, "/dev/hd%c%d", 'a' + (pairnums[loop + 1] >> 6), pairnums[loop + 1] & 31);
		if (mfs_add_volume_pair (mfs, app, media, minalloc) < 0)
		{
			fprintf (stderr, "Error adding %s-%s!\n", pairs[loop], pairs[loop + 1]);
			return 1;
		}
	}

	fprintf (stderr, "Done!\n");

	return 0;
}
