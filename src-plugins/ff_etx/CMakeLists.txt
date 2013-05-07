# set library parameters
SET (source "ff_etx.c")

# use generic plugin maker
oonf_create_app_plugin("ff_etx" ${source} "" false)
