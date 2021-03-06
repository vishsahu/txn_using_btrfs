#include <unistd.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/ioctl.h>
#include <btrfs/ioctl.h>
#include <libgen.h>
#include <fcntl.h>
#include <errno.h>
#include <semaphore.h>
#include <dirent.h>
#include <uuid/uuid.h>
#include <blkid/blkid.h>
#include <sys/stat.h>
#include <signal.h>

#include "../btrfs-progs/utils.h"
#include "../btrfs-progs/btrfs-list.h"

#include "libbtrfstrans.h"

#define BTRFSTRANS_LOCK_SEM_NAME "libbtrfstranssemaphorelock"
#define BTRFSTRANS_READONLY_SEM_NAME "libbtrfstranssemaphoreread"
#define BTRFSTRANS_RENAME_SEM_NAME "libbtrfstranssemaphorerename"

#define BTRFSTRANS_WRITABLE 0
#define BTRFSTRANS_READONLY 1
#define BTRFSTRANS_SYNCHR 0
#define BTRFSTRANS_ASYNCHR 1

#define BTRFSTRANS_HEAD_SV_NAME "/head/"
#define BTRFSTRANS_HEAD_OLD_SV_NAME "/head_old/"
#define BTRFSTRANS_WRITABLE_SV_NAME "/wr_snap/"
#define BTRFSTRANS_READONLY_SV_NAME "/ro_snaps/"

#define LIBBTRFSTRANS_RO_SNAP_NAME_PREFIX "ro_snap_"
#define BTRFSTRANS_MAX_NUM_RO_TRANS 1
#define MAX_PATH_LEN 256

enum libbtrfstrans_state_enum {
    STATE_UNINITIALIZED = 1,
    STATE_INITIALIZED,
    STATE_READ,
    STATE_WRITE,
    STATE_ERROR
};

static int acquire_write_lock();
static int release_write_lock();
static int wait_ro_sem();
static int release_ro_sem();
static int wait_rename_sem();
static int release_rename_sem();

static int exists(const char* path);
static int exists_one_of(const char* path1, const char* path2, const char* path3);
static int exist_both_of(const char* path1, const char* path2);
static int create_path_vars(const char* path);
static int create_initial_subvolumes();
static void signal_callback_handler(int signum);


static sem_t* sem_lock;
static sem_t* sem_ro;
static sem_t* sem_rename;

static char head_subvolume_path[MAX_PATH_LEN+1];
static char head_old_subvolume_path[MAX_PATH_LEN+1];
static char writable_subvolume_path[MAX_PATH_LEN+1];
static char readonly_subvolumes_path[MAX_PATH_LEN+1];
static char specific_readonly_sv_path[MAX_PATH_LEN+1];

static int state = STATE_UNINITIALIZED;


// --------------------------------------------------------


// code from cmd_subvol_get_default() from btrfs progs cmds-subvolume.c
int init_libbtrfstrans(const char* path) {
    if ( state != STATE_UNINITIALIZED ) {
        fprintf(stderr, "ERROR: libbtrfstrans was already initialized\n");
        state = STATE_ERROR;
        return E_WRONGSTATE;
    }

    printf("Initializing library for %s...\n", path);
    printf("Registering signal handlers...\n");
    //signal(SIGABRT, signal_callback_handler);
    //signal(SIGFPE, signal_callback_handler);
    //signal(SIGILL, signal_callback_handler);
    //signal(SIGINT, signal_callback_handler);
    //signal(SIGSEGV, signal_callback_handler);
    //signal(SIGTERM, signal_callback_handler);
    printf("%d %d %d %d %d %d\n", SIGABRT, SIGFPE, SIGILL, SIGINT, SIGSEGV,
    SIGTERM);
    printf("Signal handlers registered...\n");

    create_path_vars(path);

    if (!exists_one_of(head_subvolume_path, head_old_subvolume_path,\
        readonly_subvolumes_path)) { //subvolume is empty
        int ret = create_initial_subvolumes();
        if (ret){
            state = STATE_ERROR;
            return ret;
        }
        printf("Any one of subvolume is existing, state is initialized\
        now.\n");
        state = STATE_INITIALIZED;
        return SUCCESS;
    }

    if (exist_both_of(readonly_subvolumes_path, head_subvolume_path) &&
        !exists(head_old_subvolume_path)) {
        printf("Both head and ro_subvol exist, state is initialized\
        now.\n");
        state = STATE_INITIALIZED;
        return SUCCESS;
    }

    if (exist_both_of(readonly_subvolumes_path, head_old_subvolume_path) &&
        !exists(head_subvolume_path)) {
        if (rename(head_old_subvolume_path, head_subvolume_path)) {
            fprintf(stderr, "ERROR: renaming %s to %s\n",
            head_old_subvolume_path, head_subvolume_path);
            state = STATE_ERROR;
            return E_RENAME;
        }
        printf("Both head_old and ro_subvol exist, and head doesn't, renaming.\
        state is initialized now.\n");
        state = STATE_INITIALIZED;
        return SUCCESS;
    }

    state = STATE_ERROR;
    return E_CORRUPT;
}

