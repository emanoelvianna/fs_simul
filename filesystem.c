#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include "libdisksimul.h"
#include "filesystem.h"

/**
 * @brief Format disk.
 * 
 */
int fs_format()
{
	int ret, i;
	struct root_table_directory root_dir;
	struct sector_data sector;

	if ((ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 1)) != 0)
	{
		return ret;
	}

	memset(&root_dir, 0, sizeof(root_dir));

	root_dir.free_sectors_list = 1; /* first free sector. */

	ds_write_sector(0, (void *)&root_dir, SECTOR_SIZE);

	/* Create a list of free sectors. */
	memset(&sector, 0, sizeof(sector));

	for (i = 1; i < NUMBER_OF_SECTORS; i++)
	{
		if (i < NUMBER_OF_SECTORS - 1)
		{
			sector.next_sector = i + 1;
		}
		else
		{
			sector.next_sector = 0;
		}
		ds_write_sector(i, (void *)&sector, SECTOR_SIZE);
	}

	ds_stop();

	printf("Disk size %d kbytes, %d sectors.\n", (SECTOR_SIZE * NUMBER_OF_SECTORS) / 1024, NUMBER_OF_SECTORS);

	return 0;
}

/**
 * @brief Create a new file on the simulated filesystem.
 * @param input_file Source file path.
 * @param simul_file Destination file path on the simulated file system.
 * @return 0 on success.
 */
