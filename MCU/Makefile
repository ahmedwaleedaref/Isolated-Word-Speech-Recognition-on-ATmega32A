# MCU and toolchain
MCU = atmega32
F_CPU = 11059200UL
CC = avr-gcc
OBJCOPY = avr-objcopy
AVRDUDE = avrdude
PROGRAMMER = usbasp

# Source files
SRCS = /home/ahmed-walled/Projects/Word-speech-recognition/MCU/main.c  /home/ahmed-walled/Projects/Word-speech-recognition/MCU/External_libraries/my_lcd.c /home/ahmed-walled/Projects/Word-speech-recognition/MCU/External_libraries/ext_interrupt.c /home/ahmed-walled/Projects/Word-speech-recognition/MCU/External_libraries/uart.c /home/ahmed-walled/Projects/Word-speech-recognition/MCU/External_libraries/word_templates_data.c /home/ahmed-walled/Projects/Word-speech-recognition/MCU/External_libraries/word_classifier.c

# Object files
OBJS = $(SRCS:.c=.o)

# Output files
TARGET = main
ELF = $(TARGET).elf
HEX = $(TARGET).hex

# Compilation flags
CFLAGS = -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Os -Wall

# Linker flags (enable float in printf)
LDFLAGS = -Wl,-u,vfprintf -lprintf_flt -lm

# Default target
all: $(HEX)

# Compile .c → .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Link object files → ELF
$(ELF): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

# Convert ELF → HEX
$(HEX): $(ELF)
	$(OBJCOPY) -O ihex -R .eeprom $(ELF) $(HEX)

# Flash MCU
flash: $(HEX)
	$(AVRDUDE) -c $(PROGRAMMER) -p $(MCU) -U flash:w:$(HEX):i

# Clean build files
clean:
	rm -f $(OBJS) $(ELF) $(HEX)

.PHONY: all clean flash