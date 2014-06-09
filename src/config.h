void config_init(void);
int config_open(const char* name, int oflag, int omode);
void config_replace(const char* source, const char* dest);
void config_chdir(void (*handle)());