int fs_create(char *input_file, char *simul_file)
{
	int ret;
	if ((ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 0)) != 0)
	{
		return ret;
	}

	FILE *fp_in;
	fp_in = fopen(input_file, "r");

	if (fp_in == NULL)
	{
		printf("Erro ao carregar arquivo de entrada.\n");
		return 1;
	}

	FILE *fp;

	fp = fopen(FILENAME, "r+");

	if (fp == NULL)
	{
		printf("Falha ao carregar sistema de arquivos.\n");
		return 1;
	}

	struct root_table_directory root_t = loadRootTable(fp);
	struct sector_data sector_v;

	// Remove PATH do nome do arquivo
	char *token;
	char *delimiter = DIRECTORY_DELIMITER;
	token = strtok(input_file, delimiter);
	input_file = token;
	struct file_dir_entry entry_v, *entry_list_ptr;
	struct table_directory dir_t;
	unsigned int update_dir_t_sector = 0, depth = 0, limite = 0;
	unsigned char save_root = 0;

	while ((token = strtok(NULL, delimiter)) != NULL)
		input_file = token;

	if (simul_file == NULL)
	{
		simul_file = malloc(20);
		strcpy(simul_file, input_file);
	}

	// Percorre arvore de diretorios e define PATH e nome de arquivo
	token = strtok(simul_file, DIRECTORY_DELIMITER);
	entry_list_ptr = &root_t.entries[0];
	strcpy(simul_file, token);

	while ((token = strtok(NULL, DIRECTORY_DELIMITER)) != NULL)
	{
		if (depth == 0)
			entry_v = searchEntry(simul_file, entry_list_ptr, MAX_ENTRIES_ROOT);
		else
			entry_v = searchEntry(simul_file, entry_list_ptr, MAX_ENTRIES_DIRECTORY);

		if (entry_v.dir == 0)
		{
			printf("Diretorio \"%s\" nao existe. Caminho invalido!\n", simul_file);
			fclose(fp_in);
			fclose(fp);
			return 1;
		}

		// Avança descritor para setor com table_directory
		update_dir_t_sector = entry_v.sector_start;
		fseek(fp, entry_v.sector_start * SECTOR_SIZE, SEEK_SET);
		fread(&dir_t, SECTOR_DATA_SIZE, 1, fp);
		entry_list_ptr = &dir_t.entries[0];
		strcpy(simul_file, token);
		depth++;
	}

	limite = (depth > 0) ? MAX_ENTRIES_DIRECTORY : MAX_ENTRIES_ROOT;

	entry_v = searchEntry(simul_file, entry_list_ptr, limite);

	if (entry_v.dir == 'f' && entry_v.sector_start != 0)
	{
		// Arquivo explicito de destino
		printf("Existe um arquivo com nome \"%s\"!\n", simul_file);
		fclose(fp_in);
		fclose(fp);
		return 1;
	}
	else if (entry_v.dir == 'd')
	{
		// Diretorio de destino com mesmo nome do arquivo original
		struct file_dir_entry entry_v_aux;
		struct table_directory dir_t_aux;

		strcpy(simul_file, input_file);

		fseek(fp, entry_v.sector_start * SECTOR_SIZE, SEEK_SET);
		fread(&dir_t_aux, SECTOR_DATA_SIZE, 1, fp);

		// Verifica se nao existe
		entry_v_aux = searchEntry(simul_file, dir_t_aux.entries, limite);

		if (entry_v_aux.dir == 'f')
		{
			printf("Existe um arquivo com nome \"%s\"!\n", simul_file);
			fclose(fp_in);
			fclose(fp);
			return 1;
		}

		// copia para um diretorio com nome original
		update_dir_t_sector = entry_v.sector_start;
		fseek(fp, entry_v.sector_start * SECTOR_SIZE, SEEK_SET);
		fread(&dir_t, SECTOR_SIZE, 1, fp);
		entry_list_ptr = &dir_t.entries[0];
		strcpy(simul_file, input_file);
		save_root = 1;
	}
	else
	{
		save_root = (depth > 0) ? 1 : 0;
	}

	// Cria entrada do arquivo
	entry_v.dir = 'f';
	strcpy(entry_v.name, simul_file);
	entry_v.size_bytes = getFileSize(fp_in);
	entry_v.sector_start = root_t.free_sectors_list;

	if (entry_v.size_bytes >= getTotalFreeBlocks(fp) * SECTOR_DATA_SIZE)
	{
		printf("Espaco insuficiente para armazenar o arquivo!\n");
		fclose(fp_in);
		fclose(fp);
		return 1;
	}

	// Adiciona entrada e atualiza root_table free_sectors_list
	unsigned int pos_n = getFirstFreePos(entry_list_ptr, MAX_ENTRIES_ROOT);

	if (pos_n >= limite)
	{
		fclose(fp_in);
		fclose(fp);
		printf("Limite de itens no diretorio excedido!\n");
		return 1;
	}

	entry_list_ptr[pos_n] = entry_v;

	// Obtem o proximo setor livre para escrita
	// e grava root_table
	fseek(fp, root_t.free_sectors_list * SECTOR_SIZE, SEEK_SET);
	fread(&sector_v, SECTOR_SIZE, 1, fp);
	root_t.free_sectors_list = sector_v.next_sector;
	writeRootTable(fp, &root_t);

	// Se o arquivo nao estiver na root_table
	if (save_root == 1)
	{
		fseek(fp, update_dir_t_sector * SECTOR_SIZE, SEEK_SET);
		fwrite(&dir_t, SECTOR_SIZE, 1, fp);
	}

	// Escreve os dados propriamente ditos nos setores
	int file_l = entry_v.size_bytes;
	unsigned int write_block = entry_v.sector_start, next_sector = entry_v.sector_start;

	while (file_l > SECTOR_DATA_SIZE)
	{
		// Obtem next_sector
		fseek(fp, write_block * SECTOR_SIZE, SEEK_SET);
		fread(&sector_v, SECTOR_SIZE, 1, fp);
		next_sector = sector_v.next_sector;
		fseek(fp, 0, SEEK_SET);
		fwrite(&next_sector, 4, 1, fp);
		//printf("Proximo setor livre: %d\n", next_sector);

		// Le dados do arquivo de entrada e grava
		// no disco
		fread(&sector_v.data, SECTOR_DATA_SIZE, 1, fp_in);
		sector_v.next_sector = next_sector;
		fseek(fp, write_block * SECTOR_SIZE, SEEK_SET);
		fwrite(&sector_v, SECTOR_SIZE, 1, fp);

		file_l -= SECTOR_DATA_SIZE;

		write_block = next_sector;
	}

	// Escreve o ultimo setor de um arquivo
	// ou arquivo que ocupa apenas um unico setor
	fseek(fp, next_sector * SECTOR_SIZE, SEEK_SET);
	fread(&sector_v, SECTOR_SIZE, 1, fp);
	next_sector = sector_v.next_sector;

	fread(&sector_v.data, file_l, 1, fp_in);
	sector_v.next_sector = 0;

	fseek(fp, write_block * SECTOR_SIZE, SEEK_SET);
	fwrite(&sector_v, SECTOR_SIZE, 1, fp);

	fseek(fp, 0, SEEK_SET);
	fwrite(&next_sector, 4, 1, fp);
	fclose(fp);
	fclose(fp_in);

	ds_stop();

	return 0;
}