static int create_path_vars(const char* path){
    strcpy(head_subvolume_path, path);
    strcat(head_subvolume_path, BTRFSTRANS_HEAD_SV_NAME);

    strcpy(head_old_subvolume_path, path);
    strcat(head_old_subvolume_path, BTRFSTRANS_HEAD_OLD_SV_NAME);

    strcpy(writable_subvolume_path, path);
    strcat(writable_subvolume_path, BTRFSTRANS_WRITABLE_SV_NAME);

    strcpy(readonly_subvolumes_path, path);
    strcat(readonly_subvolumes_path, BTRFSTRANS_READONLY_SV_NAME);

    return SUCCESS;
}

static int create_initial_subvolumes(){
    int ret = create_subvolume(head_subvolume_path);
    if (ret) {
        fprintf(stderr, "ERROR in %s: can't create subvolume '%s'\n", __func__, head_subvolume_path);
        return E_UNSPECIFIED;
    }

    ret = create_subvolume(readonly_subvolumes_path);
    if (ret) {
        fprintf(stderr, "ERROR in %s: can't create subvolume '%s'\n", __func__, readonly_subvolumes_path);
        return E_UNSPECIFIED;
    }

    return SUCCESS;
}



int start_transaction() {
    //printf("libbtrfstrans: Starting transaction\n");

    if ( state != STATE_INITIALIZED) {
        fprintf(stderr, "ERROR: libbtrfstrans was not configured or is in the wrong state (state=%d)\n", state);
        return E_WRONGSTATE;
    }

    acquire_write_lock();

    create_snapshot(head_subvolume_path, writable_subvolume_path, BTRFSTRANS_WRITABLE, BTRFSTRANS_ASYNCHR);

    state = STATE_WRITE;

    //printf("libbtrfstrans: Finished starting transaction\n");
    return SUCCESS;
}

int commit_transaction() {
    int ret;
    //printf("libbtrfstrans: Committing transaction\n");

    if ( state != STATE_WRITE) {
        fprintf(stderr, "ERROR: transaction was not started or libbtrfstrans is in the wrong state(state=%d)\n", state);
        return E_WRONGSTATE;
    }

    wait_rename_sem();

    //puts("libbtrfstrans: Going to rename 'head' to 'head_old'. Ok?");
    //getchar();

    // rename stale subvolume
    if (rename(head_subvolume_path, head_old_subvolume_path)) {
        fprintf(stderr, "ERROR: renaming %s to %s\n", head_subvolume_path, head_old_subvolume_path);
        state = STATE_ERROR;
        return E_RENAME;
    }

    sync();
    //sleep(4); // for demonstration purposes only

    //puts("libbtrfstra/dir1_svolns: Going to rename 'wr_snap' to 'head'. Ok?");
    //getchar();

    if (rename(writable_subvolume_path, head_subvolume_path)) {
        fprintf(stderr, "ERROR: renaming %s to %s\n", writable_subvolume_path, head_subvolume_path);
        state = STATE_ERROR;
        return E_RENAME;
    }

    release_rename_sem();

    //sleep(4); // for demonstration purposes only


    //puts("libbtrfstrans: Going to delete 'head_old'. Ok?");
    //getchar();

    ret = delete_subvolume(head_old_subvolume_path);
    if (ret) {
        fprintf(stderr, "ERROR: couldn't delete subvolume %s to commit the transaction\n", head_old_subvolume_path);
        state = STATE_ERROR;
        return ret;
    }

    release_write_lock();

    state = STATE_INITIALIZED;

    printf("libbtrfstrans: Finished committing transaction\n");

    return SUCCESS;
}

