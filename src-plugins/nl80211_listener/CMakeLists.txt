# set library name
SET (source "nl80211_listener.c")

# use generic plugin maker
oonf_create_app_plugin("nl80211_listener" "${source}" "" "${OONF_LIBNL}")
