LSB_DISTRIB_CODENAME=$(shell lsb_release --short --codename)

CC=musl-gcc

CFLAGS = -I../../musl-libraries/build/include -I../../musl-libraries/build/include/igel64 -I../../musl-libraries/build/include/sysfs
CFLAGS += -Wdate-time -Wall -Wno-error=unused-result -Wformat -Werror=format-security -W -Wshadow -Wpointer-arith -Wundef -Wchar-subscripts -Wcomment -Wdeprecated-declarations -Wdisabled-optimization -Wdiv-by-zero -Wfloat-equal -Wformat-extra-args -Wformat-security -Wformat-y2k -Wimplicit -Wimplicit-function-declaration -Wimplicit-int -Wmain -Wmissing-braces -Wmissing-format-attribute -Wmultichar -Wparentheses -Wreturn-type -Wsequence-point -Wshadow -Wsign-compare -Wswitch -Wtrigraphs -Wunknown-pragmas -Wunused -Wunused-function -Wunused-label -Wunused-parameter -Wunused-value  -Wunused-variable -Wwrite-strings -Wnested-externs -Wstrict-prototypes -Wcast-align  -Wextra -Wattributes -Wendif-labels -Winit-self -Wint-to-pointer-cast -Winvalid-pch -Wmissing-field-initializers -Wnonnull -Woverflow -Wvla -Wpointer-to-int-cast -Wstrict-aliasing -Wvariadic-macros -Wvolatile-register-var -Wpointer-sign -Wmissing-include-dirs -Wmissing-prototypes -Wmissing-declarations -Wformat=2 -Werror -Wno-undef -Wno-sign-compare -Wno-unused -Wno-unused-parameter -Wno-redundant-decls -Wno-unreachable-code -Wno-conversion
CFLAGS += -Os -fomit-frame-pointer -pipe -march=x86-64

LDFLAGS= -L../../musl-libraries/build/lib -s -static -Wl,-Bstatic -lsysfs -lz -lblkid -luuid
LDFLAGS_SHARED= -L../../musl-libraries/build/lib -s -Wl,-Bstatic -lsysfs -lz -lblkid -luuid -Wl,-Bdynamic

EXT_LIBS = ../../musl-libraries/build/lib/libz.a ../../musl-libraries/build/lib/libuuid.a \
	   ../../musl-libraries/build/lib/libsysfs.a ../../musl-libraries/build/lib/libblkid.a

all: init rescue_shell init-shared rescue_shell-shared init-gzip init-strip_ddimage init-systool

../../musl-libraries/build/lib/%.a:
	cd ../../musl-libraries/ && ./gen-libraries.sh

../../musl-libraries/build/lib/%.so:
	cd ../../musl-libraries/ && ./gen-libraries.sh

init: $(EXT_LIBS) init.o file_handling.o string_helper.o alias.o gzip.o console.o modprobe.o insmod.o rmmod.o crc.o check_part_hdr.o read-write-extent.o blkid_detect.o minimal_igelmkimage.o strip_ddimage.o sysfs-handling.o loopdev.o beep.o igel_bootregfs.o igel_keyring.o
	$(CC) -o $@ $+ $(LDFLAGS)

rescue_shell: $(EXT_LIBS) tty.o rescue_shell.o
	$(CC) -o $@ $+ $(LDFLAGS) -s

init-shared: $(EXT_LIBS) init.o file_handling.o string_helper.o alias.o gzip.o console.o modprobe.o insmod.o rmmod.o crc.o check_part_hdr.o read-write-extent.o blkid_detect.o minimal_igelmkimage.o strip_ddimage.o sysfs-handling.o loopdev.o beep.o igel_bootregfs.o igel_keyring.o
	$(CC) -o $@ $+ $(LDFLAGS_SHARED)

rescue_shell-shared: $(EXT_LIBS) tty.o rescue_shell.o
	$(CC) -o $@ $+ $(LDFLAGS_SHARED) -s

init-gzip: $(EXT_LIBS) init-gzip.o file_handling.o string_helper.o console.o gzip.o
	$(CC) -o $@ $+ $(LDFLAGS_SHARED)

init-strip_ddimage: $(EXT_LIBS) strip_ddimage.o strip_ddimage_init.o file_handling.o string_helper.o console.o crc.o
	$(CC) -o $@ $+ $(LDFLAGS)

init-systool: $(EXT_LIBS) file_handling.o string_helper.o sysfs-handling.o systool.o
	$(CC) -o $@ $+ $(LDFLAGS)

%.o:	%.c
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm -f *.o init rescue_shell init-shared rescue_shell-shared init-gzip