int abort_transaction() {

    printf("libbtrfstrans: Aborting transaction\n");
    if ( state != STATE_WRITE) {
        fprintf(stderr, "ERROR: transaction was not started or libbtrfstrans is in the wrong state\n");
        return E_WRONGSTATE;
    }

    int ret = delete_subvolume(writable_subvolume_path);
    if (ret) {
        fprintf(stderr, "ERROR: couldn't delete subvolume %s to abort the transaction\n", head_old_subvolume_path);
        state = STATE_ERROR;
        return ret;
    }

    release_write_lock();

    state = STATE_INITIALIZED;
    return SUCCESS;
}

int start_ro_transaction() {
    int done = 0;
    char nr_buf[10];

    printf("libbtrfstrans: Starting read-only transaction\n");

    if (state != STATE_INITIALIZED) {
        fprintf(stderr, "ERROR: libbtrfstrans was not configured or is in the wrong state\n");
        return E_WRONGSTATE;
    }

    //wait_ro_sem();
    printf("Sema acquired\n");

    for (int i=0; i<BTRFSTRANS_MAX_NUM_RO_TRANS; i++) {
        strcpy(specific_readonly_sv_path, readonly_subvolumes_path);
        strcat(specific_readonly_sv_path, "/");
        strcat(specific_readonly_sv_path, LIBBTRFSTRANS_RO_SNAP_NAME_PREFIX);
        sprintf(nr_buf, "%d", i);
        strcat(specific_readonly_sv_path, nr_buf);
        strcat(specific_readonly_sv_path, "/");

        printf("Creating snpshot of %s at %s\n", head_subvolume_path,
            specific_readonly_sv_path);
        if (!exists(specific_readonly_sv_path)){
            //wait_rename_sem();
            create_snapshot(head_subvolume_path, specific_readonly_sv_path,\
            BTRFSTRANS_READONLY, BTRFSTRANS_ASYNCHR);
            //release_rename_sem();
            done = 1;
            break;
        }
    }
    if (!done) {
        fprintf(stderr, "ERROR: couldn't find empty slot for read-only subvolume\n");
        state = STATE_ERROR;
        return E_UNSPECIFIED;
    }

    printf("libbtrfstrans: Finished starting read-only transaction\n");

    state = STATE_READ;
    printf("State is %d.\n", state);
    return SUCCESS;
}


int stop_ro_transaction() {
    //printf("libbtrfstrans: Stopping read-only transaction\n");
    int ret;

    if ( state != STATE_READ) {
        fprintf(stderr, "ERROR: read-only transaction was not started or\
            libbtrfstrans is in the wrong state\n");
        return E_WRONGSTATE;
    }

    //puts("libbtrfstrans: Going to delete 'ro_snap_X'. Ok?");
    //getchar();

    ret = delete_subvolume(specific_readonly_sv_path);
    if (ret) {
        fprintf(stderr, "ERROR: couldn't delete subvolume %s to commit the\
            transaction\n", head_old_subvolume_path);
        state = STATE_ERROR;
        return ret;
    }


    //release_ro_sem();

    state = STATE_INITIALIZED;
    return SUCCESS;
}

