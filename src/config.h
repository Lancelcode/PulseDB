#ifndef CONFIG_H
#define CONFIG_H

typedef struct {
    int   port;
    int   hz;
    char *bind;
    char *loglevel;
    char *dir;
    char *dbfilename;
} Config;

Config *config_create_default(void);
/* Apply --port/--bind/--dir/--dbfilename. Returns 0 on success, 1 if
   --help was requested, -1 on a bad argument (message on stderr). */
int     config_parse_args(Config *cfg, int argc, char **argv);
void    config_usage(const char *prog);
void    config_free(Config *config);

#endif
