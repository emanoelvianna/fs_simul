#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "filesystem.h"

/** 
Requer: um tamanho para iniciar o disco virtual
Garante: cria arquivo no sistema de arquivos do simulador e retorna 0
**/
int fs_format(unsigned int size)
{
	FILE *fp;

	fp = fopen(FILENAME, "w");

	if (fp == NULL)
	{
		printf("Falha ao criar arquivo.\n");
		return 1;
	}

	struct sector_data sector_v;
	unsigned int num_sectors = (size * 1024 * 1024) / SECTOR_SIZE;

	// Prepara encadeamento inicial dos blocos do disco
	for (int i = 0; i < num_sectors - 1; i++)
	{
		sector_v.next_sector = i + 1;
		fwrite(&sector_v, SECTOR_SIZE, 1, fp);
	}

	// Escreve ultimo setor com next_sector = ZERO
	sector_v.next_sector = 0;
	fwrite(&sector_v, SECTOR_SIZE, 1, fp);

	// Prepara root do disco
	struct root_table_directory root_t;
	struct file_dir_entry entry_v;

	entry_v.dir = '0';
	strcpy(entry_v.name, "");
	entry_v.size_bytes = 0;
	entry_v.sector_start = 0;

	for (int i = 0; i < MAX_ENTRIES_ROOT; i++)
		root_t.entries[i] = entry_v;

	root_t.free_sectors_list = 8;

	writeRootTable(fp, &root_t);
	fclose(fp);
	return 0;
}

/** 
Copia um arquivo do sistema de arquivos corrente para o simulador
Requer: um nome de arquivo de entrada valido
Garante: cria arquivo no sistema de arquivos do simulador e retorna 0
**/
int fs_create(unsigned char *origem, unsigned char *destino)
{
	FILE *fp_in;
	fp_in = fopen(origem, "r");

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
	token = strtok(origem, delimiter);
	origem = token;
	struct file_dir_entry entry_v, *entry_list_ptr;
	struct table_directory dir_t;
	unsigned int update_dir_t_sector = 0, depth = 0, limite = 0;
	unsigned char save_root = 0;

	while ((token = strtok(NULL, delimiter)) != NULL)
		origem = token;

	if (destino == NULL)
	{
		destino = malloc(20);
		strcpy(destino, origem);
	}

	// Percorre arvore de diretorios e define PATH e nome de arquivo
	token = strtok(destino, DIRECTORY_DELIMITER);
	entry_list_ptr = &root_t.entries[0];
	strcpy(destino, token);

	while ((token = strtok(NULL, DIRECTORY_DELIMITER)) != NULL)
	{
		if (depth == 0)
			entry_v = searchEntry(destino, entry_list_ptr, MAX_ENTRIES_ROOT);
		else
			entry_v = searchEntry(destino, entry_list_ptr, MAX_ENTRIES_DIRECTORY);

		if (entry_v.dir == '0')
		{
			printf("Diretorio \"%s\" nao existe. Caminho invalido!\n", destino);
			fclose(fp_in);
			fclose(fp);
			return 1;
		}

		// Avança descritor para setor com table_directory
		update_dir_t_sector = entry_v.sector_start;
		fseek(fp, entry_v.sector_start * SECTOR_SIZE, SEEK_SET);
		fread(&dir_t, SECTOR_DATA_SIZE, 1, fp);
		entry_list_ptr = &dir_t.entries[0];
		strcpy(destino, token);
		depth++;
	}

	limite = (depth > 0) ? MAX_ENTRIES_DIRECTORY : MAX_ENTRIES_ROOT;

	entry_v = searchEntry(destino, entry_list_ptr, limite);

	if (entry_v.dir == 'f' && entry_v.sector_start != 0)
	{
		// Arquivo explicito de destino
		printf("Existe um arquivo com nome \"%s\"!\n", destino);
		fclose(fp_in);
		fclose(fp);
		return 1;
	}
	else if (entry_v.dir == 'd')
	{
		// Diretorio de destino com mesmo nome do arquivo original
		struct file_dir_entry entry_v_aux;
		struct table_directory dir_t_aux;

		strcpy(destino, origem);

		fseek(fp, entry_v.sector_start * SECTOR_SIZE, SEEK_SET);
		fread(&dir_t_aux, SECTOR_DATA_SIZE, 1, fp);

		// Verifica se nao existe
		entry_v_aux = searchEntry(destino, dir_t_aux.entries, limite);

		if (entry_v_aux.dir == 'f')
		{
			printf("Existe um arquivo com nome \"%s\"!\n", destino);
			fclose(fp_in);
			fclose(fp);
			return 1;
		}

		// copia para um diretorio com nome original
		update_dir_t_sector = entry_v.sector_start;
		fseek(fp, entry_v.sector_start * SECTOR_SIZE, SEEK_SET);
		fread(&dir_t, SECTOR_SIZE, 1, fp);
		entry_list_ptr = &dir_t.entries[0];
		strcpy(destino, origem);
		save_root = 1;
	}
	else
	{
		save_root = (depth > 0) ? 1 : 0;
	}

	// Cria entrada do arquivo
	entry_v.dir = 'f';
	strcpy(entry_v.name, destino);
	entry_v.size_bytes = getFileSize(fp_in);
	entry_v.sector_start = root_t.free_sectors_list;

	unsigned int free_sectors = getTotalFreeBlocks(fp);
	unsigned int file_size = entry_v.size_bytes;

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
	return 0;
}

