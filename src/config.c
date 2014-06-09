#define _GNU_SOURCE

#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h> // NULL
#include <assert.h>
#include <unistd.h> // read
#include <string.h> // strtok
#include <pwd.h> // getpwuid
#include <stdio.h> // fprintf, perror
#include <signal.h>




const char* home = NULL;
int dir = -1;

static void load(void) {
    int src = config_open("config",O_RDONLY,0444);
    if(src < 0)
        return;

    struct stat buf;
    assert(0==fstat(src, &buf));
    char* derp = malloc(buf.st_size);
    assert(buf.st_size == read(src,derp,buf.st_size));
    close(src);
    char* saveret = NULL;
    char* saveeql = NULL;
    char* line, *name, *value;
    for(;;) {
        line = strtok_r(derp, "\n", &saveret);
        if(line == NULL) break;
        name = strtok_r(line,"=",&saveeql);
        if(name == NULL) {
            saveeql = NULL;
            continue;
        }
        value = strtok_r(line,"=",&saveeql);
        saveeql = NULL;
        if(value == NULL) 
            continue;
        setenv(name, value, 1);
    }
}

void config_init(void) {
    home = getenv("CONFIG_PATH");
    if(home) return;
    home = getenv("HOME");
    if(home == NULL) {
        // ugh...
        struct passwd* user = getpwuid(getuid());
        if(user == NULL) {
            perror("Couldn't find home auntie Em");
            exit(3);
        }
        home = user->pw_dir;
        if(home == NULL) {
            fprintf(stderr, "%s has no home.\n",user->pw_name);
            exit(4);
        }
    }

    /* We could repeat the following code every time a peer is discovered or their port
     * changes... or we could use a single open descriptor on that directory.
     * Since UDP doesn't even accumulate descriptors, we're not gonna run out!
     */

    struct stat buf;

    assert(0 == stat(home,&buf));
    assert(buf.st_mode & S_IFDIR);

    int d = open(home,O_PATH|O_DIRECTORY);
    assert(d > 0);

    int config = openat(d,".config",O_PATH|O_DIRECTORY);
    if(config < 0) {
        mkdirat(d, ".config", 0700);
        config = openat(d,".config",O_PATH|O_DIRECTORY);
        assert(config > 0);
    }
    close(d);

    dir = openat(config,"meshchat",O_PATH|O_DIRECTORY);
    if(dir < 0) {
        mkdirat(config, "meshchat",0700);
        dir = openat(config,"meshchat",O_PATH|O_DIRECTORY);
        assert(dir > 0);
    }

    /* end directory setup stuff */

    load();
    signal(SIGUSR1,(void (*)(int))load);
}

int config_open(const char* name, int oflag, int omode) {
    return openat(dir, name, oflag, omode);
}

void config_chdir(void (*handle)()) {
    char cwd[0x400];
    assert(NULL != getcwd(cwd,0x400));
    fchdir(dir);
    handle();
    chdir(cwd);
}

void config_replace(const char* source, const char* dest) {
    // could fstatat here to make sure source exists.
    unlinkat(dir, dest, 0);
    renameat(dir, source, dir, dest);
}