/**
 * @brief Read file from the simulated filesystem.
 * @param output_file Output file path.
 * @param simul_file Source file path from the simulated file system.
 * @return 0 on success.
 */
int fs_read(char *output_file, char *simul_file)
{
	FILE *fp;

	fp = fopen(FILENAME, "r");

	if (fp == NULL)
	{
		printf("Falha ao carregar sistema de arquivos.\n");
		exit(1);
	}

	struct root_table_directory root_t = loadRootTable(fp);

	// Remove PATH do nome do arquivo
	char *token;
	char *delimiter = DIRECTORY_DELIMITER;
	unsigned int depth = 0;
	struct file_dir_entry entry_v, *entry_list_ptr;
	struct table_directory dir_t;

	// Percorre arvore de diretorios e define PATH e nome de arquivo
	token = strtok(output_file, DIRECTORY_DELIMITER);
	entry_list_ptr = &root_t.entries[0];
	strcpy(output_file, token);

	while ((token = strtok(NULL, DIRECTORY_DELIMITER)) != NULL)
	{
		if (depth == 0)
			entry_v = searchEntry(output_file, entry_list_ptr, MAX_ENTRIES_ROOT);
		else
			entry_v = searchEntry(output_file, entry_list_ptr, MAX_ENTRIES_DIRECTORY);

		if (entry_v.dir == '0')
		{
			printf("Diretorio \"%s\" nao existe. Caminho invalido!\n", output_file);
			return 1;
		}

		// Avança descritor para setor com table_directory
		fseek(fp, entry_v.sector_start * SECTOR_SIZE, SEEK_SET);
		fread(&dir_t, SECTOR_DATA_SIZE, 1, fp);
		entry_list_ptr = &dir_t.entries[0];
		strcpy(output_file, token);
		depth++;
	}

	entry_v = searchEntry(output_file, entry_list_ptr, MAX_ENTRIES_ROOT);

	if (entry_v.dir == '0')
	{
		printf("Arquivo de entrada \"%s\" nao encontrado!\n", output_file);
		fclose(fp);
		return 1;
	}
	else if (entry_v.dir == 'd')
	{
		printf("\"%s\" e um diretorio!\n", output_file);
		fclose(fp);
		return 1;
	}

	struct sector_data sector_v;
	int file_l = entry_v.size_bytes;
	unsigned int read_block = entry_v.sector_start;

	if (simul_file == NULL)
	{
		simul_file = malloc(20);
		strcpy(simul_file, output_file);
	}

	FILE *fp_out;
	fp_out = fopen(simul_file, "w+");

	if (fp_out == NULL)
	{
		printf("Erro ao criar arquivo de saida.\n");
		fclose(fp);
		return 1;
	}

	// Leitura de arquivo que ocupa apenas um bloco
	if (file_l < SECTOR_DATA_SIZE)
	{
		fseek(fp, read_block * SECTOR_SIZE, SEEK_SET);
		fread(&sector_v, file_l, 1, fp);
		fwrite(&sector_v.data, file_l, 1, fp_out);

		fclose(fp_out);
		fclose(fp);
		return 0;
	}

	while (file_l > SECTOR_DATA_SIZE)
	{
		fseek(fp, read_block * SECTOR_SIZE, SEEK_SET);
		fread(&sector_v, SECTOR_SIZE, 1, fp);
		read_block = sector_v.next_sector;

		fwrite(&sector_v.data, SECTOR_DATA_SIZE, 1, fp_out);
		file_l -= SECTOR_DATA_SIZE;
	}

	// Recupera ultimo bloco parcial do arquivo
	fseek(fp, sector_v.next_sector * SECTOR_SIZE, SEEK_SET);
	fread(&sector_v, SECTOR_SIZE, 1, fp);
	fwrite(&sector_v.data, file_l, 1, fp_out);

	fclose(fp_out);
	fclose(fp);
	return 0;
}