static int acquire_write_lock() {
    //printf("libbtrfstrans: Acquiring write lock\n");
    sem_lock = sem_open(BTRFSTRANS_LOCK_SEM_NAME, O_CREAT, 0644, 1); /* open or create semaphore */
    if (sem_lock == SEM_FAILED) {
        fprintf(stderr, "ERROR in %s (sem_open()) = %d\n", __func__, errno);
        state = STATE_ERROR;
        return errno;
    }

    int ret = sem_wait(sem_lock);
    if (ret != 0) {
        fprintf(stderr, "ERROR in %s, (sem_wait()) = %d\n", __func__, ret);
        state = STATE_ERROR;
        return ret;
    }
    return SUCCESS;
}

static int release_write_lock(){
    //printf("libbtrfstrans: Releasing write lock\n");

    int ret = sem_post(sem_lock);
    if (ret != 0) {
        fprintf(stderr, "ERROR in %s (sem_post())= %d\n", __func__, ret);
        state = STATE_ERROR;
        return ret;
    }

    ret = sem_close(sem_lock);
    if (ret != 0) {
        fprintf(stderr, "ERROR in %s (sem_close())= %d\n", __func__, ret);
        state = STATE_ERROR;
        return ret;
    }

    // we don't delete the semaphore here: other processes may want continue to use it.

    return SUCCESS;
}

static int wait_ro_sem() {
    printf("Sema opening, name: %s...\n", BTRFSTRANS_READONLY_SEM_NAME);

    sem_t* (*sem_fnp)(const char *, int, mode_t, unsigned int);
    sem_fnp = &sem_open;
    if (!sem_fnp)
        printf("Function pointer to sem_open() is %x\n.", sem_fnp);
    else
        printf("Function pointer to sem_open() is NULL.\n");

    //sem_ro = sem_open(BTRFSTRANS_READONLY_SEM_NAME, O_CREAT, 0644, BTRFSTRANS_MAX_NUM_RO_TRANS); /* open or create semaphore */
    if (sem_ro == SEM_FAILED) {
        printf("Sema failed!\n");
        fprintf(stderr, "ERROR in %s (sem_open()) = %d\n", __func__, errno);
        state = STATE_ERROR;
        return errno;
    }
    printf("Sema opened!\n");

    int ret = sem_wait(sem_ro);
    if (ret != 0) {
        fprintf(stderr, "ERROR in %s, (sem_wait()) = %d\n", __func__, ret);
        state = STATE_ERROR;
        return ret;
    }

    return SUCCESS;
}



static int release_ro_sem() {
    int ret = sem_post(sem_ro);
    if (ret != 0) {
        fprintf(stderr, "ERROR in %s (sem_post())= %d\n", __func__, ret);
        state = STATE_ERROR;
        return ret;
    }

    ret = sem_close(sem_ro);
    if (ret != 0) {
        fprintf(stderr, "ERROR in %s (sem_close())= %d\n", __func__, ret);
        state = STATE_ERROR;
        return ret;
    }

    // we don't delete the semaphore here: other processes may want continue to use it.


    return SUCCESS;
}

static int wait_rename_sem() {
    sem_rename = sem_open(BTRFSTRANS_RENAME_SEM_NAME, O_CREAT, 0644, 1); /* open or create
                                                                            aphore */
    if (sem_rename == SEM_FAILED) {
        fprintf(stderr, "ERROR in %s (sem_open()) = %d\n", __func__, errno);
        state = STATE_ERROR;
        return errno;
    }

    int ret = sem_wait(sem_rename);
    if (ret != 0) {
        fprintf(stderr, "ERROR in %s, (sem_wait()) = %d\n", __func__, ret);
        state = STATE_ERROR;
        return ret;
    }

    return SUCCESS;
}



static int release_rename_sem() {
    int ret = sem_post(sem_rename);
    if (ret != 0) {
        fprintf(stderr, "ERROR in %s (sem_post())= %d\n", __func__, ret);
        state = STATE_ERROR;
        return ret;
    }

    ret = sem_close(sem_rename);
    if (ret != 0) {
        fprintf(stderr, "ERROR in %s (sem_close())= %d\n", __func__, ret);
        state = STATE_ERROR;
        return ret;
    }

    // we don't delete the semaphore here: other processes may want continue to use it.

    return SUCCESS;
}

