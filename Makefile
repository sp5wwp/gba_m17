FILES = example.c ./libm17/decode/*.c ./libm17/encode/*.c ./libm17/math/*.c ./libm17/payload/*.c ./libm17/phy/*.c
OPTS = -ffunction-sections -fdata-sections -Wall -Wextra -Wl,--gc-sections -Os
OPTS_ADD = -mcpu=arm7tdmi -nostartfiles -Tlnkscript -specs=nano.specs -specs=nosys.specs
LIBS = -lm

all: example.c
	arm-none-eabi-gcc -I ./libm17 crt0.s $(FILES) $(OPTS) $(OPTS_ADD) -o out $(LIBS)
	arm-none-eabi-objcopy -O binary out out.gba
	./ht.pl -clo out_fixed.gba out.gba
	rm out out.gba

test:
	mgba-qt -2 out_fixed.gba

clean:
	rm out_fixed.gba