/**
Copia um arquivo do simulador para o sistema de arquivos corrente
Requer: nomes de arquivos de origem valido
Garante: cria arquivo no sistema de arquivos e retorna 0
**/
int fs_read(unsigned char *origem, unsigned char *destino)
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
	token = strtok(origem, DIRECTORY_DELIMITER);
	entry_list_ptr = &root_t.entries[0];
	strcpy(origem, token);

	while ((token = strtok(NULL, DIRECTORY_DELIMITER)) != NULL)
	{
		if (depth == 0)
			entry_v = searchEntry(origem, entry_list_ptr, MAX_ENTRIES_ROOT);
		else
			entry_v = searchEntry(origem, entry_list_ptr, MAX_ENTRIES_DIRECTORY);

		if (entry_v.dir == '0')
		{
			printf("Diretorio \"%s\" nao existe. Caminho invalido!\n", origem);
			return 1;
		}

		// Avança descritor para setor com table_directory
		fseek(fp, entry_v.sector_start * SECTOR_SIZE, SEEK_SET);
		fread(&dir_t, SECTOR_DATA_SIZE, 1, fp);
		entry_list_ptr = &dir_t.entries[0];
		strcpy(origem, token);
		depth++;
	}

	entry_v = searchEntry(origem, entry_list_ptr, MAX_ENTRIES_ROOT);

	if (entry_v.dir == '0')
	{
		printf("Arquivo de entrada \"%s\" nao encontrado!\n", origem);
		fclose(fp);
		return 1;
	}
	else if (entry_v.dir == 'd')
	{
		printf("\"%s\" e um diretorio!\n", origem);
		fclose(fp);
		return 1;
	}

	struct sector_data sector_v;
	int file_l = entry_v.size_bytes;
	unsigned int read_block = entry_v.sector_start;

	if (destino == NULL)
	{
		destino = malloc(20);
		strcpy(destino, origem);
	}

	FILE *fp_out;
	fp_out = fopen(destino, "w+");

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
Exclui um arquivo do simulador
Requer: um nome de arquivo de entrada valido
Garante: define entrada_arquivo->sector_start como zero e apenda blocos co arquivo na lista de blocos livres
**/
unsigned int fs_del(unsigned char *file)
{
	FILE *fp;

	fp = fopen(FILENAME, "r+");

	if (fp == NULL)
	{
		printf("Falha ao carregar sistema de arquivos.\n");
		exit(1);
	}

	struct root_table_directory root_t = loadRootTable(fp);

	// Remove PATH do nome do arquivo
	char *token;
	char *delimiter = DIRECTORY_DELIMITER;
	unsigned int depth = 0, update_entry_t_sector = 0;
	struct file_dir_entry entry_v, *entry_list_ptr;
	struct table_directory dir_t;
	struct sector_data sector_v;

	// Percorre arvore de diretorios e define PATH e nome de arquivo
	token = strtok(file, DIRECTORY_DELIMITER);
	entry_list_ptr = &root_t.entries[0];
	strcpy(file, token);

	while ((token = strtok(NULL, DIRECTORY_DELIMITER)) != NULL)
	{
		if (depth == 0)
			entry_v = searchEntry(file, entry_list_ptr, MAX_ENTRIES_ROOT);
		else
			entry_v = searchEntry(file, entry_list_ptr, MAX_ENTRIES_DIRECTORY);

		if (entry_v.dir == '0')
		{
			printf("Diretorio \"%s\" nao existe. Caminho invalido!\n", file);
			return 1;
		}

		// Avança descritor para setor com table_directory
		update_entry_t_sector = entry_v.sector_start;
		fseek(fp, entry_v.sector_start * SECTOR_SIZE, SEEK_SET);
		fread(&dir_t, SECTOR_DATA_SIZE, 1, fp);
		entry_list_ptr = &dir_t.entries[0];
		strcpy(file, token);
		depth++;
	}

	unsigned int pos_n = 0, limite = 0, file_sector = 0;
	limite = (depth > 0) ? MAX_ENTRIES_DIRECTORY : MAX_ENTRIES_ROOT;

	entry_v = entry_list_ptr[0];

	while (strcmp(entry_v.name, file) != 0 && pos_n <= limite)
		entry_v = entry_list_ptr[++pos_n];

	if ((pos_n >= limite) || (strcmp(entry_v.name, file) == 0 && entry_v.dir == 'f' && entry_v.sector_start == 0))
	{
		printf("Arquivo de entrada \"%s\" nao encontrado!\n", file);
		fclose(fp);
		return 1;
	}
	else if (entry_v.dir == 'd')
	{
		printf("\"%s\" e um diretorio!\n", file);
		fclose(fp);
		return 1;
	}

	file_sector = entry_v.sector_start;

	// Exclui entrada da entries - desaloca setor inicial
	entry_v.sector_start = 0;
	entry_list_ptr[pos_n] = entry_v;

	// Entrada nao armazenada em root_table
	if (depth > 0)
	{
		fseek(fp, update_entry_t_sector * SECTOR_SIZE, SEEK_SET);
		fwrite(&dir_t, SECTOR_SIZE, 1, fp);
	}

	// Encontra ultimo setor livre
	unsigned int last_sector = 0;

	if (root_t.free_sectors_list == 0)
	{
		// Todos os blocos ocupados, arquivo excluido passa a
		// ser a lista de blocos livres
		root_t.free_sectors_list = file_sector;
	}
	else
	{
		// Adiciona primeiro setor do arquivo no final da lista
		// de setores livres
		fseek(fp, root_t.free_sectors_list * SECTOR_SIZE, SEEK_SET);
		fread(&sector_v, SECTOR_SIZE, 1, fp);

		last_sector = root_t.free_sectors_list;

		while (sector_v.next_sector != 0)
		{
			fseek(fp, sector_v.next_sector * SECTOR_SIZE, SEEK_SET);
			last_sector = sector_v.next_sector;
			fread(&sector_v, SECTOR_SIZE, 1, fp);
		}

		sector_v.next_sector = file_sector;
		fseek(fp, last_sector * SECTOR_SIZE, SEEK_SET);
		fwrite(&sector_v, SECTOR_SIZE, 1, fp);
	}

	writeRootTable(fp, &root_t);
	fclose(fp);
	return 0;
}

