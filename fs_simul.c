#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "filesystem.h"

#define FS_FORMAT 1
#define FS_CREATE 2
#define FS_READ 3
#define FS_DEL 4
#define FS_LS 5
#define FS_MKDIR 6
#define FS_RMDIR 7
#define FS_STATUS 8

void usage()
{
	printf(" -format\n");
	printf(" -create <disk file> <simulated file>\n");
	printf(" -read <disk file> <simulated file>\n");
	printf(" -ls <absolute directory path>\n");
	printf(" -del <simulated file>\n");
	printf(" -mkdir <absolute directory path>\n");
	printf(" -rmdir <absolute directory path>\n");
}

int main(int argc, char **argv)
{
	int res = 0;
	if (argc < 2)
	{
		usage();
		exit(1);
	}

	unsigned char op = 9;

	if (strcmp(argv[1], "-format") == 0)
		op = FS_FORMAT;
	else if (strcmp(argv[1], "-create") == 0)
		op = FS_CREATE;
	else if (strcmp(argv[1], "-read") == 0)
		op = FS_READ;
	else if (strcmp(argv[1], "-del") == 0)
		op = FS_DEL;
	else if (strcmp(argv[1], "-ls") == 0)
		op = FS_LS;
	else if (strcmp(argv[1], "-mkdir") == 0)
		op = FS_MKDIR;
	else if (strcmp(argv[1], "-rmdir") == 0)
		op = FS_RMDIR;
	else if (strcmp(argv[1], "-status") == 0)
		op = FS_STATUS;

	unsigned int size = 0;

	switch (op)
	{
	case FS_FORMAT:
		if (argc < 3)
		{
			printf("Necessario informar tamanho do sistema de arquivos!\n\n");
			usage();
			exit(1);
		}

		size = atoi(argv[2]);
		if (size > 0)
		{
			fs_format(size);
			exit(0);
		}
		else
		{
			printf("Tamanho do sistema de arquivos invalido!\n\n");
			usage();
			exit(1);
		}
		break;

	case FS_CREATE:
		if (argc < 3)
		{
			printf("Necessario informar um nome de arquivo!\n\n");
			usage();
			exit(1);
		}
		fs_create(argv[2], argv[3]);
		break;

	case FS_READ:
		if (argc < 3)
		{
			printf("Necessario informar um nome de arquivo!\n\n");
			usage();
			exit(1);
		}
		fs_read(argv[2], argv[3]);
		break;

	case FS_DEL:
		if (argc < 3)
		{
			printf("Necessario informar um nome de arquivo!\n\n");
			usage();
			exit(1);
		}
		fs_del(argv[2]);
		break;

	case FS_LS:
		if (argc == 1)
			fs_ls(NULL);
		else
			fs_ls(argv[2]);

		break;

	case FS_MKDIR:
		if (argc < 3)
		{
			printf("Necessario informar um nome de diretorio!\n\n");
			usage();
			exit(1);
		}
		fs_mkdir(argv[2]);
		break;

	case FS_RMDIR:
		if (argc < 3)
		{
			printf("Necessario informar um nome de diretorio!\n\n");
			usage();
			exit(1);
		}
		fs_rmdir(argv[2]);
		break;

	case FS_STATUS:
		fs_status(1);
		break;

	default:
		usage();
	}

	return 0;
}