/**
 * @brief Delete file from file system.
 * @param simul_file Source file path.
 * @return 0 on success.
 */
int fs_del(char *simul_file)
{
	int ret;
	if ((ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 0)) != 0)
	{
		return ret;
	}

	/* Write the code delete a file from the simulated filesystem. */

	ds_stop();

	return 0;
}

/**
 * @brief List files from a directory.
 * @param simul_file Source file path.
 * @return 0 on success.
 */
int fs_ls(char *dir_path)
{
	FILE *fp;

	fp = fopen(FILENAME, "r");

	if (fp == NULL)
	{
		printf("Falha ao carregar sistema de arquivos.\n");
		exit(1);
	}

	struct root_table_directory root_t = loadRootTable(fp);

	// Remove PATH do nome do arquivo
	unsigned char *str = NULL;
	char *token;
	char *delimiter = DIRECTORY_DELIMITER;
	struct file_dir_entry entry_v, *entry_list_ptr;
	struct table_directory dir_t;
	unsigned int update_dir_t_sector = 0, depth = 0, limite = 0;

	// Percorre arvore de diretorios e define PATH e nome de arquivo
	token = strtok(dir_path, delimiter);
	dir_path = token;
	entry_list_ptr = &root_t.entries[0];

	while ((token = strtok(NULL, DIRECTORY_DELIMITER)) != NULL)
	{
		if (depth == 0)
			entry_v = searchEntry(dir_path, entry_list_ptr, MAX_ENTRIES_ROOT);
		else
			entry_v = searchEntry(dir_path, entry_list_ptr, MAX_ENTRIES_DIRECTORY);

		if (entry_v.dir == '0')
		{
			printf("Diretorio \"%s\" nao existe. Caminho invalido!\n", token);
			fclose(fp);
			exit(1);
		}

		// Avança descritor para setor com table_directory
		update_dir_t_sector = entry_v.sector_start;
		fseek(fp, entry_v.sector_start * SECTOR_SIZE, SEEK_SET);
		fread(&dir_t, SECTOR_DATA_SIZE, 1, fp);
		entry_list_ptr = &dir_t.entries[0];
		strcpy(dir_path, token);
		depth++;
	}

	if (dir_path == NULL)
	{
		// Imprime a partir da raiz recursivamente
		unsigned char str[30];
		cmd_ls_print(fp, &root_t.entries[0], MAX_ENTRIES_ROOT, str, 0);
	}
	else
	{
		str = "";
		limite = (depth > 0) ? MAX_ENTRIES_DIRECTORY : MAX_ENTRIES_ROOT;
		entry_v = searchEntry(dir_path, entry_list_ptr, limite);

		if (entry_v.dir == 'd')
		{
			cmd_ls_print(fp, &entry_v, limite, str, 1);
		}
		else if (entry_v.dir == 'f')
		{
			printf("O caminho informado eh de um arquivo!\n");
			fclose(fp);
			exit(1);
		}
		else
		{
			printf("Diretorio %s nao existe!\n", dir_path);
			fclose(fp);
			exit(1);
		}
	}
	fclose(fp);

	return 0;
}