/**
Exibe uma lista dos arquivos no simulador
Requer: TRUE
Garante: lista recursivamente estrutura de arquivos e diretorios
**/
void fs_ls(unsigned char *nome)
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
	token = strtok(nome, delimiter);
	nome = token;
	entry_list_ptr = &root_t.entries[0];

	while ((token = strtok(NULL, DIRECTORY_DELIMITER)) != NULL)
	{
		if (depth == 0)
			entry_v = searchEntry(nome, entry_list_ptr, MAX_ENTRIES_ROOT);
		else
			entry_v = searchEntry(nome, entry_list_ptr, MAX_ENTRIES_DIRECTORY);

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
		strcpy(nome, token);
		depth++;
	}

	if (nome == NULL)
	{
		// Imprime a partir da raiz recursivamente
		unsigned char str[30];
		fs_ls_print(fp, &root_t.entries[0], MAX_ENTRIES_ROOT, str, 0);
	}
	else
	{
		str = "";
		limite = (depth > 0) ? MAX_ENTRIES_DIRECTORY : MAX_ENTRIES_ROOT;
		entry_v = searchEntry(nome, entry_list_ptr, limite);

		if (entry_v.dir == 'd')
		{
			fs_ls_print(fp, &entry_v, limite, str, 1);
		}
		else if (entry_v.dir == 'f')
		{
			printf("O caminho informado eh de um arquivo!\n");
			fclose(fp);
			exit(1);
		}
		else
		{
			printf("Diretorio %s nao existe!\n", nome);
			fclose(fp);
			exit(1);
		}
	}
	fclose(fp);
}

