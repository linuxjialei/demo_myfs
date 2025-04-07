#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/mount.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "myfs.h"

// 超级块操作定义
struct super_operations myfs_super_ops = {
    .statfs = simple_statfs,  // 获取文件系统的状态
    .drop_inode = generic_delete_inode,  // 使用默认的drop_inode实现
};

// 填充超级块信息
static int myfs_fill_super(struct super_block *sb, void *data, int silent) {
    sb->s_magic = MYFS_MAGIC;  // 设置文件系统的魔数
    sb->s_op = &myfs_super_ops;  // 设置超级块操作
    sb->s_fs_info = NULL;  // 没有额外的文件系统信息

    // 创建根目录inode，并将其设置为超级块的根目录
    struct inode *root_inode = myfs_get_inode(sb, NULL, S_IFDIR | 0755, 0);
    if (!root_inode)
        return -ENOMEM;  // 内存分配失败

    sb->s_root = d_make_root(root_inode);  // 创建根目录的dentry
    if (!sb->s_root) {
        iput(root_inode);  // 如果创建失败，释放root_inode
        return -ENOMEM;
    }

    return 0;
}

// 挂载文件系统
static struct dentry *myfs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data) {
    return mount_nodev(fs_type, flags, data, myfs_fill_super);  // 使用无设备挂载
}

// 卸载文件系统时的操作
static void myfs_kill_super(struct super_block *sb) {
    pr_info("myfs: superblock is being killed\n");
}

// 文件系统类型定义
static struct file_system_type myfs_type = {
    .name = "myfs",  // 文件系统名称
    .mount = myfs_mount,  // 挂载函数
    .kill_sb = myfs_kill_super,  // 卸载函数
    .owner = THIS_MODULE,  // 模块所有者
};

// 初始化文件系统模块
static int __init myfs_init(void) {
    int ret = register_filesystem(&myfs_type);  // 注册文件系统
    if (ret)
        printk(KERN_ERR "myfs: unable to register myfs\n");
    else
        printk(KERN_INFO "myfs: myfs registered\n");

    return ret;
}

// 清理文件系统模块
static void __exit myfs_exit(void) {
    unregister_filesystem(&myfs_type);  // 卸载文件系统
    printk(KERN_INFO "myfs: myfs unregistered\n");
}

// 模块入口和出口
module_init(myfs_init);
module_exit(myfs_exit);

// 模块信息
MODULE_LICENSE("GPL");
MODULE_AUTHOR("kevin");
MODULE_DESCRIPTION("A simple in-memory filesystem");
