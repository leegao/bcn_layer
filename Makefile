CXX := g++
CXXFLAGS := -std=c++17 -fPIC
LDFLAGS := -shared
PREFIX := /usr
JSON := libetc2_layer.json
JSON_INSTALL := /home/leegao/.local/share/vulkan/implicit_layer.d
INSTALL := /home/leegao/.local/share/vulkan/implicit_layer.d

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

OUTPUT := libetc2_layer.so

all : $(OUTPUT)

src/%.comp : src/%.slang
	echo "// AUTO-GENERATED - DO NOT EDIT, see " $< > $@
	slangc $< -target glsl -line-directive-mode none -D DISABLE_RECONSTRUCTION >> $@

src/%.spv : src/%.comp
	glslc -O $< -o $@

src/%_spv.h : src/%.spv
	cd src && xxd -i $(notdir $<) > $(notdir $@)

$(OUTPUT) : $(SOURCES) $(HEADERS) src/etc2.comp src/etc2_spv.h src/astc_decoder.comp src/astc_decoder_spv.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(SOURCES) -o $(OUTPUT)

.PHONY: clean install

install: $(OUTPUT)
	install -d $(INSTALL)
	install -m 755 $(OUTPUT) $(INSTALL)
	install -d $(JSON_INSTALL)
	install -m 755 $(JSON) $(JSON_INSTALL)

clean:
	rm -f $(OUTPUT)
