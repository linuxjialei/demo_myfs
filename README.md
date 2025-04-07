# Linux内核文件系统入门--自己实现一个内存文件系统
非原创，转载自https://gitcode.com/weixin_47763623/myfs，感谢这位大佬，侵删

## 1. 概述

大家好，今天我们要做点有趣的事情——实现一个简单的内存文件系统。在传统的磁盘文件系统、网络文件系统之外，内存文件系统凭借其速度快、无持久化等特点有着独特的应用场景。通过 `VFS` 的支持，创建一个内存文件系统变得相对容易。只需要完成三步：

1. 注册一个文件系统类型。
2. 实现 `inode` 操作。
3. 实现文件操作。

接下来，咱们一步步实现。

---


## 2. 文件系统类型注册

实现一个内存文件系统的第一步是将它注册为内核中的一个文件系统类型。文件系统的注册入口是 `file_system_type` 结构体。它提供了文件系统的基本信息和操作入口，使得内核可以识别并使用我们的文件系统。接下来，我们将一步步讲解如何定义和注册这个结构。

### 2.1 定义和注册 `file_system_type`

首先，我们定义一个名为 `myfs` 的文件系统类型，并注册它：

```c
// 文件系统类型定义
static struct file_system_type myfs_type = {
    .name = "myfs",  // 文件系统名称
    .mount = myfs_mount,  // 挂载函数，定义如何在挂载点安装文件系统（需要自己实现）
    .kill_sb = myfs_kill_super,  // 卸载函数，定义如何释放文件系统资源（需要自己实现）
    .owner = THIS_MODULE,  // 指定模块所有者
};

// 初始化文件系统模块
static int __init myfs_init(void) {
    int ret = register_filesystem(&myfs_type);  // 注册文件系统（VFS 提供）
    if (ret)
        printk(KERN_ERR "myfs: unable to register myfs\n");  // 注册失败时打印错误信息
    else
        printk(KERN_INFO "myfs: myfs registered\n");  // 成功注册时打印信息

    return ret;
}

// 退出文件系统模块
static void __exit myfs_exit(void) {
    int ret = unregister_filesystem(&myfs_type);  // 注销文件系统（VFS 提供）
    if (ret)
        printk(KERN_ERR "myfs: unable to unregister myfs\n");
}
```

### 解释

- `myfs_type` 是一个 `file_system_type` 结构体，描述了文件系统的名称、挂载和卸载行为。
  - `.name` 用于指定文件系统的名字，在挂载文件系统时可以用它来识别。
  - `.mount` 指向挂载函数 `myfs_mount`，定义文件系统如何被挂载。
  - `.kill_sb` 指向卸载函数 `myfs_kill_super`，用于释放文件系统的资源。
  - `.owner` 表示所属模块，用于内核模块的引用计数管理。

- `register_filesystem` 是将文件系统注册到内核的关键函数（VFS 提供）。成功注册后，内核就能够识别和处理我们定义的 `myfs` 文件系统。

### 2.2 填充超级块

文件系统的核心之一是 `super_block`，它管理着文件系统的全局状态信息，并充当根目录的 `inode` 容器。实现文件系统时，我们需要提供一个函数来填充 `super_block` 的各个字段。下面是具体实现：

```c
// 填充超级块信息
static int myfs_fill_super(struct super_block *sb, void *data, int silent) {
    sb->s_magic = MYFS_MAGIC;  // 设置文件系统的魔数，用于标识文件系统类型
    sb->s_op = &myfs_super_ops;  // 指定超级块的操作集合
    sb->s_fs_info = NULL;  // 额外的文件系统信息，可以为空

    // 创建根目录的 inode
    struct inode *root_inode = myfs_get_inode(sb, NULL, S_IFDIR | 0755, 0);  // 需要自己实现
    if (!root_inode)
        return -ENOMEM;  // 如果内存分配失败，返回错误

    // 将根目录 inode 转换为 dentry 并设置为超级块的根
    sb->s_root = d_make_root(root_inode);  // VFS 提供
    if (!sb->s_root) {
        iput(root_inode);  // 释放已分配的 root_inode 资源
        return -ENOMEM;
    }

    return 0;
}
```

### 解释

- `sb->s_magic`：这是一个常量值（通常是一个整数），用来唯一标识我们的文件系统类型。不同文件系统通常会使用不同的魔数来区分类型。
- `sb->s_op`：指向 `super_operations` 结构体，描述了超级块的操作接口。
- `myfs_get_inode`：创建一个新的 `inode`，代表根目录。它的模式是 `S_IFDIR | 0755`，表示一个目录，并赋予读、写、执行权限。
- `d_make_root`：将 `inode` 转换为根目录的 `dentry`，并与超级块的 `s_root` 关联。`dentry` 是内核用于管理目录项的结构体。