/**
 * @brief Create a new directory on the simulated filesystem.
 * @param directory_path directory path.
 * @return 0 on success.
 */
int fs_mkdir(char *directory_path)
{
	int ret;
	if ((ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 0)) != 0)
	{
		return ret;
	}

	/* Write the code to create a new directory. */

	ds_stop();

	return 0;
}

/**
 * @brief Remove directory from the simulated filesystem.
 * @param directory_path directory path.
 * @return 0 on success.
 */
int fs_rmdir(char *directory_path)
{
	int ret;
	if ((ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 0)) != 0)
	{
		return ret;
	}

	/* Write the code to delete a directory. */

	ds_stop();

	return 0;
}

/**
 * @brief Generate a map of used/available sectors. 
 * @param log_f Log file with the sector map.
 * @return 0 on success.
 */
int fs_free_map(char *log_f)
{
	int ret, i, next;
	struct root_table_directory root_dir;
	struct sector_data sector;
	char *sector_array;
	FILE *log;
	int pid, status;
	int free_space = 0;
	char *exec_params[] = {"gnuplot", "sector_map.gnuplot", NULL};

	if ((ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 0)) != 0)
	{
		return ret;
	}

	/* each byte represents a sector. */
	sector_array = (char *)malloc(NUMBER_OF_SECTORS);

	/* set 0 to all sectors. Zero means that the sector is used. */
	memset(sector_array, 0, NUMBER_OF_SECTORS);

	/* Read the root dir to get the free blocks list. */
	ds_read_sector(0, (void *)&root_dir, SECTOR_SIZE);

	next = root_dir.free_sectors_list;

	while (next)
	{
		/* The sector is in the free list, mark with 1. */
		sector_array[next] = 1;

		/* move to the next free sector. */
		ds_read_sector(next, (void *)&sector, SECTOR_SIZE);

		next = sector.next_sector;

		free_space += SECTOR_SIZE;
	}

	/* Create a log file. */
	if ((log = fopen(log_f, "w")) == NULL)
	{
		perror("fopen()");
		free(sector_array);
		ds_stop();
		return 1;
	}

	/* Write the the sector map to the log file. */
	for (i = 0; i < NUMBER_OF_SECTORS; i++)
	{
		if (i % 32 == 0)
			fprintf(log, "%s", "\n");
		fprintf(log, " %d", sector_array[i]);
	}

	fclose(log);

	/* Execute gnuplot to generate the sector's free map. */
	pid = fork();
	if (pid == 0)
	{
		execvp("gnuplot", exec_params);
	}

	wait(&status);

	free(sector_array);

	ds_stop();

	printf("Free space %d kbytes.\n", free_space / 1024);

	return 0;
}

struct file_dir_entry searchEntry(char *palavra, struct file_dir_entry *entries, unsigned int list_l)
{
	struct file_dir_entry entry_v;
	unsigned int pos_n = 0;
	entry_v = entries[0];

	while (strcmp(palavra, entry_v.name) != 0 && pos_n < list_l)
	{
		entry_v = entries[++pos_n];
	}

	if (pos_n == list_l)
	{
		entry_v.dir = 0;
		return entry_v;
	}

	return entry_v;
}

int getFileSize(FILE *fp)
{
	int curr_pos = ftell(fp);
	int size = 0;
	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	fseek(fp, curr_pos, SEEK_SET);
	return size;
}

unsigned int getFirstFreePos(struct file_dir_entry *entries, unsigned int list_l)
{
	struct file_dir_entry entry_v;
	unsigned int pos_n = 0;

	entry_v = entries[0];

	while (entry_v.sector_start != 0 && pos_n <= list_l)
		entry_v = entries[++pos_n];

	return pos_n;
}

struct root_table_directory loadRootTable(FILE *fp)
{
	unsigned int curr_pos = ftell(fp);
	struct sector_data sector_v;
	struct root_table_directory root_t;
	int root_l = sizeof(struct root_table_directory);
	unsigned char *root_ptr;

