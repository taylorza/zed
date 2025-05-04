#ifndef EDITOR_H_
#define EDITOR_H_

uint16_t to_uint16(const char *s, char **p);
void edit(const char* filepath, uint16_t line, uint16_t col);

#endif // EDITOR_H_