通过以上步骤，我们完成了文件系统的注册和超级块的填充。`super_block` 和 `inode` 是文件系统的基础结构，确保文件系统能够被正确地挂载和访问。至此，我们已经为创建一个可用的内存文件系统打下了坚实的基础。

---

### 2.3 获取根 `inode`

- 在填充超级块时, 一个很重要的操作就是生成根 `inode`, 它承载着文件系统的起点。
- 根 `inode` 是整个文件系统层次结构的根节点，它与超级块紧密相连，提供了文件系统管理和访问的入口。
- 根 `inode`也是通过我们定义的 `myfs_get_inode` 函数生成的, 其他所有的inode都是通过这个函数生成的。

#### `myfs_get_inode` 函数实现

```c
struct inode *myfs_get_inode(struct super_block *sb, const struct inode *dir, int mode, dev_t dev) {
    struct inode *inode = new_inode(sb);  // 创建新的 inode（VFS 提供）
    struct myfs_inode *my_inode;

    if (!inode)
        return NULL;

    // 为自定义的 inode 结构分配内存
    my_inode = kmalloc(sizeof(*my_inode), GFP_KERNEL);
    if (!my_inode) {
        iput(inode);  // 如果分配失败，释放 inode
        return NULL;
    }

    // 为 inode 赋予唯一编号
    inode->i_ino = get_next_ino();  // 获取系统唯一的 inode 编号
    inode_init_owner(&nop_mnt_idmap, inode, dir, mode);  // 初始化 inode 所有者信息
    
    // 配置地址空间操作
    inode->i_mapping->a_ops = &ram_aops;  // 设置地址空间操作接口
    mapping_set_gfp_mask(inode->i_mapping, GFP_HIGHUSER);  // 设置内存分配策略
    mapping_set_unevictable(inode->i_mapping);  // 标记地址空间不可换出

    // 根据 inode 模式，初始化不同类型的文件
    switch (mode & S_IFMT) {
        default:
            init_special_inode(inode, mode, dev);  // 特殊文件的处理（VFS 提供）
            break;
        case S_IFDIR:
            inode->i_op = &myfs_dir_inode_ops;  // 目录的 inode 操作（需要自己实现）
            inode->i_fop = &myfs_dir_ops;       // 目录的文件操作（需要自己实现）
            break;
        case S_IFREG:
            inode->i_op = &myfs_file_inode_ops;  // 常规文件的 inode 操作（需要自己实现）
            inode->i_fop = &myfs_file_ops;       // 常规文件的文件操作（需要自己实现）
            break;
        case S_IFLNK:
            inode->i_op = &page_symlink_inode_operations;  // 符号链接的 inode 操作（VFS 提供）
            inode_nohighmem(inode);  // 设置内存使用限制（VFS 提供）
            break;
    }

    // 初始化自定义 inode 数据
    my_inode->data = NULL;
    my_inode->len = 0;
    inode->i_private = my_inode;  // 将自定义的 myfs_inode 关联到 vfs_inode 结构中

    return inode;
}
```

#### 函数详解

1. **`new_inode(sb)` 创建新 `inode`**：函数首先调用 `new_inode` 来分配一个新的 `inode` 结构体。这个结构体由内核管理，并与超级块 `sb` 关联。

2. **分配自定义数据结构 `my_inode`**：为了存储我们自定义的数据，我们分配了一个 `myfs_inode` 结构体。这个结构体可以扩展 `inode` 的功能，例如存储文件的元数据或私有信息。

3. **设置 `inode` 的属性**： 
   - `inode->i_ino` 赋予唯一编号，这是文件系统中每个文件的唯一标识符。
   - `inode_init_owner` 初始化 `inode` 的所有者信息。
   - 配置 `inode` 的地址空间操作 `inode->i_mapping->a_ops`，设置 `GFP` 内存分配策略并标记为不可换出。对于内存文件系统，这些配置有助于高效地管理文件数据在内存中的存储。

4. **根据文件类型设置操作**：
   - 使用 `switch` 语句，根据传入的 `mode`（文件类型），设置不同的 `inode` 和文件操作。
   - `S_IFDIR` 表示目录，设置 `inode` 和目录操作 `myfs_dir_inode_ops`  `myfs_dir_ops`。
   - `S_IFREG` 表示普通文件，设置相应的操作 `myfs_file_inode_ops` `myfs_file_ops`。
   - `S_IFLNK` 处理符号链接。

5. **自定义数据的初始化和关联**：`my_inode` 结构体被初始化并与 `inode->i_private` 关联，允许我们在 `inode` 结构中存储自定义数据。

