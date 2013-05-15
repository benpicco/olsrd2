include $(TOPDIR)/rules.mk

PKG_NAME:=oonf-olsrd2-debug
PKG_VERSION:=2013-05-15
PKG_RELEASE=$(PKG_SOURCE_VERSION)

CMAKE_INSTALL:=1

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

CMAKE_SOURCE_DIR=$(CURDIR)/../../
CMAKE_OPTIONS=-DOONF_DO_NOT_REGISTER_PACKAGE:Bool=true \
              -DOONF_LOGGING_LEVEL:String=debug \
              -DCMAKE_BUILD_TYPE:String=debug \
              -DINSTALL_LIB_DIR:Path=lib/oonf \
              -DINSTALL_INCLUDE_DIR:Path=include/oonf \
              -DINSTALL_CMAKE_DIR:Path=lib/oonf \

define Package/oonf-olsrd2-debug/template 
	SECTION:=net
	CATEGORY:=Network
	MAINTAINER:=Henning Rogge <hrogge@gmail.com>
	SUBMENU:=OLSR.org Routing Agent V2 (with debug symbols)
	URL:=http://www.olsr.org/
endef

define Package/oonf-olsrd2-debug
	$(call Package/oonf-olsrd2-debug/template)
	TITLE:=OLSRd V2 (debug symbols)
endef

TARGET_CFLAGS += -I$(STAGING_DIR)/usr/include -I${STAGING_DIR}/usr/include/libnl-tiny

define Package/oonf-olsrd2-debug/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(CP) \
		$(PKG_INSTALL_DIR)/usr/bin/olsrd2-debug \
		$(1)/usr/bin
endef

$(eval $(call BuildPackage,oonf-olsrd2))
