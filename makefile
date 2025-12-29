OUTPUT_DIR = output
TARGET_BIN = zed

TARGET = +zxn

ZCC     = zcc
ASM     = z80asm

MAX_ALLOCS = 200000
CFLAGS = --list -m -c -clib=sdcc_iy -SO3 -opt-code-size --max-allocs-per-node$(MAX_ALLOCS) -pragma-include:zpragma.inc
AFLAGS =
LFLAGS = --list -m -startup=30 -clib=sdcc_iy -subtype=dotn -SO3 -opt-code-size --max-allocs-per-node$(MAX_ALLOCS) -pragma-include:zpragma.inc -create-app

SOURCES = buffers.c buffers_s.asm crtio.c crtio_s.asm editor.c settings.c main.c 

OBJFILES = $(patsubst %.c,$(OUTPUT_DIR)/%.o,$(SOURCES))

.PHONY: all compile assemble clean

all: compile link

$(OUTPUT_DIR):
	mkdir $(OUTPUT_DIR)

#$(OUTPUT_DIR)/buffers.o: buffers.c buffers_s.asm | $(OUTPUT_DIR)
#	@echo "Compiling $<"
#	$(ZCC) $(TARGET) $(CFLAGS) $< -o $@ --datasegcode_l --codesegcode_l --constsegcode_l
#	@echo "-> Generated $@"

$(OUTPUT_DIR)/crtio.o: crtio.c crtio_s.asm | $(OUTPUT_DIR)
	@echo "Compiling $<"
	$(ZCC) $(TARGET) $(CFLAGS) $< -o $@ --datasegcode_l --codesegcode_l --constsegcode_l --bsssegcode_l
	@echo "-> Generated $@"

$(OUTPUT_DIR)/%.o: %.c | $(OUTPUT_DIR)
	@echo "Compiling $<"
	$(ZCC) $(TARGET) $(CFLAGS) $< -o $@
	@echo "-> Generated $@"

compile: $(OBJFILES)	
	@echo "Compilation complete."

$(TARGET_BIN): $(OBJFILES)
	@echo "Linking into $(TARGET_BIN)..."
	$(ZCC) $(TARGET) $(LFLAGS) -o$(TARGET_BIN) $(OBJFILES)
	$(ASM) font.asm -b
	copy /b zed+font.bin zed
	@echo "-> Created $(TARGET_BIN)"

link: $(TARGET_BIN)
	@echo "Linker complete."

clean:
	@echo "Cleaning generated files..."
	rm -rf $(OUTPUT_DIR) $(TARGET_BIN)
	rm -f *.lis
	rm -f font.bin font.o
	@echo "Clean complete."
