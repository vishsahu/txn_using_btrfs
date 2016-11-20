#ifndef LIBBTRFSTRANS_H_
#define LIBBTRFSTRANS_H_

#include <sys/stat.h>

#ifndef BUILD_ASSERT
#define BUILD_ASSERT(x)
#endif

#define SUCCESS 0
enum libbtrfstrans_error
{
    E_UNSPECIFIED = 1000,
    E_ACCESS,
    E_NOTASUBVOLUME,
    E_EXISTSANDNOTADIR,
    E_INCORRECTSNAPNAME,
    E_SNAPNAMETOOLONG,
    E_INCORRECTSVNAME,
    E_SVNAMETOOLONG,
    E_RENAME,
    E_DELETE,
    E_WRONGSTATE,
    E_CORRUPT,
    E_INVALIDNAME
};

int init_libbtrfstrans(const char* path);

int start_transaction();
int commit_transaction();
int abort_transaction();

int start_ro_transaction();
int stop_ro_transaction();

int test_issubvolume(const char *path);
int test_isdir(const char *path);

int create_snapshot(const char* subvol, char* dst, int readonly, int async); // from btrfs progs: cmds-subvolume.c
int make_subvolume(const char *path);
int create_subvolume(const char* dst);
int delete_subvolume(const char* path);

FILE* btrfstrans_fopen(const char *__restrict __filename, const char *__restrict __modes);
int btrfstrans_fclose(FILE* stream);
int btrfstrans_mkdir(const char* path, __mode_t mode);
int btrfstrans_rmdir(const char* path);
int btrfstrans_unlink(const char* path);
int btrfstrans_stat(const char* __restrict file, struct stat* __restrict buf);

#endif /* LIBBTRFSTRANS_H_ */