/*
  these functions redefined here from btrfs-progs. There is some linker error
  and hence this ugly way out is done */

/*
 * test if path is a subvolume:
 * this function return
 * 0-> path exists but it is not a subvolume
 * 1-> path exists and it is a subvolume
 * -1 -> path is unaccessible
 */
int test_issubvolume(const char *path) {// from btrfs progs: cmds-subvolume.c
    struct stat st;
    int res;

    res = stat(path, &st);
    if(res < 0 )
        return -1;

    return (st.st_ino == 256) && S_ISDIR(st.st_mode);
}


/*
 * test if path is a directory
 * this function return
 * 0-> path exists but it is not a directory
 * 1-> path exists and it is a directory
 * -1 -> path is unaccessible
 */
int test_isdir(const char *path) {// from btrfs progs: cmds-subvolume.c
    struct stat st;
    int res;

    res = stat(path, &st);
    if(res < 0 )
        return -1;

    return S_ISDIR(st.st_mode);
}

int open_file_or_dir3(const char *fname, DIR **dirstream, int open_flags)
{
	int ret;
	struct stat st;
	int fd;

	ret = stat(fname, &st);
	if (ret < 0) {
		return -1;
	}
	if (S_ISDIR(st.st_mode)) {
		*dirstream = opendir(fname);
		if (!*dirstream)
			return -1;
		fd = dirfd(*dirstream);
	} else if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) {
		fd = open(fname, open_flags);
	} else {
		/*
		 * we set this on purpose, in case the caller output
		 * strerror(errno) as success
		 */
		errno = EINVAL;
		return -1;
	}
	if (fd < 0) {
		fd = -1;
		if (*dirstream) {
			closedir(*dirstream);
			*dirstream = NULL;
		}
	}
	return fd;
}


int open_file_or_dir(const char *fname, DIR **dirstream)
{
	return open_file_or_dir3(fname, dirstream, O_RDWR);
}

#define strncpy_null(dest, src) __strncpy_null(dest, src, sizeof(dest))
char *__strncpy_null(char *dest, const char *src, size_t n)
{
	strncpy(dest, src, n);
	if (n > 0)
		dest[n - 1] = '\0';
	return dest;
}


