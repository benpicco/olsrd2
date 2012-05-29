include $(TOPDIR)/rules.mk

PKG_NAME:=dlep-client
PKG_VERSION:=2012-05-18
PKG_RELEASE=$(PKG_SOURCE_VERSION)

CMAKE_INSTALL:=1

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

CMAKE_SOURCE_DIR=$(CURDIR)/../../
CMAKE_OPTIONS=-DOONF_CONFIG=$(CURDIR)/files/dlep-client.cmake

define Package/dlep-client-git
	SECTION:=net
	CATEGORY:=Network
	TITLE:=DLEP-Client
endef

define Package/dlep-client/conffiles
	/etc/dlep-client.config
endef

define Build/Compile
	$(call Build/Compile/Default,dlep-client)
endef

TARGET_CFLAGS += -I$(STAGING_DIR)/usr/include

define Package/dlep-client/install
	$(INSTALL_DIR) $(1)/bin
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/bin/dlep-client $(1)/bin/
	$(INSTALL_CONF) $(CURDIR)/files/dlep-client.config $(1)/etc/dlep-client.config
endef

$(eval $(call BuildPackage,dlep-client-git))
