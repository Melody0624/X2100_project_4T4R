#ifndef _FILESYSTEM_HOTPLUG_H_
#define _FILESYSTEM_HOTPLUG_H_

#ifdef CONFIG_DFS_HOTPLUG

int file_system_root_path_is_valid(void);
int file_system_insert_partition(const char *name);
int file_system_remove_partition(const char *name);

#endif

#endif /* _FILESYSTEM_HOTPLUG_H_ */