通过 `myfs_get_inode` 函数，我们实现了根 `inode` 的生成与配置，确保文件系统结构的起始节点能够正常运行。根 `inode` 的管理至关重要，因为它承载了整个文件系统树的结构和访问入口。

---

## 3. `inode` 操作实现

接下来，我们需要为 `inode` 实现各种操作，包括创建(文件/目录)、删除、查找等。
- 这些操作将定义在 `inode_operations` 结构体中。也就是上文提到的`myfs_dir_inode_ops`和`myfs_file_inode_ops`
- 这些操作用于管理文件系统中的目录项，例如创建目录、创建文件、符号链接等
- 不管是目录还是文件, 在文件系统中都是通过一个`inode`来代表的

### 3.1 `inode` 目录操作
  
```c
static int myfs_mknod(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev) {
    struct inode *inode = myfs_get_inode(dir->i_sb, dir, mode, dev);
    int error = -ENOSPC;

    if (inode) {
        d_instantiate(dentry, inode);
        dget(dentry);  // 额外引用计数，确保 dentry 在内存中
        error = 0;
        inode_set_mtime_to_ts(dir, inode_set_ctime_current(dir));
    }
    return error;
}

static int myfs_mkdir(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode) {
    int retval = myfs_mknod(&nop_mnt_idmap, dir, dentry, mode | S_IFDIR, 0);
    if (!retval)
        inc_nlink(dir);
    return retval;
}

static int myfs_create(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl) {
    return myfs_mknod(&nop_mnt_idmap, dir, dentry, mode | S_IFREG, 0);
}

static int myfs_symlink(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, const char *symname) {
    struct inode *inode;
    int error = -ENOSPC;

    inode = myfs_get_inode(dir->i_sb, dir, S_IFLNK | S_IRWXUGO, 0);
    if (inode) {
        int l = strlen(symname) + 1;
        error = page_symlink(inode, symname, l);
        if (!error) {
            d_instantiate(dentry, inode);
            dget(dentry);
            inode_set_mtime_to_ts(dir, inode_set_ctime_current(dir));
        } else {
            iput(inode);
        }
    }
    return error;
}

struct inode_operations myfs_dir_inode_ops = {
    .create     = myfs_create,
    .lookup     = simple_lookup, // （VFS 提供）
    .link       = simple_link,
    .unlink     = simple_unlink,
    .symlink    = myfs_symlink,
    .mkdir      = myfs_mkdir,
    .rmdir      = simple_rmdir,
    .mknod      = myfs_mknod,
    .rename     = simple_rename,
    .tmpfile    = myfs_tmpfile,
};
```

- 这些函数的功能大同小异, 都是通过`myfs_get_inode`创建一个新的inode然后设置相应的属性即可
- `simple_`开头的都是`vfs`提供的库函数, 可以直接使用

### 3.2 `inode` 文件操作

- 注意, 这里不是针对文件内容的读写操作, 而是针对inode(也就是文件元数据)的操作
- 这里只需要实现文件属性的设置和获取方法即可

```c
struct inode_operations myfs_file_inode_ops = {
    .setattr = simple_setattr,  // VFS 提供
    .getattr = simple_getattr,  // VFS 提供
};
```

---

## 4. 目录和文件操作

`file_operations` 结构体定义了具体的文件和目录操作接口，负责文件的打开、读写、关闭等基本操作。在实现我们的内存文件系统中，我们需要为目录和文件分别提供操作方法。

### 4.1 目录操作

- 目录的 `file_operations` 结构通常需要支持打开、读取等操作。
- 我们可以使用内核`vfs`提供的通用方法，简化目录操作的实现：

```c
// 定义目录操作
const struct file_operations myfs_dir_ops = {
    .open       = dcache_dir_open,    // 打开目录（VFS 提供）
    .release    = dcache_dir_close,   // 关闭目录（VFS 提供）
    .llseek     = dcache_dir_lseek,   // 目录文件定位（VFS 提供）
    .read       = generic_read_dir,   // 读取目录（VFS 提供）
    .iterate_shared = dcache_readdir, // 遍历目录（VFS 提供）
    .fsync      = noop_fsync,         // 无需同步（VFS 提供）
};
```

- 这里我们主要依赖内核中现成的操作实现，例如 `dcache_dir_open` 和 `generic_read_dir`，它们处理了绝大部分目录相关的操作逻辑。
- 这简化了我们对目录操作的实现，重点放在文件系统逻辑而非实现每个细节上。

### 4.2 文件操作

相比于目录，文件操作确实更加复杂，因为它不仅涉及到文件的打开、读写等基本操作，还需要支持如文件映射这样的高级功能。为了确保文件系统的灵活性与性能，Linux 内核提供了丰富的 API 用于文件操作。接下来，我们将深入探讨 `file_operations` 结构体，并解释其中的关键成员。

