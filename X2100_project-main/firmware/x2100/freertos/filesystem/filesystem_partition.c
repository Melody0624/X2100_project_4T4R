
struct auto_mount_partition {
    char *part_name;
};


static struct auto_mount_partition parts_elm [] = {
    /* Partition0 */
    #ifdef CONFIG_DFS_ELMFAT_MOUNT_PARTITION0
    {
        CONFIG_DFS_ELMFAT_MOUNT_PARTITION0_NAME,
    },
    #endif

    /* Partition1 */
    #ifdef CONFIG_DFS_ELMFAT_MOUNT_PARTITION1
    {
        CONFIG_DFS_ELMFAT_MOUNT_PARTITION1_NAME,
    },
    #endif

    /* Partition2 */
    #ifdef CONFIG_DFS_ELMFAT_MOUNT_PARTITION2
    {
        CONFIG_DFS_ELMFAT_MOUNT_PARTITION2_NAME,
    },
    #endif

    /* Partition3 */
    #ifdef CONFIG_DFS_ELMFAT_MOUNT_PARTITION3
    {
        CONFIG_DFS_ELMFAT_MOUNT_PARTITION3_NAME,
    },
    #endif

    /* Partition4 */
    #ifdef CONFIG_DFS_ELMFAT_MOUNT_PARTITION4
    {
        CONFIG_DFS_ELMFAT_MOUNT_PARTITION4_NAME,
    },
    #endif

    /* Partition5 */
    #ifdef CONFIG_DFS_ELMFAT_MOUNT_PARTITION5
    {
        CONFIG_DFS_ELMFAT_MOUNT_PARTITION5_NAME,
    },
    #endif

    /* Partition6 */
    #ifdef CONFIG_DFS_ELMFAT_MOUNT_PARTITION6
    {
        CONFIG_DFS_ELMFAT_MOUNT_PARTITION6_NAME,
    },
    #endif

    /* Partition7 */
    #ifdef CONFIG_DFS_ELMFAT_MOUNT_PARTITION7
    {
        CONFIG_DFS_ELMFAT_MOUNT_PARTITION7_NAME,
    },
    #endif
};

static struct auto_mount_partition parts_uffs [] = {
    /* Partition0 */
    #ifdef CONFIG_DFS_UFFS_MOUNT_PARTITION0
    {
        CONFIG_DFS_UFFS_MOUNT_PARTITION0_NAME,
    },
    #endif

    /* Partition1 */
    #ifdef CONFIG_DFS_UFFS_MOUNT_PARTITION1
    {
        CONFIG_DFS_UFFS_MOUNT_PARTITION1_NAME,
    },
    #endif

    /* Partition2 */
    #ifdef CONFIG_DFS_UFFS_MOUNT_PARTITION2
    {
        CONFIG_DFS_UFFS_MOUNT_PARTITION2_NAME,
    },
    #endif

    /* Partition3 */
    #ifdef CONFIG_DFS_UFFS_MOUNT_PARTITION3
    {
        CONFIG_DFS_UFFS_MOUNT_PARTITION3_NAME,
    },
    #endif

    /* Partition4 */
    #ifdef CONFIG_DFS_UFFS_MOUNT_PARTITION4
    {
        CONFIG_DFS_UFFS_MOUNT_PARTITION4_NAME,
    },
    #endif

    /* Partition5 */
    #ifdef CONFIG_DFS_UFFS_MOUNT_PARTITION5
    {
        CONFIG_DFS_UFFS_MOUNT_PARTITION5_NAME,
    },
    #endif

    /* Partition6 */
    #ifdef CONFIG_DFS_UFFS_MOUNT_PARTITION6
    {
        CONFIG_DFS_UFFS_MOUNT_PARTITION6_NAME,
    },
    #endif

    /* Partition7 */
    #ifdef CONFIG_DFS_UFFS_MOUNT_PARTITION7
    {
        CONFIG_DFS_UFFS_MOUNT_PARTITION7_NAME,
    },
    #endif
};

static struct auto_mount_partition parts_yaffs [] = {
    /* Partition0 */
    #ifdef CONFIG_DFS_YAFFS_MOUNT_PARTITION0
    {
        CONFIG_DFS_YAFFS_MOUNT_PARTITION0_NAME,
    },
    #endif

    /* Partition1 */
    #ifdef CONFIG_DFS_YAFFS_MOUNT_PARTITION1
    {
        CONFIG_DFS_YAFFS_MOUNT_PARTITION1_NAME,
    },
    #endif

    /* Partition2 */
    #ifdef CONFIG_DFS_YAFFS_MOUNT_PARTITION2
    {
        CONFIG_DFS_YAFFS_MOUNT_PARTITION2_NAME,
    },
    #endif

    /* Partition3 */
    #ifdef CONFIG_DFS_YAFFS_MOUNT_PARTITION3
    {
        CONFIG_DFS_YAFFS_MOUNT_PARTITION3_NAME,
    },
    #endif

    /* Partition4 */
    #ifdef CONFIG_DFS_YAFFS_MOUNT_PARTITION4
    {
        CONFIG_DFS_YAFFS_MOUNT_PARTITION4_NAME,
    },
    #endif

    /* Partition5 */
    #ifdef CONFIG_DFS_YAFFS_MOUNT_PARTITION5
    {
        CONFIG_DFS_YAFFS_MOUNT_PARTITION5_NAME,
    },
    #endif

    /* Partition6 */
    #ifdef CONFIG_DFS_YAFFS_MOUNT_PARTITION6
    {
        CONFIG_DFS_YAFFS_MOUNT_PARTITION6_NAME,
    },
    #endif

    /* Partition7 */
    #ifdef CONFIG_DFS_YAFFS_MOUNT_PARTITION7
    {
        CONFIG_DFS_YAFFS_MOUNT_PARTITION7_NAME,
    },
    #endif
};
