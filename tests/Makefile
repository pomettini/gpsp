
ARMV8PFX=/opt/buildroot-armv8el-uclibc/bin/aarch64-buildroot-linux-uclibc
MIPS32PFX=/opt/buildroot-mipsel32-o32-uclibc/bin/mipsel-buildroot-linux-uclibc

all: thumb2
	gcc -o arm64gen arm64gen.c -ggdb -I../arm/
	./arm64gen > bytecode.bin
	$(ARMV8PFX)-as -o bytecoderef.o arm64gen.S
	$(ARMV8PFX)-objcopy -O binary bytecoderef.o bytecoderef.bin
	@ cmp bytecoderef.bin bytecode.bin || echo "Bytecode mismatch"
	@ cmp bytecoderef.bin bytecode.bin && echo "Test passed!"
	gcc -o mipsgen mipsgen.c -ggdb -I../mips/
	./mipsgen > bytecode.bin
	$(MIPS32PFX)-as -EL -o bytecoderef.o mipsgen.S
	$(MIPS32PFX)-objcopy -j .text -O binary bytecoderef.o bytecoderef.bin
	@ cmp bytecoderef.bin bytecode.bin || echo "Bytecode mismatch"
	@ cmp bytecoderef.bin bytecode.bin && echo "Test passed!"



# Thumb-2 (Cortex-M7) backend encoders, uses the Playdate toolchain
thumb2:
	gcc -o thumb2gen thumb2gen.c -ggdb -I../arm/
	./thumb2gen > bytecode.bin
	arm-none-eabi-gcc -c thumb2gen.S -o bytecoderef.o
	arm-none-eabi-objcopy -j .text -O binary bytecoderef.o bytecoderef.bin
	@ cmp bytecoderef.bin bytecode.bin || echo "Bytecode mismatch"
	@ cmp bytecoderef.bin bytecode.bin && echo "Test passed!"
