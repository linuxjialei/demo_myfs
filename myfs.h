#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/mount.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define MYFS_MAGIC 0x12345678  // 定义我们的文件系统魔数，用于标识文件系统

// 自定义的 inode 结构体，包含文件内容
struct myfs_inode {
    char *data;     // 文件内容
    int len;        // 文件长度
};

// 打开文件时的操作
extern int myfs_open(struct inode *inode, struct file *file);
// 读取文件内容
extern ssize_t myfs_read(struct file *file, char __user *buf, size_t count, loff_t *ppos);
// 写入数据到文件
extern ssize_t myfs_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos);
// 释放文件时的操作
extern int myfs_release(struct inode *inode, struct file *file);

// 定义文件操作
extern const struct file_operations myfs_file_ops;

// 定义目录操作
extern const struct file_operations myfs_dir_ops;

// inode 操作定义
extern struct inode_operations myfs_file_inode_ops;
extern struct inode_operations myfs_dir_inode_ops;

// 超级块操作定义
extern struct super_operations myfs_super_ops;
// 获取inode信息
extern struct inode *myfs_get_inode(struct super_block *sb, const struct inode *dir, int mode, dev_t dev);
