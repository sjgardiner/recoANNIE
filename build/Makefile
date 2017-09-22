INCLUDE_DIR = ../include
SRC_DIR = ../src

ifndef CXXFLAGS
  CXXFLAGS = -std=c++14 -O3
endif

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  SHARED_LIB_SUFFIX=dylib
else ifeq ($(UNAME_S),Linux)
  SHARED_LIB_SUFFIX=so
else
  $(warning Unrecognized operating system encountered.)
  SHARED_LIB_SUFFIX=so
endif

SHARED_LIB_NAME := RecoANNIE
SHARED_LIB := lib$(SHARED_LIB_NAME).$(SHARED_LIB_SUFFIX)

all: reco-annie

# Skip lots of initialization if all we want is "make clean/uninstall"
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(MAKECMDGOALS),uninstall)
 
  # Use g++ as the default compiler
  CXX = g++
  CXXFLAGS += -I$(INCLUDE_DIR) -Wall -Wextra -Wpedantic
  CXXFLAGS += -Werror -Wno-error=unused-parameter -Wcast-align

  # Add extra compiler flags for recognized compilers (currently just gcc
  # and clang)
  CXXVERSION = $(shell $(CXX) --version)
  COMPILER_VERSION := $(word 3, $(CXXVERSION))
  ifneq (,$(findstring clang,$(CXXVERSION)))
    # clang
    $(info Compiling using version $(COMPILER_VERSION) of clang)

    # The ROOT headers trigger clang's no-keyword-macro warning, so disable it.
    CXXFLAGS += -Wno-keyword-macro
  else
    ifneq (,$(or $(findstring GCC,$(CXXVERSION)), $(findstring g++,$(CXXVERSION))))
      # gcc
      $(info Compiling using version $(COMPILER_VERSION) of GCC)
      ifneq (,$(findstring $(COMPILER_VERSION), 4.9.))
        # g++ 4.9 gives many false positives for -Wshadow, so disable it
        # for now.
        CXXFLAGS += -Wno-shadow
      endif
      # Linking to ROOT libraries can be problematic on distributions (e.g.,
      # Ubuntu) that set the g++ flag -Wl,--as-needed by default (see
      # http://www.bnikolic.co.uk/blog/gnu-ld-as-needed.html for details), so
      # disable this behavior on Linux.
      ifneq ($(UNAME_S),Darwin)
        CXXFLAGS += -Wl,--no-as-needed
      endif
    endif
  endif
  
  OBJECTS := $(notdir $(patsubst %.cc,%.o,$(wildcard $(SRC_DIR)/*.cc)))
  OBJECTS := $(filter-out reco-annie.o, $(OBJECTS))
  
  ROOTCONFIG := $(shell command -v root-config 2> /dev/null)
  # prefer rootcling as the dictionary generator executable name, but use
  # rootcint if you can't find it
  ROOTCLING := $(shell command -v rootcling 2> /dev/null)
  ifndef ROOTCLING
    ROOTCLING := $(shell command -v rootcint 2> /dev/null)
  endif
  ROOT := $(shell command -v root 2> /dev/null)

  ifndef ROOTCONFIG
    $(error Could not find a valid ROOT installation.)
  else
    ROOT_VERSION := $(shell $(ROOTCONFIG) --version)
    $(info Found ROOT version $(ROOT_VERSION) in $(ROOT))
    ROOT_CXXFLAGS := $(shell $(ROOTCONFIG) --cflags)
    ROOT_LDFLAGS := $(shell $(ROOTCONFIG) --ldflags)
    ROOT_LIBDIR := $(shell $(ROOTCONFIG) --libdir)
    ROOT_LDFLAGS += -L$(ROOT_LIBDIR) -lCore -lRIO -lHist -lTree
    ifeq ($(UNAME_S),Linux)
      ROOT_LDFLAGS += -rdynamic
    endif
    ROOT_DICT_INCLUDES := -I$(INCLUDE_DIR) annie/RawChannel.hh \
      annie/RawCard.hh annie/RawReadout.hh annie/RecoPulse.hh
    ROOT_OBJ_DICT = recoANNIE_dict.o
    ROOT_OBJECTS = $(ROOT_OBJ_DICT)
  
$(ROOT_OBJ_DICT):
	$(RM) recoANNIE_dict*.*
	$(ROOTCLING) -f recoANNIE_dict.cc -c $(ROOT_DICT_INCLUDES)
	$(CXX) $(ROOT_CXXFLAGS) $(CXXFLAGS) -fPIC -o $(ROOT_OBJ_DICT) -c \
	  recoANNIE_dict.cc
	$(RM) recoANNIE_dict.cc

  endif
endif
endif

# Set up default variables for the install/uninstall targets and for setting
# the executable rpath
prefix = /usr
exec_prefix = $(prefix)
bindir = $(exec_prefix)/bin
libdir = $(exec_prefix)/lib
incdir = $(prefix)/include

# Causes GNU make to auto-delete the object files when the build is complete
.INTERMEDIATE: $(OBJECTS) $(ROOT_OBJECTS) reco-annie.o

%.o: $(SRC_DIR)/%.cc
	$(CXX) $(ROOT_CXXFLAGS) $(CXXFLAGS) -I$(INCLUDE_DIR) -fPIC -o $@ -c $^

$(SHARED_LIB): $(OBJECTS) $(ROOT_OBJECTS)
	$(CXX) $(CXXFLAGS) -fPIC -shared -o $@ $^

reco-annie: $(SHARED_LIB) reco-annie.o
	$(CXX) $(CXXFLAGS) -o $@ -L. \
	  -l$(SHARED_LIB_NAME) $(ROOT_LDFLAGS) \
	  -Wl,-rpath -Wl,$(libdir):$(shell pwd) reco-annie.o

.PHONY: clean install uninstall

clean:
	$(RM) *.$(SHARED_LIB_SUFFIX) *.o recoANNIE_dict*.* reco-annie
	$(RM) *.dSYM

install: reco-annie
	mkdir -p $(DESTDIR)$(bindir)
	mkdir -p $(DESTDIR)$(libdir)
	mkdir -p $(DESTDIR)$(incdir)/reco-annie
	cp reco-annie $(DESTDIR)$(bindir)
	cp $(SHARED_LIB) $(DESTDIR)$(libdir)
	cp recoANNIE_dict_rdict.pcm $(DESTDIR)$(libdir) 2> /dev/null || true
	cp -r ../include/reco-annie $(DESTDIR)$(incdir)
	ldconfig

uninstall:
	$(RM) $(DESTDIR)$(bindir)/reco-annie
	$(RM) $(DESTDIR)$(libdir)/$(SHARED_LIB)
	$(RM) $(DESTDIR)$(libdir)/recoANNIE_dict_rdict.pcm
	$(RM) -r $(DESTDIR)$(incdir)/reco-annie
	ldconfig
