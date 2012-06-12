# set library name
SET (source "src/nl80211_listener.c")

# use generic plugin maker
oonf_create_app_plugin("nl80211_listener" "${source}" false "${OONF_LIBNL}")
