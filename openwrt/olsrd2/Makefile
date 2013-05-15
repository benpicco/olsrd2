include $(TOPDIR)/rules.mk

PKG_NAME:=oonf-olsrd2
PKG_VERSION:=2013-05-15
PKG_RELEASE=$(PKG_SOURCE_VERSION)

CMAKE_INSTALL:=1

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

CMAKE_SOURCE_DIR=$(CURDIR)/../../
CMAKE_OPTIONS=-DOONF_DO_NOT_REGISTER_PACKAGE:Bool=true \
              -DOONF_LOGGING_LEVEL:String=warn \
              -DCMAKE_BUILD_TYPE:String=MinSizeRel \
              -DINSTALL_LIB_DIR:Path=lib/oonf \
              -DINSTALL_INCLUDE_DIR:Path=include/oonf \
              -DINSTALL_CMAKE_DIR:Path=lib/oonf \
              -DOONF_LIBNL:Path=nl-tiny

define Package/oonf-olsrd2/template 
	SECTION:=net
	CATEGORY:=Network
	MAINTAINER:=Henning Rogge <hrogge@gmail.com>
	SUBMENU:=OLSR.org Routing Agent V2
	URL:=http://www.olsr.org/
endef

define Package/oonf-olsrd2
	$(call Package/oonf-olsrd2/template)
	TITLE:=OLSRd V2
endef

TARGET_CFLAGS += -I$(STAGING_DIR)/usr/include -I${STAGING_DIR}/usr/include/libnl-tiny

define Package/oonf-olsrd2/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(CP) \
		$(PKG_INSTALL_DIR)/usr/bin/olsrd2 \
		$(1)/usr/bin
endef

$(eval $(call BuildPackage,oonf-olsrd2))
