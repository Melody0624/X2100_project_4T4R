#-------------------------------------------------------
package_name = yaffs
package_depends = filesystem
package_builtin_src = filesystem/yaffs/
package_make_hook = build_yaffs_utils
package_init_hook =
package_finalize_hook =
package_clean_hook = clean_yaffs_utils
#-------------------------------------------------------

YAFFS_TOOLS_ENV := env -u CC -u CROSS -u CFLAGS -u LDFLAGS -u AR -u LD -u CXX
YAFFS_TOOLS_DIR := $(TOPDIR)filesystem/yaffs/yaffs/utils/

ifeq ($(CONFIG_DFS_YAFFS_UTILS), y)
define build_yaffs_utils
	$(YAFFS_TOOLS_ENV) $(MAKE) -C $(YAFFS_TOOLS_DIR)
	mkdir -p $(DFS_TOOLS_DIR)
	cp $(YAFFS_TOOLS_DIR)mkyaffs2 $(DFS_TOOLS_DIR)
	cp $(YAFFS_TOOLS_DIR)unyaffs2 $(DFS_TOOLS_DIR)
	cp $(YAFFS_TOOLS_DIR)unspare2 $(DFS_TOOLS_DIR)
endef

define clean_yaffs_utils
	$(YAFFS_TOOLS_ENV) $(MAKE) -C $(YAFFS_TOOLS_DIR) distclean || true
	rm -rf $(DFS_TOOLS_DIR)mkyaffs2
	rm -rf $(DFS_TOOLS_DIR)unyaffs2
	rm -rf $(DFS_TOOLS_DIR)unspare2
endef
endif