/**
Exibe o conteudo de uma estrutura de diretorios recursivamente
Requer: lista de diretorios
Garante: lista recursivamente estrutura de arquivos e diretorios
**/
void fs_ls_print(FILE *fp, struct file_dir_entry *entry_t,
				 unsigned int entry_l,
				 unsigned char *str,
				 unsigned char print_recursivo)
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
			fs_ls_print(fp, &dir_v.entries[0], MAX_ENTRIES_DIRECTORY, int_str, 0);
			strcpy(int_str, str);
		}
	}
}

/**
Cria um diretorio no simulador
Requer: nome de diretorio valido
Garante: o novo diretorio eh criado e a funcao retorna 0
**/
int fs_mkdir(unsigned char *name)
{
	FILE *fp;
	fp = fopen(FILENAME, "r+");

	if (fp == NULL)
	{
		printf("Falha ao carregar sistema de arquivos.\n");
		exit(1);
	}

	if (SECTOR_SIZE > getTotalFreeBlocks(fp) * SECTOR_DATA_SIZE)
	{
		printf("Espaco insuficiente para armazenar o arquivo!\n");
		fclose(fp);
		return 1;
	}

	struct file_dir_entry entry_v, *entry_list_ptr;
	struct root_table_directory root_t = loadRootTable(fp);
	struct sector_data sector_v;
	struct table_directory dir_t;

	// Percorre PATH e verifica validade
	unsigned int depth = 0, update_dir_t_sector = 0;
	char *token;
	char *delimiter = DIRECTORY_DELIMITER;
	token = strtok(name, delimiter);
	name = token;
	entry_list_ptr = &root_t.entries[0];

	while ((token = strtok(NULL, delimiter)) != NULL)
	{
		// Possui um PATH e nao um unico nome
		if (depth == 0)
			entry_v = searchEntry(name, entry_list_ptr, MAX_ENTRIES_ROOT);
		else
			entry_v = searchEntry(name, entry_list_ptr, MAX_ENTRIES_DIRECTORY);

		if (entry_v.dir == '0')
		{
			printf("Diretorio \"%s\" nao existe. Caminho invalido!\n", name);
			return 1;
		}

		// Avança descritor para setor com table_directory
		update_dir_t_sector = entry_v.sector_start;
		fseek(fp, entry_v.sector_start * SECTOR_SIZE, SEEK_SET);
		fread(&dir_t, SECTOR_DATA_SIZE, 1, fp);
		entry_list_ptr = &dir_t.entries[0];
		strcpy(name, token);
		depth++;
	}

	if (depth == 0)
		entry_v = searchEntry(name, entry_list_ptr, MAX_ENTRIES_ROOT);
	else
		entry_v = searchEntry(name, entry_list_ptr, MAX_ENTRIES_DIRECTORY);

	if (entry_v.dir == 'd')
	{
		printf("Ja existe um diretorio de nome \"%s\"!\n", name);
		return 1;
	}
	else if (entry_v.dir == 'f')
	{
		printf("Ja existe um arquivo de nome \"%s\"!\n", name);
		return 1;
	}

	unsigned int pos_n = 0;
	if (depth == 0)
		pos_n = getFirstFreePos(entry_list_ptr, MAX_ENTRIES_ROOT);
	else
		pos_n = getFirstFreePos(entry_list_ptr, MAX_ENTRIES_DIRECTORY);

	// Define valores do novo diretorio em file_dir_entry
	entry_v.dir = 'd';
	strcpy(entry_v.name, name);
	entry_v.size_bytes = 0;
	entry_v.sector_start = root_t.free_sectors_list;

	// Obtem proximo bloco livre e atualiza root_table
	fseek(fp, root_t.free_sectors_list * SECTOR_SIZE, SEEK_SET);
	fread(&sector_v, SECTOR_SIZE, 1, fp);
	root_t.free_sectors_list = sector_v.next_sector;

	entry_list_ptr[pos_n] = entry_v;
	writeRootTable(fp, &root_t);

	// Atualiza tabela atual de diretorios
	// Se diretorio criado na raiz, ja foi salvo junto com a root_table
	if (depth > 0)
	{
		fseek(fp, update_dir_t_sector * SECTOR_SIZE, SEEK_SET);
		fwrite(&dir_t, SECTOR_SIZE, 1, fp);
	}

	unsigned int write_block = entry_v.sector_start;

	// Inicializa valores de entrada para o novo diretorio
	entry_v.dir = '0';
	strcpy(entry_v.name, "");
	entry_v.size_bytes = 0;
	entry_v.sector_start = 0;

	for (int i = 0; i < MAX_ENTRIES_DIRECTORY; i++)
	{
		dir_t.entries[i] = entry_v;
	}

	memcpy(&sector_v.data, &dir_t, SECTOR_DATA_SIZE);
	sector_v.next_sector = 0;
	fseek(fp, write_block * SECTOR_SIZE, SEEK_SET);
	fwrite(&sector_v, SECTOR_SIZE, 1, fp);
	fclose(fp);
}