	root_ptr = (char *)&root_t;

	fseek(fp, 0, SEEK_SET);

	while (root_l > 0)
	{
		fread(&sector_v, SECTOR_SIZE, 1, fp);
		memcpy(root_ptr, &sector_v.data, SECTOR_DATA_SIZE);
		root_ptr += SECTOR_DATA_SIZE;
		root_l -= SECTOR_DATA_SIZE;
	}
	fseek(fp, curr_pos, SEEK_SET);
	return root_t;
}

unsigned int getTotalFreeBlocks(FILE *fp)
{
	unsigned int curr_pos = ftell(fp);
	struct root_table_directory root_t = loadRootTable(fp);

	if (root_t.free_sectors_list == 0)
	{
		fseek(fp, curr_pos, SEEK_SET);
		return 0;
	}

	struct sector_data sector_v;
	unsigned int next_free_block = root_t.free_sectors_list;
	unsigned int tot_blocos_livres = 0;

	fseek(fp, next_free_block * SECTOR_SIZE, SEEK_SET);

	while (next_free_block != 0)
	{
		fread(&sector_v, SECTOR_SIZE, 1, fp);
		next_free_block = sector_v.next_sector;
		tot_blocos_livres++;

		fseek(fp, next_free_block * SECTOR_SIZE, SEEK_SET);
	}
	fseek(fp, curr_pos, SEEK_SET);
	return tot_blocos_livres;
}

void writeRootTable(FILE *fp, struct root_table_directory *root_table)
{
	unsigned int curr_pos = ftell(fp);
	struct sector_data sector_v;
	int root_l = sizeof(struct root_table_directory);
	unsigned int sector_n = 0;
	unsigned char *data_ptr;
	data_ptr = (char *)root_table;

	fseek(fp, 0, SEEK_SET);

	// Alimenta sector_v.data com valores de root_t
	// e escreve nos 8 primeiros setores do disco
	while (root_l > SECTOR_DATA_SIZE)
	{
		memcpy(&sector_v.data, data_ptr, SECTOR_DATA_SIZE);
		sector_v.next_sector = sector_n + 1;
		fwrite(&sector_v, SECTOR_SIZE, 1, fp);

		root_l -= SECTOR_DATA_SIZE;
		data_ptr += SECTOR_DATA_SIZE;
		sector_n++;
	}

	// Escreve ultimo setor da root_table
	memcpy(&sector_v.data, data_ptr, SECTOR_DATA_SIZE);
	sector_v.next_sector = 0;
	fwrite(&sector_v, SECTOR_SIZE, 1, fp);

	// Retorna descritor para posicao anterior
	fseek(fp, curr_pos, SEEK_SET);
}

void cmd_ls_print(FILE *fp, struct file_dir_entry *entry_t, unsigned int entry_l, unsigned char *str, unsigned char print_recursivo)
{
	unsigned char int_str[30];
	strcpy(int_str, str);
	struct file_dir_entry entry_v;
	struct table_directory dir_v;

	for (int i = 0; i < entry_l; i++)
	{
		entry_v = entry_t[i];

		if ((entry_v.dir == 'f' && entry_v.sector_start != 0) || entry_v.dir == 'd')
			printf("%s[%c] %-20s\t[%d bytes - 1st setor: %d]\n",
				   str, entry_v.dir, entry_v.name, entry_v.size_bytes, entry_v.sector_start);
		if (entry_v.dir == 'd')
		{
			fseek(fp, entry_v.sector_start * SECTOR_SIZE, SEEK_SET);
			fread(&dir_v, SECTOR_DATA_SIZE, 1, fp);
			strcat(int_str, " |\t");
			cmd_ls_print(fp, &dir_v.entries[0], MAX_ENTRIES_DIRECTORY, int_str, 0);
			strcpy(int_str, str);
		}
	}
}