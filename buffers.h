#ifndef BUFFERS_H_
#define BUFFERS_H_

#define SCRATCH_BUFFER_SIZE 8192
#define MAX_FILENAME_LEN 250
#define MAX_TEMPBUFFER_LEN 256

extern char scratch_buffer[];
extern char tmpbuffer[];
extern int32_t text_buffer_size;

void buffers_init(void);
void buffers_release(void);

char* get_text_ptr(int32_t index) MYCC;
char get_text_char(int32_t index) MYCC;
void set_text_char(int32_t index, char ch) MYCC;

#endif //BUFFERS_H_