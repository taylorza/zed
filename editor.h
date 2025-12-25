#ifndef EDITOR_H_
#define EDITOR_H_

int32_t to_int32(const char *s, const char **p) MYCC;
char* get_filename(char* path) MYCC;
void edit(char* filepath, int32_t line, int32_t col) MYCC;

#endif // EDITOR_H_