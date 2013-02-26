# set library parameters
SET (source "ff_etx.c")

# use generic plugin maker
oonf_create_plugin("ff_etx" ${source} "" false)
