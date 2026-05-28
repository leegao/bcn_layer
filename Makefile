CXX := g++
CXXFLAGS := -std=c++17 -fPIC
LDFLAGS := -shared
PREFIX := /usr
JSON := libbcn_layer.json
JSON_INSTALL := $(PREFIX)/share/vulkan/implicit_layer.d
INSTALL := $(PREFIX)/lib/x86_64-linux-gnu

SOURCES := src/bcn_layer.cpp \
		   src/image.cpp \
	       src/buffer.cpp \
	       src/bcn.cpp \
	       src/command_buffer.cpp \
	       src/queue.cpp \
	       src/fence.cpp \
	       src/logger.cpp \
	       src/staging_resources.cpp

HEADERS := src/bcn_layer.hpp \
		   src/image.hpp \
		   src/buffer.hpp \
		   src/bcn.hpp \
		   src/command_buffer.hpp \
		   src/queue.hpp \
		   src/fence.hpp \
		   src/logger.hpp \
		   src/vk_func.hpp \
		   src/vulkan/vk_layer.h \
		   src/staging_resources.hpp

SPIRV_SHADERS := src/s3tc.spv \
                 src/s3tc_iv.spv \
                 src/rgtc.spv \
                 src/rgtc_iv.spv \
                 src/bc6.spv \
                 src/bc6_iv.spv \
                 src/bc7.spv \
                 src/bc7_iv.spv

SPIRV_HEADERS := src/s3tc_spv.h \
				 src/s3tc_iv_spv.h \
				 src/rgtc_spv.h \
				 src/rgtc_iv_spv.h \
				 src/bc6_spv.h \
				 src/bc6_iv_spv.h \
				 src/bc7_spv.h \
				 src/bc7_iv_spv.h
	      
OUTPUT := libbcn_layer.so

all : $(OUTPUT)

src/%.spv : src/%.comp
	glslc $< -o $@

src/%_spv.h : src/%.spv
	cd src && xxd -i $(notdir $<) > $(notdir $@)
	
$(OUTPUT) : $(SOURCES) $(SPIRV_HEADERS) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(SOURCES) -o $(OUTPUT)

.PHONY: clean install

install: $(OUTPUT)
	install -d $(INSTALL)
	install -m 755 $(OUTPUT) $(INSTALL)
	install -d $(JSON_INSTALL)
	install -m 755 $(JSON) $(JSON_INSTALL)

clean:
	rm -f $(OUTPUT)
	rm -f $(SPIRV_SHADERS)
	rm -f $(SPIRV_HEADERS)
