# set library name
SET (source src/dlep_client.c
            src/dlep_client_incoming.c
            src/dlep_client_outgoing.c)

# use generic plugin maker
oonf_create_app_plugin("dlep_client" "${source}" false false)
