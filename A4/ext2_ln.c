/*
	ext2_ln: This program takes three command line arguments. 
	The first is the name of an ext2 formatted virtual disk. 
	The other two are absolute paths on your ext2 formatted disk. 
	The program should work like ln, creating a link from the first specified file to the second specified path. 
	This program should handle any exceptional circumstances, 
	for example: if the source file does not exist (ENOENT),
	if the link name already exists (EEXIST), 
	if a hardlink refers to a directory (EISDIR), etc. 
	then your program should return the appropriate error code. 
	Additionally, this command may take a "-s" flag, after the disk image argument. 
	When this flag is used, your program must create a symlink instead (other arguments remain the same). 
	Note:
		For symbolic links, you will see that the specs mention that if the path is short enough, it can be stored in the inode in the space that would otherwise be occupied by block pointers - these are called fast symlinks. Do *not* implement fast symlinks, just store the path in a data block regardless of length, to keep things uniform in your implementation, and to facilitate testing. If in doubt about correct operation of links, use the ext2 specs and ask on the discussion board.
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include "ext2.h"
#include "ext2_utils.h"

unsigned char *disk;

void hard_link(struct ext2_super_block *sb, struct ext2_group_desc *gd, 
												struct ext2_dir_entry * src_file_dir_entry, 
												int dest_inode_num,
												const char * dest_name){

	
	// find dest dir inode
	int len = 1024 + (gd->bg_inode_table-1)*EXT2_BLOCK_SIZE + (dest_inode_num -1)*sb->s_inode_size;
	struct ext2_inode * dest_ei = (struct ext2_inode *) (disk + len); 
	
	// find src last file inode
	len = 1024 + (gd->bg_inode_table-1)*EXT2_BLOCK_SIZE + (src_file_dir_entry->inode -1)*sb->s_inode_size;
	struct ext2_inode * src_last_file_ei = (struct ext2_inode *) (disk + len); 

	// dest dir add new  dir entry
	struct ext2_dir_entry * dest_file_dir_entry;
	int last_len;
	dest_file_dir_entry = last_file_dir_entry(dest_ei, &last_len);	
	
	dest_file_dir_entry->rec_len = sizeof(struct ext2_dir_entry) + \
							 dest_file_dir_entry->name_len + \
							 (4-((sizeof(struct ext2_dir_entry) + dest_file_dir_entry->name_len) % 4));
	struct ext2_dir_entry * new_ed = (struct ext2_dir_entry *)(((unsigned char *) dest_file_dir_entry) + \
											dest_file_dir_entry->rec_len);
	last_len += dest_file_dir_entry->rec_len;
	new_ed->inode = src_file_dir_entry->inode;
	new_ed->name_len = strlen(dest_name);
	new_ed->file_type = src_file_dir_entry->file_type;
	new_ed->rec_len = EXT2_BLOCK_SIZE - last_len;
	memcpy((char *) ((unsigned char *)new_ed + sizeof(struct ext2_dir_entry)), dest_name, strlen(dest_name));

	//set src file inode link count
	src_last_file_ei->i_links_count ++;
	
}


void symbol_like(struct ext2_super_block *sb, struct ext2_group_desc *gd, 
												int dest_inode_num,
												const char * src_name,
												const char * dest_name){
	// find dest dir inode
	int len = 1024 + (gd->bg_inode_table-1)*EXT2_BLOCK_SIZE + (dest_inode_num -1)*sb->s_inode_size;
	struct ext2_inode * dest_ei = (struct ext2_inode *) (disk + len); 
	
	// dest dir add new  dir entry	
	struct ext2_dir_entry * dest_file_dir_entry;
	int new_len;
	dest_file_dir_entry = last_file_dir_entry(dest_ei, &new_len);
	
	dest_file_dir_entry->rec_len = sizeof(struct ext2_dir_entry) + \
							 dest_file_dir_entry->name_len + \
							 (4-((sizeof(struct ext2_dir_entry) + dest_file_dir_entry->name_len) % 4));
	struct ext2_dir_entry * new_ed = (struct ext2_dir_entry *)(((unsigned char *) dest_file_dir_entry) + \
											dest_file_dir_entry->rec_len);
	new_len += dest_file_dir_entry->rec_len;

	//get inode , block
	int new_inode_num = get_free_inode_num(sb, gd);
	if (new_inode_num == 0){
		exit(ENOSPC);
	}
	int new_block_num = get_free_block_num(sb, gd);
	if (new_block_num == 0){
		exit(ENOSPC);
	}

	update_inode_bitmap(sb, gd, new_inode_num, 1);
	update_block_bitmap(sb, gd, new_block_num,1);

	//get  inode  table  position   and   set  value
	len = 1024+(gd->bg_inode_table-1)*EXT2_BLOCK_SIZE + (new_inode_num-1)*sb->s_inode_size;
	struct ext2_inode * new_ei = (struct ext2_inode *) (disk + len);

	// set file type
	new_ei->i_mode |= EXT2_S_IFLNK; 
	// set the size, At least save  .   ..
	new_ei->i_size = strlen(src_name);
	new_ei->i_blocks = 2; 
	// . link
	new_ei->i_links_count = 1; 
	for(int i = 0; i < 15; i++){
		new_ei->i_block[i] = 0; 
	}
	new_ei->i_block[0] = new_block_num;
	new_ei->i_dtime = 0;

	//set new dir entry value
	new_ed->inode = new_inode_num;
	new_ed->file_type |= EXT2_FT_SYMLINK;
	new_ed->name_len = strlen(dest_name);
	new_ed->rec_len = EXT2_BLOCK_SIZE - new_len;
	memcpy((char *) ((unsigned char *)new_ed + sizeof(struct ext2_dir_entry)), dest_name, strlen(dest_name));

	// set the new inode content
	unsigned char * data_block = (unsigned char *)(disk + 1024 + (new_ei->i_block[0] - 1) * EXT2_BLOCK_SIZE ) ;

	memcpy(data_block, src_name, strlen(src_name));


	// update count 
	gd->bg_free_blocks_count -= 1;
	gd->bg_free_inodes_count -= 1;

	sb->s_free_blocks_count -= 1;
	sb->s_free_inodes_count -= 1;
	
}

int main(int argc, char **argv) {

    if(argc != 4 && argc != 5) {
        fprintf(stderr, "Usage: %s <image file name> <-s> <src file path> <dest file path>\n", argv[0]);
        exit(1);
    }

	int symbol_link = 0;
	char image_file[255];
	char src_file_path[1024];
	char dest_file_path[1024];

	// get input param
	if (argc == 5 && strcmp(argv[2], "-s") == 0){
		symbol_link = 1;
		strcpy(image_file, argv[1]);
		strcat(src_file_path, argv[3]);
		strcat(dest_file_path, argv[4]);
	}
	else if (argc == 4){
		symbol_link = 0;
		strcpy(image_file, argv[1]);
		strcat(src_file_path, argv[2]);
		strcat(dest_file_path, argv[3]);
	}
	else{
		fprintf(stderr, "Usage: %s <image file name> <-s> <src file path> <dest file path>\n", argv[0]);
		exit(1);
	}

	//check path
	if (src_file_path[0] != '/' || dest_file_path[0] != '/'){
		return ENOENT;
	}
	
    int fd = open(image_file, O_RDWR);

    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    struct ext2_super_block *sb = (struct ext2_super_block *)(disk + 1024);
	struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 1024 + sizeof(struct ext2_super_block));

	int src_parent_inode = EXT2_ROOT_INO;
	int dest_parent_inode = EXT2_ROOT_INO;
	char *delim = "/";
	char *dir_name;
	int ret = -1;
	char src_path[1024];
	char dest_path[1024];
	char src_last_file[255];
	char dest_last_file[255];
	char *p;

	// get  src file path  and   the last filename 
	if (src_file_path[strlen(src_file_path) - 1] == '/')
		src_file_path[strlen(src_file_path) - 1] = '\0';
	if (dest_file_path[strlen(dest_file_path) - 1] == '/')
		dest_file_path[strlen(dest_file_path) - 1] = '\0';
	
	p = strrchr(src_file_path, '/');
	if (p != NULL){
		strncpy(src_path, src_file_path, p-src_file_path);
		src_path[p-src_file_path] = '\0';
		strncpy(src_last_file, p + 1, strlen(src_file_path) - strlen(src_path) - 1);
		src_last_file[strlen(src_file_path) - strlen(src_path) - 1] = '\0';
	}
	else{
		strcpy(src_path, "");
		strcpy(src_last_file, src_file_path);
	}

	p = strrchr(dest_file_path, '/');
	if (p != NULL){
		strncpy(dest_path, dest_file_path, p-dest_file_path);
		dest_path[p-dest_file_path] = '\0';
		strncpy(dest_last_file, p + 1, strlen(dest_file_path) - strlen(dest_path) - 1);
		dest_last_file[strlen(dest_file_path) - strlen(dest_path) - 1] = '\0';
	}
	else{
		strcpy(dest_path, "");
		strcpy(dest_last_file, dest_file_path);
	}
	
	// check src file path
	//    check src file path parent dir
	dir_name = strtok(src_path, delim);
	while (dir_name != NULL){
		ret = find_dir_inode(sb, gd, src_parent_inode, dir_name);
		dir_name = strtok(NULL, delim);
		if (ret == -1){
			// no exist directory
			return  ENOENT;
		}
		
		// continue find
		src_parent_inode = ret;
	}

	//    check dest file path parent dir
	dir_name = strtok(dest_path, delim);
	while (dir_name != NULL){
		ret = find_dir_inode(sb, gd, dest_parent_inode, dir_name);
		dir_name = strtok(NULL, delim);
		if (ret == -1){
			// no exist directory
			return  ENOENT;
		}
		
		// continue find
		dest_parent_inode = ret;
	}

	struct ext2_dir_entry * src_file_dir_entry = file_dir_entry(sb, gd, src_parent_inode, src_last_file);
	struct ext2_dir_entry * dest_file_dir_entry = file_dir_entry(sb, gd, dest_parent_inode, dest_last_file);

	// check src last file
	if (src_file_dir_entry == NULL){
		// no src file
		return ENOENT;
	}

	// check dest last file 
	if (dest_file_dir_entry != NULL){
		return EEXIST;
	}

	if (symbol_link == 0 && src_file_dir_entry->file_type == EXT2_FT_DIR){
		return EISDIR;
	}

	if (symbol_link == 0){
		// hard link
		hard_link(sb, gd, src_file_dir_entry, dest_parent_inode, dest_last_file);
	}
	else{
		symbol_like(sb, gd, dest_parent_inode, src_file_path, dest_last_file);
	}

	
    return 0;
}
