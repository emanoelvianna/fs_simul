#define SECTOR_SIZE 512
#define NUMBER_OF_SECTORS 2048
#define FILENAME "simul.fs"
#define DIRECTORY_DELIMITER "/"

#define SECTOR_DATA_SIZE 508
#define MAX_ENTRIES_ROOT 126
#define MAX_ENTRIES_DIRECTORY 15

/* Filesystem structures. */

/**
 * File or directory entry. 
 */
struct file_dir_entry
{
	unsigned int dir;		   /**< File or directory representation. Use 1 for directory 0 for file. */
	char name[20];			   /**< File or directorty name. */
	unsigned int size_bytes;   /**< Size of the file in bytes. Use 0 for directories. */
	unsigned int sector_start; /**< Initial sector of the file ou sector of the directory table. */
};

/**
 * Root directory table.
 * First directory table of the file system. Should be written to the sector 0.
 */
struct root_table_directory
{
	unsigned int free_sectors_list;					 /**< First free sector. */
	struct file_dir_entry entries[MAX_ENTRIES_ROOT]; /**< List of file or directories. */
	unsigned char not_used[28];						 /**< Reserved, not used. */
};

/**
 * Subdirectories file table.
 */
struct table_directory
{
	struct file_dir_entry entries[MAX_ENTRIES_DIRECTORY]; /**< List of file or directories. */
};

/**
 * Sector data.
 */
struct sector_data
{
	unsigned char data[SECTOR_DATA_SIZE]; /**< File data. */
	unsigned int next_sector;			  /**< Next sector. Use 0 if it is the last sector. */
};

int fs_format();
int fs_create(char *input_file, char *simul_file);
int fs_read(char *output_file, char *simul_file);
int fs_del(char *simul_file);
int fs_ls(char *dir_path);
int fs_mkdir(char *directory_path);
int fs_rmdir(char *directory_path);
int fs_free_map(char *log_f);
struct file_dir_entry searchEntry(char *palavra, struct file_dir_entry *entries, unsigned int list_l);
int getFileSize(FILE *fp);
unsigned int getFirstFreePos(struct file_dir_entry *list_entry, unsigned int list_l);
struct root_table_directory loadRootTable(FILE *fp);
unsigned int getTotalFreeBlocks(FILE *fp);
void writeRootTable(FILE *fp, struct root_table_directory *root_table);
void cmd_ls_print(FILE *fp, struct file_dir_entry *entry_t, unsigned int entry_l, unsigned char *str, unsigned char print_recursivo);