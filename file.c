#include "myfs.h"
#include "klog.h"

#define log_tag "[file] "

static struct myfs_inode *get_myfs(struct file *file)
{
    if (!file || !file->f_inode) {
        return NULL;  // 检查输入的有效性
    }

    // 获取关联的 inode 的 i_private
    return (struct myfs_inode*)file->f_inode->i_private;
}

// 打开文件时的操作
int myfs_open(struct inode *inode, struct file *file) {
    printk(KERN_INFO "myfs: opening file\n");

    // 追加写入操作, 移动文件指针到文件末尾
    if (file->f_flags & O_APPEND){
        generic_file_llseek(file, 0, SEEK_END);
    }
    
    return 0;
}

// 读取文件内容
ssize_t myfs_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) 
{
    struct myfs_inode *my_inode = get_myfs(file);

    // 如果文件偏移量大于文件内容，返回0表示文件结束
    if (*ppos >= my_inode->len) 
        return 0;

    // 调整读取的字节数，避免越界
    if (*ppos + count > my_inode->len)
        count = my_inode->len - *ppos;

    // 将文件内容复制到用户空间
    if (copy_to_user(buf, my_inode->data + *ppos, count))
        return -EFAULT;

    *ppos += count;  // 更新文件偏移量
    return count;
}

// 写入数据到文件
ssize_t myfs_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {
    struct inode *inode = file->f_inode;
    struct myfs_inode *my_inode = get_myfs(file);
    char *new_data;
    size_t new_len = *ppos + count;

    klogi(log_tag "user call write: file=%pS, buf=%pK, buf_len=%ld, offset=%lld", file, buf, count, *ppos);

    // 重新分配内存
    new_data = krealloc(my_inode->data, new_len + 1, GFP_KERNEL);
    if (!new_data)
        return -ENOMEM;

    my_inode->data = new_data;
    my_inode->len = new_len;

    // 将用户空间的数据拷贝到内存
    if (copy_from_user(my_inode->data + *ppos, buf, count))
        return -EFAULT;

    my_inode->data[new_len] = '\0';  // 确保字符串结尾
    klogi(log_tag "copy from user and write to mem: file=%pS, buf=%s, count=%ld, offset=%lld", file, my_inode->data, count, *ppos);

    *ppos += count;  // 更新文件偏移量
    // 更新文件大小
    inode->i_size = max(inode->i_size, my_inode->len); 
    mark_inode_dirty(inode);
    klogi(log_tag "inode -> size =%lld", inode->i_size);

    return count;
}

// 释放文件时的操作
int myfs_release(struct inode *inode, struct file *file) {
    printk(KERN_INFO "myfs: closing file\n");
    return 0;
}

/**
 * myfs_mmu_get_unmapped_area - get an address range for mapping a file.
 * @file: the struct file to map
 * @addr: the starting address to map.  If this is 0, the kernel will pick one.
 * @len: the length of the mapping
 * @pgoff: the offset of the mapping in the file
 * @flags: the mapping flags.  See mmap(2) for a list of valid flags.
 *
 * This function should return an address range that the kernel can use to
 * map the file.  If the function returns 0, the kernel will pick an address.
 */
static unsigned long myfs_mmu_get_unmapped_area(struct file *file,
		unsigned long addr, unsigned long len, unsigned long pgoff,
		unsigned long flags)
{
	return current->mm->get_unmapped_area(file, addr, len, pgoff, flags);
}

// 定义文件操作
const struct file_operations myfs_file_ops = {
    .open = myfs_open,
    // 老版本的read, 更直观, 容易理解, 但是用这个实现的文件系统无法执行ELF文件, 
    // 因为内核使用kernel_read读取ELF文件, 而kernel_read依赖read_iter
    //.read = myfs_read,
    //.write = myfs_write,
    .release = myfs_release,
	.llseek		= generic_file_llseek,
    // 新版本的read, 更高效
    .read_iter	= generic_file_read_iter,
	.write_iter	= generic_file_write_iter,
    // mmap 操作
	.mmap		= generic_file_mmap,
	.get_unmapped_area	= myfs_mmu_get_unmapped_area,
    // splice 操作zero copy
	.splice_read	= filemap_splice_read,
	.splice_write	= iter_file_splice_write,
    .fsync		= noop_fsync,
};

