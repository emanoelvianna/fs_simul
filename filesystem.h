#define SECTOR_SIZE 512
#define NUMBER_OF_SECTORS 2048
#define FILENAME "simul.fs"

#define SECTOR_DATA_SIZE 508
#define MAX_ENTRIES_ROOT 15
#define MAX_ENTRIES_DIRECTORY 16
#define DIRECTORY_DELIMITER "/"

/* Filesystem structures. */

/**
 * File or directory entry. 
 */
struct file_dir_entry
{
    unsigned int dir;          /**< File or directory representation. Use 1 for directory 0 for file. */
    char name[20];             /**< File or directorty name. */
    unsigned int size_bytes;   /**< Size of the file in bytes. Use 0 for directories. */
    unsigned int sector_start; /**< Initial sector of the file ou sector of the directory table. */
};

/**
 * Root directory table.
 * First directory table of the file system. Should be written to the sector 0.
 */
struct root_table_directory
{
    unsigned int free_sectors_list;                  /**< First free sector. */
    struct file_dir_entry entries[MAX_ENTRIES_ROOT]; /**< List of file or directories. */
    unsigned char not_used[28];                      /**< Reserved, not used. */
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
    unsigned int next_sector;             /**< Next sector. Use 0 if it is the last sector. */
};

int fs_format();
int fs_create(unsigned char *origem, unsigned char *destino);
int fs_read(unsigned char *origem, unsigned char *destino);
unsigned int fs_del(unsigned char *file);
void fs_ls(unsigned char *nome);
void fs_ls_print(FILE *fp, struct file_dir_entry *entry_t, unsigned int entry_l, unsigned char *str, unsigned char print_recursivo);
int fs_mkdir(unsigned char *name);
int fs_rmdir(unsigned char *name);
void fs_status(unsigned char debug);

/** funções auxiliares **/
int getFileSize(FILE *fp);
struct root_table_directory loadRootTable(FILE *fp);
unsigned int getTotalFreeBlocks(FILE *fp);
unsigned int getTotalUsedBytes(FILE *fp, struct file_dir_entry *dir_e);
void writeRootTable(FILE *fp, struct root_table_directory *root_table);
unsigned int getFirstFreePos(struct file_dir_entry *entries, unsigned int list_l);
struct file_dir_entry searchEntry(unsigned char *palavra, struct file_dir_entry *entries, unsigned int list_l);
int fs_free_map(char *log_f);