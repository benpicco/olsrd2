# set library name
SET (source "src/layer2_viewer.c")

# use generic plugin maker
oonf_create_app_plugin("layer2_viewer" "${source}" false false)
