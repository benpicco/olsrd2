# set library parameters
SET (source "hysteresis_olsrv1.c")

# use generic plugin maker
oonf_create_app_plugin("hysteresis_olsrv1" ${source} "" false)
