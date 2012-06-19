# set library name
SET (source dlep_client.c
            dlep_client_incoming.c
            dlep_client_outgoing.c)

# use generic plugin maker
oonf_create_app_plugin("dlep_client" "${source}" "" false)
