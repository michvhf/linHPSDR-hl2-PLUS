# find what system we are running on
UNAME_S := $(shell uname -s)

# Get git commit version and date
#GIT_VERSION := $(shell git --no-pager describe --tags --always --dirty)
GIT_DATE := $(firstword $(shell git --no-pager show --date=short --format="%ai" --name-only))
GIT_VERSION := $(shell git describe --abbrev=0 --tags)

CC=gcc
LINK=gcc

GTKINCLUDES=`pkg-config --cflags gtk+-3.0`
GTKLIBS=`pkg-config --libs gtk+-3.0`

#OPENGL_OPTIONS=-D OPENGL
#OPENGL_INCLUDES=`pkg-config --cflags epoxy`
#OPENGL_LIBS=`pkg-config --libs epoxy`

ifeq ($(UNAME_S), Linux)
AUDIO_LIBS=-lasound -lpulse-simple -lpulse -lpulse-mainloop-glib -lsoundio
AUDIO_SOURCES=audio.c
AUDIO_HEADRERS=audio.h
endif
ifeq ($(UNAME_S), Darwin)
AUDIO_LIBS=-lsoundio
AUDIO_SOURCES=portaudio.c
AUDIO_HEADRERS=portaudio.h
endif

# uncomment the line below to include SoapySDR support
#
# Note: SoapySDR support has only been tested with the RTL-SDR and LimeSDR
#       No TX support yet.
#
#       If you want to build with SoapySDR support you will need to install:
#
#       sudo apt-get install libsoapysdr-dev
#	sudo apt-get install soapysdr-module-rtlsdr
#	sudo apt-get install soapysdr-module-lms7
#
SOAPYSDR_INCLUDE=SOAPYSDR

ifeq ($(SOAPYSDR_INCLUDE),SOAPYSDR)
SOAPYSDR_OPTIONS=-D SOAPYSDR
SOAPYSDR_LIBS=-lSoapySDR
SOAPYSDR_SOURCES= \
soapy_discovery.c \
soapy_protocol.c
SOAPYSDR_HEADERS= \
soapy_discovery.h \
soapy_protocol.h
SOAPYSDR_OBJS= \
soapy_discovery.o \
soapy_protocol.o
endif

ifeq ($(UNAME_S), Linux)
# cwdaemon support. Allows linux based logging software to key an Hermes/HermesLite2
# needs :
#			https://github.com/m5evt/unixcw-3.5.1.git

CWDAEMON_INCLUDE=CWDAEMON

ifeq ($(CWDAEMON_INCLUDE),CWDAEMON)
CWDAEMON_OPTIONS=-D CWDAEMON
CWDAEMON_LIBS=-lcw
CWDAEMON_SOURCES= \
cwdaemon.c
CWDAEMON_HEADERS= \
cwdaemon.h
CWDAEMON_OBJS= \
cwdaemon.o
endif
endif

# MIDI code from piHPSDR written by Christoph van Wullen, DL1YCF.
MIDI_INCLUDE=MIDI

ifeq ($(MIDI_INCLUDE),MIDI)
MIDI_OPTIONS=-D MIDI
MIDI_HEADERS= midi.h midi_dialog.h
ifeq ($(UNAME_S), Darwin)
MIDI_SOURCES= mac_midi.c midi2.c midi3.c midi_dialog.c
MIDI_OBJS= mac_midi.o midi2.o midi3.o midi_dialog.o
MIDI_LIBS= -framework CoreMIDI -framework Foundation
endif
ifeq ($(UNAME_S), Linux)
MIDI_SOURCES= alsa_midi.c midi2.c midi3.c midi_dialog.c
MIDI_OBJS= alsa_midi.o midi2.o midi3.o midi_dialog.o
MIDI_LIBS= -lasound
endif
endif

CFLAGS=	-g -Wno-deprecated-declarations -O3

