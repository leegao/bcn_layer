echo "// AUTO-GENERATED - DO NOT EDIT, see  src/etc2_encode.slang" > src/etc2_encode.comp
slangc src/etc2_encode.slang -target glsl -line-directive-mode none -D DISABLE_RECONSTRUCTION >> src/etc2_encode.comp

echo "// AUTO-GENERATED - DO NOT EDIT, see  src/astc_encoder.slang" > src/astc_encoder.comp
slangc src/astc_encoder.slang -target glsl -line-directive-mode none -D DISABLE_RECONSTRUCTION >> src/astc_encoder.comp
