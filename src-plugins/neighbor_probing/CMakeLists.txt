# set library parameters
SET (source "neighbor_probing.c")

# use generic plugin maker
oonf_create_app_plugin("neighbor_probing" ${source} "" false)
