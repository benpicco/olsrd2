# set library name
SET (source src/dlep_service.c
            src/dlep_service_incoming.c
            src/dlep_service_outgoing.c)

# use generic plugin maker
oonf_create_app_plugin("dlep_service" "${source}" false false)
