include $(TOPDIR)/rules.mk

PKG_NAME:=dlep-service
PKG_VERSION:=2012-05-18
PKG_RELEASE=$(PKG_SOURCE_VERSION)

CMAKE_INSTALL:=1

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

CMAKE_SOURCE_DIR=$(CURDIR)/../../
CMAKE_OPTIONS=-DOONF_CONFIG=$(CURDIR)/files/dlep-service.cmake

define Package/dlep-service-git
  SECTION:=net
  CATEGORY:=Network
  DEPENDS:=+libnl-tiny
  TITLE:=DLEP-Service
endef

define Package/dlep-service/conffiles
	/etc/dlep-service.config
endef

define Build/Compile
	$(call Build/Compile/Default,dlep-service)
endef

TARGET_CFLAGS += -I$(STAGING_DIR)/usr/include -I${STAGING_DIR}/usr/include/libnl-tiny

define Package/dlep-service/install
	$(INSTALL_DIR) $(1)/bin
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/bin/dlep-service $(1)/bin/
	$(INSTALL_CONF) $(CURDIR)/files/dlep-service.config $(1)/etc/dlep-service.config
endef

$(eval $(call BuildPackage,dlep-service-git))
