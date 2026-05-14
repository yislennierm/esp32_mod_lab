ROM := build/p4_align.gbc
ASM := build/p4_align.asm
OBJ := build/p4_align.o

.PHONY: all clean

all: $(ROM)

$(ASM): generate_alignment_rom.py
	mkdir -p build
	python3 generate_alignment_rom.py > $(ASM)

$(OBJ): $(ASM)
	rgbasm -o $(OBJ) $(ASM)

$(ROM): $(OBJ)
	rgblink -o $(ROM) $(OBJ)
	rgbfix -v -C -p 0xff -t P4ALIGN $(ROM)

clean:
	rm -rf build