#### 文件操作结构体

在 Linux 内核中，文件操作是由 `file_operations` 结构体来定义的。这个结构体包含了指向各种文件操作函数的指针，这些函数负责实现文件的各种行为。以下是 `myfs` 文件系统的 `file_operations` 结构体的一个示例：

```c
// 定义文件操作
const struct file_operations myfs_file_ops = {
    .open = myfs_open,                  // 文件打开
    // 旧版的 read/write 接口
    //.read = myfs_read,                // 文件读取（需要自己实现）
    //.write = myfs_write,              // 文件写入（需要自己实现）
    .release = myfs_release,           // 释放文件
    .llseek = generic_file_llseek,     // 文件定位
    .read_iter = generic_file_read_iter,   // 新版的高效读（VFS 提供）
    .write_iter = generic_file_write_iter, // 新版的高效写（VFS 提供）
    .mmap = generic_file_mmap,         // 文件内存映射（VFS 提供）
    .get_unmapped_area = myfs_mmu_get_unmapped_area, // 获取未映射内存区域
    .splice_read = filemap_splice_read,   // 零拷贝读操作（VFS 提供）
    .splice_write = iter_file_splice_write, // 零拷贝写操作（VFS 提供）
    .fsync = noop_fsync,               // 无需同步（VFS 提供）
};
```

#### 关键成员解析

- **`.open`**: 当用户尝试打开一个文件时，会调用此函数。它负责初始化任何必要的数据结构，并为后续的操作做准备。
  
- **`.read` 和 `.write`**: 这些是传统的读写接口，它们允许直接从文件中读取数据或将数据写入文件。尽管它们仍然可用，但通常建议使用新的迭代器接口以获得更好的性能。
  
- **`.release`**: 当文件关闭时调用，用于清理 `.open` 中设置的资源。
  
- **`.llseek`**: 允许文件内的位置指针移动到一个新的位置，这对于非顺序访问文件非常有用。
  
- **`.read_iter` 和 `.write_iter`**: 这些是新版的读写方法，它们使用了迭代器模型，这使得它们能够更高效地处理大文件或需要分段处理的情况。
  
- **`.mmap`**: 通过将文件映射到内存中，应用程序可以直接通过内存地址访问文件内容，这种方式对于频繁的数据访问特别有效。
  
- **`.get_unmapped_area`**: 该函数用于查找一个适合内存映射的地址空间，对于实现高效的 `mmap` 功能至关重要。
  
- **`.splice_read` 和 `.splice_write`**: 这些函数实现了零拷贝操作，允许数据直接从一个文件描述符传输到另一个，无需经过用户空间的缓冲区。
  
- **`.fsync`**: 用于确保所有缓存中的数据已经写入磁盘，这里使用了一个空操作，因为某些文件系统可能不需要显式的同步操作。

#### 选择合适的读写接口

- 正如上面提到的，`file_operations` 结构体支持两种不同的读写接口：旧版的 `.read` 和 `.write`，以及新版的 `.read_iter` 和 `.write_iter`。虽然旧版接口逻辑更为简单，易于理解和实现，但新版接口利用了迭代器模型，能够更好地处理大数据量的传输，因此推荐在现代应用中使用新版接口。
- 并且, 如果使用旧版的文件读写接口, 则文件系统无法执行`ELF`文件,  因为内核使用`kernel_read`函数读取ELF文件, 而`kernel_read`函数内部通过`read_iter`读取文件

---

## 5. 测试与总结

### 5.1 测试文件系统

要测试我们的内存文件系统，可以通过简单的加载和挂载操作：

```bash
# 将模块插入内核
insmod myfs.ko

# 创建挂载点
mkdir /mnt/myfs

# 挂载文件系统
mount -t myfs none /mnt/myfs

# 在挂载点上创建目录和文件，测试读写
mkdir /mnt/myfs/testdir
echo "Hello, MyFS!" > /mnt/myfs/testfile
cat /mnt/myfs/testfile
```

在执行这些命令时，我们可以通过 `dmesg` 查看内核日志，跟踪文件系统的运行状态。

### 5.2 总结

通过以上实现，我们已经成功构建了一个简单的内存文件系统 `myfs`。从注册文件系统类型到实现 `inode` 和文件操作，我们了解了如何利用内核提供的 `VFS` 接口构建自己的文件系统。这不仅帮助我们更深入地理解 Linux 内核的文件系统机制，还为未来更复杂的文件系统开发奠定了基础。

玩转内核文件系统并不容易，但只要一步步走来，乐趣无穷。祝各位玩得开心，期待你们更酷的文件系统实现！