/**
Remove um diretorio vazio do sistema de arquivos
Requer: um node de diretorio valido
Garante: define entrada_dir->sector_start como zero e a funcao retorna 0
**/
int fs_rmdir(unsigned char *name)
{
	FILE *fp;

	fp = fopen(FILENAME, "r+");

	if (fp == NULL)
	{
		printf("Falha ao carregar sistema de arquivos.\n");
		exit(1);
	}

	struct root_table_directory root_t = loadRootTable(fp);

	// Remove PATH do nome do arquivo
	char *token;
	char *delimiter = DIRECTORY_DELIMITER;
	unsigned int depth = 0, update_entry_t_sector = 0;
	struct file_dir_entry entry_v, *entry_list_ptr;
	struct table_directory dir_t;
	struct sector_data sector_v;

	// Percorre arvore de diretorios e define PATH e nome de arquivo
	token = strtok(name, DIRECTORY_DELIMITER);
	entry_list_ptr = &root_t.entries[0];
	strcpy(name, token);

	while ((token = strtok(NULL, DIRECTORY_DELIMITER)) != NULL)
	{
		if (depth == 0)
			entry_v = searchEntry(name, entry_list_ptr, MAX_ENTRIES_ROOT);
		else
			entry_v = searchEntry(name, entry_list_ptr, MAX_ENTRIES_DIRECTORY);

		if (entry_v.dir == '0')
		{
			printf("Diretorio \"%s\" nao existe. Caminho invalido!\n", name);
			return 1;
		}

		// Avança descritor para setor com table_directory
		update_entry_t_sector = entry_v.sector_start;
		fseek(fp, entry_v.sector_start * SECTOR_SIZE, SEEK_SET);
		fread(&dir_t, SECTOR_DATA_SIZE, 1, fp);
		entry_list_ptr = &dir_t.entries[0];
		strcpy(name, token);
		depth++;
	}

	unsigned int pos_n = 0, limite = 0, dir_sector = 0;
	entry_v = entry_list_ptr[0];

	limite = (depth > 0) ? MAX_ENTRIES_DIRECTORY : MAX_ENTRIES_ROOT;

	while (strcmp(entry_v.name, name) != 0 && pos_n <= limite)
		entry_v = entry_list_ptr[++pos_n];

	if (pos_n >= limite)
	{
		printf("Diretorio \"%s\" nao encontrado!\n", name);
		fclose(fp);
		return 1;
	}
	else if (entry_v.dir == 'f')
	{
		printf("\"%s\" e um arquivo!\n", name);
		fclose(fp);
		return 1;
	}

	dir_sector = entry_list_ptr[pos_n].sector_start;
	fseek(fp, dir_sector * SECTOR_SIZE, SEEK_SET);
	fread(&dir_t, SECTOR_SIZE, 1, fp);
	entry_list_ptr = &dir_t.entries[0];

	// Verifica se o diretorio esta vazio
	for (int i = 0; i < MAX_ENTRIES_DIRECTORY; i++)
	{
		entry_v = entry_list_ptr[i];
		if (entry_v.sector_start != 0)
		{
			printf("Diretorio %s nao esta vazio!\n", name);
			fclose(fp);
			return 1;
		}
	}

	fseek(fp, update_entry_t_sector * SECTOR_SIZE, SEEK_SET);
	fread(&dir_t, SECTOR_SIZE, 1, fp);
	if (depth == 0)
		entry_list_ptr = &root_t.entries[0];
	else
		entry_list_ptr = &dir_t.entries[0];

	// Exclui entrada da entries - desaloca setor inicial
	entry_v.sector_start = 0;
	entry_list_ptr[pos_n] = entry_v;

	// Entrada nao armazenada em root_table
	if (depth > 0)
	{
		fseek(fp, update_entry_t_sector * SECTOR_SIZE, SEEK_SET);
		fwrite(&dir_t, SECTOR_SIZE, 1, fp);
	}

	// Encontra ultimo setor livre
	unsigned int last_sector = 0;

	if (root_t.free_sectors_list == 0)
	{
		// Todos os blocos ocupados, arquivo excluido passa a
		// ser a lista de blocos livres
		root_t.free_sectors_list = dir_sector;
	}
	else
	{
		// Adiciona primeiro setor do arquivo no final da lista
		// de setores livres
		fseek(fp, root_t.free_sectors_list * SECTOR_SIZE, SEEK_SET);
		fread(&sector_v, SECTOR_SIZE, 1, fp);

		last_sector = root_t.free_sectors_list;

		while (sector_v.next_sector != 0)
		{
			fseek(fp, sector_v.next_sector * SECTOR_SIZE, SEEK_SET);
			last_sector = sector_v.next_sector;
			fread(&sector_v, SECTOR_SIZE, 1, fp);
		}

		sector_v.next_sector = dir_sector;
		fseek(fp, last_sector * SECTOR_SIZE, SEEK_SET);
		fwrite(&sector_v, SECTOR_SIZE, 1, fp);
	}

	writeRootTable(fp, &root_t);
	fclose(fp);
	return 0;
}

