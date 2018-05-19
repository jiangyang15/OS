/*
	This program takes two command line arguments. The first is the name of an ext2 formatted virtual disk, and the second is an absolute path to a file or link (not a directory!) on that disk. The program should be the exact opposite of rm, restoring the specified file that has been previous removed. If the file does not exist (it may have been overwritten), or if it is a directory, then your program should return the appropriate error. 
	Hint: The file to be restored will not appear in the directory entries of the parent directory, unless you search the "gaps" left when files get removed. The directory entry structure is the key to finding out these gaps and searching for the removed file. 
	Note: If the directory entry for the file has not been overwritten, you will still need to make sure that the inode has not been reused, and that none of its data blocks have been reallocated. You may assume that the bitmaps are reliable indicators of such fact. If the file cannot be fully restored, your program should terminate with ENOENT, indicating that the operation was unsuccessful. 
	Note(2): For testing, you should focus primarily on restoring files that you've removed using your ext2_rm implementation, since ext2_restore should undo the exact changes made by ext2_rm. While there are some removed entries already present in some of the image files provided, the respective files have been removed on a non-ext2 file system, which is not doing the removal the same way that ext2 would. In ext2, when you do "rm", the inode's i_blocks do not get zeroed, and you can do full recovery, as stated in the assignment (which deals solely with ext2 images, hence why you only have to worry about this type of (simpler) recovery). In other FSs things work differently. In ext3, when you rm a file, the data block indexes from its inode do get zeroed, so recovery is not as trivial. For example, there are some removed files in deletedfile.img, which have their blocks zero-ed out (due to how these images were created). There are also some unrecoverable entries in images like twolevel.img, largefile.img, etc. In such cases, your code should still work, but simply recover a file as an empty file (with no data blocks), or discard the entry if it is unrecoverable. However, for the most part, try to recover files that you've ext2_rm-ed yourself, to make sure that you can restore data blocks as well. We will not be testing recovery of files removed with a non-ext2 tool. 
	Note(3): We will not try to recover files that had hardlinks at the time of removal. This is because when trying to restore a file, if its inode is already in use, there are two options: the file we're trying to restore previously had other hardlinks (and hence its inode never really got invalidated), _or_ its inode has been re-allocated to a completely new file. Since there is no way to tell between these 2 possibilities, recovery in this case should not be attempted. 
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

void restore(struct ext2_super_block *sb, struct ext2_group_desc *gd, 
												int parent_inode_num,
												const char * file_name){
	// find dir inode
	int len = 1024 + (gd->bg_inode_table-1)*EXT2_BLOCK_SIZE + (parent_inode_num -1)*sb->s_inode_size;
	struct ext2_inode * parent_ei = (struct ext2_inode *) (disk + len); 
	
	struct ext2_dir_entry * parent_dir_entry;
	struct ext2_dir_entry * delete_dir_entry = NULL; 
	int real_rec_len;
	char name[255];
	struct ext2_inode * file_ei = NULL;
	int file_inode_num;
	
	for (int i=0; i < parent_ei->i_blocks/2; ++i){
		if (parent_ei->i_block[i]){
			len = 0;
			// find delete  file_name  from dir entry
			while (1){
				parent_dir_entry = (struct ext2_dir_entry *) (disk+1024 + (parent_ei->i_block[i] - 1) * EXT2_BLOCK_SIZE + len);

				real_rec_len = sizeof(struct ext2_dir_entry) + \
								 parent_dir_entry->name_len + \
								 (4-((sizeof(struct ext2_dir_entry) + parent_dir_entry->name_len) % 4));
				if (parent_dir_entry->rec_len != real_rec_len){
					int new_len = real_rec_len;
					//find flag 0 not find  1 find
					int find;
					while (1) {
						// check  if  delete  file 
						delete_dir_entry = (struct ext2_dir_entry *) (disk+1024 + (parent_ei->i_block[i] - 1) * EXT2_BLOCK_SIZE + len + new_len);
						strncpy(name, delete_dir_entry->name, delete_dir_entry->name_len);
						name[delete_dir_entry->name_len] = '\0';
						if (strcmp(name, file_name) == 0){
							// find delete name , restore
							// parent_dir_entry->rec_len = real_rec_len;
							// finde delete file_inode
							int postion = 1024 + (gd->bg_inode_table-1)*EXT2_BLOCK_SIZE + (delete_dir_entry->inode -1)*sb->s_inode_size;
							file_ei = (struct ext2_inode *) (disk + postion);
							file_inode_num = delete_dir_entry->inode;
							find = 1;
							break;
						}
						// check if finish
						if (new_len + delete_dir_entry->rec_len >= parent_dir_entry->rec_len){
							find = 0;
							break;
						}
						new_len += delete_dir_entry->rec_len;
					}
					if (find){
						// find the delete  node
						real_rec_len = new_len;
						break;
					}
				}
			
				//next file
				if (len + parent_dir_entry->rec_len >= EXT2_BLOCK_SIZE)
					break;
				len += parent_dir_entry->rec_len;
			}		
		}
	}

	//printf(" file inode %d\n",file_inode_num);
	
	// check inode  if  used
	int used = get_inode_bitmap_by_index(sb, gd, file_inode_num);
	if (used){
		// inode used
		exit(ENOENT);
	}

	// check data block if used
	int block_nums = file_ei->i_blocks/2;
	//printf("[%d  %d]\n", file_ei->i_blocks, block_nums);
	for (int i=0; i < block_nums; ++i){
		// 0 - 12
		used = get_block_bitmap_by_index(sb, gd, file_ei->i_block[i]);
		if (used){
			// block used
			exit(ENOENT);
		}
		
		// Two level pointer 
		if (i == 12){
			unsigned int * two_level_block = (unsigned int *)(disk + 1024 + (file_ei->i_block[12] - 1) * EXT2_BLOCK_SIZE);
			for(int j = 13; j < block_nums; j++){
				used = get_block_bitmap_by_index(sb, gd, two_level_block[j-13]);
				if (used){
					// block used
					exit(ENOENT);
				}
			}
			break;
		}
		
	}

	//update 
	// restore dir entry
	parent_dir_entry->rec_len = real_rec_len;
	
	// use inode
	update_inode_bitmap(sb, gd, file_inode_num, 1);
	sb->s_free_inodes_count --;
	gd->bg_free_inodes_count --;

	// link file inode
	file_ei->i_links_count ++;
	file_ei->i_dtime = 0; 

	// use data block	
	for (int i=0; i < block_nums; ++i){
		// 0 - 12
		update_block_bitmap(sb, gd, file_ei->i_block[i], 1);
		//printf("i_block[%d]: %d\n", i, file_ei->i_block[i]);
		sb->s_free_blocks_count --;
		gd->bg_free_blocks_count --;
		
		// Two level pointer 
		if (i == 12){
			unsigned int * two_level_block = (unsigned int *)(disk + 1024 + (file_ei->i_block[12] - 1) * EXT2_BLOCK_SIZE);
			for(int j = 13; j < block_nums; j++){
				update_block_bitmap(sb, gd, two_level_block[j-13], 1);
				//printf("two_level_block[%d]: %d\n", j, two_level_block[j-13]);
				sb->s_free_blocks_count --;
				gd->bg_free_blocks_count --;
			}
			break;
		}
	}
   
}

int main(int argc, char **argv) {

    if(argc != 3) {
        fprintf(stderr, "Usage: %s <image file name> <absolute path name>\n", argv[0]);
        exit(1);
    }
	char input_path[1024];
	
	// check if absolute path
	if(argv[2][0] != '/'){
		return ENOENT;
	}
	strcpy(input_path, argv[2]);
	
    int fd = open(argv[1], O_RDWR);

    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    struct ext2_super_block *sb = (struct ext2_super_block *)(disk + 1024);
	struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 1024 + sizeof(struct ext2_super_block));

	int parent_inode = EXT2_ROOT_INO;
	char *delim = "/";
	char *dir_name;
	int ret = -1;
	char path[1024];
	char last_file[255];
	char *p;

	// get  src file path  and   the last filename 
	if (input_path[strlen(input_path) - 1] == '/')
		input_path[strlen(input_path) - 1] = '\0';
	
	p = strrchr(input_path, '/');
	strncpy(path, input_path, p-input_path);
	path[p-input_path] = '\0';
	strncpy(last_file, p + 1, strlen(input_path) - strlen(path) - 1);
	last_file[strlen(input_path) - strlen(path) - 1] = '\0';

	// check file path
	dir_name = strtok(path, delim);
	while (dir_name != NULL){
		ret = find_dir_inode(sb, gd, parent_inode, dir_name);
		dir_name = strtok(NULL, delim);
		if (ret == -1){
			// no exist directory
			return  ENOENT;
		}
		
		// continue find
		parent_inode = ret;
	}
	
	restore(sb, gd, parent_inode, last_file);
	
    return 0;
}

