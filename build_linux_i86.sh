cmake -B build_i86 \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_INSTALL_PREFIX=$HOME/.local \
  -DLIB_INSTALL_DIR="share/vulkan/implicit_layer.d" \
  -DJSON_INSTALL_DIR="share/vulkan/implicit_layer.d" \
  -DJSON_LIBRARY_PATH="./libbcn_layer32.so" \
  -DLAYER_NAME="VK_LAYER_BCN_BCnLayer32" \
  -DOUT_NAME="bcn_layer32" \
  -DCMAKE_CXX_FLAGS="-m32" \
  -DCMAKE_SHARED_LINKER_FLAGS="-m32" \
  -DCMAKE_EXE_LINKER_FLAGS="-m32"

cd build_i86

make