int create_snapshot(const char* subvol, char* dst, int readonly, int async) // from btrfs progs: cmds-subvolume.c
{
    int res, retval;
    int fd = -1, fddst = -1;
    int len;
    char *newname;
    char *dstdir;
    struct btrfs_ioctl_vol_args_v2 args;

    memset(&args, 0, sizeof(args));

    //printf("libbtrfstrans: create_snapshot(): subvol = %s, dst = %s, readonly = %d\n", subvol, dst, readonly);

    retval = E_UNSPECIFIED; /* failure */
    res = test_issubvolume(subvol);
    if (res < 0) {
        fprintf(stderr, "ERROR: error accessing '%s'\n", subvol);
        retval = E_ACCESS;
        goto out;
    }
    if (!res) {
        fprintf(stderr, "ERROR: '%s' is not a subvolume\n", subvol);
        retval = E_NOTASUBVOLUME;
        goto out;
    }

    res = test_isdir(dst);
    if (res == 0) {
        fprintf(stderr, "ERROR: '%s' exists and it is not a directory\n", dst);
        retval = E_EXISTSANDNOTADIR;
        goto out;
    }
    if (res > 0) { //path exists and is directory
        newname = strdup(subvol);
        newname = basename(newname);
        dstdir = dst;
    } else { //path is unaccessible
        newname = strdup(dst);
        newname = basename(newname);
        dstdir = strdup(dst);
        dstdir = dirname(dstdir);
    }

    if (!strcmp(newname, ".") || !strcmp(newname, "..") || strchr(newname, '/')) {
        fprintf(stderr, "ERROR: incorrect snapshot name ('%s')\n", newname);
        retval = E_INCORRECTSNAPNAME;
        goto out;
    }

    len = strlen(newname);
    if (len == 0 || len >= BTRFS_VOL_NAME_MAX) {
        fprintf(stderr, "ERROR: snapshot name too long ('%s)\n", newname);
        retval = E_SNAPNAMETOOLONG;
        goto out;
    }

    DIR *dirstream;
    fddst = open_file_or_dir(dstdir, &dirstream);
    if (fddst < 0) {
        fprintf(stderr, "ERROR: can't access to '%s'\n", dstdir);
        retval = E_ACCESS;
        goto out;
    }

    fd = open_file_or_dir(subvol, &dirstream);
    if (fd < 0) {
        fprintf(stderr, "ERROR: can't access to '%s'\n", dstdir);
        retval = E_ACCESS;
        goto out;
    }

    if (readonly) {
        args.flags |= BTRFS_SUBVOL_RDONLY;
        //printf("libbtrfstrans: Create a readonly snapshot of '%s' in '%s/%s'\n", subvol, dstdir, newname);
    } else {
        //printf("libbtrfstrans: Create a snapshot of '%s' in '%s/%s'\n", subvol, dstdir, newname);
    }

    if (async != 0) {
        args.flags |= BTRFS_SUBVOL_CREATE_ASYNC;
    }


    args.fd = fd;

    strncpy_null(args.name, newname);
    printf("Creating the snapshot\n");

    res = ioctl(fddst, BTRFS_IOC_SNAP_CREATE_V2, &args);

    if (res < 0) {
        fprintf( stderr, "ERROR: cannot snapshot '%s' - %s\n", subvol, strerror(errno));
        goto out;
    }

    retval = SUCCESS;

out:
    if (fd != -1) {
        close(fd);
    }
    if (fddst != -1) {
        close(fddst);
    }

    return retval;
}


int create_subvolume(const char* dst) // from btrfs progs: cmds-subvolume.c
{
    int retval, res, len;
    int fddst = -1;
    char *newname;
    char *dstdir;
    struct btrfs_qgroup_inherit *inherit = NULL;

    retval = E_UNSPECIFIED; /* failure */
    res = test_isdir(dst);
    if (res >= 0) {
        fprintf(stderr, "ERROR: '%s' exists\n", dst);
        goto out;
    }

    newname = strdup(dst);
    newname = basename(newname);
    dstdir = strdup(dst);
    dstdir = dirname(dstdir);

    if (!strcmp(newname, ".") || !strcmp(newname, "..") || strchr(newname, '/') ) {
        fprintf(stderr, "ERROR: incorrect subvolume name ('%s')\n", newname);
        retval = E_INCORRECTSVNAME;
        goto out;
    }

    len = strlen(newname);
    if (len == 0 || len >= BTRFS_VOL_NAME_MAX) {
        fprintf(stderr, "ERROR: subvolume name too long ('%s)\n", newname);
        retval = E_SVNAMETOOLONG;
        goto out;
    }

    DIR *dirstream;
    fddst = open_file_or_dir(dstdir, &dirstream);
    if (fddst < 0) {
        fprintf(stderr, "ERROR: can't access to '%s'\n", dstdir);
        retval = E_ACCESS;
        goto out;
    }

    //printf("libbtrfstrans: Create subvolume '%s/%s'\n", dstdir, newname);

    struct btrfs_ioctl_vol_args args;

    memset(&args, 0, sizeof(args));
    strncpy_null(args.name, newname);

    res = ioctl(fddst, BTRFS_IOC_SUBVOL_CREATE, &args);

    if (res < 0) {
        fprintf(stderr, "ERROR: cannot create subvolume - %s\n", strerror(errno));
        printf("ERROR: cannot create subvolume - %s\n", strerror(errno));
        goto out;
    }

    retval = SUCCESS;
out:
    if (fddst != -1)

        close(fddst);
    free(inherit);

    return retval;
}


