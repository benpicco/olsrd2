# set library parameters
SET (source "default_hysteresis.c")

# use generic plugin maker
oonf_create_plugin("default_hysteresis" ${source} "" false)
