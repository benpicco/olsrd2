include $(TOPDIR)/rules.mk

PKG_NAME:=olsrd2-git
PKG_VERSION:=2013-05-17
PKG_RELEASE=$(PKG_SOURCE_VERSION)
#PKG_REV:=d8f9b60b99e335e865bc3f8d6dedbab757a48ff2
#PKG_RELEASE:=$(PKG_REV)

#PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.bz2
#PKG_SOURCE_URL:=http://olsr.org/git/olsrd2.git
#PKG_SOURCE_PROTO:=git
#PKG_SOURCE_SUBDIR:=$(PKG_NAME)-$(PKG_VERSION)
#PKG_SOURCE_VERSION:=$(PKG_REV)
CMAKE_INSTALL:=1

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

CMAKE_SOURCE_DIR=$(CURDIR)/../../
CMAKE_OPTIONS=-DOONF_DO_NOT_REGISTER_PACKAGE:Bool=true \
              -DOONF_LOGGING_LEVEL:String=debug \
              -DCMAKE_BUILD_TYPE:String=MinSizeRel \
              -DINSTALL_LIB_DIR:Path=lib/oonf \
              -DINSTALL_INCLUDE_DIR:Path=include/oonf \
              -DINSTALL_CMAKE_DIR:Path=lib/oonf

define Package/olsrd2-git/template 
	SECTION:=net
	CATEGORY:=Network
	MAINTAINER:=Henning Rogge <hrogge@gmail.com>
	SUBMENU:=OLSR.org Routing Agent V2
	URL:=http://www.olsr.org/
endef

define Package/olsrd2-git
	$(call Package/olsrd2-git/template)
	TITLE:=OLSRd V2 Routing Agent
	DEPENDS:=+oonf-core-api +oonf-api-subsystems +ldconfig
endef

define Package/olsrd2-git-plugin-ff-ett
	$(call Package/olsrd2-git/template)
	TITLE:=Freifunk ETT metric
	DEPENDS:=+olsrd2-git +oonf-core-api +oonf-api-subsystems +ldconfig
endef

define Package/olsrd2-git-plugin-ff-etx
	$(call Package/olsrd2-git/template)
	TITLE:=Freifunk ETX metric
	DEPENDS:=+olsrd2-git +oonf-core-api +oonf-api-subsystems +ldconfig
endef

define Package/olsrd2-git-plugin-hysteresis-olsrv1
	$(call Package/olsrd2-git/template)
	TITLE:=Port of OLSRv1 hysteresis
	DEPENDS:=+olsrd2-git +oonf-core-api +oonf-api-subsystems +ldconfig
endef

define Package/olsrd2-git-plugin-neighbor-probing
	$(call Package/olsrd2-git/template)
	TITLE:=Neighbor probing plugin
	DEPENDS:=+olsrd2-git +oonf-core-api +oonf-api-subsystems +ldconfig
endef

define Package/olsrd2-git-plugin-nhdpcheck
	$(call Package/olsrd2-git/template)
	TITLE:=More strict checking of incoming NHDP messages
	DEPENDS:=+olsrd2-git +oonf-core-api +oonf-api-subsystems +ldconfig
endef

TARGET_CFLAGS += -I$(STAGING_DIR)/usr/include

define Package/olsrd2-git/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(CP) \
		$(PKG_INSTALL_DIR)/usr/bin/olsrd2 \
		$(1)/usr/bin
endef

define Package/olsrd2-git-plugin-ff-ett/install
	$(INSTALL_DIR) $(1)/usr/lib/oonf
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/oonf/libolsrd2_ff_ett*.so* $(1)/usr/lib/oonf
endef

define Package/olsrd2-git-plugin-ff-etx/install
	$(INSTALL_DIR) $(1)/usr/lib/oonf
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/oonf/libolsrd2_ff_etx*.so* $(1)/usr/lib/oonf
endef

define Package/olsrd2-git-plugin-hysteresis-olsrv1/install
	$(INSTALL_DIR) $(1)/usr/lib/oonf
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/oonf/libolsrd2_hysteresis_olsrv1*.so* $(1)/usr/lib/oonf
endef

define Package/olsrd2-git-plugin-neighbor-probing/install
	$(INSTALL_DIR) $(1)/usr/lib/oonf
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/oonf/libolsrd2_neighbor_probing*.so* $(1)/usr/lib/oonf
endef

define Package/olsrd2-git-plugin-nhdpcheck/install
	$(INSTALL_DIR) $(1)/usr/lib/oonf
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/oonf/libolsrd2_nhdpcheck*.so* $(1)/usr/lib/oonf
endef

define Package/olsrd2-git-core/postinst
#!/bin/sh
if [ -z "$${IPKG_INSTROOT}" ]; then
	echo "Adding /usr/lib/oonf to ld.so.conf ..."
	echo /usr/lib/oonf >> /etc/ld.so.conf
	echo "Running ldconfig ..."
	/sbin/ldconfig 2> /dev/null
fi
exit 0
endef

define Package/olsrd2-git-plugin-ff-ett/postinst
#!/bin/sh
if [ -z "$${IPKG_INSTROOT}" ]; then
	echo "Running ldconfig ..."
	/sbin/ldconfig 2> /dev/null
fi
exit 0
endef

define Package/olsrd2-git-plugin-ff-etx/postinst
#!/bin/sh
if [ -z "$${IPKG_INSTROOT}" ]; then
	echo "Running ldconfig ..."
	/sbin/ldconfig 2> /dev/null
fi
exit 0
endef

define Package/olsrd2-git-plugin-hysteresis-olsrv1/postinst
#!/bin/sh
if [ -z "$${IPKG_INSTROOT}" ]; then
	echo "Running ldconfig ..."
	/sbin/ldconfig 2> /dev/null
fi
exit 0
endef

define Package/olsrd2-git-plugin-neighbor-probing/postinst
#!/bin/sh
if [ -z "$${IPKG_INSTROOT}" ]; then
	echo "Running ldconfig ..."
	/sbin/ldconfig 2> /dev/null
fi
exit 0
endef

define Package/olsrd2-git-plugin-nhdpcheck/postinst
#!/bin/sh
if [ -z "$${IPKG_INSTROOT}" ]; then
	echo "Running ldconfig ..."
	/sbin/ldconfig 2> /dev/null
fi
exit 0
endef

$(eval $(call BuildPackage,olsrd2-git))
$(eval $(call BuildPackage,olsrd2-git-plugin-ff-ett))
$(eval $(call BuildPackage,olsrd2-git-plugin-ff-etx))
$(eval $(call BuildPackage,olsrd2-git-plugin-hysteresis-olsrv1))
$(eval $(call BuildPackage,olsrd2-git-plugin-neighbor-probing))
$(eval $(call BuildPackage,olsrd2-git-plugin-nhdpcheck))
