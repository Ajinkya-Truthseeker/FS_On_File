#include <stdio.h>
#include <stdlib.h>
#include "fs_include.h"
#include "layout.h"
#include "inode.h"
#include "fs.h"

int
main(
        int                     argc,
        char                    *argv[])
{
	struct udirentry	*ud = NULL;
	struct file_handle      *fh = NULL;
	struct fsmem            *fsm;
	FSHANDLE                fsh = NULL;
	int			nent, i;

	if (argc != 3) {
		fprintf(stderr, "Usage: %s <device file> <path> \n", argv[0]);
		return 1;
	}

	if ((fsh = fsmount(argv[1], argv[2])) == NULL) {
                fprintf(stderr, "Failed to mount file system\n");
                return 1;
        }
	printf("FS mounted successfully\n");
	fh = fsopen(fsh, argv[2], 1);
	if (fh == NULL) {
		fprintf(stderr, "Failed to open file %s\n", argv[2]);
		return 1;
	}
	printf("Opened file %s successfully\n", argv[2]);
	ud = (struct udirentry *)malloc(sizeof(struct udirentry) * 8);
	while (1) {
		nent = fsread_dir(fh, (char *)ud, 8);
		if (nent < 8) {
			printf("less than 8 entries read\n");
			if (nent == 0) {
				break;
			}
		}
		for (i = 0; i < nent; i++) {
			printf("name: %s, inum: %llu\n", ud->udir_name,
				ud->udir_inum);
			ud++;
		}
	}

	return 0;
}