int delete_subvolume(const char* path) // from btrfs progs: cmds-subvolume.c
{
    int res, fd, len, e;
    struct btrfs_ioctl_vol_args args;
    char *dname, *vname, *cpath;

    res = test_issubvolume(path);
    if(res<0){
        fprintf(stderr, "ERROR: error accessing '%s'\n", path);
        return E_ACCESS;
    }
    if(!res){
        fprintf(stderr, "ERROR: '%s' is not a subvolume\n", path);
        return E_NOTASUBVOLUME;
    }

    cpath = realpath(path, 0);
    dname = strdup(cpath);
    dname = dirname(dname);
    vname = strdup(cpath);
    vname = basename(vname);
    free(cpath);

    if( !strcmp(vname,".") || !strcmp(vname,"..") || strchr(vname, '/') ){
        fprintf(stderr, "ERROR: incorrect subvolume name ('%s')\n", vname);
        return E_INCORRECTSVNAME;
    }

    len = strlen(vname);
    if (len == 0 || len >= BTRFS_VOL_NAME_MAX) {
        fprintf(stderr, "ERROR: snapshot name too long ('%s)\n", vname);
        return E_SNAPNAMETOOLONG;
    }

    DIR *dirstream;
    fd = open_file_or_dir(dname, &dirstream);
    if (fd < 0) {
        fprintf(stderr, "ERROR: can't access to '%s'\n", dname);
        return E_ACCESS;
    }

    //printf("libbtrfstrans: Delete subvolume '%s/%s'\n", dname, vname);
    strncpy_null(args.name, vname);
    res = ioctl(fd, BTRFS_IOC_SNAP_DESTROY, &args);
    e = errno;

    close(fd);

    if(res < 0 ){
        fprintf( stderr, "ERROR: cannot delete '%s/%s' - %s\n", dname, vname, strerror(e));
        return E_DELETE;
    }

    return SUCCESS;
}


/*
 * Function to make a directory in btrfs volume a sub-volume
 */
int make_subvolume(const char* path) {
    int ret = 0;
    char svol_path[MAX_PATH_LEN];

    ret = test_issubvolume(path);
    if (ret == 1) {
        return 0; // already a subvolume, no action needed
    }
    else if (ret < 0) {
        printf("ERROR %s: path is inaccessable\n", __func__);
        return -EINVAL;
    }

    ret = snprintf(svol_path, MAX_PATH_LEN, "%s_svol", path);
    if (ret >= MAX_PATH_LEN) {
        printf("ERROR %s: Path longer than buffer, allocate larger buffer.\n",
            __func__);
        return -ENOMEM;
    }
    else if (ret < 0) {
        printf("ERROR %s: Error in writing string.\n", __func__);
        return -ENOMEM;
    }

    //printf("%s\n", svol_path);

    ret = create_subvolume(svol_path);
    if (ret) {
        printf("ERROR in %s: can't create subvolume '%s'\n", __func__,
            svol_path);
        return -EINVAL;
    }

    //
    //  We execute following commands to make existing directory a subvolume:
    //   cp -rf --reflink=always path/* svol_path/.
    //   rm -rf path/
    //   mv svol_path path
    //

    char cmd[MAX_PATH_LEN];
    ret = snprintf(cmd, MAX_PATH_LEN, "cp -rf --reflink=always %s/* %s/.", path,
        svol_path);
    if (ret >= MAX_PATH_LEN) {
        printf("ERROR %s: Path longer than buffer, allocate larger buffer.\n",
            __func__);
        return -ENOMEM;
    }
    else if (ret < 0) {
        printf("ERROR %s: Error in writing string.\n", __func__);
        return -ENOMEM;
    }
    //printf("%s\n", cmd);
    ret = system(cmd);
    if (ret < 0) {
        printf("Error in executing command\n");
        return ret;
    }

    ret = snprintf(cmd, MAX_PATH_LEN, "rm -rf %s/", path);
    if (ret >= MAX_PATH_LEN) {
        printf("ERROR %s: Path longer than buffer, allocate larger buffer.\n",
            __func__);
        return -ENOMEM;
    }
    else if (ret < 0) {
        printf("ERROR %s: Error in writing string.\n", __func__);
        return -ENOMEM;
    }
    //printf("%s\n", cmd);
    ret = system(cmd);
    if (ret < 0) {
        printf("Error in executing command\n");
        return ret;
    }

    ret = snprintf(cmd, MAX_PATH_LEN, "mv %s %s", svol_path, path);
    if (ret >= MAX_PATH_LEN) {
        printf("ERROR %s: Path longer than buffer, allocate larger buffer.\n",
            __func__);
        return -ENOMEM;
    }
    else if (ret < 0) {
        printf("ERROR %s: Error in writing string.\n", __func__);
        return -ENOMEM;
    }
    //printf("%s\n", cmd);
    ret = system(cmd);
    if (ret < 0) {
        printf("Error in executing command\n");
        return ret;
    }

    return ret;
}


