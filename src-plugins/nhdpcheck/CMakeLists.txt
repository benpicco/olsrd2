# set library parameters
SET (source "nhdpcheck.c")

# use generic plugin maker
oonf_create_app_plugin("nhdpcheck" ${source} "" false)
