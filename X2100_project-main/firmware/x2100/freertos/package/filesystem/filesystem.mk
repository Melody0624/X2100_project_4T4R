CFLAGS += -Ifilesystem/include -DDFS_USING_WORKDIR

#-------------------------------------------------------
package_name = filesystem
package_depends =
package_builtin_src = filesystem/
package_make_hook =
package_init_hook =
package_finalize_hook =
package_clean_hook = clean_output_tools
#-------------------------------------------------------

package-$(CONFIG_DFS) += package/filesystem/flash/flash.mk
package-$(CONFIG_DFS_ELMFAT) += package/filesystem/elmfat/elmfat.mk
package-$(CONFIG_DFS_DEBUGFS) += package/filesystem/debugfs/debugfs.mk
package-$(CONFIG_DFS_UFFS) += package/filesystem/uffs/uffs.mk
package-$(CONFIG_DFS_YAFFS) += package/filesystem/yaffs/yaffs.mk

DFS_TOOLS_DIR := $(TOPDIR)filesystem/output_tools/
define clean_output_tools
	rm -rf $(DFS_TOOLS_DIR)
endef