#remove -DTEMP_IN_F for temperature display in C in the transmit panadapter
OPTIONS=  $(MIDI_OPTIONS) $(AUDIO_OPTIONS)  $(SOAPYSDR_OPTIONS) \
         $(CWDAEMON_OPTIONS)  $(OPENGL_OPTIONS) \
          -D USE_VFO_B_MODE_AND_FILTER="USE_VFO_B_MODE_AND_FILTER" \
         -D GIT_DATE='"$(GIT_DATE)"' -D GIT_VERSION='"$(GIT_VERSION)"' -DTEMP_IN_F
#OPTIONS=-g -Wno-deprecated-declarations $(AUDIO_OPTIONS) -D GIT_DATE='"$(GIT_DATE)"' -D GIT_VERSION='"$(GIT_VERSION)"' -O3 -D FT8_MARKER

ifeq ($(UNAME_S), Linux)
LIBS=-lrt -lm -lpthread -lwdsp $(GTKLIBS) $(AUDIO_LIBS) $(SOAPYSDR_LIBS) $(CWDAEMON_LIBS) $(OPENGL_LIBS) $(MIDI_LIBS)
endif
ifeq ($(UNAME_S), Darwin)
LIBS=-lm -lpthread -lwdsp $(GTKLIBS) $(AUDIO_LIBS) $(SOAPYSDR_LIBS) $(MIDI_LIBS) 
endif


INCLUDES=$(GTKINCLUDES) $(PULSEINCLUDES) $(OPGL_INCLUDES)

COMPILE=$(CC) $(CFLAGS) $(OPTIONS) $(INCLUDES)

.c.o:
	$(COMPILE) -c -o $@ $<

PROGRAM=linhpsdr-hl2-PLUS

SOURCES=\
main.c\
css.c\
audio.c\
version.c\
discovered.c\
discovery.c\
protocol1_discovery.c\
protocol2_discovery.c\
property.c\
mode.c\
filter.c\
band.c\
radio.c\
receiver.c\
transmitter.c\
vfo.c\
meter.c\
rx_panadapter.c\
tx_panadapter.c\
mic_level.c\
mic_gain.c\
drive_level.c\
waterfall.c\
wideband_panadapter.c\
wideband_waterfall.c\
protocol1.c\
protocol2.c\
radio_dialog.c\
receiver_dialog.c\
transmitter_dialog.c\
pa_dialog.c\
eer_dialog.c\
wideband_dialog.c\
about_dialog.c\
button_text.c\
wideband.c\
vox.c\
ext.c\
configure_dialog.c\
bookmark_dialog.c\
puresignal_dialog.c\
oc_dialog.c\
xvtr_dialog.c\
frequency.c\
error_handler.c\
radio_info.c\
diversity_mixer.c\
diversity_dialog.c\
rigctl.c \
bpsk.c \
ringbuffer.c \
hl2.c \
level_meter.c \
tx_info.c \
tx_info_meter.c \
peak_detect.c \
subrx.c \
actions.c

HEADERS=\
main.h\
css.h\
audio.h\
version.h\
discovered.h\
discovery.h\
protocol1_discovery.h\
protocol2_discovery.h\
property.h\
agc.h\
mode.h\
filter.h\
band.h\
radio.h\
receiver.h\
transmitter.h\
vfo.h\
meter.h\
rx_panadapter.h\
tx_panadapter.h\
mic_level.h\
mic_gain.h\
drive_level.h\
wideband_panadapter.h\
wideband_waterfall.h\
waterfall.h\
protocol1.h\
protocol2.h\
radio_dialog.h\
receiver_dialog.h\
transmitter_dialog.h\
pa_dialog.h\
eer_dialog.h\
wideband_dialog.h\
about_dialog.h\
button_text.h\
wideband.h\
vox.h\
ext.h\
configure_dialog.h\
bookmark_dialog.h\
puresignal_dialog.h\
oc_dialog.h\
xvtr_dialog.h\
frequency.h\
error_handler.h\
radio_info.h\
diversity_mixer.h\
diversity_dialog.h\
rigctl.h \
bpsk.h \
ringbuffer.h \
hl2.h \
level_meter.h \
tx_info.h \
tx_info_meter.h \
peak_detect.h \
subrx.h \
actions.h