static int exists(const char* path) {
    int ret;
    struct stat st;
    ret = stat(path, &st);
    if (ret < 0) {
        return 0;
    } else {
        return 1;
    }
}

static int exists_one_of(const char* path1, const char* path2, const char* path3) {
    return exists(path1) || exists(path2) || exists(path3);
}

static int exist_both_of(const char* path1, const char* path2) {
    return exists(path1) && exists(path2);
}

static int assemble_path(const char* filename, char* assembled_path) {
    if (!strcmp(filename, ".") || !strcmp(filename, "..") || !strcmp(filename, "/")) {
        fprintf(stderr, "ERROR: Invalid filename '%s'\n", filename);
        printf("invalid file name\n");

        return E_INVALIDNAME;
    }

    if (state == STATE_READ) {
        strcpy(assembled_path, specific_readonly_sv_path);
        strcat(assembled_path, filename);
        printf("path to read is %s\n", assembled_path);
        return SUCCESS;
    } else if ( state == STATE_WRITE) {
        strcpy(assembled_path, writable_subvolume_path);
        strcat(assembled_path, filename);
        printf("path to write is %s\n", assembled_path);
        return SUCCESS;
    } else {
        printf("Wrong state\n");
        return E_WRONGSTATE;
    }
}

FILE* btrfstrans_fopen(const char *__restrict filename, const char *__restrict modes) {
    char assembled_path[257];

    int ret = assemble_path(filename, assembled_path);
    if (!ret) {
        return fopen(assembled_path, modes);
    } else {
        return NULL;
    }
}


int btrfstrans_fclose(FILE* fp){

    return fclose(fp);
}

int btrfstrans_mkdir(const char* path, __mode_t mode){
    char assembled_path[257];

    int ret = assemble_path(path, assembled_path);
    if (!ret) {
        return mkdir(assembled_path, mode);
    } else {
        return ret;
    }
}

int btrfstrans_rmdir(const char* path){
    char assembled_path[257];

    int ret = assemble_path(path, assembled_path);
    if (!ret) {
        return rmdir(assembled_path);
    } else {
        return ret;
    }
}

int btrfstrans_unlink(const char* path){
    char assembled_path[257];

    int ret = assemble_path(path, assembled_path);
    if (!ret) {
        return unlink(assembled_path);
    } else {
        return ret;
    }
}

int btrfstrans_stat(const char* __restrict file, struct stat* __restrict buf) {
    char assembled_path[257];

    int ret = assemble_path(file, assembled_path);
    if (!ret) {
        return stat(file, buf);
    } else {
        return ret;
    }
}

static void signal_callback_handler(int signum) {
    printf("\nlibbtrfstrans: Caught signal: %d\n", signum);
    if (state == STATE_READ) {
        printf("stopping ro transaction...\n");
        stop_ro_transaction();
    } else if (state == STATE_WRITE) {
        printf("aborting write transaction...\n");
        abort_transaction();
    }

    exit(signum);
}