/**
Exibe estatisticas do disco
Requer: TRUE
Garante: exibe estatisticas do disco
**/
void fs_status(unsigned char debug)
{
	FILE *fp;

	fp = fopen(FILENAME, "r");

	if (fp == NULL)
	{
		printf("Falha ao carregar sistema de arquivos.\n");
		exit(1);
	}

	struct root_table_directory root_t = loadRootTable(fp);

	unsigned int file_size = getFileSize(fp);
	unsigned int num_sectors = file_size / SECTOR_SIZE;
	unsigned int tot_blocos_livres = getTotalFreeBlocks(fp);
	unsigned int tot_bytes_usados = getTotalUsedBytes(fp, &root_t.entries[0]);
	struct sector_data sector_v;

	if (debug == 0)
	{
		printf("Listando blocos ...\n");
		fseek(fp, 0, SEEK_SET);
		for (int i = 0; i < num_sectors; i++)
		{
			fread(&sector_v, SECTOR_SIZE, 1, fp);
			printf("[%.6d -> %.6d] ", i, sector_v.next_sector);
			if ((i + 1) % 5 == 0)
				printf("\n");
		}
	}

	printf("=================================================================\n");
	printf("ESTATISTICAS DO DISCO:\n\n");
	printf("\tCapacidade: %d bytes [%d blocos]\n",
		   file_size, num_sectors);
	printf("\tUtilizado: %d bytes [%.2f %]\tLivre: %d bytes [%.2f %]\n",
		   tot_bytes_usados,
		   ((float)tot_bytes_usados / (num_sectors * SECTOR_DATA_SIZE)) * 100,
		   tot_blocos_livres * SECTOR_DATA_SIZE,
		   ((float)tot_blocos_livres / num_sectors) * 100);
	printf("\tBlocos livres: %d [1st: %d]\n",
		   tot_blocos_livres, root_t.free_sectors_list);
	printf("=================================================================\n");
	fclose(fp);
}