OBJS=\
main.o\
css.o\
audio.o\
version.o\
discovered.o\
discovery.o\
protocol1_discovery.o\
protocol2_discovery.o\
property.o\
mode.o\
filter.o\
band.o\
radio.o\
receiver.o\
transmitter.o\
vfo.o\
meter.o\
rx_panadapter.o\
tx_panadapter.o\
mic_level.o\
mic_gain.o\
drive_level.o\
wideband_panadapter.o\
wideband_waterfall.o\
waterfall.o\
protocol1.o\
protocol2.o\
radio_dialog.o\
receiver_dialog.o\
transmitter_dialog.o\
pa_dialog.o\
eer_dialog.o\
wideband_dialog.o\
about_dialog.o\
button_text.o\
wideband.o\
vox.o\
ext.o\
configure_dialog.o\
bookmark_dialog.o\
puresignal_dialog.o\
oc_dialog.o\
xvtr_dialog.o\
frequency.o\
error_handler.o\
radio_info.o\
diversity_mixer.o\
diversity_dialog.o\
rigctl.o \
bpsk.o \
ringbuffer.o \
hl2.o \
level_meter.o \
tx_info.o \
tx_info_meter.o \
peak_detect.o \
subrx.o \
actions.o


$(PROGRAM):  $(OBJS) $(SOAPYSDR_OBJS) $(CWDAEMON_OBJS) $(MIDI_OBJS)
	$(LINK) -o $(PROGRAM) $(OBJS) $(SOAPYSDR_OBJS) $(CWDAEMON_OBJS) $(MIDI_OBJS)  $(LIBS)


all: prebuild  $(PROGRAM) $(HEADERS) $(MIDI_HEADERS) $(SOURCES) $(SOAPYSDR_SOURCES) \
							 $(CWDAEMON_SOURCES) $(MIDI_SOURCES)

prebuild:
	rm -f version.o


clean:
	-rm -f *.o
	-rm -f $(PROGRAM)

install: $(PROGRAM)
	cp $(PROGRAM) /usr/local/bin
	if [ ! -d /usr/share/linhpsdr-hl2-PLUS ]; then mkdir /usr/share/linhpsdr-hl2-PLUS; fi
	cp hpsdr.png /usr/share/linhpsdr-hl2-PLUS
	cp hpsdr_icon.png /usr/share/linhpsdr-hl2-PLUS
	cp hpsdr_small.png /usr/share/linhpsdr-hl2-PLUS
	cp linhpsdr-hl2-PLUS.desktop /usr/share/applications

debian:
	mkdir -p pkg/linhpsdr-hl2-PLUS/usr/local/bin
	mkdir -p pkg/linhpsdr-hl2-PLUS/usr/local/lib
	mkdir -p pkg/linhpsdr-hl2-PLUS/usr/share/linhpsdr-hl2-PLUS
	mkdir -p pkg/linhpsdr-hl2-PLUS/usr/share/applications
	chmod -R 0755 pkg/linhpsdr-hl2-PLUS
	cp $(PROGRAM) pkg/linhpsdr-hl2-PLUS/usr/local/bin
	cp /usr/local/lib/libwdsp.so pkg/linhpsdr-hl2-PLUS/usr/local/lib
	cp hpsdr.png pkg/linhpsdr-hl2-PLUS/usr/share/linhpsdr-hl2-PLUS
	cp hpsdr_icon.png pkg/linhpsdr-hl2-PLUS/usr/share/linhpsdr-hl2-PLUS
	cp hpsdr_small.png pkg/linhpsdr-hl2-PLUS/usr/share/linhpsdr-hl2-PLUS
	cp linhpsdr-hl2-PLUS.desktop pkg/linhpsdr-hl2-PLUS/usr/share/applications
	cd pkg; dpkg-deb --build linhpsdr-hl2-PLUS

