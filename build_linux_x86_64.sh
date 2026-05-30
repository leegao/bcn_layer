cmake -B build \
  -DCMAKE_INSTALL_PREFIX=$HOME/.local \
  -DLIB_INSTALL_DIR="share/vulkan/implicit_layer.d" \
  -DJSON_INSTALL_DIR="share/vulkan/implicit_layer.d"

cd build

make