/** Funcoes auxiliares **/

/**
Retorna o tamanho em bytes de um arquivo
Requer: descritor de arquivo valido
Garante: retorna o tamanho em bytes de um arquivo
**/
int getFileSize(FILE *fp)
{
	int curr_pos = ftell(fp);
	int size = 0;
	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	fseek(fp, curr_pos, SEEK_SET);
	return size;
}

/**
Le root_table
Requer: descritor de arquivo valido
Garante: retorna struct root_table_directory
**/
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

/** 
Obtem o total de blocos livres do disco
Requer: descritor de arquivo valido
Garante: retorna total de blocos livres do disco
**/
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

/**
Obtem o total de bytes usados por arquivos e diretorios
Requer: descritor de arquivos e directory_table
Garante: retorna total de bytes usados do disco
**/
unsigned int getTotalUsedBytes(FILE *fp, struct file_dir_entry *dir_t)
{
	unsigned int curr_pos = ftell(fp);
	unsigned int tot_bytes_usados = 0;
	struct file_dir_entry *dir_e;
	struct table_directory dir_t_aux;
	struct sector_data sector_v;

	int pos_n = 0;
	dir_e = dir_t;

	while ((dir_e->dir == 'f' && dir_e->size_bytes != 0) || (dir_e->dir == 'd'))
	{
		if (dir_e->dir == 'd' && dir_e->sector_start != 0)
		{
			// Chama recursivo e trata diretorio
			fseek(fp, dir_e->sector_start * SECTOR_SIZE, SEEK_SET);
			fread(&sector_v.data, SECTOR_DATA_SIZE, 1, fp);
			memcpy(&dir_t_aux, &sector_v, SECTOR_DATA_SIZE);

			tot_bytes_usados += SECTOR_SIZE + getTotalUsedBytes(fp, &dir_t_aux.entries[0]);
		}
		else if (dir_e->dir == 'f' && dir_e->sector_start != 0)
		{
			tot_bytes_usados += dir_e->size_bytes;
		}
		dir_e = &dir_t[++pos_n];
	}
	fseek(fp, curr_pos, SEEK_SET);
	return tot_bytes_usados;
}

/**
Escreve dados da root_table em disco
Requer: descritor de arquivo valido e ponteiro para struct root_table_directory
Garante: escreve dados da root_table em disco
**/
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

/**
Obtem a posicao do primeiro registro livre
Requer: uma lista de entrada file_dir_entry e seu comprimento
Garante: retorna indice da primeira posicao livre da lista
**/
unsigned int getFirstFreePos(struct file_dir_entry *entries, unsigned int list_l)
{
	struct file_dir_entry entry_v;
	unsigned int pos_n = 0;

	entry_v = entries[0];

	while (entry_v.sector_start != 0 && pos_n <= list_l)
		entry_v = entries[++pos_n];

	return pos_n;
}

/**
Realiza busca por nome em uma lista de entidades
Requer: termo de busca, lista de entidades e comprimento da mesma
Garante: retorna file_dir_entry do registro encontrado
**/
struct file_dir_entry searchEntry(unsigned char *palavra,
								  struct file_dir_entry *entries,
								  unsigned int list_l)
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
		entry_v.dir = '0';
		return entry_v;
	}

	return entry_v;
}