# set library parameters
SET (source "olsr_hysteresis.c")

# use generic plugin maker
oonf_create_plugin("olsr_hysteresis" ${source} "" false)
