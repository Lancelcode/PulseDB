#ifndef CONFIG_H
#define CONFIG_H

typedef struct {
    int   port;
    int   hz;
    char *loglevel;
    char *dir;
    char *dbfilename;
} Config;

Config *config_create_default(void);
void    config_free(Config *config);

#endif