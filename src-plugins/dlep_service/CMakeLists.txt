# set library name
SET (source dlep_service.c
            dlep_service_incoming.c
            dlep_service_outgoing.c)

# use generic plugin maker
oonf_create_app_plugin("dlep_service" "${source}" "" false)
