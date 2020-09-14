CC = clang
CXX = clang++

PHONELIBS = ../../../phonelibs

WARN_FLAGS = -Werror=implicit-function-declaration \
             -Werror=incompatible-pointer-types \
             -Werror=int-conversion \
             -Werror=return-type \
             -Werror=format-extra-args

CFLAGS = -std=gnu11 -fPIC -O2 $(WARN_FLAGS)
CXXFLAGS = -std=c++11 -fPIC -O2 $(WARN_FLAGS)

NANOVG_FLAGS = -I$(PHONELIBS)/nanovg

OPENGL_LIBS = -lGLESv3

FRAMEBUFFER_LIBS = -lutils -lgui -lEGL -lwindoww -lframebuffer2

OBJS = spinner.o \
       $(PHONELIBS)/nanovg/nanovg.o \
       ../../common/spinner.o \
       opensans_semibold.o \
       img_spinner_track.o \
       img_spinner_comma.o

DEPS := $(OBJS:.o=.d)

.PHONY: all
all: spinner

spinner: $(OBJS)
	@echo "[ LINK ] $@"
	$(CXX) -fPIC -o '$@' $^ \
	      -s \
        $(FRAMEBUFFER_LIBS) \
        /system/lib64/liblzma.so \
        -L/system/lib64 \
        $(OPENGL_LIBS) \
        -lm -llog

opensans_semibold.o: ../../assets/fonts/opensans_semibold.ttf
	@echo "[ bin2o ] $@"
	cd '$(dir $<)' && ld -r -b binary '$(notdir $<)' -o '$(abspath $@)'

img_spinner_track.o: ../../assets/img_spinner_track.png
	@echo "[ bin2o ] $@"
	cd '$(dir $<)' && ld -r -b binary '$(notdir $<)' -o '$(abspath $@)'

img_spinner_comma.o: ../../assets/img_spinner_comma.png
	@echo "[ bin2o ] $@"
	cd '$(dir $<)' && ld -r -b binary '$(notdir $<)' -o '$(abspath $@)'

%.o: %.c
	@echo "[ CC ] $@"
	$(CC) $(CFLAGS) -MMD \
          -I../.. \
          -I$(PHONELIBS)/neos/include \
          $(NANOVG_FLAGS) \
          -c -o '$@' '$<'

.PHONY: clean
clean:
	rm -f spinner $(OBJS) $(DEPS)

-include $(DEPS)
