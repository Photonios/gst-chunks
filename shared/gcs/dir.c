#include <gcs/dir.h>

#if defined(_WIN32)
#   include <windows.h>
#else
#	include <unistd.h>
#   include <dirent.h>
#   include <sys/types.h>
#   include <sys/stat.h>
#endif

int
gcs_dir_exists(char *directory)
{
    int result = 1;

    DIR *dir = opendir(directory);
    if(!dir) {
        result = 0;
    }

    closedir(dir);
    return result;
}

void
gcs_dir_create(char *directory)
{
    mkdir(directory, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
}
