#include "myfs.h"
#include "klog.h"
#include <linux/pagemap.h>

#define log_tag "[myfs][inode] "

// 生成inode
struct inode *myfs_get_inode(struct super_block *sb, const struct inode *dir, int mode, dev_t dev) 
{
    struct inode *inode = new_inode(sb);  // 创建新的inode
    struct myfs_inode *my_inode;

    if (!inode)
        return NULL;

    // 为inode分配内存
    my_inode = kmalloc(sizeof(*my_inode), GFP_KERNEL);
    if (!my_inode) {
        iput(inode);  // 如果分配失败，释放inode
        return NULL;
    }

    inode->i_ino = get_next_ino();  // 获取唯一的inode编号
	inode_init_owner(&nop_mnt_idmap, inode, dir, mode);
	// 地址空间操作, 支持内存映射
    inode->i_mapping->a_ops = &ram_aops;  // 地址空间操作
	mapping_set_gfp_mask(inode->i_mapping, GFP_HIGHUSER);
	mapping_set_unevictable(inode->i_mapping);

	// 设置新创建的inode的时间戳
	simple_inode_init_ts(inode);

	switch (mode & S_IFMT) {
	default:
		init_special_inode(inode, mode, dev);
		break;
    case S_IFDIR:
        inode->i_op = &myfs_dir_inode_ops;  // 设置inode操作
        inode->i_fop = &myfs_dir_ops;  // 设置目录操作
        klogi(log_tag "new dir [%lu]", inode->i_ino)
		break;
	case S_IFREG:
        inode->i_op = &myfs_file_inode_ops;  // 设置inode操作
        inode->i_fop = &myfs_file_ops;  // 设置文件系统操作
        klogi(log_tag "new file [%lu]", inode->i_ino)
		break;
	case S_IFLNK:
		inode->i_op = &page_symlink_inode_operations;
		inode_nohighmem(inode);
		break;
    }

	// 自定义数据初始化
    my_inode->data = NULL;
	my_inode->len = 0;  
    inode->i_private = my_inode;  // 将自定义的myfs_inode关联到vfs_inode

    return inode;
}

/*
 * File creation. Allocate an inode, and we're done..
 */
/* SMP-safe */
static int
myfs_mknod(struct mnt_idmap *idmap, struct inode *dir,
	    struct dentry *dentry, umode_t mode, dev_t dev)
{
	struct inode * inode = myfs_get_inode(dir->i_sb, dir, mode, dev);
	int error = -ENOSPC;

	if (inode) {
		d_instantiate(dentry, inode);
		dget(dentry);	/* Extra count - pin the dentry in core */
		error = 0;
		inode_set_mtime_to_ts(dir, inode_set_ctime_current(dir));
	}
	return error;
}

static int myfs_mkdir(struct mnt_idmap *idmap, struct inode *dir,
		       struct dentry *dentry, umode_t mode)
{
	int retval = myfs_mknod(&nop_mnt_idmap, dir, dentry, mode | S_IFDIR, 0);
	if (!retval)
		inc_nlink(dir);
	return retval;
}

static int myfs_create(struct mnt_idmap *idmap, struct inode *dir,
			struct dentry *dentry, umode_t mode, bool excl)
{
	return myfs_mknod(&nop_mnt_idmap, dir, dentry, mode | S_IFREG, 0);
}

static int myfs_symlink(struct mnt_idmap *idmap, struct inode *dir,
			 struct dentry *dentry, const char *symname)
{
	struct inode *inode;
	int error = -ENOSPC;

	inode = myfs_get_inode(dir->i_sb, dir, S_IFLNK|S_IRWXUGO, 0);
	if (inode) {
		int l = strlen(symname)+1;
		error = page_symlink(inode, symname, l);
		if (!error) {
			d_instantiate(dentry, inode);
			dget(dentry);
			inode_set_mtime_to_ts(dir,
					      inode_set_ctime_current(dir));
		} else
			iput(inode);
	}
	return error;
}

static int myfs_tmpfile(struct mnt_idmap *idmap,
			 struct inode *dir, struct file *file, umode_t mode)
{
	struct inode *inode;

	inode = myfs_get_inode(dir->i_sb, dir, mode, 0);
	if (!inode)
		return -ENOSPC;
	d_tmpfile(file, inode);
	return finish_open_simple(file, 0);
}

// 文件的 inode 操作
struct inode_operations myfs_file_inode_ops = {
    .setattr = simple_setattr,
    .getattr = simple_getattr,
};

// 目录的inode操作
struct inode_operations myfs_dir_inode_ops = {
	.create		= myfs_create,
	.lookup		= simple_lookup,
	.link		= simple_link,
	.unlink		= simple_unlink,
	.symlink	= myfs_symlink,
	.mkdir		= myfs_mkdir,
	.rmdir		= simple_rmdir,
	.mknod		= myfs_mknod,
	.rename		= simple_rename,
	.tmpfile	= myfs_tmpfile,
};
