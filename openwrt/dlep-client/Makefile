include $(TOPDIR)/rules.mk

PKG_NAME:=dlep-client
PKG_VERSION:=2012-05-18
PKG_RELEASE=$(PKG_SOURCE_VERSION)

#PKG_SOURCE_PROTO:=git
#PKG_SOURCE_URL:=file://
#PKG_SOURCE_SUBDIR:=$(PKG_NAME)-$(PKG_VERSION)
#PKG_SOURCE_VERSION:=3706552c1c42182a8b656f95affc7e787a5b71dd
#PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION)-$(PKG_SOURCE_VERSION).tar.gz
#PKG_MIRROR_MD5SUM:=ec8a83b0678d3d9d2575bb975569576c
CMAKE_INSTALL:=1

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

CMAKE_SOURCE_DIR=$(CURDIR)/../../
CMAKE_OPTIONS=-DOONF_CONFIG=$(CURDIR)/files/dlep-client.cmake

define Package/dlep-client
	SECTION:=net
	CATEGORY:=Network
	TITLE:=DLEP-Client
endef

define Package/dlep-client/conffiles
	/etc/dlep-client.config
endef

TARGET_CFLAGS += -I$(STAGING_DIR)/usr/include

define Package/dlep-client/install
	$(INSTALL_DIR) $(1)/bin
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/bin/dlep-client $(1)/bin/
	$(INSTALL_CONF) $(CURDIR)/files/dlep-client.config $(1)/etc/dlep-client.config
endef

$(eval $(call BuildPackage,dlep-client))
