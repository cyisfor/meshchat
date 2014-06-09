/* vim: set expandtab ts=4 sw=4: */
/*
 * main.c
 */

#include "meshchat.h"
#include "ircd.h"
#include "config.h"

#include <uv.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

static void startup(void) {
    meshchat_t *mc;

    mc = meshchat_new();
    if (!mc) {
        fprintf(stderr, "fail\n");
        exit(1);
    }

    // Start connecting stuff
    meshchat_start(mc);
}

int main()
//int main(int argc, char *argv[])
{
    config_init();

    config_chdir(startup);

    uv_run(uv_default_loop(),UV_RUN_DEFAULT);

    return 0;
}

